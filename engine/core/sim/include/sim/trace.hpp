#pragma once
// Shared deterministic line trace (LIGHTING.md §5): the one segment test used by both Combat::cover_of (cover) and the
// light model (occlusion). Moved from combat.cpp (R-G13 phase 1), made allocation-free with identical results; there is no
// second ray algorithm.
#include <cstdint>

#include "base/math.hpp"

namespace eng::sim {

// Does the segment between lattice points p and q (tile corners; tile (x, y) spans [x, x+1] x [y, y+1]) pass an obstruction?
// Integer only. The breakpoints t = i / |dx| and j / |dy| (scaled by L = |dx| * |dy|) split it into pieces that each lie
// inside one tile, or along a grid edge (axis-aligned). A piece inside a tile is blocked when that tile blocks; a piece along
// an edge, or the segment passing exactly through a corner, only when both tiles beside it block (as Grid::line_of_sight: a
// line grazing a lone obstruction, or slipping between two diagonal ones, is not blocked).
template <class F> bool segment_blocked(Vec2i p, Vec2i q, F&& blocks) {
    // Allocation-free (perf/trace-noalloc): the breakpoints {0, L} U {i * (L / ax)} U {j * (L / ay)} are two arithmetic
    // sequences, merged in ascending order on the fly with duplicates collapsed, instead of a sorted vector. Pieces and
    // probes are visited in exactly the original order (tests/trace_reference.hpp keeps the original for equivalence).
    const int64_t dx = q.x - p.x, dy = q.y - p.y, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax == 0 && ay == 0) return false;
    const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const int64_t L = (ax ? ax : 1) * (ay ? ay : 1), L2 = 2 * L;
    const int64_t step_a = ax ? L / ax : 0, step_b = ay ? L / ay : 0;
    int64_t ia = 1, ib = 1;   // next indices in each sequence (valid while < ax / < ay)
    auto next_break = [&]() -> int64_t {
        const int64_t va = ia < ax ? ia * step_a : L, vb = ib < ay ? ib * step_b : L;
        const int64_t v = va < vb ? va : vb;
        if (ia < ax && va == v) ++ia;
        if (ib < ay && vb == v) ++ib;
        return v;
    };
    auto fdiv = [](int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
    int64_t cur = 0;
    for (bool first = true; cur < L; first = false) {
        const int64_t nxt = next_break();
        if (!first && ax && ay) {   // exactly through a lattice corner?
            const int64_t X = L * p.x + dx * cur, Y = L * p.y + dy * cur;
            if (X % L == 0 && Y % L == 0) {
                const int cx = int(X / L), cy = int(Y / L), bx = sx > 0 ? cx - 1 : cx, by = sy > 0 ? cy - 1 : cy;
                if (blocks(Vec2i{bx + sx, by}) && blocks(Vec2i{bx, by + sy})) return true;
            }
        }
        const int64_t m = cur + nxt;                                        // midpoint parameter, units of 1 / (2L)
        const int64_t X = L2 * p.x + dx * m, Y = L2 * p.y + dy * m;         // midpoint * 2L
        const int tx = int(fdiv(X, L2)), ty = int(fdiv(Y, L2));
        if (X % L2 == 0) { if (blocks(Vec2i{tx - 1, ty}) && blocks(Vec2i{tx, ty})) return true; }   // along a vertical edge
        else if (Y % L2 == 0) { if (blocks(Vec2i{tx, ty - 1}) && blocks(Vec2i{tx, ty})) return true; }   // horizontal edge
        else if (blocks(Vec2i{tx, ty})) return true;
        cur = nxt;
    }
    return false;
}

// The same test walked as an integer DDA (perf/vis-first-step): visits exactly the probes of segment_blocked (every piece
// inside a tile, every lattice corner the segment passes through, every unit edge of an axis-aligned segment), so the
// answer is identical for every predicate; only the visiting order differs (the result is an OR, so order is irrelevant).
// Crossings are ordered by comparing i * |dy| with j * |dx| instead of dividing. Pinned against segment_blocked and the
// original trace by tests/vis_first_step_tests.cpp. Used by the visibility pass; cover and light keep segment_blocked.
template <class F> bool segment_blocked_dda(Vec2i p, Vec2i q, F&& blocks) {
    const int32_t dx = q.x - p.x, dy = q.y - p.y;
    const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax == 0 && ay == 0) return false;
    const int32_t sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    if (ax == 0) {
        for (int32_t k = 0; k < ay; ++k) {
            const int32_t y = sy > 0 ? p.y + k : p.y - 1 - k;
            if (blocks(Vec2i{p.x - 1, y}) && blocks(Vec2i{p.x, y})) return true;
        }
        return false;
    }
    if (ay == 0) {
        for (int32_t k = 0; k < ax; ++k) {
            const int32_t x = sx > 0 ? p.x + k : p.x - 1 - k;
            if (blocks(Vec2i{x, p.y - 1}) && blocks(Vec2i{x, p.y})) return true;
        }
        return false;
    }
    int32_t tx = sx > 0 ? p.x : p.x - 1, ty = sy > 0 ? p.y : p.y - 1;
    int64_t i = 1, j = 1;
    for (;;) {
        if (blocks(Vec2i{tx, ty})) return true;
        const int64_t a = i * ay, b = j * ax;
        if (a < b) { tx += sx; ++i; }
        else if (b < a) { ty += sy; ++j; }
        else {
            if (i == ax) return false;   // reached q (then j == ay)
            if (blocks(Vec2i{tx + sx, ty}) && blocks(Vec2i{tx, ty + sy})) return true;
            tx += sx; ty += sy; ++i; ++j;
        }
    }
}

} // namespace eng::sim
