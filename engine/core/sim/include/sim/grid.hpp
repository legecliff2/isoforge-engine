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

    // Shortest path (by feet, ties broken by fixed neighbour order) from `from` to any tile in
    // `goals`, avoiding `occupied`. Excludes `from`; empty if unreachable or already at a goal.
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied) const;

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
// Reach in tiles from feet (5 ft = adjacent).
constexpr int reach_tiles(int feet) noexcept { return feet / kFeetPerTile; }

} // namespace eng::sim
