#pragma once
// Area-spell preview drawing. Presentation only: ViewModel::area in, sprites out. No sim, no SDL.
//
// Depth, kept off the zone slot (iso2d::kZoneOverlayLayer / kZoneOverlayZ = 0 / 1) and off the
// cover overlay (that one is screen-space in SliceView::draw_cover):
//   tile fill   layer 0, z = 2, entity = tile.y * 32 + tile.x
//   tile edge   layer 0, z = 3, same entity
//   aim cursor  layer 0, z = 4, entity = aim tile (steady; the template pulses, the cursor does not)
//   creature    layer 1, z = 9 (above equip_visuals kZOutline), one ring for the whole footprint
//
// Pulse reads AreaPreviewView::pulse_ms (the frame clock). It does not read ViewModel::anim_phase.
// A tile is drawn only when the view has a non-wall floor for it. The aim cursor is drawn whenever
// the preview is present, including while tiles_pending is set and the tile list is empty.
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
