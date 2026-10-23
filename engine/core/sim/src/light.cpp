#include "sim/light.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "rules5e/spells.hpp"
#include "sim/trace.hpp"
#include "sim/visibility.hpp"

namespace eng::sim {

namespace {
int32_t iabs(int32_t v) noexcept { return v < 0 ? -v : v; }
bool covers(const LightZone& z, Vec2i t) { return std::binary_search(z.tiles.begin(), z.tiles.end(), t); }
} // namespace

LightSource static_source(world::ChunkCoord chunk, uint32_t index, const StaticLight& l, std::string preset) {
    LightSource s;
    s.id = LightId{LightKind::Static, chunk.x, chunk.y, chunk.level, index};
    s.tile = Grid::to_world(chunk, l.tile);
    s.bright_ft = l.bright_ft;
    s.dim_ft = l.dim_ft;
    s.flags = uint8_t(l.flags & (light_src::kMagical | light_src::kSunlight));
    s.lit = (l.flags & light_src::kStartsUnlit) == 0;
    s.carrier = Carrier::Tile;
    s.preset = std::move(preset);
    return s;
}

LightSource attached_source(const AttachedLight& l, Vec2i carrier_tile, std::string preset) {
    LightSource s;
    s.id = LightId{LightKind::Attached, 0, 0, 0, l.id};
    s.tile = l.carrier == Carrier::Tile ? l.tile : carrier_tile;
    s.bright_ft = l.bright_ft;
    s.dim_ft = l.dim_ft;
    s.flags = uint8_t(l.flags & (light_src::kMagical | light_src::kSunlight));
    s.spell_level = l.spell_level;
    s.lit = (l.flags & light_src::kLit) != 0;
    s.carrier = l.carrier;
    s.carrier_id = l.carrier_id;
    s.preset = std::move(preset);
    return s;
}

void LightWorld::normalise() {
    std::stable_sort(sources.begin(), sources.end(), [](const LightSource& a, const LightSource& b) { return a.id < b.id; });
    std::stable_sort(zones.begin(), zones.end(), [](const LightZone& a, const LightZone& b) { return a.zone_id < b.zone_id; });
    for (auto& z : zones) {
        std::sort(z.tiles.begin(), z.tiles.end());
        z.tiles.erase(std::unique(z.tiles.begin(), z.tiles.end()), z.tiles.end());
    }
}

Light source_level(const LightSource& s, Vec2i t) noexcept {
    const int32_t d = std::max(iabs(t.x - s.tile.x), iabs(t.y - s.tile.y)) * kFeetPerTile;
    if (s.bright_ft > 0 && d <= int32_t(s.bright_ft)) return Light::Bright;   // a 0 ft bright radius lights nothing bright
    if (s.bright_ft + s.dim_ft > 0 && d <= int32_t(s.bright_ft) + int32_t(s.dim_ft)) return Light::Dim;
    return Light::Dark;
}

bool light_reaches(const LightWorld& w, Vec2i s, Vec2i t) {
    if (s == t) return true;
    auto wall = [&](Vec2i p) {
        if (p == s || p == t) return false;
        if (w.grid && w.grid->blocks_sight(p)) return true;
        for (const auto& z : w.zones)
            if ((z.flags & rules5e::kZoneOpaque) && covers(z, p)) return true;
        return false;
    };
    for (int oy = 0; oy <= 1; ++oy)
        for (int ox = 0; ox <= 1; ++ox)
            for (int ty = 0; ty <= 1; ++ty)
                for (int tx = 0; tx <= 1; ++tx)
                    if (!segment_blocked(Vec2i{s.x + ox, s.y + oy}, Vec2i{t.x + tx, t.y + ty}, wall)) return true;
    return false;
}

namespace {
template <class Reach> LightSample compose(const LightWorld& w, Vec2i t, Reach&& reaches) {
    LightSample out;
    // Step 2 inputs first (zones in ascending id): magical darkness level, obscurement flags.
    bool dark = false;
    int dark_level = 0;
    for (const auto& z : w.zones) {
        if (!covers(z, t)) continue;
        if (z.flags & rules5e::kZoneMagicalDarkness) { dark = true; dark_level = std::max(dark_level, int(z.spell_level)); }
        if (z.flags & rules5e::kZoneLightObscured) out.flags |= light_flags::kObscuredLight;
        if (z.flags & rules5e::kZoneHeavyObscured) out.flags |= light_flags::kObscuredHeavy;
    }
    // Step 1: max over lit sources that reach t (in id order); under magical darkness only magical sources of a higher
    // spell level than the darkness count (R1: the level comparison is the only rule).
    Light best = Light::Dark;
    bool sun = false;
    for (std::size_t si = 0; si < w.sources.size(); ++si) {
        const LightSource& s = w.sources[si];
        if (!s.lit) continue;
        const Light lv = source_level(s, t);
        if (lv == Light::Dark) continue;
        if (dark && !((s.flags & light_src::kMagical) && int(s.spell_level) > dark_level)) continue;
        const bool s_sun = (s.flags & light_src::kSunlight) != 0;
        if (lv < best || (lv == best && (sun || !s_sun))) continue;   // can't change the result
        if (!reaches(si, t)) continue;
        if (lv > best) { best = lv; sun = s_sun; }
        else sun = sun || s_sun;
    }
    // Step 3: the ambient floor, not inside magical darkness.
    if (dark) out.flags |= light_flags::kMagicalDark;
    else if (w.ambient.level != Light::Dark) {
        const bool a_sun = w.ambient.kind == AmbientKind::Sun;
        if (w.ambient.level > best) { best = w.ambient.level; sun = a_sun; }
        else if (w.ambient.level == best) sun = sun || a_sun;
    }
    out.level = best;
    if (sun && best != Light::Dark) out.flags |= light_flags::kSunlight;
    return out;
}

} // namespace

LightSample light_at(const LightWorld& w, Vec2i t) {
    return compose(w, t, [&](std::size_t si, Vec2i p) { return light_reaches(w, w.sources[si].tile, p); });
}

// LightCache: per-source fields (the §12 fallback "per-source visibility caching keyed by source tile"). For each lit
// source, a square of side 2 * reach + 1 around it holds the walls light_reaches would see (blocks_sight or an opaque zone,
// the source tile itself excluded) with 2D prefix sums, plus memoised reach answers. The same segment_blocked runs over
// the bitmap instead of Grid's map lookups; when the bounding box of s and t holds no wall (t excluded) no segment can be
// blocked, so the answer is true without tracing. Results equal light_reaches exactly (tested on every tile).
struct LightCache::Field {
    Vec2i origin;   // world tile of square index (0, 0)
    int side = 0;
    std::vector<uint8_t> wall;
    std::vector<int32_t> psum;   // (side + 1)^2
    std::vector<int8_t> reach;   // -1 unknown, 0, 1
    bool built = false;
};

namespace {
int32_t reach_tiles(const LightSource& s) { return (int32_t(s.bright_ft) + int32_t(s.dim_ft)) / kFeetPerTile; }
} // namespace

void LightCache::build_field(std::size_t si) {
    Field& f = fields_[si];
    const LightSource& s = world_.sources[si];
    const int32_t r = reach_tiles(s);
    f.side = 2 * r + 1;
    f.origin = Vec2i{s.tile.x - r, s.tile.y - r};
    const auto n = std::size_t(f.side) * std::size_t(f.side);
    f.wall.assign(n, 0);
    f.reach.assign(n, -1);
    for (int y = 0; y < f.side; ++y)
        for (int x = 0; x < f.side; ++x) {
            const Vec2i p{f.origin.x + x, f.origin.y + y};
            if (p == s.tile) continue;
            bool b = world_.grid && world_.grid->blocks_sight(p);
            for (const auto& z : world_.zones)
                if (!b && (z.flags & rules5e::kZoneOpaque) && covers(z, p)) b = true;
            f.wall[std::size_t(y * f.side + x)] = b ? 1 : 0;
        }
    const int sp = f.side + 1;
    f.psum.assign(std::size_t(sp) * std::size_t(sp), 0);
    for (int y = 0; y < f.side; ++y)
        for (int x = 0; x < f.side; ++x)
            f.psum[std::size_t((y + 1) * sp + x + 1)] = f.wall[std::size_t(y * f.side + x)] + f.psum[std::size_t(y * sp + x + 1)] +
                                                       f.psum[std::size_t((y + 1) * sp + x)] - f.psum[std::size_t(y * sp + x)];
    f.built = true;
}

bool LightCache::field_reaches(std::size_t si, Vec2i t) {
    if (!fields_[si].built) build_field(si);
    Field& f = fields_[si];
    const Vec2i s = world_.sources[si].tile;
    const int lx = t.x - f.origin.x, ly = t.y - f.origin.y;
    if (lx < 0 || ly < 0 || lx >= f.side || ly >= f.side) return light_reaches(world_, s, t);   // not reached by callers
    int8_t& memo = f.reach[std::size_t(ly * f.side + lx)];
    if (memo >= 0) return memo != 0;
    bool out;
    if (s == t) out = true;
    else {
        const int sx = s.x - f.origin.x, sy = s.y - f.origin.y, sp = f.side + 1;
        const int x0 = std::min(sx, lx), x1 = std::max(sx, lx) + 1, y0 = std::min(sy, ly), y1 = std::max(sy, ly) + 1;
        const int32_t walls = f.psum[std::size_t(y1 * sp + x1)] - f.psum[std::size_t(y0 * sp + x1)] -
                              f.psum[std::size_t(y1 * sp + x0)] + f.psum[std::size_t(y0 * sp + x0)] -
                              f.wall[std::size_t(ly * f.side + lx)];
        if (walls == 0) out = true;
        else {
            auto wall = [&](Vec2i p) {
                if (p == s || p == t) return false;
                const int px = p.x - f.origin.x, py = p.y - f.origin.y;
                return px >= 0 && py >= 0 && px < f.side && py < f.side && f.wall[std::size_t(py * f.side + px)] != 0;
            };
            out = false;
            for (int oy = 0; oy <= 1 && !out; ++oy)
                for (int ox = 0; ox <= 1 && !out; ++ox)
                    for (int ty = 0; ty <= 1 && !out; ++ty)
                        for (int tx = 0; tx <= 1 && !out; ++tx)
                            if (!segment_blocked(Vec2i{s.x + ox, s.y + oy}, Vec2i{t.x + tx, t.y + ty}, wall)) out = true;
        }
    }
    memo = int8_t(out ? 1 : 0);
    return out;
}

LightCache::LightCache() = default;
LightCache::LightCache(LightWorld w) { begin_step(std::move(w)); }
LightCache::~LightCache() = default;
LightCache::LightCache(LightCache&&) noexcept = default;
LightCache& LightCache::operator=(LightCache&&) noexcept = default;

void LightCache::reset_fields() {
    fields_.clear();
    fields_.resize(world_.sources.size());
}

void LightCache::begin_step(LightWorld w) {
    world_ = std::move(w);
    world_.normalise();
    reset_fields();
    memo_.clear();
    inc_.valid = false;
}

void LightCache::begin_step(LightWorld w, std::span<const RectI> dirty) {
    world_ = std::move(w);
    world_.normalise();
    reset_fields();   // fields are per step (sources may have moved); tiles outside `dirty` keep their samples
    inc_.valid = false;   // the caller's rects, not the incremental diff, define what is kept
    for (const RectI& r : dirty) invalidate_rect(r);
}

namespace {
// The inputs of compose / field_reaches a source contributes (id, preset and carrier are presentation / ordering only).
bool same_light(const LightSource& a, const LightSource& b) noexcept {
    return a.tile == b.tile && a.bright_ft == b.bright_ft && a.dim_ft == b.dim_ft && a.flags == b.flags &&
           a.spell_level == b.spell_level && a.lit == b.lit;
}
bool same_zone(const LightZone& a, const LightZone& b) {
    return a.zone_id == b.zone_id && a.flags == b.flags && a.spell_level == b.spell_level && a.tiles == b.tiles;
}
// Every tile a source can light (and every wall its traces can cross): the square of side 2 * reach + 1 around it.
RectI halo_of(const LightSource& s) {
    const int32_t r = reach_tiles(s);
    return RectI{s.tile.x - r, s.tile.y - r, 2 * r + 1, 2 * r + 1};
}
bool rect_has(const RectI& r, Vec2i p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }
RectI tiles_bbox(const std::vector<Vec2i>& ts) {   // non-empty
    Vec2i lo = ts.front(), hi = ts.front();
    for (const Vec2i& p : ts) {
        lo = Vec2i{std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = Vec2i{std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    return RectI{lo.x, lo.y, hi.x - lo.x + 1, hi.y - lo.y + 1};
}
template <class T, class Id> bool unique_ids(const std::vector<T>& v, Id&& id) {
    for (std::size_t i = 1; i < v.size(); ++i)
        if (!(id(v[i - 1]) < id(v[i]))) return false;
    return true;
}
} // namespace

LightCache::StepStats LightCache::begin_step_incremental(LightWorld w, std::span<const world::ChunkCoord> chunks) {
    w.normalise();
    StepStats st;
    std::vector<world::ChunkCoord> coords(chunks.begin(), chunks.end());
    std::sort(coords.begin(), coords.end());
    coords.erase(std::unique(coords.begin(), coords.end()), coords.end());

    // The read box (bounding box of the chunks) grown by the reach bound: every source and wall a read tile depends on.
    int32_t reach = int32_t(world::kMaxLightFt) / kFeetPerTile;
    for (const LightSource& src : w.sources) reach = std::max(reach, reach_tiles(src));
    RectI read{};
    if (!coords.empty()) {
        constexpr int32_t N = world::kChunkSize;
        int32_t x0 = coords.front().x, x1 = x0, y0 = coords.front().y, y1 = y0;
        for (const auto& c : coords) {
            x0 = std::min(x0, c.x); x1 = std::max(x1, c.x); y0 = std::min(y0, c.y); y1 = std::max(y1, c.y);
        }
        read = RectI{x0 * N, y0 * N, (x1 - x0 + 1) * N, (y1 - y0 + 1) * N};
    }
    const RectI box{read.x - reach, read.y - reach, read.w + 2 * reach, read.h + 2 * reach};

    // Raw walls over the box, as build_field / light_reaches see them (before excluding the trace's end tiles).
    std::vector<uint8_t> walls;
    if (!coords.empty()) {
        std::vector<const LightZone*> opaque;
        for (const auto& z : w.zones)
            if (z.flags & rules5e::kZoneOpaque) opaque.push_back(&z);
        walls.assign(std::size_t(box.w) * std::size_t(box.h), 0);
        for (int32_t y = 0; y < box.h; ++y)
            for (int32_t x = 0; x < box.w; ++x) {
                const Vec2i p{box.x + x, box.y + y};
                bool b = w.grid && w.grid->blocks_sight(p);
                for (std::size_t i = 0; !b && i < opaque.size(); ++i) b = covers(*opaque[i], p);
                walls[std::size_t(y) * std::size_t(box.w) + std::size_t(x)] = b ? 1 : 0;
            }
    }

    const bool full = !inc_.valid || coords.empty() || inc_.chunks != coords || inc_.box != box || inc_.reach != reach ||
                      w.ambient.kind != world_.ambient.kind || w.ambient.level != world_.ambient.level ||
                      !unique_ids(w.sources, [](const LightSource& a) { return a.id; }) ||
                      !unique_ids(w.zones, [](const LightZone& a) { return a.zone_id; });
    std::vector<RectI> dirty;
    if (!full) {
        // Sources (both lists ascending id, unique: checked above / last step).
        const auto& olds = world_.sources;
        const auto& news = w.sources;
        std::size_t i = 0, j = 0;
        while (i < news.size() || j < olds.size()) {
            if (j == olds.size() || (i < news.size() && news[i].id < olds[j].id)) { if (news[i].lit) dirty.push_back(halo_of(news[i])); ++i; }
            else if (i == news.size() || olds[j].id < news[i].id) { if (olds[j].lit) dirty.push_back(halo_of(olds[j])); ++j; }
            else {
                if (!same_light(news[i], olds[j])) {
                    if (olds[j].lit) dirty.push_back(halo_of(olds[j]));
                    if (news[i].lit) dirty.push_back(halo_of(news[i]));
                }
                ++i; ++j;
            }
        }
        // Walls: a changed tile can only alter traces of sources whose halo holds it (a trace from s to t stays in the
        // bounding box of s and t, inside s's halo).
        std::vector<Vec2i> changed;
        for (int32_t y = 0; y < box.h; ++y)
            for (int32_t x = 0; x < box.w; ++x) {
                const std::size_t k = std::size_t(y) * std::size_t(box.w) + std::size_t(x);
                if (walls[k] != inc_.walls[k]) changed.push_back(Vec2i{box.x + x, box.y + y});
            }
        st.wall_changes = changed.size();
        if (!changed.empty()) {
            const RectI cb = tiles_bbox(changed);
            for (const LightSource& src : news) {
                if (!src.lit) continue;
                const RectI h = halo_of(src);
                if (cb.x >= h.x + h.w || h.x >= cb.x + cb.w || cb.y >= h.y + h.h || h.y >= cb.y + cb.h) continue;
                for (const Vec2i& p : changed)
                    if (rect_has(h, p)) { dirty.push_back(h); break; }
            }
        }
        // Zones (ascending id, unique): coverage changes the tiles it covers (opaque walls are handled above).
        const auto& oz = world_.zones;
        const auto& nz = w.zones;
        i = 0; j = 0;
        auto zone_dirty = [&](const LightZone& z) { if (!z.tiles.empty()) dirty.push_back(tiles_bbox(z.tiles)); };
        while (i < nz.size() || j < oz.size()) {
            if (j == oz.size() || (i < nz.size() && nz[i].zone_id < oz[j].zone_id)) zone_dirty(nz[i++]);
            else if (i == nz.size() || oz[j].zone_id < nz[i].zone_id) zone_dirty(oz[j++]);
            else {
                if (!same_zone(nz[i], oz[j])) { zone_dirty(nz[i]); zone_dirty(oz[j]); }
                ++i; ++j;
            }
        }
    }

    world_ = std::move(w);
    reset_fields();
    st.full = full;
    if (full) memo_.clear();
    else {
        // Only tiles inside the read box are guaranteed by the diff above.
        for (auto it = memo_.begin(); it != memo_.end();) {
            if (rect_has(read, it->first)) ++it;
            else it = memo_.erase(it);
        }
        for (const RectI& r : dirty) invalidate_rect(r);
        st.dirty_rects = dirty.size();
    }
    inc_.valid = !coords.empty();
    inc_.chunks = std::move(coords);
    inc_.box = box;
    inc_.reach = reach;
    inc_.walls = std::move(walls);
    return st;
}

void LightCache::invalidate_rect(RectI r) {
    for (auto it = memo_.lower_bound(Vec2i{r.x, r.y}); it != memo_.end() && it->first.x < r.x + r.w;) {
        const Vec2i p = it->first;
        if (p.y >= r.y && p.y < r.y + r.h) it = memo_.erase(it);
        else ++it;
    }
}

LightSample LightCache::at(Vec2i t) {
    auto it = memo_.find(t);
    if (it != memo_.end()) return it->second;
    const LightSample s = compose(world_, t, [&](std::size_t si, Vec2i p) { return field_reaches(si, p); });
    memo_.emplace(t, s);
    return s;
}

std::shared_ptr<const LightFrame> publish_light_frame(LightCache& cache, std::span<const world::ChunkCoord> chunks,
                                                      uint64_t step, const LightFrame* prev) {
    LightFrameInputs in;
    in.chunks = chunks;
    return publish_light_frame(cache, in, step, prev);
}

namespace {
std::vector<world::ChunkCoord> sorted_unique(std::span<const world::ChunkCoord> in) {
    std::vector<world::ChunkCoord> v(in.begin(), in.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
// Bounding rect of the samples that differ (`old` null: the whole chunk). Precondition: they differ.
DirtyRect dirty_rect_of(const ChunkLightMap& m, const ChunkLightMap* old) {
    constexpr int N = world::kChunkSize;
    DirtyRect r{m.coord, 0, 0, uint8_t(N - 1), uint8_t(N - 1)};
    if (!old) return r;
    int x0 = N, y0 = N, x1 = -1, y1 = -1;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if (m.samples[std::size_t(y * N + x)] != old->samples[std::size_t(y * N + x)]) {
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    r.x0 = uint8_t(x0); r.y0 = uint8_t(y0); r.x1 = uint8_t(x1); r.y1 = uint8_t(y1);
    return r;
}
const ChunkVisibility* find_vis(const std::vector<TeamVisibility>& teams, std::size_t ti, world::ChunkCoord c) {
    if (ti >= teams.size()) return nullptr;
    const auto& cs = teams[ti].chunks;
    auto it = std::lower_bound(cs.begin(), cs.end(), c,
                               [](const ChunkVisibility& a, const world::ChunkCoord& b) { return a.coord < b; });
    return it != cs.end() && it->coord == c ? &*it : nullptr;
}
} // namespace

std::shared_ptr<const LightFrame> publish_light_frame(LightCache& cache, const LightFrameInputs& in, uint64_t step,
                                                      const LightFrame* prev) {
    auto f = std::make_shared<LightFrame>();
    f->step = step;
    const std::span<const world::ChunkCoord> chunks = in.chunks;
    std::vector<world::ChunkCoord> coords(chunks.begin(), chunks.end());
    std::sort(coords.begin(), coords.end());
    coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
    f->chunks.reserve(coords.size());
    constexpr int N = world::kChunkSize;
    for (const auto& c : coords) {
        ChunkLightMap m;
        m.coord = c;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) m.samples[std::size_t(y * N + x)] = cache.at(Grid::to_world(c, Vec2i{x, y}));
        f->chunks.push_back(m);
    }
    f->sources = cache.world().sources;   // normalised: ascending id

    // Dirty set against the previous frame (both lists are sorted).
    for (const auto& m : f->chunks) {
        const ChunkLightMap* old = nullptr;
        if (prev) {
            auto it = std::lower_bound(prev->chunks.begin(), prev->chunks.end(), m.coord,
                                       [](const ChunkLightMap& a, const world::ChunkCoord& b) { return a.coord < b; });
            if (it != prev->chunks.end() && it->coord == m.coord) old = &*it;
        }
        if (!old || old->samples != m.samples) {
            f->dirty_chunks.push_back(m.coord);
            f->dirty_rects.push_back(dirty_rect_of(m, old));
        }
    }

    // Tracked chunks (samples or blocked) against the previous frame. A chunk both ready and blocked counts as ready.
    {
        std::vector<world::ChunkCoord> blocked = sorted_unique(in.blocked), b2;
        std::set_difference(blocked.begin(), blocked.end(), coords.begin(), coords.end(), std::back_inserter(b2));
        f->blocked_chunks = std::move(b2);
    }
    std::vector<world::ChunkCoord> now, before;
    std::set_union(coords.begin(), coords.end(), f->blocked_chunks.begin(), f->blocked_chunks.end(), std::back_inserter(now));
    if (prev) {
        std::vector<world::ChunkCoord> pc;
        for (const auto& m : prev->chunks) pc.push_back(m.coord);
        std::set_union(pc.begin(), pc.end(), prev->blocked_chunks.begin(), prev->blocked_chunks.end(),
                       std::back_inserter(before));
    }
    std::set_difference(now.begin(), now.end(), before.begin(), before.end(), std::back_inserter(f->added_chunks));
    std::set_difference(before.begin(), before.end(), now.begin(), now.end(), std::back_inserter(f->removed_chunks));

    // Visibility: the state's masks restricted to this frame's chunks, and the chunks whose masks changed.
    if (in.visibility) {
        for (const auto& tv : in.visibility->teams()) {
            TeamVisibility out;
            out.team = tv.team;
            for (const auto& cv : tv.chunks)
                if (std::binary_search(coords.begin(), coords.end(), cv.coord)) out.chunks.push_back(cv);
            f->visibility.push_back(std::move(out));
        }
    }
    {
        const std::vector<TeamVisibility> no_teams;
        const auto& pv = prev ? prev->visibility : no_teams;
        for (const auto& c : coords) {
            bool changed = false;
            std::size_t i = 0, j = 0;   // merge the two team lists (both ascending)
            while (!changed && (i < f->visibility.size() || j < pv.size())) {
                const ChunkVisibility *a = nullptr, *b = nullptr;
                if (j == pv.size() || (i < f->visibility.size() && f->visibility[i].team < pv[j].team)) a = find_vis(f->visibility, i++, c);
                else if (i == f->visibility.size() || pv[j].team < f->visibility[i].team) b = find_vis(pv, j++, c);
                else { a = find_vis(f->visibility, i++, c); b = find_vis(pv, j++, c); }
                const ChunkVisibility empty{c, {}, {}};
                changed = !((a ? *a : empty).seen_now == (b ? *b : empty).seen_now &&
                            (a ? *a : empty).explored == (b ? *b : empty).explored);
            }
            if (changed) f->dirty_vis_chunks.push_back(c);
        }
    }
    auto same = [](const LightSource& a, const LightSource& b) {
        return a.tile == b.tile && a.bright_ft == b.bright_ft && a.dim_ft == b.dim_ft && a.flags == b.flags &&
               a.spell_level == b.spell_level && a.lit == b.lit && a.carrier == b.carrier && a.carrier_id == b.carrier_id &&
               a.preset == b.preset;
    };
    const std::vector<LightSource> none;
    const auto& olds = prev ? prev->sources : none;
    std::size_t i = 0, j = 0;
    while (i < f->sources.size() || j < olds.size()) {
        if (j == olds.size() || (i < f->sources.size() && f->sources[i].id < olds[j].id)) f->dirty_sources.push_back(f->sources[i++].id);
        else if (i == f->sources.size() || olds[j].id < f->sources[i].id) f->dirty_sources.push_back(olds[j++].id);
        else {
            if (!same(f->sources[i], olds[j])) f->dirty_sources.push_back(f->sources[i].id);
            ++i; ++j;
        }
    }
    return f;
}

} // namespace eng::sim
