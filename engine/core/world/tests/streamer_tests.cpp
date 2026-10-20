#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <set>
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

namespace {
std::set<ChunkCoord> resident_set(const ChunkStreamer& s) {
    std::set<ChunkCoord> out;
    for (const Chunk* c : s.resident(0)) out.insert(c->coord);
    return out;
}
// Applies d to acc, checking it is sorted, disjoint and never double-reports.
void apply_delta(std::set<ChunkCoord>& acc, const StreamDelta& d) {
    CHECK(std::is_sorted(d.added.begin(), d.added.end()));
    CHECK(std::is_sorted(d.removed.begin(), d.removed.end()));
    CHECK(std::adjacent_find(d.added.begin(), d.added.end()) == d.added.end());
    CHECK(std::adjacent_find(d.removed.begin(), d.removed.end()) == d.removed.end());
    for (ChunkCoord c : d.removed) CHECK(acc.erase(c) == 1);          // was resident
    for (ChunkCoord c : d.added) CHECK(acc.insert(c).second);         // was not resident
}
std::vector<Vec2i> delta_walk() {
    std::vector<Vec2i> w;
    for (int i = 0; i < 10; ++i) w.push_back({-90 + i * 29, 50 - i * 21});
    w.push_back({-90, 50});   // back to the start (cache hits)
    return w;
}
} // namespace

TEST_CASE("stream delta: per-update deltas accumulate to the resident set, never double-reported") {
    for (uint32_t threads : {0u, 1u, 6u}) {
        CAPTURE(threads);
        SynthProvider p;
        StreamerConfig cfg;
        cfg.cache_chunks = 6;
        ChunkStreamer s(p, threads, cfg);
        std::set<ChunkCoord> acc;
        for (Vec2i f : delta_walk()) {
            s.set_focus(f, 0, 1);
            for (int i = 0; i < 10000 && !s.idle(); ++i) {
                apply_delta(acc, s.update());
                CHECK(acc == resident_set(s));
                if (!s.idle()) std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            REQUIRE(s.idle());
            const StreamDelta extra = s.update();   // settled: nothing changes
            CHECK(extra.empty());
            CHECK(s.last_delta().empty());
            CHECK(acc == resident_set(s));
        }
    }
}

TEST_CASE("stream delta: wait_idle net deltas are identical across thread counts and request orders") {
    auto run = [](uint32_t threads, uint64_t shuffle) {
        SynthProvider p;
        StreamerConfig cfg;
        cfg.request_shuffle_seed = shuffle;
        ChunkStreamer s(p, threads, cfg);
        std::vector<std::pair<std::vector<ChunkCoord>, std::vector<ChunkCoord>>> out;
        std::set<ChunkCoord> acc;
        for (Vec2i f : delta_walk()) {
            s.set_focus(f, 0, 1);
            const StreamDelta d = s.wait_idle();
            CHECK(s.last_delta().added == d.added);
            apply_delta(acc, d);
            CHECK(acc == resident_set(s));
            out.push_back({d.added, d.removed});
        }
        return out;
    };
    const auto ref = run(0, 0);
    CHECK(ref.front().first.size() == 9);
    CHECK(ref.front().second.empty());
    for (uint32_t threads : {1u, 4u, 8u})
        for (uint64_t shuffle : {0ull, 11ull}) {
            CAPTURE(threads);
            CAPTURE(shuffle);
            CHECK(run(threads, shuffle) == ref);
        }
}

namespace {
std::set<ChunkCoord> ring(ChunkCoord f, int r) {
    std::set<ChunkCoord> out;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const ChunkCoord c{f.x + dx, f.y + dy, f.level};
            if (c.x >= -8 && c.x < 8 && c.y >= -8 && c.y < 8 && c.level == 0) out.insert(c);
        }
    return out;
}
// Synth, but (2,2) answers Empty (covered, nothing there = known-absent).
class GappyProvider final : public IChunkProvider {
public:
    std::string_view name() const override { return "gappy"; }
    bool covers(ChunkCoord c) const override { return inner.covers(c); }
    ChunkResult load(const ChunkRequest& r) override {
        if (r.coord.x == 2 && r.coord.y == 2) return {ChunkStatus::Empty, std::nullopt, ""};
        return inner.load(r);
    }
    SynthProvider inner{false};
};
} // namespace

TEST_CASE("multi-focus: wanted set is the union; set_focus == one-element set_foci") {
    SynthProvider p(false);
    ChunkStreamer s(p, 0);
    const std::vector<Focus> foci{{{-40, 5}, 0, 1}, {{100, 100}, 0, 0}, {{10, 10}, 0, 2}};
    s.set_foci(foci);
    std::set<ChunkCoord> want = ring({-2, 0, 0}, 1);
    for (auto c : ring({3, 3, 0}, 0)) want.insert(c);
    for (auto c : ring({0, 0, 0}, 2)) want.insert(c);
    const auto w = s.wanted();
    CHECK(std::set<ChunkCoord>(w.begin(), w.end()) == want);

    ChunkStreamer a(p, 0), b(p, 0);
    a.set_focus({-5, 40}, 0, 2);
    const Focus one{{-5, 40}, 0, 2};
    b.set_foci(std::span<const Focus>(&one, 1));
    CHECK(a.wanted() == b.wanted());
    a.wait_idle();
    b.wait_idle();
    CHECK(a.digest() == b.digest());

    s.set_foci({});   // no foci, no pins: everything goes
    s.wait_idle();
    CHECK(s.resident(0).empty());
}

TEST_CASE("multi-focus: settled digest independent of foci order, duplicates, threads, shuffle and walk") {
    const std::vector<Focus> base{{{-40, 5}, 0, 1}, {{70, 70}, 0, 1}, {{3, -60}, 0, 0}};
    auto run = [&](std::vector<Focus> f, uint32_t threads, uint64_t shuffle, bool walk) {
        SynthProvider p;
        StreamerConfig cfg;
        cfg.world_seed = 42;
        cfg.request_shuffle_seed = shuffle;
        ChunkStreamer s(p, threads, cfg);
        if (walk) {   // reach the same foci through a different history
            const Focus far{{-200, -200}, 0, 2};
            s.set_foci(std::span<const Focus>(&far, 1));
            s.wait_idle();
            s.set_foci(std::span<const Focus>(f.data(), 1));
            s.update();
        }
        s.set_foci(f);
        s.wait_idle();
        return s.digest();
    };
    const uint64_t ref = run(base, 0, 0, false);
    std::vector<Focus> rev(base.rbegin(), base.rend());
    std::vector<Focus> dup = base;
    dup.push_back(base[1]);
    dup.push_back({{-33, 1}, 0, 0});   // inside base[0]'s ring: no change
    for (uint32_t threads : {0u, 3u})
        for (uint64_t shuffle : {0ull, 5ull, 99ull})
            for (bool walk : {false, true}) {
                CAPTURE(threads);
                CAPTURE(shuffle);
                CAPTURE(walk);
                CHECK(run(base, threads, shuffle, walk) == ref);
                CHECK(run(rev, threads, shuffle, walk) == ref);
                CHECK(run(dup, threads, shuffle, walk) == ref);
            }
}

TEST_CASE("pinning: pinned chunks and their 3x3 halo survive any focus change until unpinned") {
    SynthProvider p(false);
    ChunkStreamer s(p, 0);
    s.set_focus({0, 0}, 0, 0);
    const std::vector<ChunkCoord> combat{{4, 4, 0}, {5, 4, 0}};
    s.pin(1, combat);
    s.wait_idle();
    std::set<ChunkCoord> halo = ring({4, 4, 0}, 1);
    for (auto c : ring({5, 4, 0}, 1)) halo.insert(c);
    for (auto c : halo) CHECK(s.get(c) != nullptr);
    CHECK(s.halo_state({4, 4, 0}) == HaloState::Complete);
    CHECK(s.halo_state({5, 4, 0}) == HaloState::Complete);

    s.set_focus({-200, -200}, 0, 1);   // focus far away: pins stay, the old focus chunk goes
    const StreamDelta d = s.wait_idle();
    for (auto c : halo) CHECK(s.get(c) != nullptr);
    CHECK(s.get({0, 0, 0}) == nullptr);
    for (auto c : d.removed) CHECK(halo.count(c) == 0);

    s.pin(2, std::vector<ChunkCoord>{{5, 4, 0}});   // second owner overlapping
    s.unpin(1);
    s.wait_idle();
    CHECK(s.get({5, 4, 0}) != nullptr);
    CHECK(s.get({3, 4, 0}) == nullptr);   // only in owner 1's halo
    CHECK(s.pinned() == std::vector<ChunkCoord>{{5, 4, 0}});
    s.unpin(2);
    s.wait_idle();
    CHECK(s.pinned().empty());
    CHECK(s.get({5, 4, 0}) == nullptr);
}

TEST_CASE("halo gate: complete with uncovered and Empty neighbours; pending; failed load is an error state") {
    GappyProvider p;
    ChunkStreamer s(p, 0);
    // World edge: (-8,-8)'s halo is 4 covered + 5 uncovered chunks -> known-absent, complete.
    s.set_focus({-8 * 32, -8 * 32}, 0, 1);
    CHECK(s.halo_state({-8, -8, 0}) == HaloState::Pending);   // nothing integrated yet
    s.wait_idle();
    CHECK(s.known_absent({-9, -8, 0}));
    CHECK(s.halo_state({-8, -8, 0}) == HaloState::Complete);
    CHECK(s.halo_state_of_tile({-8 * 32 + 5, -8 * 32 + 31}, 0) == HaloState::Complete);
    CHECK(s.halo_state({-7, -7, 0}) == HaloState::Pending);   // (-6,*) not wanted / not resident

    // Empty answer = known-absent, not an error.
    s.set_focus({2 * 32, 2 * 32}, 0, 1);
    s.wait_idle();
    CHECK(s.get({2, 2, 0}) == nullptr);
    CHECK(s.known_absent({2, 2, 0}));
    CHECK(s.errors().empty());
    CHECK(s.halo_state({2, 2, 0}) == HaloState::Complete);
    CHECK(s.halo_state({3, 3, 0}) == HaloState::Pending);

    // (7,7) fails: every halo touching it is Failed (even if other neighbours are still pending).
    s.set_focus({7 * 32, 7 * 32}, 0, 1);
    CHECK(s.halo_state({7, 7, 0}) != HaloState::Complete);
    s.wait_idle();
    CHECK(s.errors().size() == 1);
    CHECK_FALSE(s.known_absent({7, 7, 0}));
    CHECK(s.halo_state({7, 7, 0}) == HaloState::Failed);
    CHECK(s.halo_state({6, 6, 0}) == HaloState::Failed);
    CHECK(s.halo_state({5, 5, 0}) == HaloState::Pending);
}
