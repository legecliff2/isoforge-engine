// SDL_GPU implementation of IRenderDevice (covers Vulkan / Metal; D3D12 needs DXIL, see README).
#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <vector>

#include "platform/native_window.hpp"
#include "render/device.hpp"
#include "render/sprite.hpp"

namespace eng::render {
namespace {

#include "sprite_shaders.inl"

template <class T>
struct SlotMap {
    struct Slot { uint32_t gen = 0; bool live = false; T value{}; };
    std::vector<Slot> slots{1};
    template <class H> H add(T v) {
        for (uint32_t i = 1; i < slots.size(); ++i)
            if (!slots[i].live) { slots[i] = {slots[i].gen + 1, true, v}; return H{i, slots[i].gen}; }
        slots.push_back({1, true, v});
        return H{uint32_t(slots.size() - 1), 1};
    }
    template <class H> T* get(H h) {
        if (h.index == 0 || h.index >= slots.size()) return nullptr;
        Slot& s = slots[h.index];
        return s.live && s.gen == h.generation ? &s.value : nullptr;
    }
    template <class H> void remove(H h) { if (get(h)) slots[h.index].live = false; }
};

struct Tex { SDL_GPUTexture* tex = nullptr; SDL_GPUSampler* smp = nullptr; uint32_t w = 0, h = 0;
             Format fmt{}; bool target = false; };
struct Buf { SDL_GPUBuffer* buf = nullptr; uint32_t size = 0; bool dynamic = true; };
struct Pipe { Blend blend = Blend::Alpha; bool sprite = false;
              SDL_GPUGraphicsPipeline* swapchain = nullptr;  // built for swapchain format
              SDL_GPUGraphicsPipeline* rgba8 = nullptr; };   // built for offscreen RGBA8 targets

SDL_GPUTextureFormat to_sdl(Format f) {
    switch (f) {
    case Format::RGBA8_UNORM: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::R8_UNORM: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case Format::D24S8: return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}
uint32_t bpp(Format f) { return f == Format::R8_UNORM ? 1 : 4; }

SDL_GPUColorTargetBlendState blend_state(Blend b) {
    SDL_GPUColorTargetBlendState s{};
    s.enable_blend = b != Blend::Opaque;
    s.color_blend_op = s.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    switch (b) {
    case Blend::Opaque: break;
    case Blend::Alpha:
        s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case Blend::Premultiplied:
        s.src_color_blendfactor = s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s.dst_color_blendfactor = s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case Blend::Additive:
        s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    }
    return s;
}

class SdlGpuDevice final : public IRenderDevice, private ICommandList {
public:
    SdlGpuDevice(SDL_GPUDevice* dev, SDL_Window* win) : dev_(dev), win_(win) {
        backend_name_ = std::string("sdl_gpu/") + SDL_GetGPUDeviceDriver(dev_);
        swap_fmt_ = SDL_GetGPUSwapchainTextureFormat(dev_, win_);
        const SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(dev_);
        SDL_GPUShaderCreateInfo vs{}, fs{};
        if (fmts & SDL_GPU_SHADERFORMAT_SPIRV) {
            vs.code = kSpriteVertSpv; vs.code_size = sizeof(kSpriteVertSpv); vs.entrypoint = "main";
            fs.code = kSpriteFragSpv; fs.code_size = sizeof(kSpriteFragSpv); fs.entrypoint = "main";
            vs.format = fs.format = SDL_GPU_SHADERFORMAT_SPIRV;
        } else {
            vs.code = fs.code = reinterpret_cast<const Uint8*>(kSpriteMsl);
            vs.code_size = fs.code_size = sizeof(kSpriteMsl) - 1;
            vs.entrypoint = "sprite_vs"; fs.entrypoint = "sprite_fs";
            vs.format = fs.format = SDL_GPU_SHADERFORMAT_MSL;
        }
        vs.stage = SDL_GPU_SHADERSTAGE_VERTEX; vs.num_uniform_buffers = 1;
        fs.stage = SDL_GPU_SHADERSTAGE_FRAGMENT; fs.num_samplers = 1;
        sprite_vs_ = SDL_CreateGPUShader(dev_, &vs);
        sprite_fs_ = SDL_CreateGPUShader(dev_, &fs);
        if (!sprite_vs_ || !sprite_fs_) SDL_Log("sdl_gpu: sprite shader creation failed: %s", SDL_GetError());
    }

    ~SdlGpuDevice() override {
        SDL_WaitForGPUIdle(dev_);
        for (auto& s : textures_.slots) if (s.live) release(s.value);
        for (auto& s : buffers_.slots) if (s.live) SDL_ReleaseGPUBuffer(dev_, s.value.buf);
        for (auto& s : pipelines_.slots) if (s.live) release(s.value);
        if (sprite_vs_) SDL_ReleaseGPUShader(dev_, sprite_vs_);
        if (sprite_fs_) SDL_ReleaseGPUShader(dev_, sprite_fs_);
        SDL_ReleaseWindowFromGPUDevice(dev_, win_);
        SDL_DestroyGPUDevice(dev_);
    }

    DeviceCaps caps() const override { return {8192, true, false, backend_name_}; }

    TextureId create_texture(const TextureDesc& d, std::span<const std::byte> px) override {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = to_sdl(d.fmt);
        ci.width = d.w; ci.height = d.h; ci.layer_count_or_depth = 1; ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (d.render_target)
            ci.usage |= d.fmt == Format::D24S8 ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                               : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        Tex t;
        t.tex = SDL_CreateGPUTexture(dev_, &ci);
        if (!t.tex) { SDL_Log("sdl_gpu: create_texture failed: %s", SDL_GetError()); return {}; }
        if (!d.debug_name.empty()) SDL_SetGPUTextureName(dev_, t.tex, std::string(d.debug_name).c_str());
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = d.filter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        t.smp = SDL_CreateGPUSampler(dev_, &si);
        t.w = d.w; t.h = d.h; t.fmt = d.fmt; t.target = d.render_target;
        TextureId id = textures_.add<TextureId>(t);
        if (!px.empty()) update_texture(id, {0, 0, int32_t(d.w), int32_t(d.h)}, px);
        return id;
    }

    BufferId create_buffer(const BufferDesc& d) override {
        if (d.usage == BufferDesc::Uniform) return buffers_.add<BufferId>({nullptr, d.size, d.dynamic});
        SDL_GPUBufferCreateInfo ci{};
        ci.usage = d.usage == BufferDesc::Vertex ? SDL_GPU_BUFFERUSAGE_VERTEX : SDL_GPU_BUFFERUSAGE_INDEX;
        ci.size = d.size;
        Buf b{SDL_CreateGPUBuffer(dev_, &ci), d.size, d.dynamic};
        if (!b.buf) { SDL_Log("sdl_gpu: create_buffer failed: %s", SDL_GetError()); return {}; }
        return buffers_.add<BufferId>(b);
    }

    PipelineId create_pipeline(const PipelineDesc& d) override {
        Pipe p;
        p.blend = d.blend;
        p.sprite = d.shader == kSpriteShader;
        if (!p.sprite) { SDL_Log("sdl_gpu: unknown shader '%.*s'", int(d.shader.size()), d.shader.data()); return {}; }
        return pipelines_.add<PipelineId>(p);
    }

    void update_buffer(BufferId id, uint32_t offset, std::span<const std::byte> data) override {
        Buf* b = buffers_.get(id);
        if (!b || !b->buf || data.empty() || offset + data.size() > b->size) return;
        upload([&](SDL_GPUCopyPass* cp, SDL_GPUTransferBuffer* tb) {
            SDL_GPUTransferBufferLocation src{tb, 0};
            SDL_GPUBufferRegion dst{b->buf, offset, uint32_t(data.size())};
            SDL_UploadToGPUBuffer(cp, &src, &dst, b->dynamic && offset == 0);
        }, data);
    }

    void update_texture(TextureId id, RectI r, std::span<const std::byte> data) override {
        Tex* t = textures_.get(id);
        if (!t || data.size() < size_t(r.w) * r.h * bpp(t->fmt)) return;
        upload([&](SDL_GPUCopyPass* cp, SDL_GPUTransferBuffer* tb) {
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            src.pixels_per_row = uint32_t(r.w);
            src.rows_per_layer = uint32_t(r.h);
            SDL_GPUTextureRegion dst{};
            dst.texture = t->tex;
            dst.x = uint32_t(r.x); dst.y = uint32_t(r.y); dst.w = uint32_t(r.w); dst.h = uint32_t(r.h); dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
        }, data.first(size_t(r.w) * r.h * bpp(t->fmt)));
    }

    void destroy(TextureId id) override { if (Tex* t = textures_.get(id)) { release(*t); textures_.remove(id); } }
    void destroy(BufferId id) override {
        if (Buf* b = buffers_.get(id)) { if (b->buf) SDL_ReleaseGPUBuffer(dev_, b->buf); buffers_.remove(id); }
    }
    void destroy(PipelineId id) override { if (Pipe* p = pipelines_.get(id)) { release(*p); pipelines_.remove(id); } }

    bool begin_frame() override {
        cmd_ = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd_) { SDL_Log("sdl_gpu: acquire cmd failed: %s", SDL_GetError()); return false; }
        swap_tex_ = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd_, win_, &swap_tex_, &swap_w_, &swap_h_) || !swap_tex_) {
            SDL_SubmitGPUCommandBuffer(cmd_);  // minimized / not ready: nothing to draw
            cmd_ = nullptr;
            return false;
        }
        return true;
    }

    ICommandList& command_list() override { return *this; }

    void end_frame() override {
        if (pass_) end_pass();
        if (cmd_) SDL_SubmitGPUCommandBuffer(cmd_);
        cmd_ = nullptr;
        swap_tex_ = nullptr;
    }

    void on_resize(uint32_t, uint32_t) override {}  // SDL_GPU recreates the swapchain itself

private:
    // ---- ICommandList ----
    void begin_pass(TargetId target, Color clear) override {
        if (!cmd_ || pass_) return;
        SDL_GPUColorTargetInfo ct{};
        if (target.valid()) {
            Tex* t = textures_.get(TextureId{target.index, target.generation});
            if (!t || !t->target) return;
            ct.texture = t->tex; pass_w_ = t->w; pass_h_ = t->h; pass_fmt_ = to_sdl(t->fmt);
        } else {
            ct.texture = swap_tex_; pass_w_ = swap_w_; pass_h_ = swap_h_; pass_fmt_ = swap_fmt_;
        }
        ct.clear_color = {clear.r, clear.g, clear.b, clear.a};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        pass_ = SDL_BeginGPURenderPass(cmd_, &ct, 1, nullptr);
    }
    void end_pass() override { if (pass_) SDL_EndGPURenderPass(pass_); pass_ = nullptr; }

    void set_pipeline(PipelineId id) override {
        Pipe* p = pipelines_.get(id);
        if (!pass_ || !p) return;
        SDL_GPUGraphicsPipeline* gp = pipeline_for(*p, pass_fmt_);
        if (gp) SDL_BindGPUGraphicsPipeline(pass_, gp);
    }
    void set_viewport(RectI r) override {
        if (!pass_) return;
        SDL_GPUViewport v{float(r.x), float(r.y), float(r.w), float(r.h), 0.f, 1.f};
        SDL_SetGPUViewport(pass_, &v);
    }
    void set_scissor(RectI r) override {
        if (!pass_) return;
        SDL_Rect s{r.x, r.y, r.w, r.h};
        SDL_SetGPUScissor(pass_, &s);
    }
    void bind_texture(uint32_t slot, TextureId id) override {
        Tex* t = textures_.get(id);
        if (!pass_ || !t) return;
        SDL_GPUTextureSamplerBinding b{t->tex, t->smp};
        SDL_BindGPUFragmentSamplers(pass_, slot, &b, 1);
    }
    void bind_vertex_buffer(BufferId id, uint32_t offset) override {
        Buf* b = buffers_.get(id);
        if (!pass_ || !b || !b->buf) return;
        SDL_GPUBufferBinding bb{b->buf, offset};
        SDL_BindGPUVertexBuffers(pass_, 0, &bb, 1);
    }
    void bind_index_buffer(BufferId id, uint32_t offset) override {
        Buf* b = buffers_.get(id);
        if (!pass_ || !b || !b->buf) return;
        SDL_GPUBufferBinding bb{b->buf, offset};
        SDL_BindGPUIndexBuffer(pass_, &bb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }
    // v0: uniforms go to the vertex stage (the sprite pipeline's only uniform consumer).
    void push_uniforms(uint32_t slot, std::span<const std::byte> data) override {
        if (cmd_) SDL_PushGPUVertexUniformData(cmd_, slot, data.data(), uint32_t(data.size()));
    }
    void draw_indexed(uint32_t count, uint32_t first, int32_t voff, uint32_t instances) override {
        if (pass_) SDL_DrawGPUIndexedPrimitives(pass_, count, instances, first, voff, 0);
    }

    // ---- helpers ----
    template <class F>
    void upload(F&& record, std::span<const std::byte> data) {
        if (pass_) { SDL_Log("sdl_gpu: upload inside a render pass is not allowed"); return; }
        SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(data.size()), 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &ti);
        void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
        std::memcpy(p, data.data(), data.size());
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        const bool own = cmd_ == nullptr;  // outside a frame: one-shot command buffer
        SDL_GPUCommandBuffer* c = own ? SDL_AcquireGPUCommandBuffer(dev_) : cmd_;
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
        record(cp, tb);
        SDL_EndGPUCopyPass(cp);
        if (own) SDL_SubmitGPUCommandBuffer(c);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);  // deferred until the GPU is done
    }

    SDL_GPUGraphicsPipeline* pipeline_for(Pipe& p, SDL_GPUTextureFormat fmt) {
        const bool swap = fmt == swap_fmt_;
        SDL_GPUGraphicsPipeline*& slot = swap ? p.swapchain : p.rgba8;
        if (slot) return slot;

        SDL_GPUVertexBufferDescription vb{};
        vb.slot = 0; vb.pitch = sizeof(SpriteVertex); vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, uint32_t(offsetof(SpriteVertex, x))};
        attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, uint32_t(offsetof(SpriteVertex, u))};
        attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, uint32_t(offsetof(SpriteVertex, r))};

        SDL_GPUColorTargetDescription ctd{};
        ctd.format = fmt;
        ctd.blend_state = blend_state(p.blend);

        SDL_GPUGraphicsPipelineCreateInfo ci{};
        ci.vertex_shader = sprite_vs_;
        ci.fragment_shader = sprite_fs_;
        ci.vertex_input_state.vertex_buffer_descriptions = &vb;
        ci.vertex_input_state.num_vertex_buffers = 1;
        ci.vertex_input_state.vertex_attributes = attrs;
        ci.vertex_input_state.num_vertex_attributes = 3;
        ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        ci.target_info.color_target_descriptions = &ctd;
        ci.target_info.num_color_targets = 1;
        slot = SDL_CreateGPUGraphicsPipeline(dev_, &ci);
        if (!slot) SDL_Log("sdl_gpu: pipeline creation failed: %s", SDL_GetError());
        return slot;
    }

    void release(Tex& t) {
        if (t.smp) SDL_ReleaseGPUSampler(dev_, t.smp);
        if (t.tex) SDL_ReleaseGPUTexture(dev_, t.tex);
    }
    void release(Pipe& p) {
        if (p.swapchain) SDL_ReleaseGPUGraphicsPipeline(dev_, p.swapchain);
        if (p.rgba8) SDL_ReleaseGPUGraphicsPipeline(dev_, p.rgba8);
    }

    SDL_GPUDevice* dev_;
    SDL_Window* win_;
    std::string backend_name_;
    SDL_GPUTextureFormat swap_fmt_{}, pass_fmt_{};
    SDL_GPUShader* sprite_vs_ = nullptr;
    SDL_GPUShader* sprite_fs_ = nullptr;
    SlotMap<Tex> textures_;
    SlotMap<Buf> buffers_;
    SlotMap<Pipe> pipelines_;
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    SDL_GPURenderPass* pass_ = nullptr;
    SDL_GPUTexture* swap_tex_ = nullptr;
    Uint32 swap_w_ = 0, swap_h_ = 0, pass_w_ = 0, pass_h_ = 0;
};

} // namespace

std::unique_ptr<IRenderDevice> create_sdlgpu_device(NativeWindow& nw, bool debug) {
    auto* win = static_cast<SDL_Window*>(nw.sdl_window);
    if (!win) return nullptr;
    SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug, nullptr);
    if (!dev) { SDL_Log("sdl_gpu: SDL_CreateGPUDevice failed: %s", SDL_GetError()); return nullptr; }
    if (!SDL_ClaimWindowForGPUDevice(dev, win)) {
        SDL_Log("sdl_gpu: claim window failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(dev);
        return nullptr;
    }
    SDL_Log("sdl_gpu: using driver %s", SDL_GetGPUDeviceDriver(dev));
    return std::make_unique<SdlGpuDevice>(dev, win);
}

} // namespace eng::render
