#include "slice_view.hpp"

#include "equip_visuals.hpp"
#include "iso2d/zone_look.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace slice {

using namespace eng;
using namespace eng::render;

namespace {
#include "font_data.inl"

constexpr int kTileW = 64, kTileH = 32, kWallH = 40, kElevPx = 8;
constexpr int kPawnW = 28, kPawnH = 44;
constexpr int kCellW = 6, kCellH = 8, kAtlasCols = 16;

struct Image {
    uint32_t w, h;
    std::vector<std::byte> px;
    Image(uint32_t w_, uint32_t h_) : w(w_), h(h_), px(size_t(w_) * h_ * 4) {}
    void put(uint32_t x, uint32_t y, float r, float g, float b, float a = 1.f) {
        auto* p = &px[(size_t(y) * w + x) * 4];
        auto c = [](float v) { return std::byte(uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f)); };
        p[0] = c(r); p[1] = c(g); p[2] = c(b); p[3] = c(a);
    }
};

float diamond(float px, float py, float oy) {
    return std::fabs(px + 0.5f - kTileW / 2.f) / (kTileW / 2.f) + std::fabs(py + 0.5f - oy - kTileH / 2.f) / (kTileH / 2.f);
}
uint32_t hash2(uint32_t x, uint32_t y) {   // presentation-only speckle
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

Image floor_tile(float r, float g, float b, float speckle) {
    Image im(kTileW, kTileH);
    for (uint32_t y = 0; y < im.h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            const float d = diamond(float(x), float(y), 0);
            if (d > 1.f) continue;
            float s = d > 0.9f ? 0.7f : 1.f;
            if (speckle > 0 && (hash2(x, y) & 15) == 0) s *= 1.f - speckle;
            im.put(x, y, r * s, g * s, b * s);
        }
    return im;
}

Image block(int height, float r, float g, float b) {
    Image im(kTileW, uint32_t(kTileH + height));
    for (uint32_t y = 0; y < im.h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            const float cx = std::fabs(x + 0.5f - kTileW / 2.f) / (kTileW / 2.f);
            const float top_lower = kTileH / 2.f + (kTileH / 2.f) * (1.f - cx);
            const float d = diamond(float(x), float(y), 0);
            float s = 0;
            if (d <= 1.f) s = d > 0.9f ? 0.85f : 1.f;
            else if (cx <= 1.f && y + 0.5f >= top_lower && y + 0.5f < top_lower + height) {
                s = x < kTileW / 2 ? 0.55f : 0.72f;
                if (((y + (x < kTileW / 2 ? x / 2 : (kTileW - x) / 2)) % 10) == 0) s *= 0.8f;   // brick courses
            } else continue;
            im.put(x, y, r * s, g * s, b * s);
        }
    return im;
}

Image ring() {
    Image im(kTileW, kTileH);
    for (uint32_t y = 0; y < im.h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            const float d = diamond(float(x), float(y), 0);
            if (d <= 0.95f && d >= 0.72f) im.put(x, y, 1, 1, 1, 0.9f);
        }
    return im;
}

// White pawn (tinted per team): base disc, body, head. Anchor at the feet.
Image pawn() {
    Image im(kPawnW, kPawnH);
    const float cx = kPawnW / 2.f;
    for (uint32_t y = 0; y < im.h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            const float fx = x + 0.5f - cx, fy = y + 0.5f;
            const float head = std::hypot(fx, fy - 9.f);
            const float bodyw = 5.f + (fy - 16.f) * 0.32f;
            const bool body = fy >= 15.f && fy <= kPawnH - 5.f && std::fabs(fx) <= bodyw;
            const bool base = std::hypot(fx / 13.f, (fy - (kPawnH - 5.f)) / 4.5f) <= 1.f;
            float s = 0;
            if (head <= 7.f) s = 1.f - 0.25f * std::max(0.f, fx / 7.f);
            else if (body) s = 0.9f - 0.3f * std::max(0.f, fx / bodyw);
            else if (base) s = 0.6f;
            else continue;
            const bool edge = head > 6.f && head <= 7.f;
            im.put(x, y, edge ? s * 0.6f : s, edge ? s * 0.6f : s, edge ? s * 0.6f : s);
        }
    return im;
}

Image font_atlas() {
    const uint32_t n = uint32_t(std::size(kGlyphs));
    const uint32_t rows = (n + kAtlasCols - 1) / kAtlasCols;
    Image im(kAtlasCols * kCellW, rows * kCellH);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t ox = (i % kAtlasCols) * kCellW, oy = (i / kAtlasCols) * kCellH;
        for (uint32_t r = 0; r < 7; ++r)
            for (uint32_t c = 0; c < 5; ++c)
                if (kGlyphs[i].rows[r] & (0x10 >> c)) im.put(ox + c, oy + r, 1, 1, 1);
    }
    return im;
}

Color mul(Color a, Color b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }


constexpr float kGlowMaxScale = 1.5f;   // glow sprite size cap, in tiles

int floor_mod32(int v) { return ((v % 32) + 32) % 32; }

Color tint_for(const ActorView& a) {
    Color c = a.is_pc ? Color::rgba8(90, 150, 255) : Color::rgba8(110, 200, 80);
    if (a.dead) c = Color::rgba8(90, 90, 90, 200);
    else if (a.down) c = Color::rgba8(150, 120, 120, 230);
    // Invisible tokens that are still drawn (fog-Visible to fog_team, or same-team) ghost out.
    if (a.invisible && !a.dead) c.a *= 0.38f;
    return c;
}
} // namespace

struct SliceView::Textures {
    IRenderDevice& dev;
    TextureId raised_block, floor, rubble, raised, door, water, unknown, wall, ring, pawn, white, font;
    uint32_t font_w = 0, font_h = 0;
    explicit Textures(IRenderDevice& d) : dev(d) {
        auto make = [&](const Image& im, const char* name) {
            TextureDesc td{};
            td.w = im.w; td.h = im.h; td.fmt = Format::RGBA8_UNORM; td.filter = Filter::Nearest; td.debug_name = name;
            return dev.create_texture(td, im.px);
        };
        floor = make(floor_tile(0.52f, 0.52f, 0.56f, 0.25f), "floor");
        rubble = make(floor_tile(0.50f, 0.42f, 0.33f, 0.6f), "rubble");
        raised = make(floor_tile(0.68f, 0.66f, 0.62f, 0.15f), "raised");
        raised_block = make(block(kElevPx, 0.68f, 0.66f, 0.62f), "raised_block");
        door = make(floor_tile(0.60f, 0.40f, 0.22f, 0.3f), "door");
        water = make(floor_tile(0.25f, 0.45f, 0.75f, 0.1f), "water");
        unknown = make(floor_tile(0.8f, 0.2f, 0.8f, 0.f), "unknown");
        wall = make(block(kWallH, 0.62f, 0.58f, 0.52f), "wall");
        ring = make(::slice::ring(), "ring");
        pawn = make(::slice::pawn(), "pawn");
        Image w(1, 1); w.put(0, 0, 1, 1, 1);
        white = make(w, "white");
        Image f = font_atlas(); font_w = f.w; font_h = f.h;
        font = make(f, "font");
    }
    ~Textures() {
        for (TextureId t : {raised_block, floor, rubble, raised, door, water, unknown, wall, ring, pawn, white, font}) dev.destroy(t);
    }
    TextureId ground(TileKind k) const {
        switch (k) {
        case TileKind::Floor: return floor;
        case TileKind::Rubble: return rubble;
        case TileKind::Raised: return raised;
        case TileKind::Door: return door;
        case TileKind::Water: return water;
        default: return unknown;
        }
    }
};

SliceView::SliceView(IRenderDevice& dev)
    : world_(dev, 8192), ui_(dev, 8192), lights_(dev), fogs_(dev), tex_(std::make_unique<Textures>(dev)) {}

// Mirrors the LightFrame into per-chunk R8 light and fog maps. The tracked set (sampled + blocked
// chunks) is reconciled against the snapshot (robust to skipped frames). Blocked chunks are kept
// but never updated: they hold last frame's light and fog. Light uploads are shrunk to the texels
// that really changed; fog maps refresh on dirty_vis_chunks, new chunks, gaps or a team / on-off change.
void SliceView::sync_lights(const LightView& L) {
    const int fog_key = L.fog ? int(L.fog_team) : 256;
    if (!L.present || (L.frame == light_frame_ && fog_key == fog_key_)) return;
    const bool gap = light_frame_ != 0 && L.frame != light_frame_ + 1;   // missed frames: re-check every chunk
    const bool refog = gap || fog_key != fog_key_;
    light_frame_ = L.frame;
    fog_key_ = fog_key;
    auto same = [](const auto& a, const iso2d::LightChunkKey& k) { return a.cx == k.x && a.cy == k.y && a.level == k.level; };
    auto listed = [&](const auto& v, const iso2d::LightChunkKey& k) {
        for (const auto& c : v) if (same(c, k)) return true;
        return false;
    };
    std::vector<iso2d::LightChunkKey> add, rem;
    for (const auto& k : lights_.keys()) if (!listed(L.chunks, k) && !listed(L.blocked, k)) rem.push_back(k);
    for (const auto& c : L.chunks)
        if (!lights_.texture({c.cx, c.cy, c.level}).valid()) add.push_back({c.cx, c.cy, c.level});
    lights_.on_stream_delta(add, rem);
    fogs_.on_stream_delta(add, rem);
    for (const LightChunkView& c : L.chunks) {
        const iso2d::LightChunkKey k{c.cx, c.cy, c.level};
        const bool fresh = std::find(add.begin(), add.end(), k) != add.end();
        if (fresh || refog || listed(L.vis_dirty, k)) {
            if (c.has_fog) fogs_.update(k, c.seen_now, c.explored);
            else { std::array<uint8_t, iso2d::kLightTiles> all; all.fill(iso2d::fog_texel::kSeen); fogs_.update(k, all); }
        }
        bool dirty = gap || fresh;
        for (const LightDirtyView& d : L.dirty) if (same(d.chunk, k)) dirty = true;
        if (!dirty) continue;
        int x0 = 32, y0 = 32, x1 = -1, y1 = -1;
        const uint8_t* cur = lights_.texels(k);   // resolved once per chunk (was a map lookup per texel)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if (fresh || (cur ? cur[y * 32 + x] : 0) != c.samples[size_t(y * 32 + x)]) {
                    x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        const RectI r = x1 < 0 ? RectI{} : RectI{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
        lights_.update(k, std::span<const uint8_t, iso2d::kLightTiles>(c.samples), r);
    }
}

// Per-tile light tint + fog (Visible when there is no LightFrame). Reads the retained maps, so a
// blocked chunk shows what it showed last frame; an untracked tile is unseen.
Color SliceView::tile_tint(const LightView& L, Vec2i t, iso2d::Fog* fog_out) const {
    if (!L.present) { if (fog_out) *fog_out = iso2d::Fog::Visible; return {1, 1, 1, 1}; }
    const int32_t cx = t.x >= 0 ? t.x / 32 : -((-t.x + 31) / 32), cy = t.y >= 0 ? t.y / 32 : -((-t.y + 31) / 32);
    int16_t level = 0;
    for (const auto& c : L.chunks) if (c.cx == cx && c.cy == cy) level = c.level;
    for (const auto& c : L.blocked) if (c.cx == cx && c.cy == cy) level = c.level;
    const iso2d::LightChunkKey k{cx, cy, level};
    const Vec2i local{floor_mod32(t.x), floor_mod32(t.y)};
    const iso2d::Fog f = fogs_.fog(k, local);
    if (fog_out) *fog_out = f;
    return iso2d::light_tint(lights_.sample(k, local), f);
}

// Per-frame fast path for tile_tint: resolves each chunk's level and light / fog texels once (tile_tint scans the
// chunk lists and does two map lookups per tile). Same result as tile_tint for every tile; chunks it hasn't seen
// fall back to tile_tint itself.
struct SliceView::TintLookup {
    using Entry = TintEntry;
    const SliceView& v;
    const LightView& L;
    std::vector<Entry>& entries;
    const Entry* last = nullptr;
    TintLookup(const SliceView& view, const LightView& lv, std::vector<Entry>& scratch) : v(view), L(lv), entries(scratch) {
        entries.clear();
        if (!L.present) return;
        auto add = [&](int32_t cx, int32_t cy) {
            for (const Entry& e : entries) if (e.cx == cx && e.cy == cy) return;
            int16_t level = 0;   // same resolution as tile_tint: last match in chunks, then blocked
            for (const auto& c : L.chunks) if (c.cx == cx && c.cy == cy) level = c.level;
            for (const auto& c : L.blocked) if (c.cx == cx && c.cy == cy) level = c.level;
            const iso2d::LightChunkKey k{cx, cy, level};
            entries.push_back({cx, cy, v.lights_.texels(k), v.fogs_.texels(k)});
        };
        for (const auto& c : L.chunks) add(c.cx, c.cy);
        for (const auto& c : L.blocked) add(c.cx, c.cy);
    }
    Color operator()(Vec2i t, iso2d::Fog* fog_out = nullptr, uint8_t* packed_out = nullptr) {
        if (!L.present) {
            if (packed_out) *packed_out = 0;
            return v.tile_tint(L, t, fog_out);
        }
        const int32_t cx = t.x >= 0 ? t.x / 32 : -((-t.x + 31) / 32), cy = t.y >= 0 ? t.y / 32 : -((-t.y + 31) / 32);
        if (!last || last->cx != cx || last->cy != cy) {
            last = nullptr;
            for (const Entry& e : entries) if (e.cx == cx && e.cy == cy) { last = &e; break; }
            if (!last) {
                if (packed_out) *packed_out = 0;
                return v.tile_tint(L, t, fog_out);
            }
        }
        const int i = floor_mod32(t.y) * 32 + floor_mod32(t.x);
        const iso2d::Fog f = last->fog ? iso2d::fog_of_texel(last->fog[i]) : iso2d::Fog::Unexplored;
        const uint8_t packed = last->light ? last->light[i] : uint8_t(0);
        if (fog_out) *fog_out = f;
        if (packed_out) *packed_out = packed;
        return iso2d::light_tint(packed, f);
    }
};

SliceView::~SliceView() = default;

Vec2f SliceView::tile_center_px(float x, float y) const {
    const Vec2f p = world_.projection().to_screen(x, y);
    return {p.x, p.y + kTileH / 2.f};
}

Vec2f SliceView::actors_focus_px(const ViewModel& vm) const {
    if (vm.actors.empty()) return tile_center_px(15.5f, 15.5f);
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const ActorView& a : vm.actors) {
        const Vec2f c = tile_center_px(float(a.tile.x), float(a.tile.y));
        x0 = std::min(x0, c.x - kTileW / 2.f);
        x1 = std::max(x1, c.x + kTileW / 2.f);
        y0 = std::min(y0, c.y - float(kPawnH));     // head + HP bar sit above the tile
        y1 = std::max(y1, c.y + kTileH / 2.f);
    }
    return {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
}

void SliceView::rect(Vec2f px, Vec2f size, Color c, int32_t layer) {
    iso2d::Sprite s{tex_->white, size.x, size.y, 0, 0};
    s.tint = c;
    ui_.draw_px(s, px, layer, ui_seq_++);
}

void SliceView::text(std::string_view str, Vec2f px, float scale, Color c, int32_t layer) {
    float x = px.x;
    for (char ch : str) {
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        int idx = -1;
        for (size_t i = 0; i < std::size(kGlyphs); ++i)
            if (kGlyphs[i].c == ch) { idx = int(i); break; }
        if (idx >= 0) {
            const float u0 = float((idx % kAtlasCols) * kCellW) / float(tex_->font_w);
            const float v0 = float((idx / kAtlasCols) * kCellH) / float(tex_->font_h);
            iso2d::Sprite s{tex_->font, 5 * scale, 7 * scale, 0, 0,
                            u0, v0, u0 + 5.f / float(tex_->font_w), v0 + 7.f / float(tex_->font_h)};
            s.tint = c;
            // Same y for the whole string: keep glyph order stable via the sequence key.
            ui_.draw_px(s, {x, px.y}, layer, ui_seq_++);
        }
        x += kCellW * scale;
    }
}

void SliceView::prepare(const ViewModel& vm, const iso2d::Camera& cam, uint32_t sw, uint32_t sh) {
    world_.begin();
    ui_.begin();
    ui_seq_ = 0;
    sync_lights(vm.light);
    const LightView& L = vm.light;
    TintLookup tint(*this, L, tint_entries_);
    auto visible = [&](const ActorView& a) {
        iso2d::Fog f;
        tint(a.tile, &f);
        return f == iso2d::Fog::Visible || (L.fog && a.team == L.fog_team);   // never draw what the shown team can't see
    };

    // ---- world: tiles ----
    for (const TileView& t : vm.tiles) {
        const Vec2f iso{float(t.tile.x), float(t.tile.y)};
        const float lift = float(t.elevation * kElevPx);
        const uint32_t key = uint32_t(t.tile.y * 32 + t.tile.x);
        if (t.kind == TileKind::Wall || t.layer >= 2) {
            iso2d::Sprite s{tex_->wall, float(kTileW), float(kTileH + kWallH), kTileW / 2.f, float(kWallH) + lift};
            s.tint = tint(t.tile);
            world_.draw(s, iso, 1, key, 0);
        } else if (t.elevation > 0 && t.layer == 0) {
            // Raised ground: a short block so the step has visible sides.
            iso2d::Sprite s{tex_->raised_block, float(kTileW), float(kTileH + kElevPx), kTileW / 2.f, lift};
            s.h = float(kTileH) + lift;   // stretch the side faces for multi-step elevation
            s.tint = tint(t.tile);
            world_.draw(s, iso, 0, key, t.layer);
        } else {
            iso2d::Sprite s{tex_->ground(t.kind), float(kTileW), float(kTileH), kTileW / 2.f, lift};
            s.tint = tint(t.tile);
            world_.draw(s, iso, 0, key, t.layer);
        }
    }
    // Persistent spell zones, then the generic heavy-obscurement haze for a tile no known heavy
    // look covers. Same depth slot as the old haze: layer 0, entity key = tile.y * 32 + tile.x, z = 1.
    // Non-visible tiles stay bare (no current zone, no invented fog of war).
    if (!vm.zones.empty() || L.present) {
        for (const TileView& t : vm.tiles) {
            if (t.kind == TileKind::Wall || t.layer >= 2) continue;
            iso2d::Fog f = iso2d::Fog::Visible;
            uint8_t packed = 0;
            if (L.present) tint(t.tile, &f, &packed);
            if (f != iso2d::Fog::Visible) continue;
            const Vec2f iso{float(t.tile.x), float(t.tile.y)};
            const float lift = float(t.elevation * kElevPx);
            const uint32_t key = uint32_t(t.tile.y * 32 + t.tile.x);
            bool heavy_look = false;
            for (const ZoneOverlayView& z : vm.zones) {
                if (std::find(z.tiles.begin(), z.tiles.end(), t.tile) == z.tiles.end()) continue;
                const iso2d::ZoneLook* look = iso2d::zone_look_for(z.content_id);
                if (!look) continue;
                iso2d::Sprite fill{tex_->ground(t.kind), float(kTileW), float(kTileH), kTileW / 2.f, lift};
                fill.tint = iso2d::zone_fill_tint(*look, t.tile.x, t.tile.y, vm.anim_phase);
                world_.draw(fill, iso, iso2d::kZoneOverlayLayer, key, iso2d::kZoneOverlayZ);
                if (iso2d::zone_tile_is_edge(z.tiles, t.tile) && iso2d::zone_draws_edge(*look, t.tile)) {
                    iso2d::Sprite rim{tex_->ring, float(kTileW), float(kTileH), kTileW / 2.f, lift};
                    rim.tint = iso2d::zone_edge_tint(*look, t.tile.x, t.tile.y);
                    world_.draw(rim, iso, iso2d::kZoneOverlayLayer, key, iso2d::kZoneOverlayZ);
                }
                if (look->obscure == iso2d::ZoneObscure::Heavy) heavy_look = true;
            }
            if (L.present && !heavy_look && (packed & iso2d::light_bits::kObscuredHeavy) != 0) {
                iso2d::Sprite haze{tex_->ground(t.kind), float(kTileW), float(kTileH), kTileW / 2.f, lift};
                haze.tint = {0.55f, 0.65f, 0.90f, 0.30f};
                world_.draw(haze, iso, iso2d::kZoneOverlayLayer, key, iso2d::kZoneOverlayZ);
            }
        }
    }
    // ---- world: actors (placeholder pawns), current-turn ring ----
    if (L.present) {   // glows under lit sources (presentation; entity-anchored ones follow the drawn token)
        for (const auto& [id, st] : L.visuals) {   // content visual wins; defaults stay the fallback
            const iso2d::GlowStyle* cur = palette_.override_for(id);
            const bool same = cur && cur->color.r == st.color.r && cur->color.g == st.color.g && cur->color.b == st.color.b &&
                              cur->color.a == st.color.a && cur->intensity == st.intensity && cur->flicker == st.flicker;
            if (!same)
                palette_.set_override(id, st);   // only on change: no per-frame string copies / map inserts
        }
        std::vector<iso2d::GlowSource>& glows = glows_;
        glows_from(L, palette_, glows);
        std::vector<iso2d::GlowAnchor>& anchors = anchors_;
        anchors.clear();
        for (const ActorView& a : vm.actors) anchors.push_back({a.id, {float(a.tile.x), float(a.tile.y)}});
        iso2d::follow_anchors(glows, anchors);
        for (const iso2d::GlowSource& gs : glows) {
            iso2d::Fog gf;
            tint({int(std::floor(gs.tile.x)), int(std::floor(gs.tile.y))}, &gf);
            if (gf != iso2d::Fog::Visible) continue;   // no glow where the shown team sees nothing now
            // Token-local halo: grows with the source radius but stays capped, so it never paints
            // over tiles the sim's light map leaves Dark (the light map carries the actual area).
            const float k = std::clamp(gs.radius_tiles / 4.f, 1.f, kGlowMaxScale);
            iso2d::Sprite g{tex_->ring, kTileW * k, kTileH * k, kTileW * k / 2.f, kTileH * (k - 1.f) / 2.f};
            g.tint = iso2d::glow_tint(gs);
            world_.draw(g, gs.tile, 0, gs.key, 8);
        }
    }
    for (const ActorView& a : vm.actors) {
        if (!visible(a)) continue;
        const Vec2f iso{float(a.tile.x), float(a.tile.y)};
        if (a.current) {
            iso2d::Sprite r{tex_->ring, float(kTileW), float(kTileH), kTileW / 2.f, 0};
            r.tint = Color::rgba8(255, 220, 60);
            world_.draw(r, iso, 0, a.id, 9);
        }
        // Unseen target: a small slate-cyan diamond on the ground. It is not the full-tile current-turn
        // ring (gold, z 9) and not a Faerie Fire outline (full-tile purple on the actor). Alpha stays
        // well above the Invisible ghost (~0.38): this is a mark, not a fade of the token.
        if (!a.seen_by_viewer) {
            constexpr float k = 0.62f;
            iso2d::Sprite u{tex_->ring, float(kTileW) * k, float(kTileH) * k, kTileW * k / 2.f, 0};
            u.tint = Color::rgba8(64, 176, 196, 220);
            world_.draw(u, iso, 0, a.id, 4);
        }
        // Unseen attacker: a coral pip beside the head. They can affect the viewer while the viewer
        // cannot see them. A pip, not a ring, so it does not stack with the outline above.
        if (a.unseen_attacker) {
            iso2d::Sprite pip{tex_->white, 5.f, 9.f, -8.f, 26.f};
            pip.tint = Color::rgba8(232, 84, 58, 235);
            world_.draw(pip, iso, 1, a.id, 9);
        }
        // Pawn feet at the tile centre; drawn on the wall layer so walls in front occlude it.
        iso2d::Sprite p{tex_->pawn, float(kPawnW), float(kPawnH), kPawnW / 2.f, float(kPawnH) - kTileH / 2.f - 4.f};
        iso2d::Fog af;
        const Color at = tint(a.tile, &af);
        // Own-team tokens in remembered/unseen tiles still draw, lit as Dark (presentation).
        p.tint = mul(tint_for(a), af == iso2d::Fog::Visible ? at : iso2d::light_tint(0, iso2d::Fog::Visible));
        if (L.present) p.tint = mul(p.tint, {1.6f, 1.6f, 1.6f, 1.f});   // tokens stay readable in dim light (presentation)
        Color lit_by = af == iso2d::Fog::Visible ? at : iso2d::light_tint(0, iso2d::Fog::Visible);
        if (L.present) lit_by = mul(lit_by, {1.6f, 1.6f, 1.6f, 1.f});
        if (a.invisible && !a.dead) lit_by.a *= 0.38f;   // ghost equipment with the body
        if (a.equipment.empty()) {
            world_.draw(p, iso, 1, a.id, kZBody);
        } else {
            // Equipment layers (equip_visuals.hpp): fixed z around the body inside the actor's sort key.
            for (const EquipLayer& l : equip_layers(a)) {
                if (l.part == EquipPart::Body) { world_.draw(p, iso, 1, a.id, kZBody); continue; }
                for (const EquipQuad& q : equip_quads(*l.item, l.part, l.at)) {
                    iso2d::Sprite s{tex_->white, q.size.x, q.size.y, 0, 0};
                    s.anchor_x = q.size.x / 2.f   // bottom-centre on the token anchor + q.at (screen px)
                                  - q.at.x;
                    s.anchor_y = q.size.y - q.at.y;
                    s.tint = q.emissive ? q.color : mul(q.color, lit_by);
                    if (a.invisible && !a.dead && q.emissive) s.tint.a *= 0.38f;
                    world_.draw(s, iso, 1, a.id, l.z);
                }
            }
        }
        // Faerie Fire outline: after body / equip so the ring sits on top of the token.
        if (a.outlined && !a.dead) {
            iso2d::Sprite o{tex_->ring, float(kTileW), float(kTileH), kTileW / 2.f, 0};
            o.tint = Color::rgba8(200, 120, 255, a.invisible ? 140 : 220);
            world_.draw(o, iso, 1, a.id, kZOutline);
        }
    }
    world_.prepare(cam);

    // ---- overlay (screen px) ----
    const Color white{1, 1, 1, 1}, dim = Color::rgba8(170, 175, 190), gold = Color::rgba8(255, 220, 60);
    // HP bars + names above each actor.
    for (const ActorView& a : vm.actors) {
        if (!visible(a)) continue;
        const Vec2f head = cam.world_to_screen({tile_center_px(float(a.tile.x), float(a.tile.y)).x,
                                                tile_center_px(float(a.tile.x), float(a.tile.y)).y - (kPawnH - 4) });
        const float bw = 40.f, bh = 5.f;
        const Vec2f bp{head.x - bw / 2.f, head.y - 12.f};
        rect({bp.x - 1, bp.y - 1}, {bw + 2, bh + 2}, Color::rgba8(0, 0, 0, 200), 10);
        const float frac = a.hp_max > 0 ? std::clamp(float(a.hp) / float(a.hp_max), 0.f, 1.f) : 0.f;
        const Color hc = frac > 0.5f ? Color::rgba8(80, 210, 90) : frac > 0.25f ? Color::rgba8(230, 190, 60) : Color::rgba8(220, 60, 50);
        rect(bp, {bw * frac, bh}, hc, 11);
        const float nw = float(a.name.size() * kCellW);
        text(a.name, {head.x - nw / 2.f, bp.y - 10.f}, 1.f, a.current ? gold : white);
    }
    // Top-left panel: turn + HP.
    const float s2 = 2.f;
    const float panel_h = 30.f + 20.f * float(vm.actors.size());
    rect({8, 8}, {300, panel_h}, Color::rgba8(10, 12, 18, 200), 5);
    if (vm.sim_live) {
        std::string head = "ROUND " + std::to_string(vm.round) + "  " + (vm.ended ? std::string("ENDED") : "TURN: " + vm.turn);
        text(head, {16, 16}, s2, gold);
    } else {
        text("STATIC CHUNK VIEW", {16, 16}, s2, gold);
    }
    float y = 40.f;
    if (!vm.pending.empty()) {
        text(vm.pending, {320, 16}, s2, Color::rgba8(255, 140, 60));
    }
    for (const ActorView& a : vm.actors) {
        if (!visible(a)) continue;
        std::string line = (a.current ? "> " : "  ") + a.name + " HP " + std::to_string(a.hp) + "/" + std::to_string(a.hp_max);
        if (a.dead) line += " DEAD";
        else if (a.down) line += " DOWN";
        text(line, {16, y}, s2, a.current ? white : dim);
        y += 20.f;
    }
    // Top-right: debug panel (e.g. the debug equip view). Nothing is drawn while it is empty.
    if (!vm.debug_panel.empty()) {
        const float pw = 420.f, px = float(sw) - pw - 8.f, plh = 14.f;
        rect({px, 8}, {pw, plh * float(vm.debug_panel.size()) + 12.f}, Color::rgba8(10, 12, 18, 220), 5);
        float py = 14.f;
        for (const std::string& l : vm.debug_panel) {
            text(l, {px + 8.f, py}, 1.5f, !l.empty() && l[0] == '>' ? gold : white);
            py += plh;
        }
    }
    // Bottom-left: combat log + help.
    const float lh = 12.f;
    float ly = float(sh) - 24.f - lh * float(vm.log.size());
    if (!vm.log.empty()) rect({8, ly - 6}, {380, lh * float(vm.log.size()) + 8}, Color::rgba8(10, 12, 18, 170), 5);
    for (const std::string& l : vm.log) { text(l, {16, ly}, 1.5f, dim); ly += lh; }
    const std::string help = vm.status.empty() ? std::string("SPACE: STEP  A: AUTO  ARROWS: PAN  WHEEL/+/-: ZOOM  ESC: QUIT") : vm.status;
    text(help, {16, float(sh) - 16.f}, 1.f, dim);
    draw_cover(vm, cam);

    ui_.prepare({{0, 0}, sw, sh, 1.f});
}

namespace {
Color cover_color(CoverLevel level) {
    switch (level) {
    case CoverLevel::Half: return Color::rgba8(70, 210, 255, 230);
    case CoverLevel::ThreeQuarters: return Color::rgba8(255, 170, 40, 235);
    case CoverLevel::Total: return Color::rgba8(255, 64, 72, 240);
    case CoverLevel::None: break;
    }
    return Color::rgba8(170, 176, 188, 190);
}
float cover_thickness(CoverLevel level) {
    switch (level) {
    case CoverLevel::Half: return 4.f;
    case CoverLevel::ThreeQuarters: return 6.f;
    case CoverLevel::Total: return 8.f;
    case CoverLevel::None: break;
    }
    return 3.f;
}
const char* cover_plate(CoverLevel level) {
    switch (level) {
    case CoverLevel::Half: return "1/2";
    case CoverLevel::ThreeQuarters: return "3/4";
    case CoverLevel::Total: return "TOTAL";
    case CoverLevel::None: break;
    }
    return "NONE";
}
const char* cover_bonus_label(CoverLevel level) {
    switch (level) {
    case CoverLevel::Half: return " +2";
    case CoverLevel::ThreeQuarters: return " +5";
    case CoverLevel::None:
    case CoverLevel::Total: break;
    }
    return "";
}
} // namespace

void SliceView::draw_cover(const ViewModel& vm, const iso2d::Camera& cam) {
    const CoverDebugView& c = vm.cover;
    if (!c.present && c.note.empty()) return;
    auto name_of = [&](uint32_t id) -> std::string {
        for (const ActorView& a : vm.actors)
            if (a.id == id) return a.name;
        return "?";
    };
    // Under the turn panel so the badge on the map and this line don't fight the HP bars.
    const float panel_h = 30.f + 20.f * float(vm.actors.size());
    if (!c.present) {
        text(c.note, {16.f, 12.f + panel_h}, 1.5f, Color::rgba8(170, 176, 188), 32);
        return;
    }
    auto center = [&](Vec2i anchor, int n) {
        const float o = float(std::max(n, 1) - 1) * 0.5f;
        return cam.world_to_screen(tile_center_px(float(anchor.x) + o, float(anchor.y) + o));
    };
    // Screen px, not a world sprite: a polyline in the iso sort would slip behind walls.
    const Vec2f a = center(c.origin, c.origin_n);
    const Vec2f b = center(c.target_tile, c.target_n);
    const Color col = cover_color(c.level);
    const float thick = cover_thickness(c.level);
    const bool dashed = c.level == CoverLevel::None;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    auto stroke = [&](Vec2f p, Vec2f q, float w, bool dash) {
        const float sx = q.x - p.x, sy = q.y - p.y;
        const float sl = std::hypot(sx, sy);
        if (sl < 1.f) {
            rect({p.x - w * 0.5f, p.y - w * 0.5f}, {w, w}, col, 30);
            return;
        }
        const int n = std::max(1, int(std::ceil(sl / 6.f)));
        for (int i = 0; i <= n; ++i) {
            if (dash && (i % 2) == 1) continue;
            const float t = float(i) / float(n);
            rect({p.x + sx * t - w * 0.5f, p.y + sy * t - w * 0.5f}, {w, w}, col, 30);
        }
    };
    stroke(a, b, thick, dashed);
    const float px = len >= 1.f ? -dy / len : 0.f;
    const float py = len >= 1.f ? dx / len : -1.f;
    const float ux = len >= 1.f ? dx / len : 1.f;
    const float uy = len >= 1.f ? dy / len : 0.f;
    const Vec2f mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    auto tick = [&](Vec2f at, float half) {
        stroke({at.x - px * half, at.y - py * half}, {at.x + px * half, at.y + py * half}, 3.f, false);
    };
    if (c.level == CoverLevel::Half) tick(mid, 8.f);
    if (c.level == CoverLevel::ThreeQuarters) {
        tick({mid.x - ux * 6.f, mid.y - uy * 6.f}, 9.f);
        tick({mid.x + ux * 6.f, mid.y + uy * 6.f}, 9.f);
    }
    if (c.level == CoverLevel::Total) {
        // A second bar plus a cross, so total cover is not only a redder line.
        stroke({a.x + px * 4.f, a.y + py * 4.f}, {b.x + px * 4.f, b.y + py * 4.f}, thick, false);
        tick(mid, 10.f);
        stroke({mid.x - ux * 10.f, mid.y - uy * 10.f}, {mid.x + ux * 10.f, mid.y + uy * 10.f}, 3.f, false);
    }
    const float cap = 8.f;
    const Color cap_a = c.aim ? Color::rgba8(255, 255, 255, 235) : Color::rgba8(255, 220, 60, 235);
    rect({a.x - cap * 0.5f, a.y - cap * 0.5f}, {cap, cap}, cap_a, 31);
    rect({b.x - cap * 0.5f, b.y - cap * 0.5f}, {cap, cap}, col, 31);
    const char* plate = cover_plate(c.level);
    const float scale = 2.f;
    const float tw = float(std::char_traits<char>::length(plate)) * kCellW * scale;
    const float th = 7.f * scale;
    const Vec2f plate_at{mid.x - tw * 0.5f, mid.y - 18.f - th};
    rect({plate_at.x - 4.f, plate_at.y - 3.f}, {tw + 8.f, th + 6.f}, Color::rgba8(10, 12, 18, 220), 31);
    text(plate, plate_at, scale, col, 32);

    std::string cap_line = c.aim ? "AIM " : "";
    cap_line += name_of(c.attacker);
    cap_line += " > ";
    cap_line += name_of(c.target);
    cap_line += "  ";
    cap_line += plate;
    cap_line += cover_bonus_label(c.level);
    if (!c.note.empty()) { cap_line += "  "; cap_line += c.note; }
    cap_line += "   F4 CLOSE  TAB TARGET  N ATTACKER  M AIM";
    text(cap_line, {16.f, 12.f + panel_h}, 1.5f, col, 32);
}

void SliceView::submit(ICommandList& cl) {
    world_.submit(cl);
    ui_.submit(cl);
}

} // namespace slice

namespace slice {
constexpr float kDimOnlyGlow = 0.5f;
std::vector<eng::iso2d::GlowSource> glows_from(const LightView& L, const eng::iso2d::GlowPalette& palette) {
    std::vector<eng::iso2d::GlowSource> out;
    glows_from(L, palette, out);
    return out;
}

void glows_from(const LightView& L, const eng::iso2d::GlowPalette& palette, std::vector<eng::iso2d::GlowSource>& out) {
    out.clear();
    if (!L.present) return;
    for (const LightSourceView& ls : L.sources) {
        if (!ls.lit) continue;
        if (ls.bright_ft == 0 && ls.dim_ft == 0) continue;   // lit but emitting nothing (a state with no light)
        eng::iso2d::GlowSource g;
        g.key = ls.sort_key();
        if (ls.carrier == 1) {
            g.anchor = eng::iso2d::GlowAnchorKind::Entity;
            g.anchor_id = ls.carrier_id;
        }
        g.tile = {float(ls.tile.x), float(ls.tile.y)};
        g.radius_tiles = eng::iso2d::glow_radius_tiles(ls.bright_ft, ls.dim_ft);
        const eng::iso2d::GlowStyle st = palette.style_for(ls.preset, ls.magical);
        g.color = st.color;
        g.intensity = st.intensity;
        if (ls.bright_ft == 0) g.intensity *= kDimOnlyGlow;   // dim-only (hooded lantern 0/5): small, faint glow
        out.push_back(g);
    }
}
} // namespace slice
