#include "aoe_overlay.hpp"

#include <algorithm>
#include <string>

#include "footprint_visuals.hpp"

namespace slice {

namespace {

eng::Color shape_rgb(AreaPreviewShape shape) noexcept {
    switch (shape) {
    case AreaPreviewShape::Cone: return eng::Color::rgba8(255, 110, 30);
    case AreaPreviewShape::Line: return eng::Color::rgba8(250, 210, 40);
    case AreaPreviewShape::Cube: return eng::Color::rgba8(50, 130, 255);
    case AreaPreviewShape::Sphere: return eng::Color::rgba8(170, 80, 255);
    case AreaPreviewShape::Cylinder: return eng::Color::rgba8(30, 200, 190);
    case AreaPreviewShape::None: break;
    }
    return {0.f, 0.f, 0.f, 0.f};
}

eng::Color with_alpha(eng::Color c, float a) noexcept {
    c.a = a;
    return c;
}

const TileView* floor_tile(const ViewModel& vm, eng::Vec2i t) noexcept {
    const TileView* found = nullptr;
    for (const TileView& tv : vm.tiles) {
        if (tv.tile != t || tv.kind == TileKind::Wall || tv.layer >= 2) continue;
        if (!found || tv.layer < found->layer) found = &tv;
    }
    return found;
}

float floor_lift(const ViewModel& vm, eng::Vec2i t, int elev_px) noexcept {
    const TileView* tv = floor_tile(vm, t);
    return tv ? float(tv->elevation) * float(elev_px) : 0.f;
}

const char* shape_name(AreaPreviewShape shape) noexcept {
    switch (shape) {
    case AreaPreviewShape::Cone: return "CONE";
    case AreaPreviewShape::Line: return "LINE";
    case AreaPreviewShape::Cube: return "CUBE";
    case AreaPreviewShape::Sphere: return "SPHERE";
    case AreaPreviewShape::Cylinder: return "CYLINDER";
    case AreaPreviewShape::None: break;
    }
    return "AREA";
}

} // namespace

float area_pulse01(uint32_t frame_ms) noexcept {
    constexpr uint32_t kPeriod = 800;
    const uint32_t t = frame_ms % kPeriod;
    const uint32_t up = t <= kPeriod / 2 ? t : kPeriod - t;
    return float(up) / float(kPeriod / 2);
}

eng::Color area_fill_tint(AreaPreviewShape shape, uint32_t frame_ms) noexcept {
    const eng::Color rgb = shape_rgb(shape);
    if (rgb.a <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    const float p = area_pulse01(frame_ms);
    return with_alpha(rgb, 0.32f + 0.20f * p);
}

eng::Color area_edge_tint(AreaPreviewShape shape, uint32_t frame_ms) noexcept {
    const eng::Color rgb = shape_rgb(shape);
    if (rgb.a <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    const float p = area_pulse01(frame_ms);
    return with_alpha(rgb, 0.78f + 0.16f * p);
}

eng::Color area_hit_tint(AreaHitSide side, uint32_t frame_ms) noexcept {
    const eng::Color rgb = side == AreaHitSide::Ally ? eng::Color::rgba8(70, 220, 185) : eng::Color::rgba8(240, 64, 56);
    const float p = area_pulse01(frame_ms);
    return with_alpha(rgb, 0.72f + 0.22f * p);
}

eng::Color area_aim_tint() noexcept { return eng::Color::rgba8(255, 220, 60, 210); }

bool area_tile_is_edge(const std::vector<eng::Vec2i>& tiles, eng::Vec2i tile) noexcept {
    auto has = [&](eng::Vec2i n) { return std::find(tiles.begin(), tiles.end(), n) != tiles.end(); };
    return !has({tile.x - 1, tile.y}) || !has({tile.x + 1, tile.y}) || !has({tile.x, tile.y - 1}) ||
           !has({tile.x, tile.y + 1});
}

void paint_area_overlay(const AreaOverlayGfx& g, const ViewModel& vm) {
    const AreaPreviewView& a = vm.area;
    if (!a.present) return;

    auto diamond = [&](eng::render::TextureId tex, eng::Color tint, int32_t z, float w, float h, float ax, float ay,
                       eng::Vec2f draw_iso, eng::Vec2f sort_iso, int32_t layer, uint32_t key) {
        if (tint.a <= 0.f) return;
        eng::iso2d::Sprite s{tex, w, h, ax, ay};
        s.tint = tint;
        g.world.draw(s, draw_iso, sort_iso, layer, key, z);
    };

    std::vector<eng::Vec2i> painted;
    painted.reserve(a.tiles.size());
    for (eng::Vec2i t : a.tiles) {
        if (std::find(painted.begin(), painted.end(), t) != painted.end()) continue;
        const TileView* floor = floor_tile(vm, t);
        if (!floor) continue;
        painted.push_back(t);
        const float lift = float(floor->elevation) * float(g.elev_px);
        const eng::Vec2f iso{float(t.x), float(t.y)};
        const uint32_t key = uint32_t(t.y * 32 + t.x);
        const float tw = float(g.tile_w), th = float(g.tile_h);
        diamond(g.floor, area_fill_tint(a.shape, a.pulse_ms), kAreaFillZ, tw, th, tw * 0.5f, lift, iso, iso, 0, key);
        if (area_tile_is_edge(a.tiles, t))
            diamond(g.ring, area_edge_tint(a.shape, a.pulse_ms), kAreaEdgeZ, tw, th, tw * 0.5f, lift, iso, iso, 0, key);
    }

    {
        const float lift = floor_lift(vm, a.aim, g.elev_px);
        const eng::Vec2f iso{float(a.aim.x), float(a.aim.y)};
        const uint32_t key = uint32_t(a.aim.y * 32 + a.aim.x);
        const float tw = float(g.tile_w), th = float(g.tile_h);
        diamond(g.ring, area_aim_tint(), kAreaAimZ, tw, th, tw * 0.5f, lift, iso, iso, 0, key);
    }

    for (const AreaHitView& h : a.hits) {
        const ActorView* actor = nullptr;
        for (const ActorView& act : vm.actors)
            if (act.id == h.id) { actor = &act; break; }
        if (!actor || actor->dead) continue;
        const FootprintGeom fp = footprint_geom(actor->tile, actor->footprint, float(g.tile_w), float(g.tile_h));
        const eng::Vec2f draw = fp.sort_iso;
        const eng::Vec2f sort = actor_sort_iso(fp);
        diamond(g.ring, area_hit_tint(h.side, a.pulse_ms), kAreaHitZ, fp.ring_w, fp.ring_h, fp.ring_anchor_x,
                fp.ring_anchor_y, draw, sort, 1, actor->id);
    }
}

std::string area_caption_line(const ViewModel& vm) {
    const AreaPreviewView& a = vm.area;
    if (!a.present && a.note.empty()) return {};
    if (!a.present) return a.note + "   F5 CLOSE";
    std::string line = shape_name(a.shape);
    if (!a.spell_label.empty()) {
        line += ' ';
        line += a.spell_label;
    }
    line += "  AIM ";
    line += std::to_string(a.aim.x);
    line += ' ';
    line += std::to_string(a.aim.y);
    line += "  HIT ";
    line += std::to_string(a.hits.size());
    if (a.tiles_pending && a.note.empty()) line += "  TILES PENDING";
    if (!a.note.empty()) {
        line += "  ";
        line += a.note;
    }
    line += "   F5 CLOSE  Q SPELL";
    return line;
}

} // namespace slice
