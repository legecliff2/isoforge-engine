// Vertical slice viewer: sample.vertical_slice chunk via world, goblin vs fighter via sim,
// drawn with iso2d on SDL_GPU. The view only reads ViewModel snapshots.
//   vertical_slice [--config FILE] [--pack-root DIR]... [--pack ID]... [--seed N] [--static] [--steps K] [--frames N] [--zoom Z] [--debug] [--no-torches]
//                  [--fog-team T] [--no-fog] [--equipment]
// Keys: SPACE step, A autoplay, arrows/W/S pan, wheel or 1/2 zoom, ESC quit.
// F3: debug equip / inventory view (debug_equip.hpp): UP/DOWN select, TAB next PC, E equip, U unequip, L light state.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <string>

#include "platform/clock.hpp"
#include "platform/events.hpp"
#include "platform/platform.hpp"
#include "platform/window.hpp"
#include "render/device.hpp"
#include "debug_equip.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;

#ifndef SLICE_DEFAULT_PACK
#define SLICE_DEFAULT_PACK "sample.vertical_slice"
#endif
#ifndef SLICE_DEFAULT_PACK_ROOT
#define SLICE_DEFAULT_PACK_ROOT "content/packs"
#endif

int main(int argc, char** argv) {
    slice::Session::Options opt;
    std::vector<std::string> roots, packs;
    const char* config = nullptr;
    int max_frames = -1, pre_steps = 0;
    float zoom = 1.25f;
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* n) { return !std::strcmp(argv[i], n) && i + 1 < argc; };
        if (arg("--pack-root")) roots.push_back(argv[++i]);   // repeatable
        else if (arg("--pack")) packs.push_back(argv[++i]);   // repeatable: requested pack ids
        else if (arg("--config")) config = argv[++i];
        else if (arg("--seed")) opt.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (arg("--steps")) pre_steps = std::atoi(argv[++i]);
        else if (arg("--frames")) max_frames = std::atoi(argv[++i]);
        else if (arg("--zoom")) zoom = float(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--static")) opt.with_sim = false;
        else if (!std::strcmp(argv[i], "--debug")) debug = true;
        else if (!std::strcmp(argv[i], "--no-torches")) opt.pc_torches = false;
        else if (arg("--fog-team")) opt.fog_team = uint8_t(std::atoi(argv[++i]));   // default: player team
        else if (!std::strcmp(argv[i], "--no-fog")) opt.fog = false;
        else if (!std::strcmp(argv[i], "--equipment")) opt.equipment = true;   // loadouts (debug equip view, F3)
    }

    // Defaults, then the config file, then the command line (last wins per field).
    opt.pack_roots = {SLICE_DEFAULT_PACK_ROOT};
    opt.packs = {SLICE_DEFAULT_PACK};
    if (config) {
        std::ifstream in(config);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string cerr_msg;
        if (!in || !slice::Session::apply_pack_config(ss.str(), opt, &cerr_msg)) {
            std::fprintf(stderr, "vertical_slice: %s\n", in ? cerr_msg.c_str() : "cannot read --config file");
            return 1;
        }
    }
    if (!roots.empty()) opt.pack_roots = roots;
    if (!packs.empty()) opt.packs = packs;

    std::string err;
    auto session = slice::Session::open(opt, &err);
    if (!session) { std::fprintf(stderr, "vertical_slice: %s\n", err.c_str()); return 1; }
    for (const std::string& w : session->warnings()) std::fprintf(stderr, "vertical_slice: warning: %s\n", w.c_str());
    for (int i = 0; i < pre_steps && session->step(); ++i) {}

    if (!platform::init()) return 2;
    auto window = platform::Window::create({"isoforge - vertical slice", 1280, 720});
    if (!window) return 3;
    auto device = render::create_sdlgpu_device(window->native(), debug);
    if (!device) { std::fprintf(stderr, "no SDL_GPU device: %s\n", platform::last_error()); return 4; }

    auto view = std::make_unique<slice::SliceView>(*device);
    platform::EventPump events;
    platform::Clock clock;
    platform::FixedTimestep tick(1'000'000'000ull / 60);
    Vec2f pan{0, 0};
    slice::DebugEquipView equip_view;   // F3 (debug only)
    bool autoplay = false;
    uint32_t auto_timer = 0;
    int frame = 0;

    // The camera follows the centre of the combatants' bounding box; pan is an offset from it.
    Vec2f focus = view->actors_focus_px(session->view());

    while (!events.input().quit_requested && (max_frames < 0 || frame < max_frames)) {
        events.pump();
        platform::Event ev;
        while (events.poll(ev)) {
            if (ev.type == platform::EventType::MouseWheel) zoom = std::clamp(zoom * (ev.wheel.dy > 0 ? 1.1f : 1 / 1.1f), 0.25f, 4.f);
        }
        const auto& in = events.input();
        if (in.is_down(platform::Key::Escape)) break;
        if (in.was_pressed(platform::Key::Space)) session->step();
        if (in.was_pressed(platform::Key::A)) autoplay = !autoplay;
        using EqCmd = slice::DebugEquipView::Cmd;
        if (in.was_pressed(platform::Key::F3)) equip_view.handle(EqCmd::Toggle, *session);
        if (equip_view.open()) {
            if (in.was_pressed(platform::Key::Up)) equip_view.handle(EqCmd::Up, *session);
            if (in.was_pressed(platform::Key::Down)) equip_view.handle(EqCmd::Down, *session);
            if (in.was_pressed(platform::Key::Tab)) equip_view.handle(EqCmd::NextPc, *session);
            if (in.was_pressed(platform::Key::E)) equip_view.handle(EqCmd::Equip, *session);
            if (in.was_pressed(platform::Key::U)) equip_view.handle(EqCmd::Unequip, *session);
            if (in.was_pressed(platform::Key::L)) equip_view.handle(EqCmd::CycleLight, *session);
        }

        for (uint32_t s = tick.advance(clock.tick()); s > 0; --s) {
            const float v = 8.f / zoom;
            const bool arrows = !equip_view.open();   // the debug equip view owns the arrows while open
            if (arrows && in.is_down(platform::Key::Left)) pan.x -= v;
            if (arrows && in.is_down(platform::Key::Right)) pan.x += v;
            if ((arrows && in.is_down(platform::Key::Up)) || in.is_down(platform::Key::W)) pan.y -= v;
            if ((arrows && in.is_down(platform::Key::Down)) || in.is_down(platform::Key::S)) pan.y += v;
            if (in.is_down(platform::Key::Num1)) zoom = std::min(4.f, zoom * 1.02f);   // '+' on most layouts needs shift
            if (in.is_down(platform::Key::Num2)) zoom = std::max(0.25f, zoom / 1.02f);
            if (autoplay && ++auto_timer % 20 == 0) session->step();
        }

        slice::ViewModel vm = session->view();
        equip_view.decorate(*session, vm);
        const Vec2f target = view->actors_focus_px(vm);
        focus = {focus.x + (target.x - focus.x) * 0.15f, focus.y + (target.y - focus.y) * 0.15f};   // smooth follow
        const Vec2i px = window->size_in_pixels();
        iso2d::Camera cam;
        cam.zoom = zoom;
        cam.viewport_w = uint32_t(px.x);
        cam.viewport_h = uint32_t(px.y);
        cam.position = {focus.x + pan.x - px.x / (2.f * zoom), focus.y + pan.y - px.y / (2.f * zoom)};

        if (device->begin_frame()) {
            view->prepare(vm, cam, uint32_t(px.x), uint32_t(px.y));
            auto& cl = device->command_list();
            cl.begin_pass(render::TargetId{}, Color::rgba8(16, 17, 24));
            view->submit(cl);
            cl.end_pass();
            device->end_frame();
        }
        ++frame;
    }
    std::printf("frames: %d, ended: %d, error: %s\n", frame, int(session->ended()), session->error().c_str());
    view.reset();
    device.reset();
    window.reset();
    platform::shutdown();
    return 0;
}
