// Equipment drawn on character sprites (equip_visuals.hpp): fixed layer order around the body, determinism, unequip, and
// byte-exact frames through the null backend; plus the live path Session (sim loadout) -> ViewModel -> layers.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "assets/schema_registry.hpp"
#include "equip_scene.hpp"
#include "equip_visuals.hpp"
#include "render_null/null_device.hpp"
#include "rules5e/equipment.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
uint64_t frame_hash(const render::NullFrameStats& st) {
    uint64_t h = st.upload_hash;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } };
    mix(st.draw_calls);
    for (const auto& d : st.draws) { mix(d.texture.index); mix(d.index_count); mix(d.first_index); mix(uint64_t(uint32_t(d.vertex_offset))); }
    return h;
}
struct Frame { uint64_t hash = 0; uint32_t world_sprites = 0; };
Frame render_frame(const ViewModel& vm) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(4.5f, 4.5f);
    cam.position = {c.x - 320.f, c.y - 180.f};
    cam.viewport_w = 640; cam.viewport_h = 360;
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
std::vector<EquipPart> parts(const ActorView& a) {
    std::vector<EquipPart> out;
    for (const EquipLayer& l : equip_layers(a)) out.push_back(l.part);
    return out;
}
size_t quads_of(const ActorView& a) {
    size_t n = 0;
    for (const EquipLayer& l : equip_layers(a)) if (l.item) n += equip_quads(*l.item, l.part, l.at).size();
    return n;
}
} // namespace

TEST_CASE("equip visuals: fixed draw order around the body; facing West swaps the hands and mirrors the points") {
    ViewModel vm = equip_scene::build();
    ActorView a = vm.actors[0];
    a.equipment.push_back(equip_scene::item(13, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch"));
    using P = EquipPart;
    CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::OffHand, P::MainHand, P::HeldLight});
    // Any input order gives the same layers.
    ActorView r = a;
    std::reverse(r.equipment.begin(), r.equipment.end());
    std::vector<EquipLayer> la = equip_layers(a), lr = equip_layers(r);
    REQUIRE(la.size() == lr.size());
    for (size_t i = 0; i < la.size(); ++i) {
        CHECK(la[i].z == lr[i].z);
        CHECK((la[i].item ? la[i].item->item : 0) == (lr[i].item ? lr[i].item->item : 0));
    }
    // z strictly increases through back < body < hands < light, and stays above walls (z 0) on the actor layer.
    CHECK(la.front().z > 0);
    for (size_t i = 1; i < la.size(); ++i) CHECK(la[i - 1].z < la[i].z);
    for (Facing f : {Facing::SouthWest, Facing::NorthWest}) {
        CHECK(attach_points(f).main_hand.x < 0.f);
        CHECK(attach_points(f).back.x == -attach_points(Facing::SouthEast).back.x);
        CHECK(attach_points(f).hip.x == -attach_points(Facing::SouthEast).hip.x);
    }
    // Toward (SW): the back stays behind the body. Away (NW / NE): it draws in front of the body, hands still mirrored.
    a.facing = Facing::SouthWest;
    CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::MainHand, P::OffHand, P::HeldLight});
    a.facing = Facing::NorthWest;
    CHECK(parts(a) == std::vector<P>{P::Body, P::Back, P::MainHand, P::OffHand, P::HeldLight});
    a.facing = Facing::NorthEast;
    CHECK(parts(a) == std::vector<P>{P::Body, P::Back, P::OffHand, P::MainHand, P::HeldLight});
    CHECK(attach_points(Facing::SouthEast).main_hand.x > 0.f);
}

TEST_CASE("equip visuals: back and hip z flip around the body on all four facings") {
    ActorView a;
    a.equipment = {equip_scene::item(2, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword"),
                   equip_scene::item(3, AttachSlot::OffHand, EquipKind::Shield, "srd51.item.shield"),
                   equip_scene::item(8, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch"),
                   equip_scene::item(4, AttachSlot::Back, EquipKind::Other, "srd51.item.backpack"),
                   equip_scene::item(9, AttachSlot::Back, EquipKind::Weapon, "srd51.item.dagger"),
                   equip_scene::item(5, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch")};
    auto find_layer = [](const std::vector<EquipLayer>& ls, uint32_t id) -> const EquipLayer* {
        for (const EquipLayer& l : ls)
            if (l.item && l.item->item == id) return &l;
        return nullptr;
    };
    auto index_of = [](const std::vector<EquipLayer>& ls, uint32_t id) {
        for (size_t i = 0; i < ls.size(); ++i)
            if (ls[i].item && ls[i].item->item == id) return i;
        return ls.size();
    };
    for (Facing f : {Facing::SouthEast, Facing::SouthWest, Facing::NorthEast, Facing::NorthWest}) {
        a.facing = f;
        const bool away = facing_shows_back(f);
        const std::vector<EquipLayer> ls = equip_layers(a);
        const EquipLayer* body = nullptr;
        for (const EquipLayer& l : ls)
            if (l.part == EquipPart::Body) body = &l;
        const EquipLayer* pack = find_layer(ls, 4);
        const EquipLayer* dagger = find_layer(ls, 9);
        const EquipLayer* pouch = find_layer(ls, 5);
        const EquipLayer* sword = find_layer(ls, 2);
        const EquipLayer* shield = find_layer(ls, 3);
        const EquipLayer* torch = find_layer(ls, 8);
        REQUIRE(body);
        REQUIRE(pack);
        REQUIRE(dagger);
        REQUIRE(pouch);
        REQUIRE(sword);
        REQUIRE(shield);
        REQUIRE(torch);
        CHECK(pack->part == EquipPart::Back);
        CHECK(dagger->part == EquipPart::Back);
        CHECK(pouch->part == EquipPart::Hip);
        CHECK(pack->z == worn_z(AttachSlot::Back, f));
        CHECK(dagger->z == pack->z);
        CHECK(pouch->z == worn_z(AttachSlot::Hip, f));
        if (away) {
            CHECK(pack->z == kZWornFront);
            CHECK(pack->z > body->z);
            CHECK(pouch->z == kZBack);
            CHECK(pouch->z < body->z);
        } else {
            CHECK(pack->z == kZBack);
            CHECK(pack->z < body->z);
            CHECK(pouch->z == kZWornFront);
            CHECK(pouch->z > body->z);
        }
        // Both worn layers stay behind both hands. The light stays above the hands, the outline above the light.
        CHECK(pack->z < sword->z);
        CHECK(pack->z < shield->z);
        CHECK(pouch->z < sword->z);
        CHECK(pouch->z < shield->z);
        CHECK(sword->z > body->z);
        CHECK(shield->z > body->z);
        CHECK(sword->z == ((f == Facing::SouthWest || f == Facing::NorthWest) ? kZFarHand : kZNearHand));
        CHECK(shield->z == ((f == Facing::SouthWest || f == Facing::NorthWest) ? kZNearHand : kZFarHand));
        CHECK(torch->z == kZLight);
        CHECK(torch->z > sword->z);
        CHECK(torch->z > shield->z);
        CHECK(kZOutline > kZLight);
        CHECK(kZLight > kZNearHand);
        CHECK(kZNearHand > kZFarHand);
        CHECK(kZFarHand > kZWornFront);
        CHECK(kZWornFront > kZBody);
        CHECK(kZBody > kZBack);
        // Same z: lower item instance first (pack 4 before the slung dagger 9), on either side of the body.
        CHECK(index_of(ls, 4) < index_of(ls, 9));
        const AttachPoints ap = attach_points(f);
        CHECK(pack->at.x == ap.back.x);
        CHECK(pack->at.y == ap.back.y);
        CHECK(pouch->at.x == ap.hip.x);
        CHECK(pouch->at.y == ap.hip.y);
    }
    // Input order does not change the tie-break.
    std::reverse(a.equipment.begin(), a.equipment.end());
    a.facing = Facing::NorthEast;
    const std::vector<EquipLayer> away = equip_layers(a);
    CHECK(index_of(away, 4) < index_of(away, 9));
    a.facing = Facing::SouthEast;
    const std::vector<EquipLayer> toward = equip_layers(a);
    CHECK(index_of(toward, 4) < index_of(toward, 9));
    // Placeholder quads: a pack is a bag, a slung weapon stays a shaft, a pouch is the belt bag.
    const EquipLayer* pack_layer = find_layer(toward, 4);
    const EquipLayer* dagger_layer = find_layer(toward, 9);
    const EquipLayer* pouch_layer = find_layer(toward, 5);
    REQUIRE(pack_layer);
    REQUIRE(dagger_layer);
    REQUIRE(pouch_layer);
    const EquipVisual* pack_item = pack_layer->item;
    const EquipVisual* dagger_item = dagger_layer->item;
    const EquipVisual* pouch_item = pouch_layer->item;
    REQUIRE(pack_item);
    REQUIRE(dagger_item);
    REQUIRE(pouch_item);
    const std::vector<EquipQuad> pack_q = equip_quads(*pack_item, EquipPart::Back, {0, 0});
    const std::vector<EquipQuad> shaft_q = equip_quads(*dagger_item, EquipPart::Back, {0, 0});
    const std::vector<EquipQuad> pouch_q = equip_quads(*pouch_item, EquipPart::Hip, {0, 0});
    REQUIRE(pack_q.size() == 1);
    REQUIRE(shaft_q.size() == 1);
    REQUIRE(pouch_q.size() == 1);
    CHECK(pack_q[0].size.x > shaft_q[0].size.x);
    CHECK(pouch_q[0].size.x < pack_q[0].size.x);
}

TEST_CASE("equip visuals: held light flame follows the lit state exactly like the glow") {
    EquipVisual l = equip_scene::item(1, AttachSlot::MainHand, EquipKind::Light, "srd51.item.lantern_hooded");
    auto emissive = [&] {
        int n = 0;
        for (const EquipQuad& q : equip_quads(l, EquipPart::HeldLight, {0, 0})) n += q.emissive ? 1 : 0;
        return n;
    };
    CHECK(emissive() == 0);                           // unlit: the item, no flame
    CHECK(equip_quads(l, EquipPart::HeldLight, {0, 0}).size() == 1);
    l.lit = true; l.bright_ft = 30; l.dim_ft = 30;
    CHECK(emissive() == 1);                           // lit
    l.light_state = 1; l.bright_ft = 0; l.dim_ft = 5;
    CHECK(emissive() == 1);                           // hooded: dim-only ember
    CHECK(equip_quads(l, EquipPart::HeldLight, {0, 0}).back().color.a < 1.f);
    l.bright_ft = 0; l.dim_ft = 0;
    CHECK(emissive() == 0);                           // lit but shedding nothing: no flame, as glows_from skips it
    l.lit = false;
    CHECK(emissive() == 0);                           // doused
}

TEST_CASE("equip visuals: frames are byte-identical across runs and input orders; unequip removes the overlay") {
    const ViewModel eq = equip_scene::build(true), bare = equip_scene::build(false);
    const Frame f1 = render_frame(eq), f2 = render_frame(eq), fb = render_frame(bare);
    CHECK(f1.hash == f2.hash);
    ViewModel shuffled = eq;
    for (ActorView& a : shuffled.actors) std::reverse(a.equipment.begin(), a.equipment.end());
    CHECK(render_frame(shuffled).hash == f1.hash);
    size_t quads = 0;
    for (const ActorView& a : eq.actors) quads += quads_of(a);
    CHECK(f1.world_sprites == fb.world_sprites + quads);
    // Unequip everything: exactly the never-equipped frame (no leftover overlay sprite or vertex).
    ViewModel un = eq;
    for (ActorView& a : un.actors) a.equipment.clear();
    CHECK(render_frame(un).hash == fb.hash);
    // Unequip one item: one item's quads fewer.
    ViewModel one = eq;
    const size_t before = quads_of(one.actors[0]);
    one.actors[0].equipment.erase(one.actors[0].equipment.begin());   // the longsword
    const Frame fo = render_frame(one);
    CHECK(fo.world_sprites == f1.world_sprites - (before - quads_of(one.actors[0])));
    CHECK(fo.hash != f1.hash);
    // MSVC /WX treats getenv as C4996; _dupenv_s is the CRT-safe substitute.
    bool print_golden = false;
#if defined(_MSC_VER)
    char* equip_print = nullptr;
    size_t equip_print_len = 0;
    if (_dupenv_s(&equip_print, &equip_print_len, "EQUIP_PRINT_GOLDEN") == 0 && equip_print) {
        print_golden = equip_print[0] != '\0';
        std::free(equip_print);
    }
#else
    if (const char* v = std::getenv("EQUIP_PRINT_GOLDEN")) print_golden = v[0] != '\0';
#endif
    if (print_golden) std::printf("equip golden: 0x%016llxull 0x%016llxull\n",
                                  (unsigned long long)f1.hash, (unsigned long long)fb.hash);
    // Golden (null backend draw list + upload bytes) for the fixed scene.
    CHECK(f1.hash == 0xcfd377e872395730ull);
    CHECK(fb.hash == 0x53ccc747bdd098ccull);
}

namespace {
namespace fs = std::filesystem;
std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}
// Temp pack: the sample pack with the fighter carrying chain mail, a longsword and a hooded lantern (equipped: main + off
// hand) and an unequipped dagger (carried: slung on the back).
// Per-process suffix so concurrent/self-hosted runs never collide on a fixed /tmp path (and so a
// ghrunner-owned leftover cannot Permission-denied the throwing remove_all).
const std::string& run_suffix() {
#ifdef _WIN32
    static const std::string s = std::to_string(_getpid());
#else
    static const std::string s = std::to_string(getpid());
#endif
    return s;
}
fs::path make_kit_pack() {
    const fs::path root = fs::temp_directory_path() / ("isoforge_equip_visuals_" + run_suffix());
    std::error_code ec;
    fs::remove_all(root, ec);   // ignore: leftover from another user/run must not fail the test
    fs::create_directories(root);
    const fs::path pack = root / "test.equip";
    fs::copy(fs::path(SLICE_PACK_ROOT) / SLICE_PACK, pack, fs::copy_options::recursive);
    std::string man = slurp(pack / "manifest.json");
    man.replace(man.find("\"sample.vertical_slice\""), 23, "\"test.equip\"");
    std::ofstream(pack / "manifest.json") << man;
    assets::Json f = assets::Json::parse(slurp(fs::path(SLICE_PACK_ROOT) / "srd51.core/characters/fighter_level_1.json"));
    f["id"] = "srd51.character.fighter_1";
    f["equipment"] = assets::Json::array({assets::Json{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.longsword"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.lantern_hooded"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.dagger"}, {"equipped", false}}});
    std::ofstream(pack / "characters/fighter_1.json") << f.dump(2);
    return root;
}
// Same temp pack, with the fighter carrying a slung weapon and shield plus the four body-gear items.
// component_pouch is carried too, and must not be drawn as the belt pouch.
fs::path make_body_gear_pack() {
    const fs::path root = fs::temp_directory_path() / ("isoforge_equip_body_" + run_suffix());
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path pack = root / "test.equip";
    fs::copy(fs::path(SLICE_PACK_ROOT) / SLICE_PACK, pack, fs::copy_options::recursive);
    std::string man = slurp(pack / "manifest.json");
    man.replace(man.find("\"sample.vertical_slice\""), 23, "\"test.equip\"");
    std::ofstream(pack / "manifest.json") << man;
    assets::Json f = assets::Json::parse(slurp(fs::path(SLICE_PACK_ROOT) / "srd51.core/characters/fighter_level_1.json"));
    f["id"] = "srd51.character.fighter_1";
    f["equipment"] = assets::Json::array({assets::Json{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.longsword"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.dagger"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.shield"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.backpack"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.quiver"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.case_crossbow_bolt"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.pouch"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.component_pouch"}, {"equipped", false}}});
    std::ofstream(pack / "characters/fighter_1.json") << f.dump(2);
    return root;
}
const ActorView* actor(const ViewModel& vm, uint32_t id) {
    for (const ActorView& a : vm.actors) if (a.id == id) return &a;
    return nullptr;
}
const EquipVisual* find_item(const ActorView& a, const std::string& id) {
    for (const EquipVisual& e : a.equipment) if (e.content_id == id) return &e;
    return nullptr;
}
} // namespace

TEST_CASE("equip visuals: the session reads the sim loadout; stow slings, drop removes, lighting adds the flame") {
    const fs::path root = make_kit_pack();
    Session::Options o{{root.string(), SLICE_PACK_ROOT}, {"test.equip"}, 1, true};
    o.pc_torches = false;
    o.equipment = true;
    std::string err;
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    uint32_t pc = 0;
    for (const ActorView& a : s->view().actors) if (a.is_pc) pc = a.id;
    REQUIRE(pc);
    ViewModel vm = s->view();
    const ActorView* a = actor(vm, pc);
    REQUIRE(a);
    const EquipVisual* sword = find_item(*a, "srd51.item.longsword");
    const EquipVisual* lantern = find_item(*a, "srd51.item.lantern_hooded");
    const EquipVisual* dagger = find_item(*a, "srd51.item.dagger");
    REQUIRE(sword); REQUIRE(lantern); REQUIRE(dagger);
    CHECK(sword->slot == AttachSlot::MainHand);
    CHECK(sword->kind == EquipKind::Weapon);
    CHECK(lantern->slot == AttachSlot::OffHand);
    CHECK(lantern->kind == EquipKind::Light);
    CHECK_FALSE(sheds_light(*lantern));
    CHECK(dagger->slot == AttachSlot::Back);
    CHECK(find_item(*a, "srd51.item.chain_mail") == nullptr);   // worn armor is not an attachment
    const uint32_t sword_id = sword->item, lantern_id = lantern->item;

    REQUIRE(s->end_turns_until(pc));
    REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::SetLightState), lantern_id, 0, 0));
    vm = s->view();
    a = actor(vm, pc);
    lantern = find_item(*a, "srd51.item.lantern_hooded");
    REQUIRE(lantern);
    CHECK(sheds_light(*lantern));
    CHECK(lantern->bright_ft == 30);
    // The lit source the glow is made from agrees with the drawn flame.
    bool glow_lit = false;
    for (const LightSourceView& ls : vm.light.sources) if (ls.carrier_id == pc && ls.lit && ls.bright_ft == 30) glow_lit = true;
    CHECK(glow_lit);

    // Drop the lantern (free): it leaves the sprite.
    REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::Drop), lantern_id));
    vm = s->view();
    CHECK(find_item(*actor(vm, pc), "srd51.item.lantern_hooded") == nullptr);
    // Stow the longsword (next turn's interaction): from the main hand to the back.
    for (const ActorView& o2 : s->view().actors)
        if (o2.id != pc && s->end_turns_until(o2.id)) break;
    REQUIRE(s->end_turns_until(pc));
    REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::Stow), sword_id));
    vm = s->view();
    sword = find_item(*actor(vm, pc), "srd51.item.longsword");
    REQUIRE(sword);
    CHECK(sword->slot == AttachSlot::Back);
    for (const EquipVisual& e : actor(vm, pc)->equipment) CHECK(e.slot == AttachSlot::Back);
    { std::error_code ec; fs::remove_all(root, ec); }   // best-effort; do not fail the test on cleanup
}

TEST_CASE("equip visuals: backpack, quiver and bolt case go to the back; the pouch goes to the hip") {
    const fs::path root = make_body_gear_pack();
    Session::Options o{{root.string(), SLICE_PACK_ROOT}, {"test.equip"}, 1, true};
    o.pc_torches = false;
    o.equipment = true;
    std::string err;
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    ViewModel vm = s->view();
    uint32_t pc = 0;
    for (const ActorView& a : vm.actors)
        if (a.is_pc) pc = a.id;
    REQUIRE(pc);
    const ActorView* a = actor(vm, pc);
    REQUIRE(a);
    const EquipVisual* sword = find_item(*a, "srd51.item.longsword");
    const EquipVisual* dagger = find_item(*a, "srd51.item.dagger");
    const EquipVisual* shield = find_item(*a, "srd51.item.shield");
    const EquipVisual* pack = find_item(*a, "srd51.item.backpack");
    const EquipVisual* quiver = find_item(*a, "srd51.item.quiver");
    const EquipVisual* bolts = find_item(*a, "srd51.item.case_crossbow_bolt");
    const EquipVisual* pouch = find_item(*a, "srd51.item.pouch");
    REQUIRE(sword);
    REQUIRE(dagger);
    REQUIRE(shield);
    REQUIRE(pack);
    REQUIRE(quiver);
    REQUIRE(bolts);
    REQUIRE(pouch);
    CHECK(sword->slot == AttachSlot::MainHand);
    CHECK(dagger->slot == AttachSlot::Back);
    CHECK(dagger->kind == EquipKind::Weapon);
    CHECK(shield->slot == AttachSlot::Back);
    CHECK(shield->kind == EquipKind::Shield);
    CHECK(pack->slot == AttachSlot::Back);
    CHECK(pack->kind == EquipKind::Other);
    CHECK(quiver->slot == AttachSlot::Back);
    CHECK(bolts->slot == AttachSlot::Back);
    CHECK(pouch->slot == AttachSlot::Hip);
    CHECK(pouch->kind == EquipKind::Other);
    CHECK(find_item(*a, "srd51.item.component_pouch") == nullptr);   // not the belt pouch
    CHECK(find_item(*a, "srd51.item.chain_mail") == nullptr);        // worn armor stays undrawn
    { std::error_code ec; fs::remove_all(root, ec); }
}
