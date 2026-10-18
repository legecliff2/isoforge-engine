#pragma once
// Tactical grid over one or more chunks of one level: 5 ft per tile, 8-way movement, diagonals
// cost 5 ft (SRD default, §7 M1). Difficult terrain costs 10 ft per tile.
// Coordinates are WORLD tiles: world = chunk.coord * kChunkSize + local (floor division back).
// A chunk at coord (0,0) therefore keeps local == world (all v0 goldens). Tiles of chunks not in
// the grid are out of bounds (not walkable, do not block sight).
#include <array>
#include <map>
#include <span>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/math.hpp"
#include "world/chunk.hpp"

namespace eng::sim {

inline constexpr int kFeetPerTile = 5;

class Grid {
public:
    // One chunk in its chunk-local frame (as if at coord (0,0)); v0 behaviour.
    explicit Grid(const world::Chunk& c);
    // Several chunks (same level, distinct coords; order irrelevant). Empty or mixed levels:
    // check ok() / error().
    explicit Grid(std::span<const world::Chunk* const> chunks);

    bool ok() const noexcept { return error_.empty(); }
    const std::string& error() const noexcept { return error_; }
    int16_t level() const noexcept { return level_; }
    // World tile of a chunk-local tile, and back.
    static Vec2i to_world(world::ChunkCoord c, Vec2i local) noexcept {
        return {c.x * world::kChunkSize + local.x, c.y * world::kChunkSize + local.y};
    }
    static std::pair<int32_t, int32_t> chunk_of(Vec2i world_tile) noexcept;

    bool in_bounds(Vec2i p) const noexcept;
    // Walkable: has a ground tile (layer 0 tile != 0) and no layer sets blocks_move.
    bool walkable(Vec2i p) const noexcept;
    bool difficult(Vec2i p) const noexcept;
    // Some layer sets blocks_sight (flag bit 1).
    bool blocks_sight(Vec2i p) const noexcept;
    // Line of sight between tile centres (integer supercover traversal, symmetric). Blocked if a
    // tile strictly between the endpoints blocks sight; where the line passes exactly through a
    // corner it is blocked only if both tiles beside the corner block sight.
    bool line_of_sight(Vec2i a, Vec2i b) const noexcept;
    int  step_cost_ft(Vec2i to) const noexcept { return difficult(to) ? 2 * kFeetPerTile : kFeetPerTile; }
    // Square footprints (side n, anchored at the min-x/min-y tile). All n*n tiles walkable.
    bool footprint_walkable(Vec2i anchor, int n) const noexcept;
    // Cost in feet of moving a footprint from anchor `from` one tile to the adjacent anchor `to`: the
    // dearest step_cost_ft over the tiles it newly enters (just step_cost_ft(to) when n == 1).
    int  move_cost_ft(Vec2i from, Vec2i to, int n) const noexcept;

    // Shortest path (by feet, ties broken by fixed neighbour order) from `from` to any tile in
    // `goals`, avoiding `occupied`. Excludes `from`; empty if unreachable or already at a goal.
    // `footprint` > 1 paths an n x n creature: `from`, `goals` and the result are anchors, a node is
    // enterable only if every tile of its footprint is walkable and not in `occupied` (the mover's own
    // tiles must not be listed there), and a step costs move_cost_ft. No squeezing: a Large creature
    // does not fit a one-tile gap. footprint == 1 is the v0 search, unchanged.
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                            int footprint = 1) const;

private:
    struct Cells {
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> walk{};   // 0 blocked, 1 normal, 2 difficult
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> sight{};  // 1 = blocks sight
    };
    void add_chunk(const world::Chunk& c);
    const uint8_t* walk_at(Vec2i p) const noexcept;
    const uint8_t* sight_at(Vec2i p) const noexcept;

    std::map<std::pair<int32_t, int32_t>, Cells> chunks_;   // by chunk (x, y)
    int16_t level_ = 0;
    std::string error_;
};

constexpr int chebyshev(Vec2i a, Vec2i b) noexcept {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}
// Distance in tiles between the nearest tiles of two square footprints (side na / nb, anchored at their
// min-x/min-y tile): 0 when they overlap, 1 when adjacent (incl. diagonally). SRD reach and range are
// measured to the nearest part of a creature's space. For na == nb == 1 this is chebyshev(a, b).
constexpr int rect_distance(Vec2i a, int na, Vec2i b, int nb) noexcept {
    const int dx = a.x > b.x + nb - 1 ? a.x - (b.x + nb - 1) : (b.x > a.x + na - 1 ? b.x - (a.x + na - 1) : 0);
    const int dy = a.y > b.y + nb - 1 ? a.y - (b.y + nb - 1) : (b.y > a.y + na - 1 ? b.y - (a.y + na - 1) : 0);
    return dx > dy ? dx : dy;
}
// Reach in tiles from feet (5 ft = adjacent).
constexpr int reach_tiles(int feet) noexcept { return feet / kFeetPerTile; }

} // namespace eng::sim
