#include "sim/grid.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "world/chunk_codec.hpp"

namespace eng::sim {

namespace {
constexpr int N = world::kChunkSize;
// Fixed neighbour order: deterministic tie-breaking.
constexpr Vec2i kDirs[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
} // namespace

Grid::Grid(const world::Chunk& c) {
    for (int i = 0; i < N * N; ++i) {
        const auto ui = std::size_t(i);
        if (c.layers[0][ui].tile == 0) continue;
        bool blocked = false, diff = false;
        for (const auto& layer : c.layers) {
            blocked |= (layer[ui].flags & world::tile_flags::kBlocksMove) != 0;
            diff |= (layer[ui].flags & world::tile_flags::kDifficult) != 0;
        }
        walk_[ui] = blocked ? 0 : (diff ? 2 : 1);
    }
}

bool Grid::in_bounds(Vec2i p) const noexcept { return p.x >= 0 && p.y >= 0 && p.x < N && p.y < N; }
bool Grid::walkable(Vec2i p) const noexcept { return in_bounds(p) && walk_[std::size_t(p.y * N + p.x)] != 0; }
bool Grid::difficult(Vec2i p) const noexcept { return in_bounds(p) && walk_[std::size_t(p.y * N + p.x)] == 2; }

std::vector<Vec2i> Grid::path(Vec2i from, const std::vector<Vec2i>& goals, const std::vector<Vec2i>& occupied) const {
    auto idx = [](Vec2i p) { return std::size_t(p.y * N + p.x); };
    std::vector<int> dist(std::size_t(N * N), -1);
    std::vector<int> prev(std::size_t(N * N), -1);
    std::vector<uint8_t> blocked(std::size_t(N * N), 0), goal(std::size_t(N * N), 0);
    for (Vec2i o : occupied) if (in_bounds(o)) blocked[idx(o)] = 1;
    for (Vec2i g : goals) if (in_bounds(g)) goal[idx(g)] = 1;
    if (!in_bounds(from) || goal[idx(from)]) return {};
    // Dijkstra with an ordered set (cost, y, x): fully deterministic.
    std::set<std::tuple<int, int, int>> open;
    dist[idx(from)] = 0;
    open.insert({0, from.y, from.x});
    while (!open.empty()) {
        auto [d, y, x] = *open.begin();
        open.erase(open.begin());
        const Vec2i p{x, y};
        if (d != dist[idx(p)]) continue;
        if (goal[idx(p)]) {
            std::vector<Vec2i> out;
            for (int i = int(idx(p)); i != int(idx(from)); i = prev[std::size_t(i)]) out.push_back({i % N, i / N});
            std::reverse(out.begin(), out.end());
            return out;
        }
        for (Vec2i dxy : kDirs) {
            const Vec2i q{p.x + dxy.x, p.y + dxy.y};
            if (!walkable(q) || blocked[idx(q)]) continue;
            const int nd = d + step_cost_ft(q);
            if (dist[idx(q)] == -1 || nd < dist[idx(q)]) {
                dist[idx(q)] = nd;
                prev[idx(q)] = int(idx(p));
                open.insert({nd, q.y, q.x});
            }
        }
    }
    return {};
}

} // namespace eng::sim
