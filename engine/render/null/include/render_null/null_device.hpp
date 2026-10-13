#pragma once
// Backend header: only `engine` and tests may include this (§2.2 rule 4).
#include <cstdint>
#include <vector>
#include "render/device.hpp"

namespace eng::render {

struct NullDrawCall {
    PipelineId pipeline;
    TextureId  texture;
    uint32_t   index_count, first_index;
    int32_t    vertex_offset;
};

struct NullFrameStats {
    uint32_t passes = 0;
    uint32_t draw_calls = 0;
    uint32_t indices = 0;
    std::vector<NullDrawCall> draws;
};

// Validates API usage (pass nesting, live handles) and records what was drawn.
class NullDevice final : public IRenderDevice, private ICommandList {
public:
    DeviceCaps caps() const override;
    TextureId  create_texture(const TextureDesc&, std::span<const std::byte> pixels = {}) override;
    BufferId   create_buffer(const BufferDesc&) override;
    PipelineId create_pipeline(const PipelineDesc&) override;
    void update_buffer(BufferId, uint32_t offset, std::span<const std::byte>) override;
    void update_texture(TextureId, RectI region, std::span<const std::byte>) override;
    void destroy(TextureId) override;
    void destroy(BufferId) override;
    void destroy(PipelineId) override;
    bool begin_frame() override;
    ICommandList& command_list() override { return *this; }
    void end_frame() override;
    void on_resize(uint32_t w, uint32_t h) override { width_ = w; height_ = h; }

    const NullFrameStats& last_frame() const { return last_; }
    uint32_t errors() const { return errors_; }
    uint32_t live_textures() const;
    uint32_t live_buffers() const;

private:
    void begin_pass(TargetId, Color) override;
    void end_pass() override;
    void set_pipeline(PipelineId p) override { pipeline_ = p; }
    void set_viewport(RectI) override {}
    void set_scissor(RectI) override {}
    void bind_texture(uint32_t slot, TextureId t) override;
    void bind_vertex_buffer(BufferId, uint32_t) override {}
    void bind_index_buffer(BufferId, uint32_t) override {}
    void push_uniforms(uint32_t, std::span<const std::byte>) override {}
    void draw_indexed(uint32_t, uint32_t, int32_t, uint32_t) override;

    struct Slot { uint32_t generation = 0; bool live = false; uint32_t size = 0; };
    template <class H> H alloc(std::vector<Slot>&, uint32_t size);
    template <class H> bool check(const std::vector<Slot>&, H) const;

    std::vector<Slot> textures_{1}, buffers_{1}, pipelines_{1};
    bool in_frame_ = false, in_pass_ = false;
    PipelineId pipeline_{};
    TextureId texture_{};
    NullFrameStats cur_, last_;
    uint32_t errors_ = 0, width_ = 0, height_ = 0;
};

} // namespace eng::render
