// R-G13 phase 1 benchmark (docs/proposals/LIGHTING.md §12 phase 1, P4): a full LightCache rebuild of a 3 x 3 chunk
// window (9,216 tiles) with N sources, where each tile can need up to 16 corner-to-corner segment_blocked traces per
// source in range. Wall-clock timing lives here, in tools/, outside the detlint-checked sim tree.
//   light_bench [--quick] [--enforce]   budget: <= 2 ms per rebuild (Release). Full rebuilds are reported against it; per
//   §12 the fallback when they exceed it is incremental invalidation, and --enforce exits 1 if that per-step path is over.
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sim/grid.hpp"
#include "sim/light.hpp"
#include "world/chunk.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
constexpr int N = world::kChunkSize;

std::vector<world::Chunk> window(bool walls) {
    std::vector<world::Chunk> cs;
    for (int cy = -1; cy <= 1; ++cy)
        for (int cx = -1; cx <= 1; ++cx) {
            world::Chunk c{};
            c.coord = {cx, cy, 0};
            c.palette = {"", "floor", "wall"};
            for (auto& cell : c.layers[0]) cell.tile = 1;
            if (walls)   // pillars every 4 tiles plus a broken wall every 8 rows: many partly blocked traces
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x)
                        if ((x % 4 == 2 && y % 4 == 2) || (y % 8 == 5 && x % 6 != 0))
                            c.layers[2][std::size_t(y * N + x)].flags = world::tile_flags::kBlocksSight;
            cs.push_back(c);
        }
    return cs;
}

struct Result { double median_ms, max_ms; long long pairs; double incr_median_ms; };

Result run(const Grid& g, int sources, uint16_t bright, uint16_t dim, int reps) {
    LightWorld w;
    w.grid = &g;
    for (int i = 0; i < sources; ++i) {   // spread deterministically over the 96 x 96 window
        AttachedLight a;
        a.id = uint32_t(i + 1); a.carrier = Carrier::Tile;
        a.tile = Vec2i{-N + 5 + (i * 37) % (3 * N - 10), -N + 5 + (i * 53) % (3 * N - 10)};
        a.bright_ft = bright; a.dim_ft = dim; a.flags = light_src::kLit;
        w.sources.push_back(attached_source(a, a.tile));
    }
    long long pairs = 0;
    const int reach = (bright + dim) / kFeetPerTile;
    for (int y = -N; y < 2 * N; ++y)
        for (int x = -N; x < 2 * N; ++x)
            for (const auto& s : w.sources)
                if (std::max(std::abs(x - s.tile.x), std::abs(y - s.tile.y)) <= reach) ++pairs;
    std::vector<double> ms;
    for (int r = 0; r < reps; ++r) {
        LightCache cache(w);
        const auto t0 = std::chrono::steady_clock::now();
        unsigned sink = 0;
        for (int y = -N; y < 2 * N; ++y)
            for (int x = -N; x < 2 * N; ++x) sink += unsigned(cache.at({x, y}).level);
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (sink == 0xFFFFFFFFu) std::puts("");
    }
    std::sort(ms.begin(), ms.end());
    // Incremental fallback (§7 / §12): source 1 moves one tile; only its old and new halo rects are invalidated.
    std::vector<double> inc;
    for (int r = 0; r < reps; ++r) {
        LightCache cache(w);
        for (int y = -N; y < 2 * N; ++y)
            for (int x = -N; x < 2 * N; ++x) (void)cache.at({x, y});
        LightWorld moved = w;
        const Vec2i from = moved.sources[0].tile, to{from.x + 1, from.y};
        moved.sources[0].tile = to;
        const RectI dirty[] = {{from.x - reach, from.y - reach, 2 * reach + 1, 2 * reach + 1},
                               {to.x - reach, to.y - reach, 2 * reach + 1, 2 * reach + 1}};
        const auto t0 = std::chrono::steady_clock::now();
        cache.begin_step(moved, dirty);
        unsigned sink = 0;
        for (int y = -N; y < 2 * N; ++y)
            for (int x = -N; x < 2 * N; ++x) sink += unsigned(cache.at({x, y}).level);
        const auto t1 = std::chrono::steady_clock::now();
        inc.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (sink == 0xFFFFFFFFu) std::puts("");
    }
    std::sort(inc.begin(), inc.end());
    return {ms[ms.size() / 2], ms.back(), pairs, inc[inc.size() / 2]};
}
} // namespace

int main(int argc, char** argv) {
    bool quick = false, enforce = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) quick = true;
        if (!std::strcmp(argv[i], "--enforce")) enforce = true;
    }
    const int reps = quick ? 1 : 9;
    const auto open_cs = window(false), wall_cs = window(true);
    std::vector<const world::Chunk*> po, pw;
    for (const auto& c : open_cs) po.push_back(&c);
    for (const auto& c : wall_cs) pw.push_back(&c);
    const Grid open_g(po), wall_g(pw);
    struct Case { const char* name; const Grid* g; int n; uint16_t b, d; bool budgeted; };
    const Case cases[] = {
        {"20 torches (20/20), open", &open_g, 20, 20, 20, true},
        {"20 torches (20/20), walls", &wall_g, 20, 20, 20, true},
        {"20 lanterns (30/30), walls", &wall_g, 20, 30, 30, true},
        {"20 Daylight (60/60, max radius), walls", &wall_g, 20, 60, 60, false},
    };
    bool over = false;
    std::printf("light_bench: full rebuild of 3x3 chunks (9216 tiles), %d reps, budget 2 ms\n", reps);
    for (const Case& c : cases) {
        const Result r = run(*c.g, c.n, c.b, c.d, reps);
        std::printf("  %-40s full median %8.3f ms (max %8.3f)  one-source-moved incremental %7.3f ms  pairs %lld (<= %lld traces)%s\n",
                    c.name, r.median_ms, r.max_ms, r.incr_median_ms, r.pairs, r.pairs * 16,
                    c.budgeted && r.median_ms > 2.0 ? "  FULL OVER BUDGET" : "");
        if (c.budgeted && r.incr_median_ms > 2.0) over = true;   // the per-step path (full rebuilds only on chunk / clock events)
    }
    return enforce && over ? 1 : 0;
}
