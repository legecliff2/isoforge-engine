#version 450
// SDL_GPU SPIR-V conventions: vertex uniform buffers live in set 1.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(set = 1, binding = 0) uniform SpriteUniforms { vec2 scale; vec2 offset; } u;
void main() {
    gl_Position = vec4(a_pos * u.scale + u.offset, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
