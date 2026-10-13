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

bool Grid::in_bounds(Vec2i p) const noexcept { return walk_at(p) != nullptr; }
bool Grid::walkable(Vec2i p) const noexcept { const uint8_t* w = walk_at(p); return w && *w != 0; }
bool Grid::difficult(Vec2i p) const noexcept { const uint8_t* w = walk_at(p); return w && *w == 2; }
bool Grid::blocks_sight(Vec2i p) const noexcept { const uint8_t* s = sight_at(p); return s && *s != 0; }

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

std::vector<Vec2i> Grid::path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied) const {
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
            if (!walkable(q) || blocked.count(key(q))) continue;
            const int nd = d + step_cost_ft(q);
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
