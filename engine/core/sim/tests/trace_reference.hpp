#pragma once
// TEST-ONLY reference: the original allocating segment_blocked from sim/trace.hpp at 480c0ae (moved from combat.cpp in
// #205), kept verbatim so trace_equivalence_tests can prove the allocation-free version gives identical results.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/math.hpp"

namespace eng::sim::ref {

// Does the segment between lattice points p and q (tile corners; tile (x, y) spans [x, x+1] x [y, y+1]) pass an obstruction?
// Integer only. The breakpoints t = i / |dx| and j / |dy| (scaled by L = |dx| * |dy|) split it into pieces that each lie
// inside one tile, or along a grid edge (axis-aligned). A piece inside a tile is blocked when that tile blocks; a piece along
// an edge, or the segment passing exactly through a corner, only when both tiles beside it block (as Grid::line_of_sight: a
// line grazing a lone obstruction, or slipping between two diagonal ones, is not blocked).
template <class F> bool segment_blocked(Vec2i p, Vec2i q, F&& blocks) {
    const int64_t dx = q.x - p.x, dy = q.y - p.y, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax == 0 && ay == 0) return false;
    const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const int64_t L = (ax ? ax : 1) * (ay ? ay : 1), L2 = 2 * L;
    std::vector<int64_t> br{0, L};
    for (int64_t i = 1; i < ax; ++i) br.push_back(i * (L / ax));
    for (int64_t j = 1; j < ay; ++j) br.push_back(j * (L / ay));
    std::sort(br.begin(), br.end());
    br.erase(std::unique(br.begin(), br.end()), br.end());
    auto fdiv = [](int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
    for (std::size_t k = 0; k + 1 < br.size(); ++k) {
        if (k > 0 && ax && ay) {   // exactly through a lattice corner?
            const int64_t X = L * p.x + dx * br[k], Y = L * p.y + dy * br[k];
            if (X % L == 0 && Y % L == 0) {
                const int cx = int(X / L), cy = int(Y / L), bx = sx > 0 ? cx - 1 : cx, by = sy > 0 ? cy - 1 : cy;
                if (blocks(Vec2i{bx + sx, by}) && blocks(Vec2i{bx, by + sy})) return true;
            }
        }
        const int64_t m = br[k] + br[k + 1];                                // midpoint parameter, units of 1 / (2L)
        const int64_t X = L2 * p.x + dx * m, Y = L2 * p.y + dy * m;         // midpoint * 2L
        const int tx = int(fdiv(X, L2)), ty = int(fdiv(Y, L2));
        if (X % L2 == 0) { if (blocks(Vec2i{tx - 1, ty}) && blocks(Vec2i{tx, ty})) return true; }   // along a vertical edge
        else if (Y % L2 == 0) { if (blocks(Vec2i{tx, ty - 1}) && blocks(Vec2i{tx, ty})) return true; }   // horizontal edge
        else if (blocks(Vec2i{tx, ty})) return true;
    }
    return false;
}

} // namespace eng::sim::ref
