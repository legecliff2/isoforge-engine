#include "sim/visibility.hpp"

#include <algorithm>
#include <utility>

#include "rules5e/spells.hpp"
#include "rules5e/types.hpp"
#include "sim/trace.hpp"
#include "world/chunk_streamer.hpp"

namespace eng::sim {

namespace {
int32_t iabs32(int32_t v) noexcept { return v < 0 ? -v : v; }
int dist_ft(Vec2i a, Vec2i b) noexcept {
    return int(std::max(iabs32(a.x - b.x), iabs32(a.y - b.y))) * kFeetPerTile;
}
bool within(uint16_t r, int d) noexcept { return r != 0 && d <= int(r); }

// The tile rule shared by the reference and the chunk pass; `los` is only called when the answer depends on it.
template <class Los> bool visible_rule(const Viewer& v, LightSample s, int d, Los&& los) {
    if (within(v.senses.blindsight_ft, d) || within(v.senses.tremorsense_ft, d)) return true;
    if (v.blinded) return false;
    const bool heavy = perceived_light(s, v.senses, d) == Light::Dark || (s.flags & light_flags::kObscuredHeavy) != 0;
    if (heavy && !within(v.senses.truesight_ft, d)) return false;
    return los();
}

// Walls as light_reaches sees them (blocks_sight or an opaque zone) over a box, with 2D prefix sums.
struct WallMap {
    Vec2i lo{}, hi{};   // inclusive
    int w = 0, h = 0;
    std::vector<uint8_t> wall;
    std::vector<int32_t> psum;   // (w + 1) * (h + 1)
    void build(const LightWorld& lw, Vec2i a, Vec2i b) {
        lo = a; hi = b;
        w = hi.x - lo.x + 1; h = hi.y - lo.y + 1;
        wall.assign(std::size_t(w) * std::size_t(h), 0);
        std::vector<const LightZone*> opaque;
        for (const auto& z : lw.zones)
            if (z.flags & rules5e::kZoneOpaque) opaque.push_back(&z);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Vec2i p{lo.x + x, lo.y + y};
                bool b2 = lw.grid && lw.grid->blocks_sight(p);
                for (std::size_t i = 0; !b2 && i < opaque.size(); ++i)
                    b2 = std::binary_search(opaque[i]->tiles.begin(), opaque[i]->tiles.end(), p);
                wall[std::size_t(y * w + x)] = b2 ? 1 : 0;
            }
        psum.assign(std::size_t(w + 1) * std::size_t(h + 1), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                psum[std::size_t((y + 1) * (w + 1) + x + 1)] = wall[std::size_t(y * w + x)] +
                    psum[std::size_t(y * (w + 1) + x + 1)] + psum[std::size_t((y + 1) * (w + 1) + x)] -
                    psum[std::size_t(y * (w + 1) + x)];
    }
    bool at(Vec2i p) const noexcept {
        if (p.x < lo.x || p.y < lo.y || p.x > hi.x || p.y > hi.y) return false;   // never reached: segments stay in the box
        return wall[std::size_t((p.y - lo.y) * w + (p.x - lo.x))] != 0;
    }
    int32_t count(Vec2i a, Vec2i b) const noexcept {   // walls in the inclusive box spanned by a and b
        const int x0 = std::min(a.x, b.x) - lo.x, x1 = std::max(a.x, b.x) - lo.x + 1;
        const int y0 = std::min(a.y, b.y) - lo.y, y1 = std::max(a.y, b.y) - lo.y + 1;
        auto P = [&](int x, int y) { return psum[std::size_t(y * (w + 1) + x)]; };
        return P(x1, y1) - P(x0, y1) - P(x1, y0) + P(x0, y0);
    }
    // == light_reaches(lw, s, t) for s, t inside the box.
    bool reaches(Vec2i s, Vec2i t) const {
        if (s == t) return true;
        if (count(s, t) - (at(s) ? 1 : 0) - (at(t) ? 1 : 0) == 0) return true;
        auto blocks = [&](Vec2i p) { return p != s && p != t && at(p); };
        for (int oy = 0; oy <= 1; ++oy)
            for (int ox = 0; ox <= 1; ++ox)
                for (int ty = 0; ty <= 1; ++ty)
                    for (int tx = 0; tx <= 1; ++tx)
                        if (!segment_blocked(Vec2i{s.x + ox, s.y + oy}, Vec2i{t.x + tx, t.y + ty}, blocks)) return true;
        return false;
    }
};
} // namespace

SenseRanges senses_of(const rules5e::Combatant& c) noexcept {
    return SenseRanges{c.darkvision_ft, c.blindsight_ft, c.tremorsense_ft, c.truesight_ft};
}

Light perceived_light(LightSample s, const SenseRanges& senses, int d) noexcept {
    Light l = s.level;
    if (within(senses.darkvision_ft, d) && (s.flags & light_flags::kMagicalDark) == 0 && l != Light::Bright)
        l = Light(uint8_t(uint8_t(l) + 1));
    return l;
}

bool tile_visible(const LightWorld& w, LightCache& cache, const Viewer& v, Vec2i t) {
    if (v.on_footprint(t)) return true;
    return visible_rule(v, cache.at(t), dist_ft(v.tile, t), [&] { return light_reaches(w, v.tile, t); });
}

TileMask VisibilityState::explored(TeamId team, world::ChunkCoord c) const {
    auto it = explored_.find(team);
    if (it == explored_.end()) return {};
    auto jt = it->second.find(c);
    return jt == it->second.end() ? TileMask{} : jt->second;
}

void VisibilityState::begin_step(LightCache& cache, std::span<const Viewer> viewers,
                                 std::span<const world::ChunkCoord> chunks) {
    std::vector<world::ChunkCoord> coords(chunks.begin(), chunks.end());
    std::sort(coords.begin(), coords.end());
    coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
    std::vector<Viewer> vs(viewers.begin(), viewers.end());
    std::sort(vs.begin(), vs.end(), [](const Viewer& a, const Viewer& b) {
        return a.team != b.team ? a.team < b.team : a.id < b.id;
    });
    for (const auto& v : vs) explored_[v.team];   // a team is known once it has had a viewer

    constexpr int N = world::kChunkSize;
    WallMap walls;
    const bool any_work = !coords.empty() && !vs.empty();
    if (!any_work) los_.reset();
    if (any_work) {
        Vec2i lo = Grid::to_world(coords.front(), {0, 0}), hi = lo;
        auto grow = [&](Vec2i p) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        };
        for (const auto& c : coords) { grow(Grid::to_world(c, {0, 0})); grow(Grid::to_world(c, {N - 1, N - 1})); }
        for (const auto& v : vs) grow(v.tile);
        walls.build(cache.world(), lo, hi);
        // Line-of-sight answers are memoised per viewer tile across steps while the walls (and the box) stay the same:
        // a viewer that doesn't move costs no traces. Any wall / opaque zone change or a new box drops them all.
        if (!los_ || los_->lo != walls.lo || los_->hi != walls.hi || los_->wall != walls.wall) {
            seen_.clear();   // line of sight may differ anywhere
            los_.emplace();
            los_->lo = walls.lo; los_->hi = walls.hi; los_->wall = walls.wall;
        }
        std::map<Vec2i, std::vector<uint8_t>> keep;   // only this step's viewer tiles survive
        for (const auto& v : vs) {
            auto it = los_->by_tile.find(v.tile);
            if (it != los_->by_tile.end()) keep.emplace(v.tile, std::move(it->second));
            else keep.emplace(v.tile, std::vector<uint8_t>(walls.wall.size(), 0));
        }
        los_->by_tile = std::move(keep);
    }
    auto los = [&](const Viewer& v, Vec2i t) {
        uint8_t& m = los_->by_tile[v.tile][std::size_t((t.y - walls.lo.y) * walls.w + (t.x - walls.lo.x))];
        if (m == 0) m = walls.reaches(v.tile, t) ? 1 : 2;
        return m == 1;
    };

    // Samples once per chunk (shared by every viewer).
    std::vector<std::array<LightSample, N * N>> samples(any_work ? coords.size() : 0);
    for (std::size_t ci = 0; ci < samples.size(); ++ci)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) samples[ci][std::size_t(y * N + x)] = cache.at(Grid::to_world(coords[ci], {x, y}));

    auto same_viewer = [](const Viewer& a, const Viewer& b) {
        return a.id == b.id && a.team == b.team && a.tile == b.tile && a.senses == b.senses && a.blinded == b.blinded &&
               a.anchor == b.anchor && a.footprint == b.footprint;
    };
    published_.clear();
    std::map<TeamId, std::map<world::ChunkCoord, SeenMemo>> next_seen;
    for (auto& [team, per_chunk] : explored_) {
        TeamVisibility tv;
        tv.team = team;
        std::vector<Viewer> team_vs;   // vs is sorted by (team, id)
        for (const auto& v : vs)
            if (v.team == team) team_vs.push_back(v);
        auto prev_team = seen_.find(team);
        for (std::size_t ci = 0; ci < coords.size(); ++ci) {
            ChunkVisibility cv;
            cv.coord = coords[ci];
            const SeenMemo* old = nullptr;
            if (prev_team != seen_.end()) {
                auto it = prev_team->second.find(cv.coord);
                if (it != prev_team->second.end()) old = &it->second;
            }
            const bool reuse = old && old->samples == samples[ci] && old->viewers.size() == team_vs.size() &&
                               std::equal(team_vs.begin(), team_vs.end(), old->viewers.begin(), same_viewer);
            if (reuse) cv.seen_now = old->seen;
            else for (const auto& v : team_vs) {
                if (v.team != team) continue;
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x) {
                        if (cv.seen_now.test(x, y)) continue;
                        const Vec2i t = Grid::to_world(coords[ci], {x, y});
                        if (v.on_footprint(t) || visible_rule(v, samples[ci][std::size_t(y * N + x)], dist_ft(v.tile, t),
                                         [&] { return los(v, t); }))
                            cv.seen_now.set(x, y);
                    }
            }
            if (!team_vs.empty()) {
                SeenMemo m;
                m.viewers = team_vs;
                m.samples = samples[ci];
                m.seen = cv.seen_now;
                next_seen[team].emplace(cv.coord, std::move(m));
            }
            TileMask& ex = per_chunk[cv.coord];
            ex |= cv.seen_now;
            cv.explored = ex;
            if (!ex.any()) { per_chunk.erase(cv.coord); continue; }
            tv.chunks.push_back(cv);
        }
        published_.push_back(std::move(tv));
    }
    seen_ = std::move(next_seen);
}

void split_by_halo(std::span<const world::ChunkCoord> wanted, const std::function<world::HaloState(world::ChunkCoord)>& state,
                   std::vector<world::ChunkCoord>& ready, std::vector<world::ChunkCoord>& blocked) {
    std::vector<world::ChunkCoord> coords(wanted.begin(), wanted.end());
    std::sort(coords.begin(), coords.end());
    coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
    ready.clear();
    blocked.clear();
    for (const auto& c : coords) (state(c) == world::HaloState::Complete ? ready : blocked).push_back(c);
}

void split_by_halo(const world::ChunkStreamer& streamer, std::span<const world::ChunkCoord> wanted,
                   std::vector<world::ChunkCoord>& ready, std::vector<world::ChunkCoord>& blocked) {
    split_by_halo(wanted, [&](world::ChunkCoord c) { return streamer.halo_state(c); }, ready, blocked);
}

} // namespace eng::sim
