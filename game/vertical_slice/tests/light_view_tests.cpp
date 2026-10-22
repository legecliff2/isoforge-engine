#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
std::unique_ptr<Session> open_sim(bool torches = true) {
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.pc_torches = torches;
    std::string err;
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    return s;
}
uint8_t at(const ViewModel& vm, Vec2i t) {
    const LightChunkView* c = vm.light.chunk_of_tile(t);
    REQUIRE(c);
    return c->samples[size_t(t.y * 32 + t.x)];
}
const ActorView& pc(const ViewModel& vm) {
    for (const auto& a : vm.actors) if (a.is_pc) return a;
    FAIL("no PC");
    return vm.actors.front();
}
void frame(render::NullDevice& dev, SliceView& v, const ViewModel& vm) {
    iso2d::Camera cam;
    REQUIRE(dev.begin_frame());
    v.prepare(vm, cam, 1280, 720);
    auto& cl = dev.command_list();
    cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
    v.submit(cl);
    cl.end_pass();
    dev.end_frame();
}
} // namespace

TEST_CASE("session copies the sim LightFrame: samples, dirty set, sources with stable ids, carrier, preset") {
    auto s = open_sim();
    const ViewModel vm = s->view();
    REQUIRE(vm.light.present);
    REQUIRE(vm.light.chunks.size() == 1);
    CHECK(vm.light.added.size() == 1);
    REQUIRE(vm.light.dirty.size() == 1);   // first frame: the chunk is new
    CHECK(vm.light.dirty[0].rect == RectI{0, 0, 32, 32});
    CHECK(vm.light.fog);                    // fog on by default; masks come from the frame
    CHECK(vm.light.chunks[0].has_fog);
    const ActorView& p = pc(vm);
    REQUIRE(vm.light.sources.size() == 1);
    const LightSourceView& src = vm.light.sources[0];
    CHECK(src.attached);
    CHECK(src.index == kInterimLightIdBase);   // interim ids never collide with sim light ids
    CHECK(src.carrier == 1);
    CHECK(src.carrier_id == p.id);
    CHECK(src.tile == p.tile);
    CHECK(src.lit);
    CHECK(src.preset == "srd51.core.light.torch");
    CHECK((at(vm, p.tile) & 3) == uint8_t(iso2d::LightLevel::Bright));
    // Underground ambient: far corners are Dark, the 20-40 ft ring is Dim (where nothing blocks).
    bool any_dark = false;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) any_dark |= (at(vm, {x, y}) & 3) == 0;
    CHECK(any_dark);

    // Steps without movement keep samples: no dirty chunk; a move makes the chunk dirty.
    s->step();
    const ViewModel vm2 = s->view();
    CHECK(vm2.light.frame > vm.light.frame);
    CHECK(vm2.light.added.empty());
    if (pc(vm2).tile == p.tile) CHECK(vm2.light.chunks[0].samples == vm.light.chunks[0].samples);
}

TEST_CASE("no torches: ambient only (underground = Dark everywhere); render path stays valid") {
    auto s = open_sim(false);
    const ViewModel vm = s->view();
    REQUIRE(vm.light.present);
    CHECK(vm.light.sources.empty());
    for (uint8_t b : vm.light.chunks[0].samples) CHECK((b & 3) == 0);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(dev.errors() == 0);
}

TEST_CASE("render mirrors the real frame: one R8 map, full upload once, minimal rects after, none when unchanged") {
    auto s = open_sim();
    render::NullDevice dev;
    SliceView v(dev);
    ViewModel vm = s->view();
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(v.light_maps().uploads() == 1);
    CHECK(v.light_maps().bytes_uploaded() == 1024);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) CHECK(v.light_maps().sample({0, 0, 0}, {x, y}) == at(vm, {x, y}));
    frame(dev, v, vm);   // same frame again: nothing
    CHECK(v.light_maps().uploads() == 1);
    uint64_t bytes = v.light_maps().bytes_uploaded();
    for (int i = 0; i < 40 && !s->ended(); ++i) {
        s->step();
        vm = s->view();
        frame(dev, v, vm);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) REQUIRE(v.light_maps().sample({0, 0, 0}, {x, y}) == at(vm, {x, y}));
    }
    CHECK(v.light_maps().bytes_uploaded() - bytes < uint64_t(1024) * 40);   // shrunk rects, not a chunk per step
    CHECK(dev.errors() == 0);
}

TEST_CASE("render without a LightFrame and fog masks (when present) still behave") {
    // Static view: no sim, no light.
    std::string err;
    auto st = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE(st);
    CHECK_FALSE(st->view().light.present);
    // Hand-built masks (the contract shape for when the sim publishes them): unseen foes are hidden.
    ViewModel vm = open_sim()->view();
    LightChunkView& c = vm.light.chunks[0];
    c.has_fog = true;
    c.seen_now = {};
    c.explored = {};
    const Vec2i p = pc(vm).tile;
    for (int y = p.y - 2; y <= p.y + 2; ++y)
        for (int x = p.x - 2; x <= p.x + 2; ++x)
            if (x >= 0 && y >= 0 && x < 32 && y < 32) {
                c.seen_now[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64);
                c.explored[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64);
            }
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    int far_foes = 0;
    for (const auto& a : vm.actors)
        if (!a.is_pc && std::max(std::abs(a.tile.x - p.x), std::abs(a.tile.y - p.y)) > 2) ++far_foes;
    CHECK(v.world_sprites() == vm.tiles.size() + vm.light.sources.size() + vm.actors.size() - size_t(far_foes) + 1);   // +1 current-turn ring
    CHECK(iso2d::light_tint(0xFF, iso2d::Fog::Unexplored).r == 0.f);
}

TEST_CASE("carried lights: pc_torches is one producer of a generic list; glows anchor to the carrier") {
    // Torches off, one non-PC carrier with a non-torch preset: the same path as the torches.
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.pc_torches = false;
    std::string err;
    uint32_t foe = 0;
    {
        auto probe = Session::open(o, &err);
        REQUIRE_MESSAGE(probe, err);
        for (const auto& a : probe->view().actors) if (!a.is_pc) { foe = a.id; break; }
    }
    REQUIRE(foe != 0);
    CarriedLight lantern;
    lantern.carrier_id = foe;
    lantern.bright_ft = 30;
    lantern.dim_ft = 30;
    lantern.preset = "srd51.core.light.lantern_hooded";
    o.carried_lights.push_back(lantern);
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    ViewModel vm = s->view();
    REQUIRE(vm.light.present);
    REQUIRE(vm.light.sources.size() == 1);
    const LightSourceView& src = vm.light.sources[0];
    CHECK(src.attached);
    CHECK(src.carrier == 1);
    CHECK(src.carrier_id == foe);
    CHECK(src.preset == "srd51.core.light.lantern_hooded");

    const iso2d::GlowPalette pal;
    auto glows = glows_from(vm.light, pal);
    REQUIRE(glows.size() == 1);
    CHECK(glows[0].anchor == iso2d::GlowAnchorKind::Entity);
    CHECK(glows[0].anchor_id == foe);
    CHECK(glows[0].radius_tiles == doctest::Approx(9.f));
    CHECK(glows[0].intensity == doctest::Approx(pal.style_for("x.lantern", false).intensity));
    const iso2d::GlowAnchor moved[] = {{foe, {3.5f, 4.5f}}};
    iso2d::follow_anchors(glows, moved);
    CHECK(glows[0].tile.x == 3.5f);

    // pc_torches + the extra light: both producers feed the list (torches first).
    o.pc_torches = true;
    auto both = Session::open(o, &err);
    REQUIRE_MESSAGE(both, err);
    const ViewModel vb = both->view();
    size_t torches = 0, lanterns = 0;
    for (const auto& ls : vb.light.sources) (ls.preset == "srd51.core.light.torch" ? torches : lanterns)++;
    CHECK(torches >= 1);
    CHECK(lanterns == 1);
    CHECK(glows_from(vb.light, pal).size() == vb.light.sources.size());

    // No LightFrame -> no glows.
    CHECK(glows_from(LightView{}, pal).empty());
}

namespace {
// Temp pack "test.visual": the sample pack (same chunk, same fighter) plus item@1 docs. With `with_visual`,
// two items style the same preset "test.visual.light.lantern" (ids differ from the preset; lowest id wins)
// and one item has a visual but no light.preset (skipped). Resolved next to the real packs root.
const char* kPreset = "test.visual.light.lantern";
std::filesystem::path make_visual_pack(const std::string& tag, bool with_visual) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("isoforge_visual_" + tag);
    fs::remove_all(root);
    const fs::path pack = root / "test.visual";
    fs::create_directories(root);
    fs::copy(fs::path(SLICE_PACK_ROOT) / SLICE_PACK, pack, fs::copy_options::recursive);
    std::ifstream mi(pack / "manifest.json");
    std::string man((std::istreambuf_iterator<char>(mi)), {});
    mi.close();
    man.replace(man.find("\"sample.vertical_slice\""), 23, "\"test.visual\"");
    man.replace(man.find("\"chunks/0_0_0.json\""), 19,
                "\"chunks/0_0_0.json\",\n    \"items/b_lantern.json\",\n    \"items/a_lantern.json\",\n    \"items/glowstone.json\"");
    std::ofstream(pack / "manifest.json") << man;
    fs::create_directories(pack / "items");
    auto item = [&](const char* file, const char* id, bool preset, const char* visual) {
        std::ofstream(pack / "items" / file)
            << "{\"$schema\": \"item@1\", \"id\": \"" << id << "\", \"name\": \"Lantern\", \"category\": \"valuable\", "
            << "\"cost_cp\": 500" << (preset ? std::string(", \"light\": {\"preset\": \"") + kPreset + "\"}" : std::string())
            << (with_visual ? std::string(", \"visual\": ") + visual : std::string()) << "}";
    };
    // a_lantern (lowest id) wins over b_lantern, whatever the file / manifest order.
    item("b_lantern.json", "test.visual.item.lantern_b", true, "{\"color\": {\"r\": 255, \"g\": 0, \"b\": 0}, \"intensity\": 900}");
    item("a_lantern.json", "test.visual.item.lantern_a", true,
         "{\"color\": {\"r\": 51, \"g\": 102, \"b\": 255}, \"intensity\": 400, \"flicker\": \"candle\"}");
    item("glowstone.json", "test.visual.item.glowstone", false, "{\"color\": {\"r\": 0, \"g\": 255, \"b\": 0}, \"intensity\": 700}");
    return root;
}
std::unique_ptr<Session> open_visual(const std::filesystem::path& root) {
    Session::Options o{{root.string(), SLICE_PACK_ROOT}, {"test.visual"}, 1, true};
    CarriedLight l;
    l.preset = kPreset;   // the light preset, not an item id
    o.carried_lights.push_back(l);
    std::string err;
    {
        Session::Options probe = o;
        probe.carried_lights.clear();
        auto p = Session::open(probe, &err);
        REQUIRE_MESSAGE(p, err);
        for (const auto& a : p->view().actors) if (a.is_pc) o.carried_lights[0].carrier_id = a.id;
    }
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    return s;
}
} // namespace

TEST_CASE("item@1 visual: keyed by light.preset, lowest id wins, converted at load, sim untouched") {
    const auto with = make_visual_pack("with", true), without = make_visual_pack("without", false);
    auto a = open_visual(with), b = open_visual(without);
    ViewModel va = a->view(), vb = b->view();

    REQUIRE(va.light.visuals.size() == 1);   // only the preset; no entry under item ids, glowstone skipped
    REQUIRE(va.light.visuals.count(kPreset) == 1);
    const iso2d::GlowStyle& st = va.light.visuals.find(kPreset)->second;
    CHECK(st.color.r == doctest::Approx(0.2f));   // lantern_a (lowest content id), not lantern_b
    CHECK(st.color.g == doctest::Approx(0.4f));
    CHECK(st.color.b == doctest::Approx(1.f));
    CHECK(st.intensity == doctest::Approx(0.4f));
    CHECK(st.flicker == iso2d::GlowFlicker::Candle);
    CHECK(vb.light.visuals.empty());
    bool conflict = false, skipped = false;
    for (const auto& w : a->warnings()) {
        if (w.find("conflicting visual from item test.visual.item.lantern_b") != std::string::npos &&
            w.find("using test.visual.item.lantern_a") != std::string::npos) conflict = true;
        if (w.find("test.visual.item.glowstone") != std::string::npos && w.find("no light.preset") != std::string::npos) skipped = true;
    }
    CHECK(conflict);
    CHECK(skipped);
    CHECK(b->warnings().empty());

    // The item id differs from the preset, yet the preset's glow is overridden.
    iso2d::GlowPalette pa, pb;
    for (const auto& [id, s] : va.light.visuals) pa.set_override(id, s);
    const auto ga = glows_from(va.light, pa), gb = glows_from(vb.light, pb);
    bool found = false;
    for (const auto& g : ga)
        if (g.color.b == doctest::Approx(1.f) && g.intensity == doctest::Approx(0.4f)) found = true;
    CHECK(found);
    for (const auto& g : gb) CHECK(g.intensity != doctest::Approx(0.4f));   // no visual: built-in default kept

    // Presentation only: the fight and the published light samples are identical with and without it.
    for (int i = 0; i < 30 && a->step(); ++i) REQUIRE(b->step());
    va = a->view();
    vb = b->view();
    CHECK(va.log == vb.log);
    REQUIRE(va.actors.size() == vb.actors.size());
    for (size_t i = 0; i < va.actors.size(); ++i) {
        CHECK(va.actors[i].tile == vb.actors[i].tile);
        CHECK(va.actors[i].hp == vb.actors[i].hp);
    }
    REQUIRE(va.light.chunks.size() == vb.light.chunks.size());
    for (size_t i = 0; i < va.light.chunks.size(); ++i) CHECK(va.light.chunks[i].samples == vb.light.chunks[i].samples);
    std::filesystem::remove_all(with);
    std::filesystem::remove_all(without);
}

namespace {
iso2d::Fog fog_at(const SliceView& v, const ViewModel& vm, Vec2i t) {
    iso2d::Fog f{};
    v.tile_tint(vm.light, t, &f);
    return f;
}
void set_bit(std::array<uint64_t, 16>& m, int x, int y) { m[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64); }
} // namespace

TEST_CASE("fog: shown team defaults to the player team; team choice and fog off are options") {
    auto s = open_sim();
    const ViewModel vm = s->view();
    const ActorView& p = pc(vm);
    CHECK(vm.light.fog_team == p.team);
    REQUIRE(vm.light.fog);
    const LightChunkView& c = vm.light.chunks[0];
    CHECK(c.seen(p.tile.x, p.tile.y));    // a viewer sees its own (torch-lit) tile
    CHECK(c.known(p.tile.x, p.tile.y));
    bool any_unseen = false;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) any_unseen |= !c.known(x, y);
    CHECK(any_unseen);                    // underground: the far dark stays unexplored

    uint8_t foe_team = p.team;
    for (const auto& a : vm.actors) if (a.team != p.team) foe_team = a.team;
    REQUIRE(foe_team != p.team);
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.fog_team = foe_team;
    std::string err;
    auto sf = Session::open(o, &err);
    REQUIRE(sf);
    const ViewModel vf = sf->view();
    CHECK(vf.light.fog_team == foe_team);
    CHECK(vf.light.chunks[0].seen_now != c.seen_now);   // a different team's mask
    // (Interim normal sight: the foes stand in unlit dark, so they may see nothing; only the mask choice is checked.)

    o.fog_team.reset();
    o.fog = false;
    auto so = Session::open(o, &err);
    REQUIRE(so);
    const ViewModel vo = so->view();
    CHECK_FALSE(vo.light.fog);
    CHECK_FALSE(vo.light.chunks[0].has_fog);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vo);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) REQUIRE(fog_at(v, vo, {x, y}) == iso2d::Fog::Visible);
    // Switching the shown team / turning fog on re-uploads every fog map without a new sim frame.
    frame(dev, v, vm);
    CHECK(fog_at(v, vm, {p.tile.x, p.tile.y}) == iso2d::Fog::Visible);
    CHECK(dev.errors() == 0);
}

TEST_CASE("fog: three states render (unseen black, explored dimmed, seen normal); a missing team entry is unseen") {
    ViewModel vm = open_sim()->view();
    LightChunkView& c = vm.light.chunks[0];
    REQUIRE(c.has_fog);
    c.seen_now = {}; c.explored = {};
    set_bit(c.seen_now, 3, 3); set_bit(c.explored, 3, 3);
    set_bit(c.explored, 4, 3);
    for (auto& b : c.samples) b = iso2d::pack_light(iso2d::LightLevel::Bright, 0);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    iso2d::Fog f{};
    const Color seen = v.tile_tint(vm.light, {3, 3}, &f);
    CHECK(f == iso2d::Fog::Visible);
    CHECK(seen.r == 1.f);
    const Color expl = v.tile_tint(vm.light, {4, 3}, &f);
    CHECK(f == iso2d::Fog::Remembered);
    CHECK(expl.r > 0.f); CHECK(expl.r < seen.r);
    const Color none = v.tile_tint(vm.light, {5, 3}, &f);
    CHECK(f == iso2d::Fog::Unexplored);
    CHECK(none.r == 0.f); CHECK(none.g == 0.f); CHECK(none.b == 0.f);
    CHECK(fog_at(v, vm, {40, 3}) == iso2d::Fog::Unexplored);   // untracked chunk
    CHECK(v.fog_maps().chunks() == 1);
}

TEST_CASE("fog: a blocked chunk holds its last light and fog textures; removal frees them") {
    ViewModel vm = open_sim()->view();
    REQUIRE(vm.light.chunks.size() == 1);
    LightChunkView& c = vm.light.chunks[0];
    c.seen_now = {}; c.explored = {};
    set_bit(c.seen_now, 2, 2); set_bit(c.explored, 2, 2); set_bit(c.explored, 6, 6);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    const auto tex_l = v.light_maps().texture({0, 0, 0});
    const auto tex_f = v.fog_maps().texture({0, 0, 0});
    const uint8_t s22 = v.light_maps().sample({0, 0, 0}, {2, 2});
    const uint32_t lu = v.light_maps().uploads(), fu = v.fog_maps().uploads();

    // Next frame: the chunk's halo is pending, so it is tracked but blocked (no samples, no masks).
    ViewModel b = vm;
    b.light.frame = vm.light.frame + 1;
    b.light.chunks.clear();
    b.light.added.clear();
    b.light.dirty.clear();
    b.light.vis_dirty = {{0, 0, 0}};   // even if listed, a blocked chunk is not refreshed
    b.light.blocked = {{0, 0, 0}};
    frame(dev, v, b);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(v.fog_maps().chunks() == 1);
    CHECK(v.light_maps().texture({0, 0, 0}) == tex_l);
    CHECK(v.fog_maps().texture({0, 0, 0}) == tex_f);
    CHECK(v.light_maps().uploads() == lu);
    CHECK(v.fog_maps().uploads() == fu);
    CHECK(v.light_maps().sample({0, 0, 0}, {2, 2}) == s22);
    CHECK(fog_at(v, b, {2, 2}) == iso2d::Fog::Visible);
    CHECK(fog_at(v, b, {6, 6}) == iso2d::Fog::Remembered);
    CHECK(fog_at(v, b, {9, 9}) == iso2d::Fog::Unexplored);

    // Then it leaves the tracked set: both maps are freed.
    ViewModel r = b;
    r.light.frame = b.light.frame + 1;
    r.light.blocked.clear();
    r.light.vis_dirty.clear();
    r.light.removed = {{0, 0, 0}};
    frame(dev, v, r);
    CHECK(v.light_maps().chunks() == 0);
    CHECK(v.fog_maps().chunks() == 0);
    CHECK(dev.errors() == 0);
}
