#pragma once
#include <cstdint>
#include <vector>
#include "base/math.hpp"
#include "base/present_math.hpp"
#include "iso2d/iso.hpp"
#include "render/device.hpp"
#include "render/sprite.hpp"

namespace eng::iso2d {

struct Sprite {
    render::TextureId texture;
    float w = 0.f, h = 0.f;              // size in px
    float anchor_x = 0.f, anchor_y = 0.f; // px from the sprite's top-left placed on the iso point
    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    Color tint{1.f, 1.f, 1.f, 1.f};
};

struct Camera {
    Vec2f position;          // world px at the top-left of the viewport
    uint32_t viewport_w = 1280, viewport_h = 720;
    float zoom = 1.f;        // screen px per world px

    Vec2f world_to_screen(Vec2f w) const { return {(w.x - position.x) * zoom, (w.y - position.y) * zoom}; }
    Vec2f screen_to_world(Vec2f s) const { return {s.x / zoom + position.x, s.y / zoom + position.y}; }
};

// Collects sprites, depth-sorts them, and emits one draw per run of equal texture.
// Never drops a sprite: `max_sprites` is the size of one vertex page. When a frame holds more, prepare()
// allocates extra pages and submit() flushes the globally sorted sequence page by page (a texture run that
// crosses a page boundary becomes two draws), so the draw order is exactly the unsplit order.
// Usage per frame: begin() → draw()* → prepare(camera) [before begin_pass] → submit(cl) [inside pass].
class SpriteBatch {
public:
    explicit SpriteBatch(render::IRenderDevice&, uint32_t max_sprites = 16384);
    ~SpriteBatch();
    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    void begin();
    void draw(const Sprite&, Vec2f iso_pos, int32_t layer, uint32_t sort_key, int32_t z = 0);
    // Places the sprite's anchor at a pixel position (no iso projection), e.g. for UI drawn
    // with an identity camera. Sort key = (layer, px.y, z, sort_key).
    void draw_px(const Sprite&, Vec2f px, int32_t layer, uint32_t sort_key, int32_t z = 0);
    void prepare(const Camera&);           // sorts + uploads; must be outside a render pass
    void submit(render::ICommandList&);    // records draws inside the current pass

    const IsoProjection& projection() const { return proj_; }
    void set_projection(IsoProjection p) { proj_ = p; }

    uint32_t sprite_count() const { return uint32_t(items_.size()); }
    uint32_t batch_count() const { return uint32_t(runs_.size()); }   // texture runs (page splits not counted)
    uint32_t page_count() const { return pages_used_; }                // vertex pages this frame (>1 = split)
    uint64_t split_frames() const { return split_frames_; }            // prepare() calls that needed >1 page
    // Draw order after prepare(): indices into submission order (for tests/debug).
    const std::vector<uint32_t>& order() const { return order_; }

private:
    struct Item { Sprite sprite; Vec2f iso; SortKey key; bool px = false; };
    struct Run  { render::TextureId texture; uint32_t first_quad, quads; };

    render::IRenderDevice& dev_;
    IsoProjection proj_;
    uint32_t max_sprites_;
    render::BufferId vbo_, ibo_;
    std::vector<render::BufferId> extra_vbos_;   // pages 1.. (created on first overflow, kept for later frames)
    uint32_t pages_used_ = 0;
    uint64_t split_frames_ = 0;
    render::PipelineId pipeline_;
    std::vector<Item> items_;
    std::vector<uint32_t> order_;
    std::vector<render::SpriteVertex> verts_;
    std::vector<Run> runs_;
    render::SpriteUniforms uniforms_{};
};

} // namespace eng::iso2d
