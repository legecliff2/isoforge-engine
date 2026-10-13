#include "iso2d/sprite_batch.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eng::iso2d {

using namespace eng::render;

namespace {
template <class T> std::span<const std::byte> bytes(const std::vector<T>& v) {
    return std::as_bytes(std::span<const T>(v));
}
uint8_t to8(float f) { return uint8_t(std::lround(std::clamp(f, 0.f, 1.f) * 255.f)); }
} // namespace

SpriteBatch::SpriteBatch(IRenderDevice& dev, uint32_t max_sprites) : dev_(dev), max_sprites_(max_sprites) {
    vbo_ = dev_.create_buffer({uint32_t(max_sprites * 4 * sizeof(SpriteVertex)), BufferDesc::Vertex, true});
    ibo_ = dev_.create_buffer({uint32_t(max_sprites * 6 * sizeof(uint32_t)), BufferDesc::Index, false});
    pipeline_ = dev_.create_pipeline({kSpriteShader, Blend::Alpha, false});
    std::vector<uint32_t> idx(size_t(max_sprites) * 6);
    for (uint32_t q = 0; q < max_sprites; ++q) {
        const uint32_t b = q * 4;
        const uint32_t quad[6] = {b, b + 1, b + 2, b + 2, b + 3, b};
        std::copy(std::begin(quad), std::end(quad), idx.begin() + q * 6);
    }
    dev_.update_buffer(ibo_, 0, bytes(idx));
}

SpriteBatch::~SpriteBatch() {
    dev_.destroy(vbo_);
    dev_.destroy(ibo_);
    dev_.destroy(pipeline_);
}

void SpriteBatch::begin() {
    items_.clear();
    order_.clear();
    runs_.clear();
}

void SpriteBatch::draw(const Sprite& s, Vec2f iso_pos, int32_t layer, uint32_t sort_key, int32_t z) {
    if (items_.size() >= max_sprites_) return;  // v0: drop overflow
    items_.push_back({s, iso_pos, make_sort_key(iso_pos, layer, z, sort_key)});
}

void SpriteBatch::prepare(const Camera& cam) {
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    // Total order on SortKey, stable on submission order for exact duplicates.
    std::stable_sort(order_.begin(), order_.end(),
                     [&](uint32_t a, uint32_t b) { return items_[a].key < items_[b].key; });

    verts_.clear();
    runs_.clear();
    verts_.reserve(items_.size() * 4);
    for (uint32_t i : order_) {
        const Item& it = items_[i];
        const Sprite& s = it.sprite;
        const Vec2f p = proj_.to_screen(it.iso.x, it.iso.y);
        const float x0 = p.x - s.anchor_x, y0 = p.y - s.anchor_y, x1 = x0 + s.w, y1 = y0 + s.h;
        const uint8_t r = to8(s.tint.r), g = to8(s.tint.g), b = to8(s.tint.b), a = to8(s.tint.a);
        verts_.push_back({x0, y0, s.u0, s.v0, r, g, b, a});
        verts_.push_back({x1, y0, s.u1, s.v0, r, g, b, a});
        verts_.push_back({x1, y1, s.u1, s.v1, r, g, b, a});
        verts_.push_back({x0, y1, s.u0, s.v1, r, g, b, a});
        const uint32_t q = uint32_t(verts_.size() / 4 - 1);
        if (runs_.empty() || !(runs_.back().texture == s.texture)) runs_.push_back({s.texture, q, 1});
        else ++runs_.back().quads;
    }
    if (!verts_.empty()) dev_.update_buffer(vbo_, 0, bytes(verts_));

    const float w = float(cam.viewport_w), h = float(cam.viewport_h);
    uniforms_.scale[0] = 2.f / w;
    uniforms_.scale[1] = -2.f / h;
    uniforms_.offset[0] = -1.f - 2.f * cam.position.x / w;
    uniforms_.offset[1] = 1.f + 2.f * cam.position.y / h;
}

void SpriteBatch::submit(ICommandList& cl) {
    if (runs_.empty()) return;
    cl.set_pipeline(pipeline_);
    cl.bind_vertex_buffer(vbo_, 0);
    cl.bind_index_buffer(ibo_, 0);
    cl.push_uniforms(kSpriteUniformSlot, std::as_bytes(std::span(&uniforms_, 1)));
    for (const Run& r : runs_) {
        cl.bind_texture(kSpriteTextureSlot, r.texture);
        cl.draw_indexed(r.quads * 6, r.first_quad * 6, 0);
    }
}

} // namespace eng::iso2d
