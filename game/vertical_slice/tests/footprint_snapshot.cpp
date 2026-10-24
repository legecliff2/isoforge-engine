// Offscreen snapshot of one multi-tile creature (2x2, 3x3 or 4x4) with a 1x1 neighbour in front,
// one behind, and a wall past the front corner. CPU rasteriser, same path as equip_snapshot.
// Built with the tests, never run by ctest.
//   vertical_slice_footprint_snapshot out.ppm <side> [zoom]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "footprint_visuals.hpp"
#include "render/sprite.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace eng::render;

namespace {
class SoftDevice final : public IRenderDevice, private ICommandList {
public:
    SoftDevice(uint32_t w, uint32_t h) : w_(w), h_(h), fb_(size_t(w) * h * 3) {}
    DeviceCaps caps() const override { return {4096, false, false, "soft"}; }
    TextureId create_texture(const TextureDesc& d, std::span<const std::byte> px) override {
        Tex t{d.w, d.h, d.fmt == Format::R8_UNORM ? 1u : 4u, {}};
        t.px.assign(size_t(d.w) * d.h * t.bpp, std::byte{0});
        if (!px.empty()) std::memcpy(t.px.data(), px.data(), std::min(px.size(), t.px.size()));
        tex_.push_back(std::move(t));
        return TextureId{uint32_t(tex_.size() - 1), 1};
    }
    BufferId create_buffer(const BufferDesc& d) override { buf_.emplace_back(d.size); return BufferId{uint32_t(buf_.size() - 1), 1}; }
    PipelineId create_pipeline(const PipelineDesc&) override { return PipelineId{1, 1}; }
    void update_buffer(BufferId b, uint32_t off, std::span<const std::byte> d) override {
        std::memcpy(buf_[b.index].data() + off, d.data(), d.size());
    }
    void update_texture(TextureId t, RectI r, std::span<const std::byte> d) override {
        Tex& x = tex_[t.index];
        for (int y = 0; y < r.h; ++y)
            std::memcpy(&x.px[(size_t(r.y + y) * x.w + size_t(r.x)) * x.bpp], &d[size_t(y) * size_t(r.w) * x.bpp], size_t(r.w) * x.bpp);
    }
    void destroy(TextureId) override {}
    void destroy(BufferId) override {}
    void destroy(PipelineId) override {}
    bool begin_frame() override { return true; }
    ICommandList& command_list() override { return *this; }
    void end_frame() override {}
    void on_resize(uint32_t, uint32_t) override {}
    bool write_ppm(const char* path) const {
        FILE* f = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&f, path, "wb") != 0) f = nullptr;
#else
        f = std::fopen(path, "wb");
#endif
        if (!f) return false;
        std::fprintf(f, "P6\n%u %u\n255\n", w_, h_);
        std::fwrite(fb_.data(), 1, fb_.size(), f);
        std::fclose(f);
        return true;
    }

private:
    struct Tex { uint32_t w, h, bpp; std::vector<std::byte> px; };
    void begin_pass(TargetId, Color c) override {
        auto b = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        for (size_t i = 0; i < fb_.size(); i += 3) { fb_[i] = b(c.r); fb_[i + 1] = b(c.g); fb_[i + 2] = b(c.b); }
    }
    void end_pass() override {}
    void set_pipeline(PipelineId) override {}
    void set_viewport(RectI) override {}
    void set_scissor(RectI) override {}
    void bind_texture(uint32_t, TextureId t) override { cur_tex_ = t.index; }
    void bind_vertex_buffer(BufferId b, uint32_t) override { vbo_ = b.index; }
    void bind_index_buffer(BufferId b, uint32_t) override { ibo_ = b.index; }
    void push_uniforms(uint32_t, std::span<const std::byte> d) override { std::memcpy(&u_, d.data(), sizeof u_); }
    void draw_indexed(uint32_t count, uint32_t first, int32_t voff, uint32_t) override {
        const auto* idx = reinterpret_cast<const uint32_t*>(buf_[ibo_].data());
        const auto* vtx = reinterpret_cast<const SpriteVertex*>(buf_[vbo_].data());
        for (uint32_t q = first; q + 6 <= first + count; q += 6) {
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            SpriteVertex c{};
            for (int k = 0; k < 6; ++k) {
                const SpriteVertex& v = vtx[int32_t(idx[q + uint32_t(k)]) + voff];
                if (v.x < x0) { x0 = v.x; u0 = v.u; } if (v.x > x1) { x1 = v.x; u1 = v.u; }
                if (v.y < y0) { y0 = v.y; v0 = v.v; } if (v.y > y1) { y1 = v.y; v1 = v.v; }
                c = v;
            }
            auto sx = [&](float x) { return ((x * u_.scale[0] + u_.offset[0]) * 0.5f + 0.5f) * float(w_); };
            auto sy = [&](float y) { return (0.5f - (y * u_.scale[1] + u_.offset[1]) * 0.5f) * float(h_); };
            const float X0 = sx(x0), X1 = sx(x1), Ya = sy(y0), Yb = sy(y1);
            const float Y0 = std::min(Ya, Yb), Y1 = std::max(Ya, Yb);
            const Tex& t = tex_[cur_tex_];
            for (int py = std::max(0, int(std::ceil(Y0 - 0.5f))); py < std::min(int(h_), int(std::ceil(Y1 - 0.5f))); ++py)
                for (int px = std::max(0, int(std::ceil(X0 - 0.5f))); px < std::min(int(w_), int(std::ceil(X1 - 0.5f))); ++px) {
                    const float fx = (float(px) + 0.5f - X0) / (X1 - X0), fy = (float(py) + 0.5f - Ya) / (Yb - Ya);
                    const float uu = u0 + (u1 - u0) * fx, vv = v0 + (v1 - v0) * fy;
                    const uint32_t tx = std::min(t.w - 1, uint32_t(std::max(0.f, uu * float(t.w))));
                    const uint32_t ty = std::min(t.h - 1, uint32_t(std::max(0.f, vv * float(t.h))));
                    const std::byte* s = &t.px[(size_t(ty) * t.w + tx) * t.bpp];
                    float r = float(s[0]) / 255.f, g = r, b = r, a = 1.f;
                    if (t.bpp == 4) { g = float(s[1]) / 255.f; b = float(s[2]) / 255.f; a = float(s[3]) / 255.f; }
                    r *= c.r / 255.f; g *= c.g / 255.f; b *= c.b / 255.f; a *= c.a / 255.f;
                    uint8_t* d = &fb_[(size_t(py) * w_ + size_t(px)) * 3];
                    d[0] = uint8_t(std::lround(r * 255.f * a + d[0] * (1.f - a)));
                    d[1] = uint8_t(std::lround(g * 255.f * a + d[1] * (1.f - a)));
                    d[2] = uint8_t(std::lround(b * 255.f * a + d[2] * (1.f - a)));
                }
        }
    }

    uint32_t w_, h_;
    std::vector<uint8_t> fb_;
    std::vector<Tex> tex_;
    std::vector<std::vector<std::byte>> buf_;
    uint32_t cur_tex_ = 0, vbo_ = 0, ibo_ = 0;
    SpriteUniforms u_{};
};

slice::ActorView pawn(uint32_t id, const char* name, Vec2i tile, int side, bool current, bool pc) {
    slice::ActorView a;
    a.id = id;
    a.name = name;
    a.tile = tile;
    a.footprint = side;
    a.hp = 30;
    a.hp_max = 40;
    a.current = current;
    a.is_pc = pc;
    a.team = pc ? 0 : 1;
    return a;
}

slice::ViewModel scene(int n) {
    const Vec2i anchor{6, 6};
    const char* name = n == 2 ? "LARGE" : n == 3 ? "HUGE" : "GARGANTUAN";
    slice::ViewModel vm;
    const Vec2i wall{anchor.x + n, anchor.y + n - 1};
    const Vec2i front{anchor.x + n - 1, anchor.y + n};
    const Vec2i back{anchor.x - 1, anchor.y + n / 2};
    for (int y = anchor.y - 3; y <= anchor.y + n + 3; ++y)
        for (int x = anchor.x - 3; x <= anchor.x + n + 3; ++x) {
            slice::TileView t;
            t.tile = {x, y};
            if (t.tile == wall) { t.kind = slice::TileKind::Wall; t.layer = 2; }
            else t.kind = ((x + y) & 1) ? slice::TileKind::Floor : slice::TileKind::Rubble;
            vm.tiles.push_back(t);
        }
    vm.actors.push_back(pawn(1, "BACK", back, 1, false, false));
    vm.actors.push_back(pawn(2, name, anchor, n, true, false));
    vm.actors.push_back(pawn(3, "FRONT", front, 1, false, true));
    vm.status = std::string(name) + " " + std::to_string(n) + "x" + std::to_string(n);
    return vm;
}
} // namespace

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "footprint.ppm";
    const int side = argc > 2 ? std::atoi(argv[2]) : 2;
    const float zoom = argc > 3 ? float(std::atof(argv[3])) : 2.f;
    if (side < 1) { std::fprintf(stderr, "side must be >= 1\n"); return 2; }
    const uint32_t W = 1100, H = 760;
    SoftDevice dev(W, H);
    slice::SliceView view(dev);
    const slice::ViewModel vm = scene(side);
    const slice::FootprintGeom fp = slice::footprint_geom(vm.actors[1].tile, side, 64.f, 32.f);
    iso2d::Camera cam;
    cam.zoom = zoom;
    cam.viewport_w = W;
    cam.viewport_h = H;
    const Vec2f c = view.tile_center_px(fp.centre_iso.x, fp.centre_iso.y);
    // Sit the footprint centre right of the turn panel and a little below mid-frame (the body grows up).
    cam.position = {c.x - (float(W) * 0.55f) / zoom, c.y - (float(H) * 0.62f) / zoom};
    dev.begin_frame();
    view.prepare(vm, cam, W, H);
    auto& cl = dev.command_list();
    cl.begin_pass(TargetId{}, Color::rgba8(16, 17, 24));
    view.submit(cl);
    cl.end_pass();
    dev.end_frame();
    if (!dev.write_ppm(out)) { std::fprintf(stderr, "cannot write %s\n", out); return 1; }
    std::printf("wrote %s (%ux%u) side %d\n", out, W, H, side);
    return 0;
}
