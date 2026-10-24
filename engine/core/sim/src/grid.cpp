#include "sim/grid.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "world/chunk_codec.hpp"

namespace eng::sim {

namespace {
constexpr int32_t N = world::kChunkSize;
// Fixed neighbour order: deterministic tie-breaking.
constexpr Vec2i kDirs[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
} // namespace

static int32_t floor_div(int32_t v) noexcept { return v >= 0 ? v / N : -((-v + N - 1) / N); }

Grid::Grid(const world::Chunk& c) {
    // Chunk-local frame (v0): the chunk is placed at (0,0) whatever its coord.
    level_ = c.coord.level;
    world::Chunk local = c;
    local.coord.x = 0;
    local.coord.y = 0;
    add_chunk(local);
}

Grid::Grid(std::span<const world::Chunk* const> chunks) {
    if (chunks.empty()) { error_ = "no chunks"; return; }
    level_ = chunks[0]->coord.level;
    for (const world::Chunk* c : chunks) {
        if (c->coord.level != level_) { error_ = "chunks span several levels"; return; }
        if (chunks_.count({c->coord.x, c->coord.y})) { error_ = "duplicate chunk coord"; return; }
        add_chunk(*c);
    }
}

void Grid::add_chunk(const world::Chunk& c) {
    Cells& cells = chunks_[{c.coord.x, c.coord.y}];
    for (int i = 0; i < N * N; ++i) {
        const auto ui = std::size_t(i);
        for (const auto& layer : c.layers)
            if (layer[ui].flags & world::tile_flags::kBlocksSight) cells.sight[ui] = 1;
        {   // feat/move-modes terrain bits
            bool water = false, climb = false, air = false, blocks = false;
            for (const auto& layer : c.layers) {
                water |= (layer[ui].flags & world::tile_flags::kWater) != 0;
                climb |= (layer[ui].flags & world::tile_flags::kClimbable) != 0;
                air |= (layer[ui].flags & world::tile_flags::kOpenAir) != 0;
                blocks |= (layer[ui].flags & world::tile_flags::kBlocksMove) != 0;
            }
            cells.terrain[ui] = uint8_t((water ? 1 : 0) | (climb ? 2 : 0) | (air && !blocks ? 4 : 0));
        }
        if (c.elevation_is_ground) {   // altitude PR2: chunk@3 ground + obstacle tops
            uint8_t g = 0, t = 0;
            for (const auto& layer : c.layers) {
                g = std::max(g, layer[ui].elevation);
                if (layer[ui].flags & world::tile_flags::kBlocksMove) t = std::max(t, layer[ui].elevation);
            }
            if (cells.terrain[ui] & 1) { cells.depth[ui] = g; g = 0; }   // water: elevation = depth, surface at 0
            cells.ground[ui] = g;
            cells.top[ui] = t;
            if (g != 0) has_ground_ = true;
        }
        if (c.layers[0][ui].tile == 0) continue;
        bool blocked = false, diff = false;
        for (const auto& layer : c.layers) {
            blocked |= (layer[ui].flags & world::tile_flags::kBlocksMove) != 0;
            diff |= (layer[ui].flags & world::tile_flags::kDifficult) != 0;
        }
        cells.walk[ui] = blocked ? 0 : (diff ? 2 : 1);
    }
}

std::pair<int32_t, int32_t> Grid::chunk_of(Vec2i p) noexcept { return {floor_div(p.x), floor_div(p.y)}; }

const uint8_t* Grid::walk_at(Vec2i p) const noexcept {
    const auto k = chunk_of(p);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) return nullptr;
    return &it->second.walk[std::size_t((p.y - k.second * N) * N + (p.x - k.first * N))];
}
const uint8_t* Grid::sight_at(Vec2i p) const noexcept {
    const auto k = chunk_of(p);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) return nullptr;
    return &it->second.sight[std::size_t((p.y - k.second * N) * N + (p.x - k.first * N))];
}

const Grid::Cells* Grid::cells_at(Vec2i p, std::size_t& idx) const noexcept {
    const auto k = chunk_of(p);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) return nullptr;
    idx = std::size_t((p.y - k.second * N) * N + (p.x - k.first * N));
    return &it->second;
}
int Grid::ground_ft(Vec2i p) const noexcept {
    std::size_t i = 0;
    const Cells* c = cells_at(p, i);
    return c ? int(c->ground[i]) * kFeetPerTile : 0;
}
int Grid::footprint_ground_ft(Vec2i anchor, int n) const noexcept {
    int g = 0;
    for (int dy = 0; dy < std::max(n, 1); ++dy)
        for (int dx = 0; dx < std::max(n, 1); ++dx) g = std::max(g, ground_ft({anchor.x + dx, anchor.y + dy}));
    return g;
}
int Grid::water_depth_ft(Vec2i p) const noexcept {
    std::size_t i = 0;
    const Cells* c = cells_at(p, i);
    return c && water(p) ? int(c->depth[i]) * kFeetPerTile : 0;
}
int Grid::obstacle_top_ft(Vec2i p) const noexcept {
    std::size_t i = 0;
    const Cells* c = cells_at(p, i);
    if (!c || c->top[i] == 0) return kFullHeight;
    return int(c->top[i]) * kFeetPerTile;
}
bool Grid::passable_over(Vec2i p, const MoveCaps& caps, int abs_ft) const noexcept {
    if (enterable(p, caps)) return true;
    return in_bounds(p) && obstacle_top_ft(p) < abs_ft;   // over a low wall / boulder / pillar
}

bool Grid::in_bounds(Vec2i p) const noexcept { return walk_at(p) != nullptr; }
bool Grid::walkable(Vec2i p) const noexcept { const uint8_t* w = walk_at(p); return w && *w != 0; }
bool Grid::difficult(Vec2i p) const noexcept { const uint8_t* w = walk_at(p); return w && *w == 2; }
bool Grid::blocks_sight(Vec2i p) const noexcept { const uint8_t* s = sight_at(p); return s && *s != 0; }

uint8_t Grid::terrain_at(Vec2i p) const noexcept {
    const auto k = chunk_of(p);
    auto it = chunks_.find(k);
    if (it == chunks_.end()) return 0;
    return it->second.terrain[std::size_t((p.y - k.second * N) * N + (p.x - k.first * N))];
}
bool Grid::water(Vec2i p) const noexcept { return (terrain_at(p) & 1) != 0 && walkable(p); }
bool Grid::climbable(Vec2i p) const noexcept { return (terrain_at(p) & 2) != 0; }
bool Grid::open_air(Vec2i p) const noexcept { return (terrain_at(p) & 4) != 0 && !walkable(p) && !climbable(p); }
bool Grid::enterable(Vec2i p, const MoveCaps& caps) const noexcept {
    return walkable(p) || climbable(p) || (caps.fly && open_air(p));
}
int Grid::tile_cost_ft(Vec2i p, const MoveCaps& caps) const noexcept {
    if (!walkable(p)) {
        if (climbable(p)) return (caps.climb || caps.fly) ? kFeetPerTile : 2 * kFeetPerTile;
        return kFeetPerTile;   // open air (flyer)
    }
    const int base = step_cost_ft(p);
    if (water(p) && !caps.swim && !caps.fly) return 2 * base;
    return base;
}
int Grid::move_cost_ft(Vec2i from, Vec2i to, int n, const MoveCaps& caps) const noexcept {
    int cost = 0;
    for (int dy = 0; dy < n; ++dy)
        for (int dx = 0; dx < n; ++dx) {
            const Vec2i t{to.x + dx, to.y + dy};
            const bool was_inside = n > 1 && t.x >= from.x && t.x < from.x + n && t.y >= from.y && t.y < from.y + n;
            if (!was_inside) cost = std::max(cost, tile_cost_ft(t, caps));
        }
    return cost;
}

bool Grid::footprint_walkable(Vec2i anchor, int n) const noexcept {
    for (int dy = 0; dy < n; ++dy)
        for (int dx = 0; dx < n; ++dx)
            if (!walkable({anchor.x + dx, anchor.y + dy})) return false;
    return true;
}

int Grid::move_cost_ft(Vec2i from, Vec2i to, int n) const noexcept {
    if (n <= 1) return step_cost_ft(to);
    int cost = 0;
    for (int dy = 0; dy < n; ++dy)
        for (int dx = 0; dx < n; ++dx) {
            const Vec2i t{to.x + dx, to.y + dy};
            const bool was_inside = t.x >= from.x && t.x < from.x + n && t.y >= from.y && t.y < from.y + n;
            if (!was_inside) cost = std::max(cost, step_cost_ft(t));
        }
    return cost;
}

bool Grid::line_of_sight(Vec2i a, Vec2i b) const noexcept {
    const int dx = a.x < b.x ? b.x - a.x : a.x - b.x, dy = a.y < b.y ? b.y - a.y : a.y - b.y;
    const int sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
    int x = a.x, y = a.y, ix = 0, iy = 0;
    auto blocked_mid = [&](Vec2i p) { return !(p == b) && blocks_sight(p); };
    while (ix < dx || iy < dy) {
        // Next boundary crossed along the centre-to-centre segment: compare (1+2ix)/dx vs (1+2iy)/dy.
        const int64_t l = int64_t(1 + 2 * ix) * dy, r = int64_t(1 + 2 * iy) * dx;
        if (l == r) {   // exactly through a corner
            if (blocked_mid({x + sx, y}) && blocked_mid({x, y + sy})) return false;
            x += sx; y += sy; ++ix; ++iy;
        } else if (l < r) { x += sx; ++ix; }
        else { y += sy; ++iy; }
        if (blocked_mid({x, y})) return false;
    }
    return true;
}

std::vector<Vec2i> Grid::path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                              int footprint) const {
    return path_impl(from, goals, occupied, footprint, nullptr);
}
std::vector<Vec2i> Grid::path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                              int footprint, const MoveCaps& caps) const {
    return path_impl(from, goals, occupied, footprint, &caps);
}
std::vector<Vec2i> Grid::path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                              int footprint, const MoveCaps& caps, int airborne_abs_ft) const {
    return path_impl(from, goals, occupied, footprint, &caps, airborne_abs_ft);
}
std::vector<Vec2i> Grid::path_impl(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied,
                                   int footprint, const MoveCaps* caps, int airborne_abs_ft) const {
    const bool air = caps && caps->fly && airborne_abs_ft >= 0;
    const bool under = caps && caps->burrow && airborne_abs_ft == kPathBurrowed;
    auto ok = [&](Vec2i t) {
        if (under) return walkable(t) && !water(t);
        if (air) return passable_over(t, *caps, airborne_abs_ft);
        return caps ? enterable(t, *caps) : walkable(t);
    };
    const bool vertical = caps && has_ground_;   // chunk@3 ground: vertical costs (flat maps: the old search)
    // Same search as v0 (Dijkstra, ordered set (cost, y, x), fixed neighbour order), over world
    // tiles with sparse bookkeeping so paths cross chunk boundaries.
    using Key = std::pair<int32_t, int32_t>;   // (y, x)
    auto key = [](Vec2i p) { return Key{p.y, p.x}; };
    std::map<Key, int> dist;
    std::map<Key, Vec2i> prev;
    std::set<Key> blocked, goal;
    for (Vec2i o : occupied) if (in_bounds(o)) blocked.insert(key(o));
    for (Vec2i g : goals) if (in_bounds(g)) goal.insert(key(g));
    if (!in_bounds(from) || goal.count(key(from))) return {};
    std::set<std::tuple<int, int, int>> open;
    dist[key(from)] = 0;
    open.insert({0, from.y, from.x});
    while (!open.empty()) {
        auto [d, y, x] = *open.begin();
        open.erase(open.begin());
        const Vec2i p{x, y};
        if (d != dist[key(p)]) continue;
        if (goal.count(key(p))) {
            std::vector<Vec2i> out;
            for (Vec2i q = p; !(q == from); q = prev[key(q)]) out.push_back(q);
            std::reverse(out.begin(), out.end());
            return out;
        }
        for (Vec2i dxy : kDirs) {
            const Vec2i q{p.x + dxy.x, p.y + dxy.y};
            if (footprint <= 1) {
                if (!ok(q) || blocked.count(key(q))) continue;
            } else {
                bool fits = true;
                for (int fy = 0; fy < footprint && fits; ++fy)
                    for (int fx = 0; fx < footprint && fits; ++fx) {
                        const Vec2i t{q.x + fx, q.y + fy};
                        fits = ok(t) && !blocked.count(key(t));
                    }
                if (!fits) continue;
            }
            int step = caps ? move_cost_ft(p, q, footprint, *caps) : move_cost_ft(p, q, footprint);
            if (under) {
                step = kFeetPerTile;
            } else if (air) {
                step = kFeetPerTile + std::max(0, footprint_ground_ft(q, footprint) - airborne_abs_ft);
            } else if (vertical) {
                const int g0 = footprint_ground_ft(p, footprint), g1 = footprint_ground_ft(q, footprint);
                if (!caps->fly && ledge_ft(g0, g1) < -kWalkableStepFt) continue;   // never plan a fall
                step += eng::sim::step_cost_ft(g0, g1, *caps);
            }
            const int nd = d + step;
            auto it = dist.find(key(q));
            if (it == dist.end() || nd < it->second) {
                dist[key(q)] = nd;
                prev[key(q)] = p;
                open.insert({nd, q.y, q.x});
            }
        }
    }
    return {};
}

} // namespace eng::sim
