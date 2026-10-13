#include "render_null/null_device.hpp"

namespace eng::render {

template <class H>
H NullDevice::alloc(std::vector<Slot>& v, uint32_t size) {
    for (uint32_t i = 1; i < v.size(); ++i) {
        if (!v[i].live) {
            v[i].live = true; v[i].size = size; ++v[i].generation;
            return H{i, v[i].generation};
        }
    }
    v.push_back(Slot{1, true, size});
    return H{uint32_t(v.size() - 1), 1};
}

template <class H>
bool NullDevice::check(const std::vector<Slot>& v, H h) const {
    return h.index != 0 && h.index < v.size() && v[h.index].live && v[h.index].generation == h.generation;
}

DeviceCaps NullDevice::caps() const { return {16384, false, false, "null"}; }

TextureId NullDevice::create_texture(const TextureDesc& d, std::span<const std::byte> px) {
    if (!px.empty() && px.size() < size_t(d.w) * d.h * (d.fmt == Format::R8_UNORM ? 1 : 4)) ++errors_;
    return alloc<TextureId>(textures_, d.w * d.h);
}
BufferId NullDevice::create_buffer(const BufferDesc& d) { return alloc<BufferId>(buffers_, d.size); }
PipelineId NullDevice::create_pipeline(const PipelineDesc&) { return alloc<PipelineId>(pipelines_, 0); }

void NullDevice::update_buffer(BufferId b, uint32_t off, std::span<const std::byte> data) {
    if (!check(buffers_, b) || off + data.size() > buffers_[b.index].size || in_pass_) ++errors_;
}
void NullDevice::update_texture(TextureId t, RectI, std::span<const std::byte>) {
    if (!check(textures_, t) || in_pass_) ++errors_;
}
void NullDevice::destroy(TextureId t) { if (check(textures_, t)) textures_[t.index].live = false; else ++errors_; }
void NullDevice::destroy(BufferId b) { if (check(buffers_, b)) buffers_[b.index].live = false; else ++errors_; }
void NullDevice::destroy(PipelineId p) { if (check(pipelines_, p)) pipelines_[p.index].live = false; else ++errors_; }

bool NullDevice::begin_frame() {
    if (in_frame_) ++errors_;
    in_frame_ = true;
    cur_ = {};
    return true;
}
void NullDevice::end_frame() {
    if (!in_frame_ || in_pass_) ++errors_;
    in_frame_ = false;
    last_ = std::move(cur_);
}
void NullDevice::begin_pass(TargetId, Color) {
    if (!in_frame_ || in_pass_) ++errors_;
    in_pass_ = true;
    ++cur_.passes;
}
void NullDevice::end_pass() { if (!in_pass_) ++errors_; in_pass_ = false; }
void NullDevice::bind_texture(uint32_t, TextureId t) { if (!check(textures_, t)) ++errors_; texture_ = t; }
void NullDevice::draw_indexed(uint32_t count, uint32_t first, int32_t voff, uint32_t) {
    if (!in_pass_ || !check(pipelines_, pipeline_)) ++errors_;
    ++cur_.draw_calls;
    cur_.indices += count;
    cur_.draws.push_back({pipeline_, texture_, count, first, voff});
}

uint32_t NullDevice::live_textures() const {
    uint32_t n = 0; for (auto& s : textures_) n += s.live; return n;
}
uint32_t NullDevice::live_buffers() const {
    uint32_t n = 0; for (auto& s : buffers_) n += s.live; return n;
}

std::unique_ptr<IRenderDevice> create_null_device() { return std::make_unique<NullDevice>(); }

} // namespace eng::render
