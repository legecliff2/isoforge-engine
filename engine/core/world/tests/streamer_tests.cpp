#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "world/chunk_codec.hpp"
#include "world/chunk_streamer.hpp"
#include "world/layered_provider.hpp"

using namespace eng;
using namespace eng::world;

namespace {
// Pure synthetic provider over x,y in [-8, 8): contents depend only on (seed, coord). Loads sleep
// a coord-dependent 0-2 ms so worker completion order is scrambled.
class SynthProvider final : public IChunkProvider {
public:
    explicit SynthProvider(bool jitter = true) : jitter_(jitter) {}
    std::string_view name() const override { return "synth"; }
    bool covers(ChunkCoord c) const override { return c.x >= -8 && c.x < 8 && c.y >= -8 && c.y < 8 && c.level == 0; }
    ChunkResult load(const ChunkRequest& r) override {
        loads.fetch_add(1);
        const uint32_t h = uint32_t(r.coord.x * 73856093) ^ uint32_t(r.coord.y * 19349663) ^ uint32_t(r.world_seed);
        if (jitter_) std::this_thread::sleep_for(std::chrono::microseconds((h % 3) * 700));
        if (r.coord.x == 7 && r.coord.y == 7) return {ChunkStatus::Error, std::nullopt, "corrupt"};
        Chunk c{};
        c.coord = r.coord;
        c.palette = {"", "tiles.floor"};
        for (int i = 0; i < kChunkSize * kChunkSize; ++i) {
            c.layers[0][std::size_t(i)].tile = 1;
            if ((uint32_t(i) * 2654435761u ^ h) % 17 == 0) c.layers[2][std::size_t(i)].flags = tile_flags::kBlocksMove;
        }
        for (uint32_t s = 0; s < h % 3; ++s)
            c.spawns.push_back({"srd51.monster.goblin", Vec2i{int32_t(s), int32_t(h % 32)}, spawn_stable_id(c.coord, s), ""});
        c.origin = Chunk::Origin::Procedural;
        c.content_hash = compute_content_hash(c);
        return {ChunkStatus::Ready, std::move(c), ""};
    }
    std::atomic<int> loads{0};

private:
    bool jitter_;
};

uint64_t settle_digest(uint32_t threads, uint64_t shuffle, Vec2i focus, int radius, int16_t level = 0) {
    SynthProvider p;
    StreamerConfig cfg;
    cfg.world_seed = 42;
    cfg.request_shuffle_seed = shuffle;
    ChunkStreamer s(p, threads, cfg);
    s.set_focus(focus, level, radius);
    s.wait_idle();
    return s.digest();
}
} // namespace

TEST_CASE("streamer: chunk_of_tile floors toward -inf") {
    CHECK(chunk_of_tile({0, 0}, 0) == ChunkCoord{0, 0, 0});
    CHECK(chunk_of_tile({31, 32}, 2) == ChunkCoord{0, 1, 2});
    CHECK(chunk_of_tile({-1, -32}, 0) == ChunkCoord{-1, -1, 0});
    CHECK(chunk_of_tile({-33, 64}, 0) == ChunkCoord{-2, 2, 0});
}

TEST_CASE("streamer: resident set = covered chunks within radius; independent of threads and request order") {
    const Vec2i focus{-5, 40};   // chunk (-1, 1)
    const uint64_t ref = settle_digest(0, 0, focus, 2);
    for (uint32_t threads : {0u, 1u, 3u, 8u})
        for (uint64_t shuffle : {0ull, 1ull, 2ull, 99ull}) {
            CAPTURE(threads);
            CAPTURE(shuffle);
            CHECK(settle_digest(threads, shuffle, focus, 2) == ref);
        }
    SynthProvider p(false);
    ChunkStreamer s(p, 4);
    s.set_focus(focus, 0, 2);
    s.wait_idle();
    CHECK(s.wanted().size() == 25);
    CHECK(s.resident(0).size() == 25);
    for (const Chunk* c : s.resident(0)) {
        CHECK(std::max(std::abs(c->coord.x + 1), std::abs(c->coord.y - 1)) <= 2);
        CHECK(s.get(c->coord) == c);
    }
    CHECK(s.errors().empty());
    CHECK(s.get({5, 5, 0}) == nullptr);
}

TEST_CASE("streamer: walks forward and reversed agree at every focus; eviction + LRU cache") {
    std::vector<Vec2i> walk;
    for (int i = 0; i < 12; ++i) walk.push_back({-100 + i * 23, 60 - i * 17});
    auto run = [&](const std::vector<Vec2i>& w, uint32_t threads, uint64_t shuffle) {
        SynthProvider p;
        StreamerConfig cfg;
        cfg.world_seed = 42;
        cfg.request_shuffle_seed = shuffle;
        cfg.cache_chunks = 8;
        ChunkStreamer s(p, threads, cfg);
        std::vector<uint64_t> out;
        for (Vec2i f : w) {
            s.set_focus(f, 0, 1);
            s.update();   // partially loaded frames are fine; only settled state is compared
            s.wait_idle();
            out.push_back(s.digest());
            CHECK(s.resident(0).size() <= 9);
        }
        return out;
    };
    const auto fwd = run(walk, 1, 0);
    auto rev_walk = walk;
    std::reverse(rev_walk.begin(), rev_walk.end());
    auto rev = run(rev_walk, 6, 5);
    std::reverse(rev.begin(), rev.end());
    CHECK(fwd == rev);
    for (std::size_t i = 0; i < walk.size(); ++i) CHECK(fwd[i] == settle_digest(0, 0, walk[i], 1));

    // Back and forth: the cache serves the return trip without reloading.
    SynthProvider p(false);
    ChunkStreamer s(p, 2);
    s.set_focus({0, 0}, 0, 1);
    s.wait_idle();
    const uint64_t d0 = s.digest();
    const int loads0 = p.loads.load();
    s.set_focus({40, 0}, 0, 1);   // chunk (1,0): (-1,*) evicted
    s.wait_idle();
    CHECK(s.get({-1, 0, 0}) == nullptr);
    s.set_focus({0, 0}, 0, 1);
    s.wait_idle();
    CHECK(s.digest() == d0);
    CHECK(p.loads.load() == loads0 + 3);   // only the 3 new (2,*) chunks were loaded
}

TEST_CASE("streamer: failed loads are reported, not resident; uncovered coords are skipped") {
    SynthProvider p(false);
    ChunkStreamer s(p, 2);
    s.set_focus({7 * 32 + 3, 7 * 32 + 3}, 0, 1);   // (7,7) errors; (8,*) not covered
    s.wait_idle();
    CHECK(s.wanted().size() == 4);
    CHECK(s.resident(0).size() == 3);
    CHECK(s.errors() == std::vector<std::string>{"7,7,0: corrupt"});
    CHECK(s.idle());
}

TEST_CASE("layered provider: first covering layer wins") {
    SynthProvider a(false), b(false);
    struct Only00 final : IChunkProvider {
        std::string_view name() const override { return "only00"; }
        bool covers(ChunkCoord c) const override { return c == ChunkCoord{0, 0, 0}; }
        ChunkResult load(const ChunkRequest& r) override {
            Chunk c{};
            c.coord = r.coord;
            c.origin = Chunk::Origin::Handcrafted;
            c.content_hash = compute_content_hash(c);
            return {ChunkStatus::Ready, std::move(c), ""};
        }
    } top;
    LayeredProvider l({&top, &a});
    CHECK(l.covers({0, 0, 0}));
    CHECK(l.covers({1, 0, 0}));
    CHECK_FALSE(l.covers({0, 0, 1}));
    CHECK(l.load({{0, 0, 0}, 1, 1}).chunk->origin == Chunk::Origin::Handcrafted);
    CHECK(l.load({{1, 0, 0}, 1, 1}).chunk->origin == Chunk::Origin::Procedural);
    CHECK(a.loads.load() == 1);
    CHECK(l.load({{0, 0, 1}, 1, 1}).status == ChunkStatus::NotFound);
    (void)b;
}
