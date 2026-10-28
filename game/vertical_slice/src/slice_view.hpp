#pragma once
// View-only renderer for the vertical slice: ViewModel -> iso2d sprites (+ screen-space
// overlay). Holds only GPU resources and presentation constants, never game state.
#include <memory>
#include <string_view>
#include <vector>

#include "iso2d/light_glow.hpp"
#include "iso2d/light_map.hpp"
#include "light_glows.hpp"
#include "iso2d/sprite_batch.hpp"
#include "render/device.hpp"
#include "view_model.hpp"

namespace slice {

class SliceView {
public:
    explicit SliceView(eng::render::IRenderDevice&);
    ~SliceView();

    // World px of the centre of tile (x, y) at elevation 0.
    eng::Vec2f tile_center_px(float x, float y) const;
    // Tile under a viewport pixel. Inverse of tile_center_px for that tile's centre.
    eng::Vec2i tile_at_screen(eng::Vec2f screen_px, const eng::iso2d::Camera& cam) const;
    // World px centre of the combatants' screen-space bounding box (tile diamonds + sprite
    // height), or of the whole map when there are no actors. Presentation only.
    eng::Vec2f actors_focus_px(const ViewModel&) const;

    // Builds and uploads this frame's sprites. Call outside a render pass.
    void prepare(const ViewModel&, const eng::iso2d::Camera& world_cam, uint32_t screen_w, uint32_t screen_h);
    // Records draws inside the current pass (world first, then overlay).
    void submit(eng::render::ICommandList&);

    uint32_t world_sprites() const { return world_.sprite_count(); }
    uint32_t ui_sprites() const { return ui_.sprite_count(); }
    const eng::iso2d::LightMaps& light_maps() const { return lights_; }
    const eng::iso2d::FogMaps& fog_maps() const { return fogs_; }
    // Tint + fog state of a world tile from the retained light / fog maps (held for blocked chunks).
    eng::Color tile_tint(const LightView&, eng::Vec2i tile, eng::iso2d::Fog* fog_out = nullptr) const;
    uint32_t batches() const { return world_.batch_count() + ui_.batch_count(); }

private:
    void text(std::string_view s, eng::Vec2f px, float scale, eng::Color c, int32_t layer = 20);
    void rect(eng::Vec2f px, eng::Vec2f size, eng::Color c, int32_t layer = 10);
    // Screen-space cover debug line + badge. No-op unless ViewModel::cover.present (or a note).
    void draw_cover(const ViewModel&, const eng::iso2d::Camera&);

    void sync_lights(const LightView&);

    eng::iso2d::SpriteBatch world_, ui_;
    eng::iso2d::LightMaps lights_;
    eng::iso2d::GlowPalette palette_;   // light preset -> glow colour / intensity (presentation only)
    eng::iso2d::FogMaps fogs_;
    uint64_t light_frame_ = 0;
    int fog_key_ = -1;   // (fog on, team) last uploaded; a change refreshes every fog map
    struct Textures;
    std::unique_ptr<Textures> tex_;
    struct TintLookup;
    struct TintEntry { int32_t cx, cy; const uint8_t* light; const uint8_t* fog; };
    // Per-frame scratch, reused so a steady frame does no heap allocation on the glow path.
    std::vector<eng::iso2d::GlowSource> glows_;
    std::vector<eng::iso2d::GlowAnchor> anchors_;
    std::vector<TintEntry> tint_entries_;
    uint32_t ui_seq_ = 0;
};

} // namespace slice
