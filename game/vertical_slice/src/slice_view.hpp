#pragma once
// View-only renderer for the vertical slice: ViewModel -> iso2d sprites (+ screen-space
// overlay). Holds only GPU resources and presentation constants, never game state.
#include <memory>
#include <string_view>

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

    // Builds and uploads this frame's sprites. Call outside a render pass.
    void prepare(const ViewModel&, const eng::iso2d::Camera& world_cam, uint32_t screen_w, uint32_t screen_h);
    // Records draws inside the current pass (world first, then overlay).
    void submit(eng::render::ICommandList&);

    uint32_t world_sprites() const { return world_.sprite_count(); }
    uint32_t ui_sprites() const { return ui_.sprite_count(); }
    uint32_t batches() const { return world_.batch_count() + ui_.batch_count(); }

private:
    void text(std::string_view s, eng::Vec2f px, float scale, eng::Color c, int32_t layer = 20);
    void rect(eng::Vec2f px, eng::Vec2f size, eng::Color c, int32_t layer = 10);

    eng::render::IRenderDevice& dev_;
    eng::iso2d::SpriteBatch world_, ui_;
    struct Textures;
    std::unique_ptr<Textures> tex_;
    uint32_t ui_seq_ = 0;
};

} // namespace slice
