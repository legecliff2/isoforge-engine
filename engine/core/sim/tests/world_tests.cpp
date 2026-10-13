// Cross-chunk sim: world-space tiles, pathing across chunk boundaries, combat spanning chunks,
// combat over a ChunkStreamer resident set (thread / order independent).
#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/chunk_streamer.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::EventKind;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

// Open floor; `wall_x` (local) is a wall column with a gap at local y 20..21.
world::Chunk open_chunk(world::ChunkCoord coord, std::vector<std::tuple<std::string, int, int>> spawns, int wall_x = -1) {
    world::Chunk c{};
    c.coord = coord;
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    if (wall_x >= 0)
        for (int y = 0; y < 32; ++y)
            if (y < 20 || y > 21) c.layers[2][std::size_t(y * 32 + wall_x)].flags = world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        auto& [a, x, y] = spawns[i];
        c.spawns.push_back({a, Vec2i{x, y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}
// West chunk (-1,2) holds the fighter behind a wall with a gap; east chunk (0,2) the goblin.
world::Chunk west() { return open_chunk({-1, 2, 0}, {{kFighter, 26, 10}}, 29); }
world::Chunk east() { return open_chunk({0, 2, 0}, {{kGoblin, 4, 10}}); }

ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load_pack(kPack).empty());
    return l;
}

struct Fight { uint64_t hash = 0, log_hash = 0; std::vector<LogEntry> log; };
Fight fight(std::span<const world::Chunk* const> chunks, const ContentLibrary& l, uint64_t seed) {
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, chunks, l, seed, &err);
    REQUIRE_MESSAGE(c, err);
    NearestEnemyPolicy p;
    auto rr = run_to_end(*c, [&](CombatantId) { return &p; });
    REQUIRE_MESSAGE(rr.ended, rr.error);
    Fight f{c->hash(), c->log_hash(), c->log()};
    return f;
}

class ListProvider final : public world::IChunkProvider {
public:
    explicit ListProvider(std::vector<world::Chunk> cs) : cs_(std::move(cs)) {}
    std::string_view name() const override { return "list"; }
    bool covers(world::ChunkCoord c) const override {
        return std::any_of(cs_.begin(), cs_.end(), [&](const world::Chunk& k) { return k.coord == c; });
    }
    world::ChunkResult load(const world::ChunkRequest& r) override {
        for (const auto& k : cs_) if (k.coord == r.coord) return {world::ChunkStatus::Ready, k, ""};
        return {world::ChunkStatus::NotFound, std::nullopt, "not found"};
    }

private:
    std::vector<world::Chunk> cs_;
};
} // namespace

TEST_CASE("world grid: world tiles, floor division, cross-chunk paths and LOS") {
    const auto w = west(), e = east();
    const world::Chunk* cs[] = {&w, &e};
    Grid g{std::span<const world::Chunk* const>(cs)};
    REQUIRE(g.ok());
    CHECK(Grid::to_world({-1, 2, 0}, {26, 10}) == Vec2i{-6, 74});
    CHECK(Grid::chunk_of({-1, 64}) == std::pair<int32_t, int32_t>{-1, 2});
    CHECK(Grid::chunk_of({0, 95}) == std::pair<int32_t, int32_t>{0, 2});
    CHECK(g.in_bounds({-32, 64}));
    CHECK(g.in_bounds({31, 95}));
    CHECK_FALSE(g.in_bounds({-33, 64}));
    CHECK_FALSE(g.in_bounds({0, 63}));
    CHECK_FALSE(g.walkable({-3, 74}));   // wall column (local x 29)
    CHECK(g.walkable({-3, 84}));         // gap (local y 20)
    const auto path = g.path({-6, 74}, {{3, 74}}, {});
    REQUIRE_FALSE(path.empty());
    CHECK(path.back() == Vec2i{3, 74});
    CHECK(std::find(path.begin(), path.end(), Vec2i{-3, 84}) != path.end());   // through the gap
    bool crossed = false;
    for (std::size_t i = 1; i < path.size(); ++i) crossed |= path[i - 1].x == -1 && path[i].x == 0;
    CHECK(crossed);
    CHECK(g.line_of_sight({-1, 70}, {5, 70}));
    // Errors: mixed levels, duplicates, nothing.
    const auto other = open_chunk({0, 2, 1}, {});
    const world::Chunk* mixed[] = {&w, &other};
    CHECK_FALSE(Grid{std::span<const world::Chunk* const>(mixed)}.ok());
    const world::Chunk* dup[] = {&w, &w};
    CHECK_FALSE(Grid{std::span<const world::Chunk* const>(dup)}.ok());
    CHECK_FALSE(Grid{std::span<const world::Chunk* const>()}.ok());
}

TEST_CASE("combat spanning chunks: world-space positions, deterministic, order independent, replayable") {
    const auto l = lib();
    const auto w = west(), e = east();
    const world::Chunk* we[] = {&w, &e};
    const world::Chunk* ew[] = {&e, &w};
    {
        ecs::Registry reg;
        auto c = Combat::create(reg, std::span<const world::Chunk* const>(we), l, 1);
        REQUIRE(c);
        CHECK(c->pos(w.spawns[0].stable_id) == Vec2i{-6, 74});
        CHECK(c->pos(e.spawns[0].stable_id) == Vec2i{4, 74});
    }
    // New goldens (cross-chunk combat; Combat::hash / log_hash).
    struct Pin { uint64_t seed, hash, log_hash; };
    const Pin pins[] = {
        {1, 14882011023945014056ull, 523992291452462904ull},
        {2, 16194671849302917909ull, 446799502391117340ull},
        {3, 1996883113502086049ull, 4768670429643826030ull},
    };
    for (const Pin& p : pins) {
        CAPTURE(p.seed);
        const Fight a = fight(we, l, p.seed);
        const Fight b = fight(ew, l, p.seed);
        MESSAGE("{" << p.seed << ", " << a.hash << "ull, " << a.log_hash << "ull},");
        CHECK(a.hash == b.hash);
        CHECK(a.log_hash == b.log_hash);
        CHECK(a.hash == p.hash);
        CHECK(a.log_hash == p.log_hash);
        ecs::Registry reg;
        std::string err;
        auto r = replay(reg, std::span<const world::Chunk* const>(ew), l, p.seed, a.log, &err);
        REQUIRE_MESSAGE(r, err);
        CHECK(r->hash() == a.hash);
    }
    // Someone ends up on the other side of the chunk boundary in at least one fight.
    bool any = false;
    for (const Pin& p : pins) {
        ecs::Registry reg;
        auto c = Combat::create(reg, std::span<const world::Chunk* const>(we), l, p.seed);
        NearestEnemyPolicy pol;
        REQUIRE(run_to_end(*c, [&](CombatantId) { return &pol; }).ended);
        any |= c->pos(w.spawns[0].stable_id).x >= 0 || c->pos(e.spawns[0].stable_id).x < 0;
    }
    CHECK(any);
}

TEST_CASE("combat over a streamed resident set: independent of threads and request order") {
    const auto l = lib();
    ListProvider prov({west(), east(), open_chunk({1, 2, 0}, {}), open_chunk({-1, 3, 0}, {})});
    const auto w = west(), e = east();
    const world::Chunk* we[] = {&w, &e};
    const uint64_t direct = fight(we, l, 7).hash;
    for (uint32_t threads : {0u, 4u})
        for (uint64_t shuffle : {0ull, 3ull}) {
            CAPTURE(threads);
            world::StreamerConfig cfg;
            cfg.request_shuffle_seed = shuffle;
            world::ChunkStreamer s(prov, threads, cfg);
            s.set_focus({-6, 74}, 0, 1);   // chunk (-1,2): 3x3 around it
            s.wait_idle();
            const auto res = s.resident(0);
            CHECK(res.size() == 3);   // (-1,2), (0,2), (-1,3); (1,2) is 2 chunks away
            ecs::Registry reg;
            auto c = Combat::create(reg, std::span<const world::Chunk* const>(res), l, 7);
            REQUIRE(c);
            NearestEnemyPolicy pol;
            REQUIRE(run_to_end(*c, [&](CombatantId) { return &pol; }).ended);
            // Extra empty chunks only enlarge the grid; this fight stays in the west/east pair.
            CHECK(c->hash() == direct);
        }
}

TEST_CASE("single-chunk combat keeps the chunk-local frame (v0 goldens)") {
    const auto l = lib();
    const auto e = east();   // coord (0,2): local (4,10)
    ecs::Registry reg;
    auto c = Combat::create(reg, e, l, 1);
    REQUIRE(c);
    CHECK(c->pos(e.spawns[0].stable_id) == Vec2i{4, 10});
}
