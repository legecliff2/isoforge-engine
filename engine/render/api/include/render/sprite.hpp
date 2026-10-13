#pragma once
// Contract for the built-in "sprite" pipeline (PipelineDesc::shader = "sprite").
// Every backend that resolves "sprite" must accept this vertex layout and uniforms.
#include <cstdint>
#include <string_view>

namespace eng::render {

inline constexpr std::string_view kSpriteShader = "sprite";

// 20 bytes: position (px), uv, RGBA8 tint.
struct SpriteVertex {
    float   x, y;
    float   u, v;
    uint8_t r, g, b, a;
};
static_assert(sizeof(SpriteVertex) == 20);

// Vertex uniform slot 0: clip = pos * scale + offset.
struct SpriteUniforms {
    float scale[2];
    float offset[2];
};
static_assert(sizeof(SpriteUniforms) == 16);

// Fragment texture slot 0 is the sprite texture.
inline constexpr uint32_t kSpriteTextureSlot = 0;
inline constexpr uint32_t kSpriteUniformSlot = 0;

} // namespace eng::render
