#include "sim/light.hpp"

#include <algorithm>
#include <utility>

#include "rules5e/spells.hpp"
#include "sim/trace.hpp"

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
}

void LightCache::begin_step(LightWorld w, std::span<const RectI> dirty) {
    world_ = std::move(w);
    world_.normalise();
    reset_fields();   // fields are per step (sources may have moved); tiles outside `dirty` keep their samples
    for (const RectI& r : dirty) invalidate_rect(r);
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
    auto f = std::make_shared<LightFrame>();
    f->step = step;
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
        if (!old || old->samples != m.samples) f->dirty_chunks.push_back(m.coord);
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
