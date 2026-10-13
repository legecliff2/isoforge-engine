#pragma once
// Tactical grid over one chunk (v0): 5 ft per tile, 8-way movement, diagonals cost 5 ft
// (SRD default, §7 M1). Difficult terrain costs 10 ft per tile.
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "base/math.hpp"
#include "world/chunk.hpp"

namespace eng::sim {

inline constexpr int kFeetPerTile = 5;

class Grid {
public:
    explicit Grid(const world::Chunk& c);

    bool in_bounds(Vec2i p) const noexcept;
    // Walkable: has a ground tile (layer 0 tile != 0) and no layer sets blocks_move.
    bool walkable(Vec2i p) const noexcept;
    bool difficult(Vec2i p) const noexcept;
    int  step_cost_ft(Vec2i to) const noexcept { return difficult(to) ? 2 * kFeetPerTile : kFeetPerTile; }

    // Shortest path (by feet, ties broken by fixed neighbour order) from `from` to any tile in
    // `goals`, avoiding `occupied`. Excludes `from`; empty if unreachable or already at a goal.
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied) const;

private:
    std::array<uint8_t, world::kChunkSize * world::kChunkSize> walk_{};   // 0 blocked, 1 normal, 2 difficult
};

constexpr int chebyshev(Vec2i a, Vec2i b) noexcept {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}
// Reach in tiles from feet (5 ft = adjacent).
constexpr int reach_tiles(int feet) noexcept { return feet / kFeetPerTile; }

} // namespace eng::sim
