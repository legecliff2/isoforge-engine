// Fog + glow overlay benchmark (render-side CPU cost of torch-heavy scenes). Null backend: measures building, sorting,
// batching and uploading the frame (SliceView::prepare + submit), not GPU raster time. Not part of ctest: wall-clock
// numbers vary per machine and Debug is far slower. Run a Release build:
//   vertical_slice_overlay_bench [--frames F] [--quick]
// Prints per N in {0, 16, 64, 256}: median / p90 CPU ms per frame, draw calls, world sprites, bytes uploaded per frame
// (vertex buffer and light / fog textures) and a draw-list + upload hash over all frames (identical run to run).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "overlay_scene.hpp"
#include "render_null/null_device.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

int main(int argc, char** argv) {
    int frames = 240;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) frames = 16;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
    }
    std::printf("overlay_bench: 2x2 chunks (%d tiles), 8 moving PCs, fog on, %d frames (frame 0 = cold, excluded)\n",
                overlay_scene::kTiles * overlay_scene::kTiles, frames);
    std::printf("  %5s %10s %10s %8s %8s %12s %12s  %s\n", "N", "median_ms", "p90_ms", "draws", "sprites", "vtx_B/frame",
                "tex_B/frame", "hash");
    for (int n : {0, 16, 64, 256}) {
        std::vector<double> timed; uint64_t tvtx = 0, ttex = 0; uint32_t tdraws = 0, tsprites = 0;
        std::vector<ViewModel> vms;
        vms.reserve(size_t(frames));
        for (int f = 0; f < frames; ++f) vms.push_back(overlay_scene::build(n, uint64_t(f)));
      for (int pass = 0; pass < 2; ++pass) {   // pass 0 timed (no hashing), pass 1 hashes uploads (untimed)
        render::NullDevice dev;
        dev.set_hash_uploads(pass == 1);
        SliceView view(dev);
        iso2d::Camera cam;
        { const Vec2f c = view.tile_center_px(32.f, 32.f); cam.position = {c.x - 640.f, c.y - 360.f}; }
        std::vector<double> ms;
        uint64_t hash = 1469598103934665603ull, vtx = 0, tex = 0;
        uint32_t draws = 0, sprites = 0;
        for (int f = 0; f < frames; ++f) {
            const auto t0 = std::chrono::steady_clock::now();
            dev.begin_frame();
            view.prepare(vms[size_t(f)], cam, 1280, 720);
            auto& cl = dev.command_list();
            cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
            view.submit(cl);
            cl.end_pass();
            dev.end_frame();
            const auto t1 = std::chrono::steady_clock::now();
            const auto& st = dev.last_frame();
            hash = (hash ^ st.upload_hash) * 1099511628211ull;
            for (const auto& d : st.draws) hash = (hash ^ (uint64_t(d.texture.index) << 40 ^ uint64_t(d.index_count) << 20 ^ d.first_index)) * 1099511628211ull;
            if (f == 0) continue;
            ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            vtx += st.buffer_bytes; tex += st.texture_bytes;
            draws = st.draw_calls; sprites = view.world_sprites();
        }
        if (dev.errors()) { std::printf("null device reported %u errors\n", dev.errors()); return 1; }
        if (pass == 0) { timed = ms; tvtx = vtx; ttex = tex; tdraws = draws; tsprites = sprites; continue; }
        ms = timed; vtx = tvtx; tex = ttex; draws = tdraws; sprites = tsprites;
        std::sort(ms.begin(), ms.end());
        const double k = double(ms.size());
        std::printf("  %5d %10.3f %10.3f %8u %8u %12.0f %12.0f  %016llx\n", n, ms[ms.size() / 2], ms[size_t(k * 0.9)], draws,
                    sprites, double(vtx) / k, double(tex) / k, (unsigned long long)hash);
      }
    }
    return 0;
}
