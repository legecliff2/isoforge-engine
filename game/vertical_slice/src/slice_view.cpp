#include "slice_view.hpp"

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

// Per-tile light tint + fog (Visible when there is no LightFrame).
Color tile_light(const LightView& L, Vec2i t, iso2d::Fog* fog_out = nullptr) {
    if (!L.present) { if (fog_out) *fog_out = iso2d::Fog::Visible; return {1, 1, 1, 1}; }
    const LightChunkView* c = L.chunk_of_tile(t);
    if (!c) { if (fog_out) *fog_out = iso2d::Fog::Unexplored; return iso2d::light_tint(0, iso2d::Fog::Unexplored); }
    const int lx = floor_mod32(t.x), ly = floor_mod32(t.y);
    const iso2d::Fog f = iso2d::fog_of(c->seen(lx, ly), c->known(lx, ly));
    if (fog_out) *fog_out = f;
    return iso2d::light_tint(c->samples[size_t(ly * 32 + lx)], f);
}

Color tint_for(const ActorView& a) {
    Color c = a.is_pc ? Color::rgba8(90, 150, 255) : Color::rgba8(110, 200, 80);
    if (a.dead) c = Color::rgba8(90, 90, 90, 200);
    else if (a.down) c = Color::rgba8(150, 120, 120, 230);
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
    : world_(dev, 8192), ui_(dev, 8192), lights_(dev), tex_(std::make_unique<Textures>(dev)) {}

// Mirrors the LightFrame into per-chunk R8 light maps. The resident set is reconciled against the
// snapshot's chunk list (robust to skipped frames); the upload per dirty chunk is shrunk to the
// bounding box of texels that really changed (the sim's dirty set is per chunk).
void SliceView::sync_lights(const LightView& L) {
    if (!L.present || L.frame == light_frame_) return;
    const bool gap = light_frame_ != 0 && L.frame != light_frame_ + 1;   // missed frames: re-check every chunk
    light_frame_ = L.frame;
    std::vector<iso2d::LightChunkKey> add, rem;
    auto in_frame = [&](const iso2d::LightChunkKey& k) {
        for (const auto& c : L.chunks) if (c.cx == k.x && c.cy == k.y && c.level == k.level) return true;
        return false;
    };
    for (const auto& k : lights_.keys()) if (!in_frame(k)) rem.push_back(k);
    for (const auto& c : L.chunks)
        if (!lights_.texture({c.cx, c.cy, c.level}).valid()) add.push_back({c.cx, c.cy, c.level});
    lights_.on_stream_delta(add, rem);
    for (const LightChunkView& c : L.chunks) {
        const iso2d::LightChunkKey k{c.cx, c.cy, c.level};
        bool dirty = gap || std::find(add.begin(), add.end(), k) != add.end();
        for (const LightDirtyView& d : L.dirty)
            if (d.chunk.cx == c.cx && d.chunk.cy == c.cy && d.chunk.level == c.level) dirty = true;
        if (!dirty) continue;
        int x0 = 32, y0 = 32, x1 = -1, y1 = -1;
        const bool fresh = std::find(add.begin(), add.end(), k) != add.end();
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if (fresh || lights_.sample(k, {x, y}) != c.samples[size_t(y * 32 + x)]) {
                    x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        const RectI r = x1 < 0 ? RectI{} : RectI{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
        lights_.update(k, std::span<const uint8_t, iso2d::kLightTiles>(c.samples), r);
    }
}

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
    auto visible = [&](const ActorView& a) {
        iso2d::Fog f;
        tile_light(L, a.tile, &f);
        return f == iso2d::Fog::Visible || a.is_pc;   // never draw what the team can't see
    };

    // ---- world: tiles ----
    for (const TileView& t : vm.tiles) {
        const Vec2f iso{float(t.tile.x), float(t.tile.y)};
        const float lift = float(t.elevation * kElevPx);
        const uint32_t key = uint32_t(t.tile.y * 32 + t.tile.x);
        if (t.kind == TileKind::Wall || t.layer >= 2) {
            iso2d::Sprite s{tex_->wall, float(kTileW), float(kTileH + kWallH), kTileW / 2.f, float(kWallH) + lift};
            s.tint = tile_light(L, t.tile);
            world_.draw(s, iso, 1, key, 0);
        } else if (t.elevation > 0 && t.layer == 0) {
            // Raised ground: a short block so the step has visible sides.
            iso2d::Sprite s{tex_->raised_block, float(kTileW), float(kTileH + kElevPx), kTileW / 2.f, lift};
            s.h = float(kTileH) + lift;   // stretch the side faces for multi-step elevation
            s.tint = tile_light(L, t.tile);
            world_.draw(s, iso, 0, key, t.layer);
        } else {
            iso2d::Sprite s{tex_->ground(t.kind), float(kTileW), float(kTileH), kTileW / 2.f, lift};
            s.tint = tile_light(L, t.tile);
            world_.draw(s, iso, 0, key, t.layer);
        }
    }
    // ---- world: actors (placeholder pawns), current-turn ring ----
    if (L.present) {   // glows under lit sources (presentation; entity-anchored ones follow the drawn token)
        std::vector<iso2d::GlowSource> glows = glows_from(L, palette_);
        std::vector<iso2d::GlowAnchor> anchors;
        anchors.reserve(vm.actors.size());
        for (const ActorView& a : vm.actors) anchors.push_back({a.id, {float(a.tile.x), float(a.tile.y)}});
        iso2d::follow_anchors(glows, anchors);
        for (const iso2d::GlowSource& gs : glows) {
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
        // Pawn feet at the tile centre; drawn on the wall layer so walls in front occlude it.
        iso2d::Sprite p{tex_->pawn, float(kPawnW), float(kPawnH), kPawnW / 2.f, float(kPawnH) - kTileH / 2.f - 4.f};
        p.tint = mul(tint_for(a), L.present ? iso2d::light_tint(
                                       L.chunk_of_tile(a.tile) ? L.chunk_of_tile(a.tile)->samples[size_t(floor_mod32(a.tile.y) * 32 + floor_mod32(a.tile.x))] : 0,
                                       iso2d::Fog::Visible) : Color{1, 1, 1, 1});
        if (L.present) p.tint = mul(p.tint, {1.6f, 1.6f, 1.6f, 1.f});   // tokens stay readable in dim light (presentation)
        world_.draw(p, iso, 1, a.id, 1);
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
    // Bottom-left: combat log + help.
    const float lh = 12.f;
    float ly = float(sh) - 24.f - lh * float(vm.log.size());
    if (!vm.log.empty()) rect({8, ly - 6}, {380, lh * float(vm.log.size()) + 8}, Color::rgba8(10, 12, 18, 170), 5);
    for (const std::string& l : vm.log) { text(l, {16, ly}, 1.5f, dim); ly += lh; }
    const std::string help = vm.status.empty() ? std::string("SPACE: STEP  A: AUTO  ARROWS: PAN  WHEEL/+/-: ZOOM  ESC: QUIT") : vm.status;
    text(help, {16, float(sh) - 16.f}, 1.f, dim);

    ui_.prepare({{0, 0}, sw, sh, 1.f});
}

void SliceView::submit(ICommandList& cl) {
    world_.submit(cl);
    ui_.submit(cl);
}

} // namespace slice

namespace slice {
std::vector<eng::iso2d::GlowSource> glows_from(const LightView& L, const eng::iso2d::GlowPalette& palette) {
    std::vector<eng::iso2d::GlowSource> out;
    if (!L.present) return out;
    for (const LightSourceView& ls : L.sources) {
        if (!ls.lit) continue;
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
        out.push_back(g);
    }
    return out;
}
} // namespace slice
