#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "overlay_scene.hpp"
#include "render_null/null_device.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
// Draw list (texture, index range per draw, in order) + every buffer / texture upload byte of a frame.
uint64_t frame_hash(const render::NullFrameStats& st) {
    uint64_t h = st.upload_hash;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } };
    mix(st.draw_calls);
    for (const auto& d : st.draws) { mix(d.texture.index); mix(d.index_count); mix(d.first_index); mix(uint64_t(uint32_t(d.vertex_offset))); }
    return h;
}
std::vector<uint64_t> run(int glows, int frames) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(32.f, 32.f);
    cam.position = {c.x - 640.f, c.y - 360.f};
    std::vector<uint64_t> out;
    for (int f = 0; f < frames; ++f) {
        const ViewModel vm = overlay_scene::build(glows, uint64_t(f));
        REQUIRE(dev.begin_frame());
        view.prepare(vm, cam, 1280, 720);
        auto& cl = dev.command_list();
        cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        out.push_back(frame_hash(dev.last_frame()));
    }
    CHECK(dev.errors() == 0);
    // MSVC /WX treats getenv as C4996; _dupenv_s is the CRT-safe substitute.
    bool print_golden = false;
#if defined(_MSC_VER)
    char* overlay_print = nullptr;
    size_t overlay_print_len = 0;
    if (_dupenv_s(&overlay_print, &overlay_print_len, "OVERLAY_PRINT_GOLDEN") == 0 && overlay_print) {
        print_golden = overlay_print[0] != '\0';
        std::free(overlay_print);
    }
#else
    if (const char* v = std::getenv("OVERLAY_PRINT_GOLDEN")) print_golden = v[0] != '\0';
#endif
    if (print_golden) {   // regenerate: OVERLAY_PRINT_GOLDEN=1 vertical_slice_tests -tc="overlay*"
        std::printf("glows %d:", glows);
        for (uint64_t h : out) std::printf(" 0x%016llxull,", (unsigned long long)h);
        std::printf("\n");
    }
    return out;
}
} // namespace

// Golden hashes recorded on render/equipped-lights (before the overlay perf work) for the fixed torch-heavy scene. The
// overlay optimisations must not change a single draw, vertex or texel upload: same inputs, same frame.
TEST_CASE("overlay determinism: fog + 64 overlapping glows produce the pre-optimisation draw list and uploads") {
    const std::vector<uint64_t> a = run(64, 6), b = run(64, 6);
    CHECK(a == b);
    const std::vector<uint64_t> golden = {0x137eef0050507d8cull, 0x71f6429c32999b7bull, 0x0c151a1e6eeef1c6ull, 0x6b2216b288be7482ull,
                                          0x49985135998fff3eull, 0x08678acde81bd2ffull};
    REQUIRE(a.size() == golden.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CAPTURE(i);
        CHECK(a[i] == golden[i]);
    }
}

TEST_CASE("overlay determinism: 0 and 256 glows") {
    for (int n : {0, 256}) {
        CAPTURE(n);
        CHECK(run(n, 3) == run(n, 3));
    }
    const std::vector<uint64_t> g256 = run(256, 3), golden256 = {0x3a752cff4c6af991ull, 0x18533927deaba0fcull, 0xad93ffb1a16bb601ull};
    CHECK(g256 == golden256);
}
