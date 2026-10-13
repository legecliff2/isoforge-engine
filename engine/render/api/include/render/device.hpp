#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <memory>
#include "base/handle.hpp"
#include "base/math.hpp"

namespace eng::render {

struct TextureTag; struct BufferTag; struct PipelineTag; struct TargetTag;
using TextureId  = Handle<TextureTag>;
using BufferId   = Handle<BufferTag>;
using PipelineId = Handle<PipelineTag>;
using TargetId   = Handle<TargetTag>;   // offscreen render target; TargetId{} = swapchain

enum class Format : uint8_t { RGBA8_UNORM, RGBA8_SRGB, R8_UNORM, D24S8 };
enum class Filter : uint8_t { Nearest, Linear };
enum class Blend  : uint8_t { Opaque, Alpha, Premultiplied, Additive };

struct TextureDesc  { uint32_t w, h; Format fmt; Filter filter = Filter::Nearest;
                      bool render_target = false; std::string_view debug_name; };
struct BufferDesc   { uint32_t size; enum Usage : uint8_t { Vertex, Index, Uniform } usage;
                      bool dynamic = true; };
struct PipelineDesc { std::string_view shader;     // logical name; backend resolves to SPIR-V/MSL/DXIL
                      Blend blend = Blend::Alpha; bool depth_test = false; };

struct DeviceCaps {
    uint32_t max_texture_size;
    bool     supports_compute;
    bool     supports_bindless;
    std::string_view backend_name;  // "sdl_gpu/vulkan", "null", ...
};

// Recorded on any thread, submitted on the render thread.
class ICommandList {
public:
    virtual ~ICommandList() = default;
    virtual void begin_pass(TargetId target, Color clear) = 0;
    virtual void end_pass() = 0;
    virtual void set_pipeline(PipelineId) = 0;
    virtual void set_viewport(RectI) = 0;
    virtual void set_scissor(RectI) = 0;
    virtual void bind_texture(uint32_t slot, TextureId) = 0;
    virtual void bind_vertex_buffer(BufferId, uint32_t offset) = 0;
    virtual void bind_index_buffer(BufferId, uint32_t offset) = 0;
    virtual void push_uniforms(uint32_t slot, std::span<const std::byte>) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t first_index,
                              int32_t vertex_offset, uint32_t instance_count = 1) = 0;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual DeviceCaps caps() const = 0;

    virtual TextureId  create_texture(const TextureDesc&, std::span<const std::byte> pixels = {}) = 0;
    virtual BufferId   create_buffer(const BufferDesc&) = 0;
    virtual PipelineId create_pipeline(const PipelineDesc&) = 0;
    virtual void       update_buffer(BufferId, uint32_t offset, std::span<const std::byte>) = 0;
    virtual void       update_texture(TextureId, RectI region, std::span<const std::byte>) = 0;
    virtual void       destroy(TextureId) = 0;
    virtual void       destroy(BufferId) = 0;
    virtual void       destroy(PipelineId) = 0;

    virtual bool          begin_frame() = 0;          // false => skip frame (minimized, lost)
    virtual ICommandList& command_list() = 0;          // valid between begin/end
    virtual void          end_frame() = 0;             // submit + present
    virtual void          on_resize(uint32_t w, uint32_t h) = 0;
};

struct NativeWindow;  // opaque, provided by platform
std::unique_ptr<IRenderDevice> create_sdlgpu_device(NativeWindow&, bool debug);
std::unique_ptr<IRenderDevice> create_null_device();

} // namespace eng::render
