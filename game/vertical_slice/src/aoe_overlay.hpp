#pragma once
// Area-spell preview drawing. Presentation only: ViewModel::area in, sprites out. No sim, no SDL.
//
// Depth, kept off the zone slot (iso2d::kZoneOverlayLayer / kZoneOverlayZ = 0 / 1) and off the
// cover overlay (that one is screen-space in SliceView::draw_cover):
//   tile fill   layer 0, z = 2, entity = (tile.y - y0) * map_width + (tile.x - x0)
//   tile edge   layer 0, z = 3, same entity
//   aim cursor  layer 0, z = 4, entity = aim tile (steady; the template pulses, the cursor does not)
//   creature    layer 1, z = 9 (above equip_visuals kZOutline), one ring for the whole footprint
//
// map_width is the view's tile span (max x - min x + 1), not a fixed 32. A stride of 32 aliases
// once the map is wider than one chunk, and a negative cursor wraps through uint32_t.
// Pulse reads AreaPreviewView::pulse_ms (the frame clock). It does not read ViewModel::anim_phase.
// A tile is drawn only when it is inside that span and the view has a non-wall floor for it.
// The aim cursor is drawn when the aim tile is inside the span, including while tiles_pending is
// set and the tile list is empty. An aim outside the span is not drawn.
#include <cstdint>
#include <string>
#include <vector>

#include "iso2d/sprite_batch.hpp"
#include "render/device.hpp"
#include "view_model.hpp"

namespace slice {

inline constexpr int32_t kAreaFillZ = 2;
inline constexpr int32_t kAreaEdgeZ = 3;
inline constexpr int32_t kAreaAimZ = 4;
inline constexpr int32_t kAreaHitZ = 9;

// Triangle wave, period 800 ms, 0 at 0 and 1 at 400. frame_ms is the frame clock.
float area_pulse01(uint32_t frame_ms) noexcept;

eng::Color area_fill_tint(AreaPreviewShape shape, uint32_t frame_ms) noexcept;
eng::Color area_edge_tint(AreaPreviewShape shape, uint32_t frame_ms) noexcept;
eng::Color area_hit_tint(AreaHitSide side, uint32_t frame_ms) noexcept;
eng::Color area_aim_tint() noexcept;

// Orthogonal edge: a listed tile with any 4-neighbour missing from the set.
bool area_tile_is_edge(const std::vector<eng::Vec2i>& tiles, eng::Vec2i tile) noexcept;

// Inclusive tile rectangle of the view. x1 < x0 when the view has no tiles.
// key() is unique inside the rectangle: (y - y0) * width + (x - x0), width = x1 - x0 + 1.
struct AreaMapSpan {
    int x0 = 0;
    int y0 = 0;
    int x1 = -1;
    int y1 = -1;
    bool contains(eng::Vec2i t) const noexcept { return t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1; }
    uint32_t key(eng::Vec2i t) const noexcept {
        const int w = x1 - x0 + 1;
        return uint32_t(int64_t(t.y - y0) * int64_t(w) + int64_t(t.x - x0));
    }
};
AreaMapSpan area_map_span(const ViewModel& vm) noexcept;

struct AreaOverlayGfx {
    eng::iso2d::SpriteBatch& world;
    eng::render::TextureId floor;
    eng::render::TextureId ring;
    int tile_w = 64;
    int tile_h = 32;
    int elev_px = 8;
};

void paint_area_overlay(const AreaOverlayGfx& g, const ViewModel& vm);

// Empty when the preview is fully off. The slice draws it under the turn panel, below the
// cover caption when that overlay is up.
std::string area_caption_line(const ViewModel& vm);

} // namespace slice
