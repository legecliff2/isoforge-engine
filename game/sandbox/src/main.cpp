// Isometric tile grid demo: checkerboard diamond map + stone pillars, depth sorted by iso2d.
//   sandbox [--frames N] [--debug]
// Arrow keys / WASD pan (fixed 60 Hz sim step), Esc quits.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "iso2d/sprite_batch.hpp"
#include "platform/clock.hpp"
#include "platform/events.hpp"
#include "platform/platform.hpp"
#include "platform/window.hpp"
#include "render/device.hpp"

using namespace eng;
using namespace eng::render;

namespace {

constexpr int kMap = 16;
constexpr int kTileW = 64, kTileH = 32, kPillarH = 48;

struct Image { uint32_t w, h; std::vector<std::byte> px; };

void put(Image& im, uint32_t x, uint32_t y, float r, float g, float b, float a = 1.f) {
    auto* p = &im.px[(size_t(y) * im.w + x) * 4];
    p[0] = std::byte(uint8_t(r * 255)); p[1] = std::byte(uint8_t(g * 255));
    p[2] = std::byte(uint8_t(b * 255)); p[3] = std::byte(uint8_t(a * 255));
}

// |dx|/32 + |dy|/16 <= 1 : the 2:1 diamond, sampled at pixel centres.
float diamond(float px, float py, float oy) {
    return std::fabs(px + 0.5f - kTileW / 2.f) / (kTileW / 2.f) + std::fabs(py + 0.5f - oy - kTileH / 2.f) / (kTileH / 2.f);
}

Image make_tile() {
    Image im{kTileW, kTileH, std::vector<std::byte>(kTileW * kTileH * 4)};
    for (uint32_t y = 0; y < im.h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            float d = diamond(float(x), float(y), 0);
            if (d <= 1.f) { float s = d > 0.92f ? 0.75f : 1.f; put(im, x, y, s, s, s); }
        }
    return im;
}

Image make_pillar() {
    const uint32_t h = kTileH + kPillarH;
    Image im{kTileW, h, std::vector<std::byte>(size_t(kTileW) * h * 4)};
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < im.w; ++x) {
            const float cx = std::fabs(x + 0.5f - kTileW / 2.f) / (kTileW / 2.f);
            const float top_lower = kTileH / 2.f + (kTileH / 2.f) * (1.f - cx);
            const float d = diamond(float(x), float(y), 0);
            if (d <= 1.f) {
                float s = d > 0.9f ? 0.8f : 0.95f;
                put(im, x, y, 0.85f * s, 0.82f * s, 0.75f * s);
            } else if (cx <= 1.f && y + 0.5f >= top_lower && y + 0.5f < top_lower + kPillarH) {
                float s = x < kTileW / 2 ? 0.55f : 0.72f;
                put(im, x, y, 0.85f * s, 0.82f * s, 0.75f * s);
            }
        }
    return im;
}

bool is_pillar(int x, int y) {
    // A ring plus a diagonal line: lots of mutual overlap to exercise the depth sort.
    if ((x == 3 || x == 12) && y >= 3 && y <= 12) return true;
    if ((y == 3 || y == 12) && x >= 3 && x <= 12) return (x + y) % 2 == 0;
    return x == y && x >= 6 && x <= 9;
}

} // namespace

int main(int argc, char** argv) {
    int max_frames = -1;
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--debug")) debug = true;
    }

    if (!platform::init()) return 1;
    auto window = platform::Window::create({"isoforge sandbox — iso grid", 1280, 720});
    if (!window) return 2;
    auto device = create_sdlgpu_device(window->native(), debug);
    if (!device) { std::fprintf(stderr, "no SDL_GPU device: %s\n", platform::last_error()); return 3; }
    std::printf("backend: %.*s\n", int(device->caps().backend_name.size()), device->caps().backend_name.data());

    const Image tile_img = make_tile(), pillar_img = make_pillar();
    TextureId tile_tex = device->create_texture({tile_img.w, tile_img.h, Format::RGBA8_UNORM, Filter::Nearest, false, "tile"}, tile_img.px);
    TextureId pillar_tex = device->create_texture({pillar_img.w, pillar_img.h, Format::RGBA8_UNORM, Filter::Nearest, false, "pillar"}, pillar_img.px);

    auto batch_owner = std::make_unique<iso2d::SpriteBatch>(*device);
    iso2d::SpriteBatch& batch = *batch_owner;
    const iso2d::Sprite tile{tile_tex, float(kTileW), float(kTileH), kTileW / 2.f, 0.f};
    iso2d::Sprite pillar{pillar_tex, float(kTileW), float(kTileH + kPillarH), kTileW / 2.f, float(kPillarH)};
    const Color light = Color::rgba8(122, 168, 98), dark = Color::rgba8(70, 110, 64);

    platform::EventPump events;
    platform::Clock clock;
    platform::FixedTimestep sim_step(1'000'000'000ull / 60);
    Vec2f cam{0, 0};  // world-px offset of the map centre from screen centre
    int frame = 0;

    while (!events.input().quit_requested && (max_frames < 0 || frame < max_frames)) {
        events.pump_all();
        if (events.input().is_down(platform::Key::Escape)) break;

        for (uint32_t s = sim_step.advance(clock.tick()); s > 0; --s) {
            const auto& in = events.input();
            const float v = 6.f;
            if (in.is_down(platform::Key::Left) || in.is_down(platform::Key::A)) cam.x -= v;
            if (in.is_down(platform::Key::Right) || in.is_down(platform::Key::D)) cam.x += v;
            if (in.is_down(platform::Key::Up) || in.is_down(platform::Key::W)) cam.y -= v;
            if (in.is_down(platform::Key::Down) || in.is_down(platform::Key::S)) cam.y += v;
        }

        const Vec2i px = window->size_in_pixels();
        batch.begin();
        for (int y = 0; y < kMap; ++y)
            for (int x = 0; x < kMap; ++x) {
                iso2d::Sprite t = tile;
                t.tint = (x + y) % 2 ? dark : light;
                const uint32_t id = uint32_t(y * kMap + x);
                batch.draw(t, {float(x), float(y)}, 0, id);
                if (is_pillar(x, y)) batch.draw(pillar, {float(x), float(y)}, 1, id);
            }
        // Centre the map: its middle is iso (kMap/2, kMap/2).
        const Vec2f mid = batch.projection().to_screen(kMap / 2.f, kMap / 2.f);
        iso2d::Camera camera{{mid.x - px.x / 2.f + cam.x, mid.y - px.y / 2.f + cam.y}, uint32_t(px.x), uint32_t(px.y)};

        if (device->begin_frame()) {
            batch.prepare(camera);  // uploads: before the render pass
            auto& cl = device->command_list();
            cl.begin_pass(TargetId{}, Color::rgba8(24, 26, 34));
            batch.submit(cl);
            cl.end_pass();
            device->end_frame();
        }
        ++frame;
    }

    std::printf("frames: %d, sprites: %u, batches: %u\n", frame, batch.sprite_count(), batch.batch_count());
    device->destroy(tile_tex);
    device->destroy(pillar_tex);
    batch_owner.reset();  // releases its GPU buffers: must die before the device
    device.reset();
    window.reset();
    platform::shutdown();
    return 0;
}
