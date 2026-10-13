#include <metal_stdlib>
using namespace metal;

struct VIn  { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 color [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 color; };
struct SpriteUniforms { float2 scale; float2 offset; };

vertex VOut sprite_vs(VIn in [[stage_in]], constant SpriteUniforms& u [[buffer(0)]]) {
    VOut o;
    o.pos = float4(in.pos * u.scale + u.offset, 0.0, 1.0);
    o.uv = in.uv;
    o.color = in.color;
    return o;
}

fragment float4 sprite_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                          sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
}
