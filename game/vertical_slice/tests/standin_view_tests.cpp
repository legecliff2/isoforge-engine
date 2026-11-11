#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

#include "assets/clip_registry.hpp"
#include "footprint_visuals.hpp"
#include "render/sprite.hpp"
#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

#ifndef SLICE_CLIP_FILE
#define SLICE_CLIP_FILE ""
#endif

TEST_CASE("Core clip registry is the only stand-in alias path") {
    assets::ClipFile file;
    const auto err = assets::load_clip_file(std::filesystem::path{SLICE_CLIP_FILE}, file);
    REQUIRE_FALSE(err);
    const assets::ClipRegistry* standin = nullptr;
    const assets::ClipRegistry* doll = nullptr;
    for (const assets::ClipRegistry& k : file.kits) {
        if (k.id == "standin.small") standin = &k;
        if (k.id == "paperdoll") doll = &k;
    }
    REQUIRE(standin);
    REQUIRE(doll);
    CHECK(standin->resolve("climb") == std::optional<std::string>{"walk"});
    CHECK(standin->resolve("crawl") == std::optional<std::string>{"walk"});
    CHECK(standin->resolve("hide") == std::optional<std::string>{"idle"});
    // melee → attack is in Core's stand-in registry (not a second table).
    CHECK(standin->resolve("melee") == std::optional<std::string>{"attack"});
    CHECK(standin->resolve("hard-land") == std::optional<std::string>{"fall_impact"});
    CHECK(standin->resolve("no-such") == std::optional<std::string>{"idle"});
    CHECK(doll->resolve("hard-land") == std::optional<std::string>{"prone_idle"});
}

TEST_CASE("session view: Core classifies the humanoid goblin and the fighter as paperdoll") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(s, err);
    const ViewModel vm = s->view();
    const ActorView* pc = nullptr;
    const ActorView* gob = nullptr;
    for (const ActorView& a : vm.actors) (a.is_pc ? pc : gob) = &a;
    REQUIRE(pc);
    REQUIRE(gob);
    CHECK(pc->visual_kind == "paperdoll");
    CHECK(pc->kit == "paperdoll");
    CHECK(pc->standin_bin.empty());
    CHECK(pc->clip.empty());
    CHECK(gob->visual_kind == "paperdoll");
    CHECK(gob->kit == "paperdoll");
    CHECK(gob->standin_bin.empty());
    CHECK_FALSE(gob->size_short);
}

namespace {

uint64_t frame_hash(const render::NullFrameStats& st) {
    uint64_t h = st.upload_hash;
    auto mix = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    mix(st.draw_calls);
    for (const auto& d : st.draws) {
        mix(d.texture.index);
        mix(d.index_count);
        mix(d.first_index);
        mix(uint64_t(uint32_t(d.vertex_offset)));
    }
    return h;
}

struct Shot {
    uint64_t hash = 0;
    uint32_t sprites = 0;
};

Shot draw(const ViewModel& vm, double t) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    view.set_present_time(t);
    iso2d::Camera cam;
    cam.viewport_w = 640;
    cam.viewport_h = 360;
    REQUIRE(dev.begin_frame());
    view.prepare(vm, cam, 640, 360);
    auto& cl = dev.command_list();
    cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
    view.submit(cl);
    cl.end_pass();
    dev.end_frame();
    CHECK(dev.errors() == 0);
    return {frame_hash(dev.last_frame()), view.world_sprites()};
}

ActorView body(uint32_t id, Vec2i tile) {
    ActorView a;
    a.id = id;
    a.name = "WOLF";
    a.tile = tile;
    a.hp = 7;
    a.hp_max = 7;
    a.visual_kind = "standin";
    a.standin_bin = "small";
    a.kit = "standin.small";
    a.clip = "idle";
    return a;
}

ViewModel scene(ActorView a) {
    ViewModel vm;
    TileView t;
    t.tile = a.tile;
    t.kind = TileKind::Floor;
    vm.tiles.push_back(t);
    vm.actors.push_back(std::move(a));
    return vm;
}

// Records sprite quads in world pixels (camera is applied later, in the shader).
struct Quad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t tw = 0, th = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
};

class CaptureDevice final : public render::IRenderDevice, private render::ICommandList {
public:
    std::vector<Quad> quads;
    uint32_t errors = 0;

    render::DeviceCaps caps() const override { return {4096, false, false, "capture"}; }
    render::TextureId create_texture(const render::TextureDesc& d, std::span<const std::byte>) override {
        tex_.push_back({d.w, d.h});
        return render::TextureId{uint32_t(tex_.size() - 1), 1};
    }
    render::BufferId create_buffer(const render::BufferDesc& d) override {
        buf_.push_back(std::vector<std::byte>(d.size));
        return render::BufferId{uint32_t(buf_.size() - 1), 1};
    }
    render::PipelineId create_pipeline(const render::PipelineDesc&) override { return render::PipelineId{1, 1}; }
    void update_buffer(render::BufferId b, uint32_t off, std::span<const std::byte> data) override {
        if (b.index == 0 || b.index >= buf_.size() || size_t(off) + data.size() > buf_[b.index].size()) {
            ++errors;
            return;
        }
        std::memcpy(buf_[b.index].data() + off, data.data(), data.size());
    }
    void update_texture(render::TextureId, RectI, std::span<const std::byte>) override {}
    void destroy(render::TextureId) override {}
    void destroy(render::BufferId) override {}
    void destroy(render::PipelineId) override {}
    bool begin_frame() override { quads.clear(); return true; }
    render::ICommandList& command_list() override { return *this; }
    void end_frame() override {}
    void on_resize(uint32_t, uint32_t) override {}

    CaptureDevice() { tex_.push_back({}); buf_.push_back({}); }

private:
    struct Tex { uint32_t w = 0, h = 0; };
    void begin_pass(render::TargetId, Color) override {}
    void end_pass() override {}
    void set_pipeline(render::PipelineId) override {}
    void set_viewport(RectI) override {}
    void set_scissor(RectI) override {}
    void bind_texture(uint32_t, render::TextureId t) override { cur_tex_ = t.index; }
    void bind_vertex_buffer(render::BufferId b, uint32_t) override { vbo_ = b.index; }
    void bind_index_buffer(render::BufferId b, uint32_t) override { ibo_ = b.index; }
    void push_uniforms(uint32_t, std::span<const std::byte>) override {}
    void draw_indexed(uint32_t count, uint32_t first, int32_t voff, uint32_t) override {
        if (vbo_ == 0 || vbo_ >= buf_.size() || ibo_ == 0 || ibo_ >= buf_.size() || cur_tex_ == 0 || cur_tex_ >= tex_.size()) {
            ++errors;
            return;
        }
        const auto& vb = buf_[vbo_];
        const auto& ib = buf_[ibo_];
        const Tex& tex = tex_[cur_tex_];
        for (uint32_t i = 0; i + 6 <= count; i += 6) {
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            bool ok = true;
            for (uint32_t k = 0; k < 6; ++k) {
                const size_t ioff = (size_t(first) + i + k) * sizeof(uint32_t);
                if (ioff + sizeof(uint32_t) > ib.size()) { ok = false; break; }
                uint32_t idx = 0;
                std::memcpy(&idx, ib.data() + ioff, sizeof idx);
                const int32_t vi = int32_t(idx) + voff;
                if (vi < 0) { ok = false; break; }
                const size_t vpos = size_t(vi) * sizeof(render::SpriteVertex);
                if (vpos + sizeof(render::SpriteVertex) > vb.size()) { ok = false; break; }
                render::SpriteVertex v{};
                std::memcpy(&v, vb.data() + vpos, sizeof v);
                x0 = std::min(x0, v.x);
                y0 = std::min(y0, v.y);
                x1 = std::max(x1, v.x);
                y1 = std::max(y1, v.y);
            }
            if (!ok) { ++errors; continue; }
            quads.push_back({x0, y0, x1, y1, tex.w, tex.h});
        }
    }

    std::vector<Tex> tex_;
    std::vector<std::vector<std::byte>> buf_;
    uint32_t cur_tex_ = 0, vbo_ = 0, ibo_ = 0;
};

std::vector<Quad> draw_quads(CaptureDevice& dev, SliceView& view, const ViewModel& vm) {
    iso2d::Camera cam;
    cam.position = {0.f, 0.f};
    cam.zoom = 1.f;
    cam.viewport_w = 640;
    cam.viewport_h = 360;
    REQUIRE(dev.begin_frame());
    view.prepare(vm, cam, 640, 360);
    auto& cl = dev.command_list();
    cl.begin_pass(render::TargetId{}, {});
    view.submit(cl);
    cl.end_pass();
    dev.end_frame();
    CHECK(dev.errors == 0);
    return dev.quads;
}

int count_sized(const std::vector<Quad>& qs, float w, float h) {
    int n = 0;
    for (const Quad& q : qs)
        if (std::fabs(q.w() - w) < 0.01f && std::fabs(q.h() - h) < 0.01f) ++n;
    return n;
}

const Quad* only_sized(const std::vector<Quad>& qs, float w, float h) {
    const Quad* found = nullptr;
    int n = 0;
    for (const Quad& q : qs)
        if (std::fabs(q.w() - w) < 0.01f && std::fabs(q.h() - h) < 0.01f) { found = &q; ++n; }
    REQUIRE(n == 1);
    return found;
}

EquipVisual sword() {
    EquipVisual e;
    e.item = 3;
    e.slot = AttachSlot::MainHand;
    e.kind = EquipKind::Weapon;
    e.content_id = "srd51.item.longsword";
    return e;
}

} // namespace

TEST_CASE("stand-in draw: replaces the pawn, aliases share a frame, the clock advances death") {
    const ViewModel pawn = scene([] {
        ActorView a = body(1, {4, 4});
        a.visual_kind.clear();
        a.clip.clear();
        return a;
    }());
    const ViewModel idle = scene(body(1, {4, 4}));
    const Shot pawn_shot = draw(pawn, 0);
    const Shot idle_shot = draw(idle, 0);
    const Shot idle_again = draw(idle, 0);
    CHECK(pawn_shot.sprites == 2);   // floor + body
    CHECK(idle_shot.sprites == pawn_shot.sprites);
    CHECK(idle_shot.hash != pawn_shot.hash);
    CHECK(idle_shot.hash == idle_again.hash);

    ActorView climb = body(1, {4, 4});
    climb.clip = "climb";
    ActorView walk = body(1, {4, 4});
    walk.clip = "walk";
    CHECK(draw(scene(climb), 0).hash == draw(scene(walk), 0).hash);

    ActorView landed = body(1, {4, 4});
    landed.clip = "hard-land";
    ActorView impact = body(1, {4, 4});
    impact.clip = "fall_impact";
    CHECK(draw(scene(landed), 0).hash == draw(scene(impact), 0).hash);

    ActorView dead = body(1, {4, 4});
    dead.clip = "death";
    dead.dead = true;
    const Shot d0 = draw(scene(dead), 0);
    const Shot d1 = draw(scene(dead), 10);
    CHECK(d0.hash != d1.hash);
    CHECK(d1.hash == draw(scene(dead), 10).hash);

    ActorView doll = body(1, {4, 4});
    doll.visual_kind = "paperdoll";
    doll.kit = "doll.human";
    doll.standin_bin.clear();
    doll.clip.clear();
    CHECK(draw(scene(doll), 0).hash == pawn_shot.hash);

    ActorView short_flag = body(1, {4, 4});
    short_flag.size_short = true;
    CHECK(draw(scene(short_flag), 0).hash == idle_shot.hash);
}

TEST_CASE("stand-in walk plays when the tile changes and idle holds when it does not") {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    auto shot = [&](const ViewModel& vm) {
        view.set_present_time(0);
        iso2d::Camera cam;
        cam.viewport_w = 320;
        cam.viewport_h = 240;
        REQUIRE(dev.begin_frame());
        view.prepare(vm, cam, 320, 240);
        auto& cl = dev.command_list();
        cl.begin_pass(render::TargetId{}, {});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        CHECK(dev.errors() == 0);
        return frame_hash(dev.last_frame());
    };
    const uint64_t stay = shot(scene(body(1, {4, 4})));
    CHECK(shot(scene(body(1, {4, 4}))) == stay);
    CHECK(shot(scene(body(1, {5, 4}))) != stay);
}

TEST_CASE("large stand-in draws 1:1 on a 2x2; a pawn on that footprint still grows") {
    CaptureDevice dev;
    SliceView view(dev);

    auto centre_of = [&](Vec2i tile, int side) {
        const FootprintGeom fp = footprint_geom(tile, side, 64.f, 32.f);
        return std::pair<FootprintGeom, Vec2f>{fp, view.tile_center_px(fp.centre_iso.x, fp.centre_iso.y)};
    };

    ActorView ogre = body(1, {4, 6});
    ogre.name = "OGRE";
    ogre.standin_bin = "large";
    ogre.kit = "standin.large";
    ogre.footprint = 2;
    ogre.hp = ogre.hp_max = 20;
    ogre.unseen_attacker = true;
    ogre.equipment.push_back(sword());
    const ViewModel large = scene(ogre);
    const auto [fp2, centre2] = centre_of(ogre.tile, 2);
    const Vec2f focus2 = view.actors_focus_px(large);
    CHECK(focus2.x == doctest::Approx(centre2.x));
    // Camera box uses the 64px canvas at scale 1, not 64 * pawn_scale.
    CHECK(focus2.y == doctest::Approx(centre2.y + (fp2.ring_h * 0.5f - 64.f) * 0.5f));
    CHECK(focus2.y == doctest::Approx(centre2.y - 16.f));

    const std::vector<Quad> lq = draw_quads(dev, view, large);
    const Quad* bodyq = only_sized(lq, 48.f, 64.f);
    CHECK(bodyq->tw == 48);
    CHECK(bodyq->th == 64);
    CHECK(bodyq->x0 + 24.f == doctest::Approx(centre2.x));
    CHECK(bodyq->y0 + 63.f == doctest::Approx(centre2.y));
    CHECK(count_sized(lq, 96.f, 128.f) == 0);

    // Name / HP anchor is the same 1x height. The bar stays footprint-wide (40 * pawn_scale).
    const Vec2f head = footprint_head_px(centre2, 64.f, 1.f);
    CHECK(head.y == doctest::Approx(centre2.y - 60.f));
    const Quad* bar = only_sized(lq, 82.f, 7.f);
    CHECK(bar->tw == 1);
    CHECK(bar->th == 1);
    CHECK(bar->x0 == doctest::Approx(head.x - 41.f));
    CHECK(bar->y0 == doctest::Approx(head.y - 13.f));

    // Pip and the sword use the body's scale: native sizes, centre slide, no grow.
    const Quad* pip = only_sized(lq, 5.f, 9.f);
    CHECK(pip->y0 == doctest::Approx(bodyq->y0 - 2.f));
    CHECK(count_sized(lq, 10.f, 18.f) == 0);
    CHECK(count_sized(lq, 3.f, 16.f) == 1);
    CHECK(count_sized(lq, 6.f, 32.f) == 0);

    // A larger footprint does not resize the stand-in. The pivot follows the new centre.
    ogre.footprint = 4;
    ogre.unseen_attacker = false;
    ogre.equipment.clear();
    const ViewModel enlarged = scene(ogre);
    const auto [fp4, centre4] = centre_of(ogre.tile, 4);
    const Vec2f focus4 = view.actors_focus_px(enlarged);
    CHECK(focus4.y == doctest::Approx(centre4.y + (fp4.ring_h * 0.5f - 64.f) * 0.5f));
    const std::vector<Quad> eq = draw_quads(dev, view, enlarged);
    const Quad* still = only_sized(eq, 48.f, 64.f);
    CHECK(still->tw == 48);
    CHECK(still->th == 64);
    CHECK(still->x0 + 24.f == doctest::Approx(centre4.x));
    CHECK(still->y0 + 63.f == doctest::Approx(centre4.y));

    // Placeholder pawn, paperdoll PC, and an unbinned huge token keep growing.
    ActorView pawn = body(1, {4, 6});
    pawn.visual_kind.clear();
    pawn.standin_bin.clear();
    pawn.kit.clear();
    pawn.clip.clear();
    pawn.name = "PAWN";
    pawn.footprint = 2;
    pawn.hp = pawn.hp_max = 20;
    pawn.unseen_attacker = true;
    pawn.equipment.push_back(sword());
    const ViewModel pawns = scene(pawn);
    const Vec2f pawn_focus = view.actors_focus_px(pawns);
    CHECK(pawn_focus.y == doctest::Approx(centre2.y + (fp2.ring_h * 0.5f - 88.f) * 0.5f));
    const std::vector<Quad> pq = draw_quads(dev, view, pawns);
    const Quad* grown = only_sized(pq, 56.f, 88.f);
    CHECK(grown->tw == 28);
    CHECK(grown->th == 44);
    CHECK(grown->x0 + 28.f == doctest::Approx(centre2.x));
    CHECK(grown->y0 + 80.f == doctest::Approx(centre2.y));
    const Vec2f pawn_head = footprint_head_px(centre2, 44.f, fp2.pawn_scale);
    CHECK(pawn_head.y == doctest::Approx(centre2.y - 80.f));
    const Quad* pawn_bar = only_sized(pq, 82.f, 7.f);
    CHECK(pawn_bar->y0 == doctest::Approx(pawn_head.y - 13.f));
    const Quad* pawn_pip = only_sized(pq, 10.f, 18.f);
    CHECK(pawn_pip->y0 == doctest::Approx(grown->y0 - 4.f));
    CHECK(count_sized(pq, 5.f, 9.f) == 0);
    CHECK(count_sized(pq, 6.f, 32.f) == 1);
    CHECK(count_sized(pq, 3.f, 16.f) == 0);

    pawn.visual_kind = "paperdoll";
    pawn.kit = "paperdoll";
    pawn.is_pc = true;
    pawn.unseen_attacker = false;
    pawn.equipment.clear();
    const std::vector<Quad> pcq = draw_quads(dev, view, scene(pawn));
    const Quad* pc = only_sized(pcq, 56.f, 88.f);
    CHECK(pc->tw == 28);
    CHECK(pc->th == 44);

    pawn.visual_kind = "unbinned";
    pawn.standin_bin = "huge";
    pawn.is_pc = false;
    pawn.footprint = 3;
    const auto [fp3, centre3] = centre_of(pawn.tile, 3);
    const std::vector<Quad> hq = draw_quads(dev, view, scene(pawn));
    const Quad* huge = only_sized(hq, 84.f, 132.f);
    CHECK(huge->x0 + 42.f == doctest::Approx(centre3.x));
    CHECK(huge->y0 + 120.f == doctest::Approx(centre3.y));
    CHECK(view.actors_focus_px(scene(pawn)).y == doctest::Approx(centre3.y + (fp3.ring_h * 0.5f - 132.f) * 0.5f));

    // Tiny / small / medium are footprint 1: native canvas, pivot on the tile centre.
    // A small token enlarged to 2x2 stays 24x32.
    struct Bin {
        const char* name;
        float w, h, px, py;
    };
    const Bin bins[] = {{"tiny", 16.f, 16.f, 8.f, 15.f}, {"small", 24.f, 32.f, 12.f, 31.f}, {"medium", 32.f, 48.f, 16.f, 47.f}};
    for (const Bin& b : bins) {
        CAPTURE(b.name);
        ActorView a = body(1, {4, 6});
        a.standin_bin = b.name;
        a.kit = std::string("standin.") + b.name;
        a.footprint = 1;
        a.unseen_attacker = true;
        const auto [one, tile_c] = centre_of(a.tile, 1);
        const std::vector<Quad> sq = draw_quads(dev, view, scene(a));
        const Quad* q = only_sized(sq, b.w, b.h);
        const Quad* bin_pip = only_sized(sq, 5.f, 9.f);
        CHECK(bin_pip->y0 == doctest::Approx(q->y0 - 2.f));
        CHECK(q->tw == uint32_t(b.w));
        CHECK(q->th == uint32_t(b.h));
        CHECK(q->x0 + b.px == doctest::Approx(tile_c.x));
        CHECK(q->y0 + b.py == doctest::Approx(tile_c.y));
        CHECK(view.actors_focus_px(scene(a)).y == doctest::Approx(tile_c.y + (one.ring_h * 0.5f - b.h) * 0.5f));
    }
    ActorView small2 = body(1, {4, 6});
    small2.footprint = 2;
    const std::vector<Quad> s2 = draw_quads(dev, view, scene(small2));
    const Quad* small_big = only_sized(s2, 24.f, 32.f);
    CHECK(small_big->tw == 24);
    CHECK(small_big->th == 32);
    CHECK(small_big->x0 + 12.f == doctest::Approx(centre2.x));
    CHECK(small_big->y0 + 31.f == doctest::Approx(centre2.y));
    CHECK(count_sized(s2, 48.f, 64.f) == 0);
}

TEST_CASE("missing huge kit and an empty bin anchor the label to the body drawn") {
    CaptureDevice dev;
    SliceView view(dev);
    auto centre_of = [&](Vec2i tile, int side) {
        const FootprintGeom fp = footprint_geom(tile, side, 64.f, 32.f);
        return std::pair<FootprintGeom, Vec2f>{fp, view.tile_center_px(fp.centre_iso.x, fp.centre_iso.y)};
    };

    // Huge bin, no standin.huge kit: the body is the grown 84x132 pawn. The HP bar uses that
    // pawn, not the 96px canvas, so it stays above the head.
    ActorView huge = body(1, {4, 6});
    huge.visual_kind = "standin";
    huge.standin_bin = "huge";
    huge.kit = "standin.huge";
    huge.clip = "idle";
    huge.footprint = 3;
    huge.name = "HUGE";
    huge.hp = huge.hp_max = 20;
    huge.unseen_attacker = true;
    const auto [fp3, centre3] = centre_of(huge.tile, 3);
    const ViewModel huge_vm = scene(huge);
    CHECK(view.actors_focus_px(huge_vm).y == doctest::Approx(centre3.y + (fp3.ring_h * 0.5f - 132.f) * 0.5f));
    const std::vector<Quad> hq = draw_quads(dev, view, huge_vm);
    const Quad* grown = only_sized(hq, 84.f, 132.f);
    CHECK(grown->y0 + 120.f == doctest::Approx(centre3.y));
    const Vec2f head = footprint_head_px(centre3, 44.f, fp3.pawn_scale);
    CHECK(head.y == doctest::Approx(centre3.y - 120.f));
    const Quad* bar = only_sized(hq, 122.f, 7.f);
    CHECK(bar->y0 == doctest::Approx(head.y - 13.f));
    CHECK(bar->y1 <= grown->y0);
    const Quad* pip = only_sized(hq, 15.f, 27.f);
    CHECK(pip->y0 == doctest::Approx(grown->y0 - 6.f));
    CHECK(count_sized(hq, 72.f, 96.f) == 0);

    // Empty bin loads medium and draws it 1:1. The label is the 48px canvas, not the 2x pawn.
    ActorView empty = body(1, {4, 6});
    empty.visual_kind = "standin";
    empty.standin_bin.clear();
    empty.kit.clear();
    empty.clip = "idle";
    empty.footprint = 2;
    empty.name = "MED";
    empty.hp = empty.hp_max = 20;
    const auto [fp2, centre2] = centre_of(empty.tile, 2);
    const ViewModel empty_vm = scene(empty);
    CHECK(view.actors_focus_px(empty_vm).y == doctest::Approx(centre2.y + (fp2.ring_h * 0.5f - 48.f) * 0.5f));
    const std::vector<Quad> eq = draw_quads(dev, view, empty_vm);
    const Quad* med = only_sized(eq, 32.f, 48.f);
    CHECK(med->y0 + 47.f == doctest::Approx(centre2.y));
    CHECK(count_sized(eq, 56.f, 88.f) == 0);
    const Vec2f med_head = footprint_head_px(centre2, 48.f, 1.f);
    CHECK(med_head.y == doctest::Approx(centre2.y - 44.f));
    const Quad* med_bar = only_sized(eq, 82.f, 7.f);
    CHECK(med_bar->y0 == doctest::Approx(med_head.y - 13.f));
    const Vec2f floated = footprint_head_px(centre2, 44.f, fp2.pawn_scale);
    CHECK(floated.y < med_head.y);
    CHECK(med_bar->y0 != doctest::Approx(floated.y - 13.f));
}
