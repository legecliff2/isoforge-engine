// perf/vis-first-step: the batched first-step line of sight in VisibilityState (segment_blocked_dda + the per-viewer-tile
// lattice-corner memo) against the shared trace. (1) segment_blocked_dda == segment_blocked == the original trace on Pcg32
// random boards (long, negative, axis-aligned, diagonal and zero-length segments, sparse to dense walls); (2) the masks of
// a cold VisibilityState equal the brute-force tile_visible (light_reaches, the old trace) on every tile of randomised wall
// layouts with opaque zones, lights and mixed senses.
#include <doctest/doctest.h>

#include <set>
#include <vector>

#include "rng/rng.hpp"
#include "rules5e/spells.hpp"
#include "sim/grid.hpp"
#include "sim/light.hpp"
#include "sim/trace.hpp"
#include "sim/visibility.hpp"
#include "trace_reference.hpp"
#include "world/chunk.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;

TEST_CASE("vis first step: segment_blocked_dda equals segment_blocked and the original trace on random boards") {
    rng::Pcg32 r(0x56495331u, 1);
    long checked = 0, blocked = 0;
    for (int board = 0; board < 40; ++board) {
        const uint32_t pct = r.below(60);
        std::set<Vec2i> walls;
        for (int y = -20; y <= 20; ++y)
            for (int x = -20; x <= 20; ++x)
                if (r.below(100) < pct) walls.insert({x, y});
        auto b = [&](Vec2i p) { return walls.count(p) != 0; };
        for (int k = 0; k < 1500; ++k) {
            const Vec2i p{r.range(-21, 21), r.range(-21, 21)};
            Vec2i q{r.range(-21, 21), r.range(-21, 21)};
            switch (r.below(6)) {
            case 0: q.x = p.x; break;                                   // vertical (along tile edges)
            case 1: q.y = p.y; break;                                   // horizontal
            case 2: { const int32_t d = r.range(-15, 15); q = {p.x + d, p.y + (r.chance(1, 2) ? d : -d)}; break; }   // diagonal
            case 3: { const int32_t d = r.range(1, 7); q = {p.x + 2 * d, p.y + 3 * d}; break; }                     // corners
            default: break;
            }
            const bool a = segment_blocked_dda(p, q, b);
            REQUIRE_MESSAGE(a == segment_blocked(p, q, b), "(" << p.x << "," << p.y << ") -> (" << q.x << "," << q.y << ")");
            REQUIRE(a == ref::segment_blocked(p, q, b));
            ++checked;
            blocked += a ? 1 : 0;
        }
    }
    CHECK(checked == 40 * 1500);
    CHECK(blocked > checked / 5);
    CHECK(blocked < checked - checked / 5);
}

namespace {
constexpr int N = world::kChunkSize;
LightSource lamp(uint32_t id, Vec2i t, uint16_t b, uint16_t d) {
    AttachedLight a;
    a.id = id; a.carrier = Carrier::Tile; a.tile = t; a.bright_ft = b; a.dim_ft = d; a.flags = light_src::kLit;
    return attached_source(a, t);
}
} // namespace

TEST_CASE("vis first step: cold masks equal the brute-force tile_visible on randomised wall layouts") {
    rng::Pcg32 r(0x56495332u, 2);
    for (int layout = 0; layout < 5; ++layout) {
        CAPTURE(layout);
        std::vector<world::Chunk> cs;
        const uint32_t pct = 3 + r.below(25);
        for (int cx = 0; cx <= 1; ++cx) {
            world::Chunk c{};
            c.coord = {cx, 0, 0};
            c.palette = {"", "floor", "wall"};
            for (auto& cell : c.layers[0]) cell.tile = 1;
            for (auto& cell : c.layers[2])
                if (r.below(100) < pct) cell.flags = world::tile_flags::kBlocksSight;
            if (layout % 2)   // long walls with gaps: many corner-grazing and edge-running segments
                for (int y = 0; y < N; ++y)
                    if (y % 6 != 0) c.layers[2][std::size_t(y * N + 7 + 9 * cx)].flags = world::tile_flags::kBlocksSight;
            cs.push_back(c);
        }
        const world::Chunk* ptrs[] = {&cs[0], &cs[1]};
        const Grid g{std::span<const world::Chunk* const>(ptrs)};
        REQUIRE(g.ok());
        LightWorld w;
        w.grid = &g;
        w.ambient = Ambient{AmbientKind::Moon, layout == 4 ? Light::Dim : Light::Dark};
        for (uint32_t i = 0; i < 6; ++i)
            w.sources.push_back(lamp(i + 1, {r.range(0, 2 * N - 1), r.range(0, N - 1)}, uint16_t(5 * r.range(1, 6)), 20));
        LightZone z;
        z.zone_id = 1; z.flags = uint8_t(rules5e::kZoneOpaque);
        const Vec2i zc{r.range(4, 2 * N - 5), r.range(4, N - 5)};
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) z.tiles.push_back({zc.x + x, zc.y + y});
        w.zones.push_back(z);
        w.normalise();
        LightCache cache(w);
        std::vector<Viewer> vs;
        for (uint32_t i = 0; i < 6; ++i) {
            Viewer v;
            v.id = i + 1; v.team = TeamId(i % 2);
            v.tile = {r.range(0, 2 * N - 1), r.range(0, N - 1)};
            v.anchor = v.tile; v.footprint = 1;
            if (i % 3 == 1) v.senses.darkvision_ft = 60;
            if (i % 3 == 2) v.senses.darkvision_ft = 120;
            if (i == 5) v.senses.truesight_ft = 30;
            vs.push_back(v);
        }
        vs[1].tile = vs[0].tile;   // two viewers on one tile (different teams) share its memo
        vs[1].anchor = vs[1].tile;
        const world::ChunkCoord chunks[] = {{0, 0, 0}, {1, 0, 0}};
        VisibilityState vis;
        vis.begin_step(cache, vs, chunks);
        long seen = 0;
        for (const TeamVisibility& tv : vis.teams())
            for (const auto& c : chunks) {
                const ChunkVisibility* cv = nullptr;
                for (const auto& x : tv.chunks)
                    if (x.coord == c) cv = &x;
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x) {
                        const Vec2i t = Grid::to_world(c, {x, y});
                        bool want = false;
                        for (const auto& v : vs)
                            if (v.team == tv.team) want = want || tile_visible(w, cache, v, t);
                        const bool got = cv && cv->seen_now.test(x, y);
                        REQUIRE_MESSAGE(got == want, "team " << int(tv.team) << " t = (" << t.x << ", " << t.y << ")");
                        seen += got ? 1 : 0;
                    }
            }
        CHECK(seen > 50);
    }
}
