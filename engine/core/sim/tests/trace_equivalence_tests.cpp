// perf/trace-noalloc: the allocation-free segment_blocked (sim/trace.hpp) against the original (trace_reference.hpp).
// Same answer AND the same sequence of blocks() probes, over exhaustive small sweeps and large Pcg32 sweeps on empty,
// random and dense-wall boards (corners, edges, collinear, zero-length segments); plus a pinned cover_of digest over all
// the cover test boards and a pinned light digest, recorded with the original implementation.
#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "rng/rng.hpp"
#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/light.hpp"
#include "sim/trace.hpp"
#include "trace_reference.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::Cover;
using rules5e::CoverQuery;

namespace {
struct Board2 {
    std::set<Vec2i> walls;
    bool operator()(Vec2i p) const { return walls.count(p) != 0; }
};
Board2 random_board(rng::Pcg32& r, int lo, int hi, uint32_t pct) {
    Board2 b;
    for (int y = lo; y <= hi; ++y)
        for (int x = lo; x <= hi; ++x)
            if (r.next_u32() % 100 < pct) b.walls.insert({x, y});
    return b;
}
// Compare answer and probe sequence for one segment.
void same(const Board2& b, Vec2i p, Vec2i q) {
    std::vector<Vec2i> pa, pb;
    const bool a = segment_blocked(p, q, [&](Vec2i t) { pa.push_back(t); return b(t); });
    const bool c = ref::segment_blocked(p, q, [&](Vec2i t) { pb.push_back(t); return b(t); });
    REQUIRE_MESSAGE(a == c, "(" << p.x << "," << p.y << ") -> (" << q.x << "," << q.y << ")");
    REQUIRE(pa == pb);
}
} // namespace

TEST_CASE("trace: exhaustive small sweep - every segment between lattice points in [-5, 5]^2, five boards") {
    rng::Pcg32 r(0x54524143u, 1);
    std::vector<Board2> boards;
    boards.push_back({});                                   // empty: nothing blocks
    Board2 all;
    for (int y = -6; y <= 6; ++y)
        for (int x = -6; x <= 6; ++x) all.walls.insert({x, y});
    boards.push_back(all);                                  // every tile blocks (edges / corners need both sides)
    Board2 checker;
    for (int y = -6; y <= 6; ++y)
        for (int x = -6; x <= 6; ++x)
            if ((x + y) % 2 == 0) checker.walls.insert({x, y});
    boards.push_back(checker);                              // diagonal pairs: slipping between corners
    boards.push_back(random_board(r, -6, 6, 30));
    boards.push_back(random_board(r, -6, 6, 70));
    long long n = 0;
    for (const auto& b : boards)
        for (int py = -5; py <= 5; ++py)
            for (int px = -5; px <= 5; ++px)
                for (int qy = -5; qy <= 5; ++qy)
                    for (int qx = -5; qx <= 5; ++qx) { same(b, {px, py}, {qx, qy}); ++n; }
    CHECK(n == 5LL * 121 * 121);   // includes zero-length, axis-aligned, 45-degree and other collinear cases
}

TEST_CASE("trace: large Pcg32 sweeps - long segments, negative coords, random and dense boards") {
    rng::Pcg32 r(0x4E4F414Cu, 2);
    for (uint32_t pct : {0u, 10u, 35u, 60u, 90u}) {
        const Board2 b = random_board(r, -60, 60, pct);
        for (int i = 0; i < 20000; ++i) {
            auto c = [&]() { return int(r.next_u32() % 121) - 60; };
            const Vec2i p{c(), c()};
            Vec2i q{c(), c()};
            switch (r.next_u32() % 4) {   // bias toward special directions
                case 0: q.y = p.y; break;                                    // horizontal
                case 1: q.x = p.x + (q.y - p.y); break;                      // 45 degrees
                case 2: q.x = p.x + 2 * (q.y - p.y); break;                  // slope 1/2: passes lattice corners
                default: break;
            }
            same(b, p, q);
            same(b, q, p);
        }
    }
}

namespace {
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";
struct Spawn { std::string a; int x, y; };
world::Chunk arena(const std::vector<Spawn>& sp, const std::vector<Vec2i>& walls) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls)
        c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < sp.size(); ++i)
        c.spawns.push_back({sp[i].a, Vec2i{sp[i].x, sp[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void fold(uint64_t& h, uint32_t v) {
    for (int k = 0; k < 4; ++k) { h ^= (v >> (8 * k)) & 0xFFu; h *= 1099511628211ull; }
}
} // namespace

TEST_CASE("trace: cover_of over every cover test board is unchanged (digest pinned with the original trace)") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    assets::Json big = assets::Json::parse(R"({"x":0})");
    {
        // Large goblin, as cover_tests' "t.large".
        std::ifstream f(std::string(ENGINE_SOURCE_ROOT) + "/content/packs/srd51.core/monsters/goblin.json");
        REQUIRE(f.good());
        big = assets::Json::parse(f);
        big["$schema"] = "monster@4";
        big["id"] = "t.large";
        big["size"] = "large";
        REQUIRE_FALSE(l.add(big, "monster"));
    }
    std::vector<Vec2i> full;
    for (int y = 0; y < 12; ++y) full.push_back({4, y});
    struct B { std::vector<Spawn> sp; std::vector<Vec2i> walls; };
    const std::vector<B> boards = {
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}}, {}},
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}}, {}},
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 7}}, {}},
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}}, {{4, 5}}},
        {{{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 6}, {6, 5}}},
        {{{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}}},
        {{{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}, {5, 6}, {6, 5}}},
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}}, full},
        {{{kFighter, 1, 5}, {kGoblin, 6, 6}}, {{4, 5}, {4, 6}}},
        {{{"t.large", 8, 1}, {kGoblin, 10, 5}, {kGoblin, 9, 5}}, {}},
        {{{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}}, {{6, 3}, {7, 3}, {8, 3}, {5, 3}}},
        {{{kFighter, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}}, {}},
    };
    uint64_t h = 1469598103934665603ull;
    long long queries = 0;
    for (const auto& b : boards) {
        const auto ch = arena(b.sp, b.walls);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 1);
        REQUIRE(c);
        for (std::size_t s = 0; s < ch.spawns.size(); ++s)
            for (std::size_t t = 0; t < ch.spawns.size(); ++t) {
                CoverQuery q{ch.spawns[s].stable_id, ch.spawns[t].stable_id};
                fold(h, uint32_t(c->cover_of(q)));
                q.from_area = true;
                fold(h, uint32_t(c->cover_of(q)));
                for (int oy = 0; oy < 12; ++oy)          // every explicit origin in the board area
                    for (int ox = 0; ox < 14; ++ox) {
                        q.has_origin = true; q.origin_x = ox; q.origin_y = oy;
                        fold(h, uint32_t(c->cover_of(q)));
                        ++queries;
                    }
                q.has_origin = false;
            }
    }
    CHECK(queries > 5000);
    CHECK(h == 0xde7e789ff503ce53ull);   // pinned at 480c0ae (original trace)
}

TEST_CASE("trace: light over a dense-wall board is unchanged (digest pinned with the original trace)") {
    world::Chunk c{};
    c.palette = {"", "floor", "wall"};
    rng::Pcg32 r(0x4C494748u, 3);
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (auto& cell : c.layers[2])
        if (r.next_u32() % 100 < 30) cell.flags = world::tile_flags::kBlocksSight;
    const Grid g(c);
    LightWorld w;
    w.grid = &g;
    for (uint32_t i = 0; i < 12; ++i) {
        AttachedLight a;
        a.id = i + 1; a.carrier = Carrier::Tile;
        a.tile = Vec2i{int(r.next_u32() % 32), int(r.next_u32() % 32)};
        a.bright_ft = 30; a.dim_ft = 30; a.flags = light_src::kLit;
        w.sources.push_back(attached_source(a, a.tile));
    }
    w.normalise();
    uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const LightSample s = light_at(w, {x, y});
            fold(h, uint32_t(s.level) | (uint32_t(s.flags) << 8));
        }
    CHECK(h == 0x2261c01493f2da3ull);   // pinned at 480c0ae (original trace)
}
