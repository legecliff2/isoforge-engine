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

// SRD 5.1 movement modes (feat/move-modes): which non-walking speeds a mover has. Default: a walker only.
struct MoveCaps {
    bool fly = false, swim = false, climb = false;
};

// ---- altitude PR2: ground height (chunk@3, docs/CHUNK_HASH.md "chunk@3 ground") ----
// Pure rules, mirrored in Python by tools/validate/validate_packs.py (ledge_ft / step_extra_ft):
//   ground_ft(tile)  = 5 * max elevation over the cell's layers, only for chunk@3 (0 for chunk@1 / chunk@2).
//   ledge_ft(a, b)   = b - a: the signed rise in feet from ground a to ground b (negative = a drop).
//   step_cost_ft(a, b, caps) = EXTRA feet for a step from ground a to ground b, on top of the tile cost:
//     |rise| <= 5 (kWalkableStepFt): |rise| (5 ft of movement per 5 ft of rise or descent);
//     rise  > 5: a climb: rise with a climb or fly speed, else 2 * rise (SRD climbing without a climb speed);
//     drop  > 5: 0 - the mover falls (walker) or becomes airborne (flyer); the sim records it, pathing avoids it
//                for non-flyers.
inline constexpr int kWalkableStepFt = 5;
constexpr int ledge_ft(int from_ground_ft, int to_ground_ft) noexcept { return to_ground_ft - from_ground_ft; }
constexpr int step_cost_ft(int from_ground_ft, int to_ground_ft, const MoveCaps& caps) noexcept {
    const int rise = ledge_ft(from_ground_ft, to_ground_ft);
    if (rise < -kWalkableStepFt) return 0;
    if (rise <= kWalkableStepFt) return rise < 0 ? -rise : rise;
    return (caps.climb || caps.fly) ? rise : 2 * rise;
}
// House rule (altitude design §3): a creature's vertical span is [abs, abs + tiles * 5) ft, where Medium and smaller
// are 1 tile tall and an N x N creature is N tiles tall.
constexpr int body_height_ft(int footprint) noexcept { return (footprint < 1 ? 1 : footprint) * kFeetPerTile; }
constexpr bool spans_overlap(int a_abs_ft, int a_footprint, int b_abs_ft, int b_footprint) noexcept {
    return a_abs_ft < b_abs_ft + body_height_ft(b_footprint) && b_abs_ft < a_abs_ft + body_height_ft(a_footprint);
}

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

    // feat/move-modes terrain (chunk flags water / climbable / open_air). Maps without them behave as before.
    bool water(Vec2i p) const noexcept;
    bool climbable(Vec2i p) const noexcept;   // climb terrain (even when it blocks walking)
    bool open_air(Vec2i p) const noexcept;    // no footing, nothing blocks: only flyers enter
    // May a mover with `caps` occupy p: walkable, or climbable (anyone may climb), or open air for a flyer.
    bool enterable(Vec2i p, const MoveCaps& caps) const noexcept;
    // Feet to enter p: 5 (10 difficult); SRD "climbing, swimming": without a climb / swim speed (or flight) each foot
    // costs 1 extra foot (2 in difficult terrain). Open air costs 5 for a flyer.
    int  tile_cost_ft(Vec2i p, const MoveCaps& caps) const noexcept;
    int  move_cost_ft(Vec2i from, Vec2i to, int n, const MoveCaps& caps) const noexcept;

    // Shortest path (by feet, ties broken by fixed neighbour order) from `from` to any tile in
    // `goals`, avoiding `occupied`. Excludes `from`; empty if unreachable or already at a goal.
    // `footprint` > 1 paths an n x n creature: `from`, `goals` and the result are anchors, a node is
    // enterable only if every tile of its footprint is walkable and not in `occupied` (the mover's own
    // tiles must not be listed there), and a step costs move_cost_ft. No squeezing: a Large creature
    // does not fit a one-tile gap. footprint == 1 is the v0 search, unchanged.
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                            int footprint = 1) const;
    // feat/move-modes: the same search over enterable(caps) tiles priced by tile_cost_ft(caps).
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                            int footprint, const MoveCaps& caps) const;
    // altitude PR2: with chunk@3 ground the caps search adds step_cost_ft(ground) per step and never plans a drop of
    // more than 5 ft for a non-flyer. airborne_abs_ft >= 0 plans an airborne flyer at that absolute height: 5 ft per
    // tile (difficult ignored), obstacles lower than it are passable, higher ground costs the extra rise.
    std::vector<Vec2i> path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                            int footprint, const MoveCaps& caps, int airborne_abs_ft) const;

    // altitude PR2 ground (chunk@3 only; 0 everywhere on chunk@1 / chunk@2 maps).
    bool has_ground() const noexcept { return has_ground_; }
    int  ground_ft(Vec2i p) const noexcept;
    // Footprint ground = the MAX ground over the n x n tiles (a creature stands on the highest tile it covers).
    int  footprint_ground_ft(Vec2i anchor, int n) const noexcept;
    // Top of a blocks_move obstacle in ft (5 * its elevation, chunk@3 only); kFullHeight for a full-height blocker
    // (elevation 0, or any chunk@1 / chunk@2 blocker): no flyer passes over those.
    static constexpr int kFullHeight = 1 << 30;
    int  obstacle_top_ft(Vec2i p) const noexcept;
    // chunk@3 water: a water cell's elevation is its DEPTH (the surface is ground 0); a swimmer may descend to
    // alt_ft = -water_depth_ft. 0 on flat maps and dry tiles (no descent).
    int  water_depth_ft(Vec2i p) const noexcept;
    // May an airborne creature at abs_ft occupy p: enterable(caps) or an obstacle lower than abs_ft.
    bool passable_over(Vec2i p, const MoveCaps& caps, int abs_ft) const noexcept;

private:
    struct Cells {
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> walk{};   // 0 blocked, 1 normal, 2 difficult
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> sight{};  // 1 = blocks sight
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> terrain{}; // bit0 water, bit1 climbable, bit2 open air
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> ground{};  // chunk@3 ground (elevation steps)
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> top{};     // chunk@3 obstacle top steps (0 = full)
        std::array<uint8_t, world::kChunkSize * world::kChunkSize> depth{};   // chunk@3 water depth steps
    };
    void add_chunk(const world::Chunk& c);
    std::vector<Vec2i> path_impl(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                                 int footprint, const MoveCaps* caps, int airborne_abs_ft = -1) const;
    const Cells* cells_at(Vec2i p, std::size_t& idx) const noexcept;
    const uint8_t* walk_at(Vec2i p) const noexcept;
    const uint8_t* sight_at(Vec2i p) const noexcept;
    uint8_t terrain_at(Vec2i p) const noexcept;

    std::map<std::pair<int32_t, int32_t>, Cells> chunks_;   // by chunk (x, y)
    int16_t level_ = 0;
    bool has_ground_ = false;
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
