// Equipment drawn on character sprites (equip_visuals.hpp): fixed layer order around the body, determinism, unequip, and
// byte-exact frames through the null backend; plus the live path Session (sim loadout) -> ViewModel -> layers.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#include "presentation/equip_art.hpp"
#include "render_null/null_device.hpp"
#include "rules5e/equipment.hpp"
#include "session.hpp"
#include "sim/presentation.hpp"
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
#ifndef SLICE_EMPTY_EQUIP_KIT
#define SLICE_EMPTY_EQUIP_KIT ""
#endif
Frame render_frame(const ViewModel& vm) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    // Placeholder quads. The empty kit keeps these goldens off the shipped art.
    SliceView view(dev, SLICE_EMPTY_EQUIP_KIT);
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
    CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::HipMain, P::OffHand, P::MainHand, P::HeldLight});
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
        const BodyAttach shared = shared_body_attach();
        CHECK(attach_points(shared, f).main_hand.x < 0.f);
        CHECK(attach_points(shared, f).back.x == -attach_points(shared, Facing::SouthEast).back.x);
        CHECK(attach_points(shared, f).hip_main.x == -attach_points(shared, Facing::SouthEast).hip_main.x);
        CHECK(attach_points(shared, f).hip_off.x == -attach_points(shared, Facing::SouthEast).hip_off.x);
    }
    // Toward (SW): the back stays behind the body. Away (NW / NE): it draws in front of the body, hands still mirrored.
    // The longsword's sheath is hip-main and uses the hip z rule, so it swaps sides of the body with the back.
    a.facing = Facing::SouthWest;
    CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::HipMain, P::MainHand, P::OffHand, P::HeldLight});
    a.facing = Facing::NorthWest;
    CHECK(parts(a) == std::vector<P>{P::HipMain, P::Body, P::Back, P::MainHand, P::OffHand, P::HeldLight});
    a.facing = Facing::NorthEast;
    CHECK(parts(a) == std::vector<P>{P::HipMain, P::Body, P::Back, P::OffHand, P::MainHand, P::HeldLight});
    CHECK(attach_points(shared_body_attach(), Facing::SouthEast).main_hand.x > 0.f);
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
        const EquipLayer* shield = find_layer(ls, 3);
        const EquipLayer* torch = find_layer(ls, 8);
        // The sword is both the hand and the hip-main sheath.
        const EquipLayer* sheath_layer = nullptr;
        const EquipLayer* sword_hand = nullptr;
        for (const EquipLayer& l : ls) {
            if (!l.item || l.item->item != 2) continue;
            if (l.part == EquipPart::HipMain) sheath_layer = &l;
            if (l.part == EquipPart::MainHand) sword_hand = &l;
        }
        REQUIRE(body);
        REQUIRE(pack);
        CHECK(dagger == nullptr);   // one-handed, loses the back to the backpack
        REQUIRE(pouch);
        REQUIRE(sheath_layer);
        REQUIRE(sword_hand);
        REQUIRE(shield);
        REQUIRE(torch);
        CHECK(pack->part == EquipPart::Back);
        CHECK(pouch->part == EquipPart::HipOff);   // one sheath (the sword): the pouch takes hip-off
        CHECK(sheath_layer->part == EquipPart::HipMain);
        CHECK(pack->z == worn_z(AttachSlot::Back, f));
        CHECK(pouch->z == worn_z(AttachSlot::Hip, f));
        CHECK(sheath_layer->z == pouch->z);
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
        CHECK(pack->z < sword_hand->z);
        CHECK(pack->z < shield->z);
        CHECK(pouch->z < sword_hand->z);
        CHECK(pouch->z < shield->z);
        CHECK(sheath_layer->z < sword_hand->z);
        CHECK(sword_hand->z > body->z);
        CHECK(shield->z > body->z);
        CHECK(sword_hand->z == ((f == Facing::SouthWest || f == Facing::NorthWest) ? kZFarHand : kZNearHand));
        CHECK(shield->z == ((f == Facing::SouthWest || f == Facing::NorthWest) ? kZNearHand : kZFarHand));
        CHECK(torch->z == kZLight);
        CHECK(torch->z > sword_hand->z);
        CHECK(torch->z > shield->z);
        CHECK(kZOutline > kZLight);
        CHECK(kZLight > kZNearHand);
        CHECK(kZNearHand > kZFarHand);
        CHECK(kZFarHand > kZWornFront);
        CHECK(kZWornFront > kZBody);
        CHECK(kZBody > kZBack);
        // Same hip z: lower item instance first (sheath 2 before the pouch 5).
        CHECK(index_of(ls, 2) < index_of(ls, 5));
        const AttachPoints ap = attach_points(shared_body_attach(), f);
        CHECK(pack->at.x == ap.back.x);
        CHECK(pack->at.y == ap.back.y);
        CHECK(sheath_layer->at.x == ap.hip_main.x);
        CHECK(sheath_layer->at.y == ap.hip_main.y);
        CHECK(pouch->at.x == ap.hip_off.x);
        CHECK(pouch->at.y == ap.hip_off.y);
        CHECK(equip_quads(*sheath_layer->item, EquipPart::HipMain, sheath_layer->at).empty());
    }
    // Input order does not change the tie-break.
    std::reverse(a.equipment.begin(), a.equipment.end());
    a.facing = Facing::NorthEast;
    const std::vector<EquipLayer> away = equip_layers(a);
    CHECK(index_of(away, 2) < index_of(away, 5));
    a.facing = Facing::SouthEast;
    const std::vector<EquipLayer> toward = equip_layers(a);
    CHECK(index_of(toward, 2) < index_of(toward, 5));
    // Placeholder quads: a pack is a bag, a slung weapon stays a shaft, a pouch is the belt bag.
    const EquipLayer* pack_layer = find_layer(toward, 4);
    const EquipLayer* pouch_layer = find_layer(toward, 5);
    REQUIRE(pack_layer);
    REQUIRE(pouch_layer);
    const EquipVisual* pack_item = pack_layer->item;
    const EquipVisual* dagger_item = nullptr;
    for (const EquipVisual& e : a.equipment)
        if (e.item == 9) dagger_item = &e;
    const EquipVisual* pouch_item = pouch_layer->item;
    REQUIRE(pack_item);
    REQUIRE(dagger_item);
    REQUIRE(pouch_item);
    const std::vector<EquipQuad> pack_q = equip_quads(*pack_item, EquipPart::Back, {0, 0});
    const std::vector<EquipQuad> shaft_q = equip_quads(*dagger_item, EquipPart::Back, {0, 0});
    const std::vector<EquipQuad> pouch_q = equip_quads(*pouch_item, EquipPart::HipOff, {0, 0});
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
// component_pouch is a hip item of its own. The amulet is the neck.
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
                                          assets::Json{{"item", "srd51.item.component_pouch"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.holy_symbol_amulet"}, {"equipped", false}}});
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
    const EquipVisual* components = find_item(*a, "srd51.item.component_pouch");
    const EquipVisual* amulet = find_item(*a, "srd51.item.holy_symbol_amulet");
    REQUIRE(components);
    REQUIRE(amulet);
    CHECK(components->slot == AttachSlot::Hip);
    CHECK(amulet->slot == AttachSlot::Neck);
    CHECK(sword->versatile);   // longsword: presentation copy of the item, not a schema field
    CHECK(find_item(*a, "srd51.item.chain_mail") == nullptr);        // worn armor stays undrawn
    { std::error_code ec; fs::remove_all(root, ec); }
}

namespace {
bool same_quads(const std::vector<EquipQuad>& a, const std::vector<EquipQuad>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].at.x != b[i].at.x || a[i].at.y != b[i].at.y) return false;
        if (a[i].size.x != b[i].size.x || a[i].size.y != b[i].size.y) return false;
        if (a[i].emissive != b[i].emissive) return false;
        if (a[i].color.r != b[i].color.r || a[i].color.g != b[i].color.g || a[i].color.b != b[i].color.b ||
            a[i].color.a != b[i].color.a)
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("equip art: unmapped items keep today's quads, anchors, and z") {
    const BodyAttach shared = shared_body_attach();
    const AttachPoints se = attach_points(shared, Facing::SouthEast);
    CHECK(se.main_hand.x == 9.f);
    CHECK(se.main_hand.y == 4.f);
    CHECK(se.off_hand.x == -9.f);
    CHECK(se.off_hand.y == 4.f);
    CHECK(se.back.x == -7.f);
    CHECK(se.back.y == 2.f);
    CHECK(se.hip_main.x == 6.f);
    CHECK(se.hip_main.y == 8.f);
    CHECK(se.hip_off.x == -6.f);
    CHECK(se.hip_off.y == 8.f);
    CHECK(se.neck.x == 0.f);   // provisional chest point
    CHECK(se.neck.y == 2.f);
    const AttachPoints sw = attach_points(shared, Facing::SouthWest);
    const AttachPoints nw = attach_points(shared, Facing::NorthWest);
    CHECK(sw.main_hand.x == -se.main_hand.x);
    CHECK(sw.off_hand.x == -se.off_hand.x);
    CHECK(sw.back.x == -se.back.x);
    CHECK(sw.hip_main.x == -se.hip_main.x);
    CHECK(sw.hip_off.x == -se.hip_off.x);
    CHECK(nw.back.x == -se.back.x);
    CHECK(nw.hip_main.x == -se.hip_main.x);
    CHECK(nw.hip_off.x == -se.hip_off.x);
    CHECK(sw.main_hand.y == se.main_hand.y);
    CHECK(attach_points(shared, Facing::NorthEast).back.x == se.back.x);

    CHECK(kZBack == 2);
    CHECK(kZBody == 4);
    CHECK(kZWornFront == 5);
    CHECK(kZFarHand == 6);
    CHECK(kZNearHand == 7);
    CHECK(kZLight == 8);
    CHECK(kZOutline == 9);
    CHECK(worn_z(AttachSlot::Back, Facing::SouthEast) == kZBack);
    CHECK(worn_z(AttachSlot::Back, Facing::SouthWest) == kZBack);
    CHECK(worn_z(AttachSlot::Back, Facing::NorthEast) == kZWornFront);
    CHECK(worn_z(AttachSlot::Back, Facing::NorthWest) == kZWornFront);
    CHECK(worn_z(AttachSlot::Hip, Facing::SouthEast) == kZWornFront);
    CHECK(worn_z(AttachSlot::Hip, Facing::NorthEast) == kZBack);
    CHECK(kZWornFront < kZFarHand);
    CHECK(kZFarHand < kZNearHand);
    CHECK(kZNearHand < kZLight);
    CHECK(kZLight < kZOutline);

    ActorView a;
    a.facing = Facing::SouthEast;
    a.equipment = {equip_scene::item(2, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword"),
                   equip_scene::item(3, AttachSlot::OffHand, EquipKind::Shield, "srd51.item.shield"),
                   equip_scene::item(8, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch"),
                   equip_scene::item(4, AttachSlot::Back, EquipKind::Other, "srd51.item.backpack"),
                   equip_scene::item(9, AttachSlot::Back, EquipKind::Weapon, "srd51.item.longbow"),
                   equip_scene::item(6, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver"),
                   equip_scene::item(7, AttachSlot::Back, EquipKind::Other, "srd51.item.case_crossbow_bolt"),
                   equip_scene::item(5, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch")};
    const std::vector<EquipLayer> layers = equip_layers(a);
    const EquipLayer* body = nullptr;
    for (const EquipLayer& l : layers)
        if (l.part == EquipPart::Body) body = &l;
    REQUIRE(body);
    CHECK(body->z == kZBody);
    CHECK(body->at.x == 0.f);
    CHECK(body->at.y == 0.f);
    for (const EquipLayer& l : layers) {
        if (!l.item) continue;
        CHECK(equip_art_choice(nullptr, *l.item, l.part, a.facing, a.standin_bin).sprite == nullptr);
        CHECK(equip_art_choice(nullptr, *l.item, l.part, a.facing, "huge").mirror_x == false);
        const std::vector<EquipQuad> quads = equip_quads(*l.item, l.part, l.at);
        const bool sheath = (l.part == EquipPart::HipMain || l.part == EquipPart::HipOff) && l.item->kind == EquipKind::Weapon;
        if (!sheath) CHECK_FALSE(quads.empty());
        if (l.item->content_id == "srd51.item.longsword" && l.part == EquipPart::MainHand) {
            CHECK(l.z == kZNearHand);
            CHECK(l.at.x == se.main_hand.x);
            CHECK(l.at.y == se.main_hand.y);
        } else if (l.item->content_id == "srd51.item.longsword" && l.part == EquipPart::HipMain) {
            CHECK(l.z == kZWornFront);
            CHECK(l.at.x == se.hip_main.x);
            CHECK(equip_quads(*l.item, l.part, l.at).empty());
        } else if (l.item->content_id == "srd51.item.shield") {
            CHECK(l.part == EquipPart::OffHand);
            CHECK(l.z == kZFarHand);
            CHECK(l.at.x == se.off_hand.x);
        } else if (l.item->content_id == "srd51.item.torch") {
            CHECK(l.part == EquipPart::HeldLight);
            CHECK(l.z == kZLight);
            CHECK(l.at.x == se.main_hand.x);
        }
        if (l.part == EquipPart::Back) {
            CHECK(l.z == kZBack);
            CHECK(l.at.x == se.back.x);
            CHECK(l.at.y == se.back.y);
        }
        if (l.part == EquipPart::HipOff) {
            CHECK(l.z == kZWornFront);
            CHECK(l.at.x == se.hip_off.x);
            CHECK(l.at.y == se.hip_off.y);
        }
    }
    const EquipVisual* quiver = nullptr;
    const EquipVisual* bolts = nullptr;
    const EquipVisual* bow = nullptr;
    const EquipVisual* sword = nullptr;
    for (const EquipVisual& e : a.equipment) {
        if (e.content_id == "srd51.item.quiver") quiver = &e;
        if (e.content_id == "srd51.item.case_crossbow_bolt") bolts = &e;
        if (e.content_id == "srd51.item.longbow") bow = &e;
        if (e.content_id == "srd51.item.longsword") sword = &e;
    }
    REQUIRE(quiver);
    REQUIRE(bolts);
    REQUIRE(bow);
    REQUIRE(sword);
    const std::vector<EquipQuad> quiver_q = equip_quads(*quiver, EquipPart::Back, se.back);
    const std::vector<EquipQuad> bolt_q = equip_quads(*bolts, EquipPart::Back, se.back);
    CHECK(same_quads(quiver_q, bolt_q));
    CHECK(quiver_q.size() == 1);
    CHECK(quiver_q[0].size.x == 14.f);
    CHECK(quiver_q[0].size.y == 16.f);
    const std::vector<EquipQuad> bow_q = equip_quads(*bow, EquipPart::Back, se.back);
    const std::vector<EquipQuad> sword_back = equip_quads(*sword, EquipPart::Back, se.back);
    CHECK(bow_q[0].color.r != sword_back[0].color.r);
    EquipVisual torch = equip_scene::item(8, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch");
    torch.lit = true;
    torch.bright_ft = 20;
    torch.dim_ft = 20;
    int flames = 0;
    for (const EquipQuad& q : equip_quads(torch, EquipPart::HeldLight, se.main_hand)) flames += q.emissive ? 1 : 0;
    CHECK(flames == 1);

    a.facing = Facing::SouthWest;
    const EquipArtChoice west = equip_art_choice(nullptr, *quiver, EquipPart::Back, a.facing, "medium");
    CHECK(west.sprite == nullptr);
    CHECK(west.mirror_x);
    const std::vector<EquipLayer> west_layers = equip_layers(a);
    for (const EquipLayer& l : west_layers) {
        if (l.part == EquipPart::Back) CHECK(l.at.x == sw.back.x);
        if (l.part == EquipPart::HipMain) CHECK(l.at.x == sw.hip_main.x);
        if (l.part == EquipPart::HipOff) CHECK(l.at.x == sw.hip_off.x);
        if (l.part == EquipPart::MainHand) CHECK(l.at.x == sw.main_hand.x);
    }
}

#ifndef SLICE_STANDIN_ROOT
#define SLICE_STANDIN_ROOT ""
#endif

TEST_CASE("equip art: choice follows content id and view, and leaves the attach point alone") {
    const fs::path art = fs::temp_directory_path() / ("isoforge_equip_art_choice_" + run_suffix());
    std::error_code ec;
    fs::remove_all(art, ec);
    fs::create_directories(art / "srd51.item.quiver");
    fs::create_directories(art / "srd51.item.case_crossbow_bolt");
    fs::create_directories(art / "_kind" / "weapon");
    const fs::path png = fs::path(SLICE_STANDIN_ROOT);
    fs::copy_file(png / "frames/tiny/se/mon_tiny_se_idle_00.png", art / "srd51.item.quiver" / "back.se.png");
    fs::copy_file(png / "frames/small/se/mon_small_se_idle_00.png", art / "srd51.item.case_crossbow_bolt" / "back.se.png");
    fs::copy_file(png / "frames/tiny/ne/mon_tiny_ne_idle_00.png", art / "srd51.item.quiver" / "back.ne.png");
    fs::copy_file(png / "frames/medium/se/mon_medium_se_idle_00.png", art / "_kind" / "weapon" / "main_hand.se.png");
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(art);

    const EquipVisual quiver = equip_scene::item(6, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver");
    const EquipVisual bolts = equip_scene::item(7, AttachSlot::Back, EquipKind::Other, "srd51.item.case_crossbow_bolt");
    const EquipVisual bow = equip_scene::item(9, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longbow");
    const EquipArtChoice quiver_se = equip_art_choice(&table, quiver, EquipPart::Back, Facing::SouthEast, "medium");
    const EquipArtChoice quiver_sw = equip_art_choice(&table, quiver, EquipPart::Back, Facing::SouthWest, "large");
    const EquipArtChoice quiver_ne = equip_art_choice(&table, quiver, EquipPart::Back, Facing::NorthEast, "");
    const EquipArtChoice quiver_nw = equip_art_choice(&table, quiver, EquipPart::Back, Facing::NorthWest, "huge");
    const EquipArtChoice bolt_se = equip_art_choice(&table, bolts, EquipPart::Back, Facing::SouthEast, "medium");
    REQUIRE(quiver_se.sprite);
    REQUIRE(bolt_se.sprite);
    REQUIRE(quiver_ne.sprite);
    CHECK_FALSE(quiver_se.mirror_x);
    CHECK(quiver_sw.mirror_x);
    CHECK(quiver_sw.sprite == quiver_se.sprite);
    CHECK_FALSE(quiver_ne.mirror_x);
    CHECK(quiver_nw.mirror_x);
    CHECK(quiver_nw.sprite == quiver_ne.sprite);
    CHECK(quiver_se.sprite->image.w == 16);
    CHECK(bolt_se.sprite->image.w == 24);
    CHECK(quiver_se.sprite != bolt_se.sprite);
    CHECK(quiver_ne.sprite != quiver_se.sprite);
    const eng::Vec2f back = attach_points(shared_body_attach(), Facing::SouthEast).back;
    const eng::Vec2f anchor = equip_sprite_anchor(quiver_se.sprite->pivot_x, quiver_se.sprite->pivot_y, back);
    CHECK(anchor.x == float(quiver_se.sprite->pivot_x) - back.x);
    CHECK(anchor.y == float(quiver_se.sprite->pivot_y) - back.y);
    CHECK(back.x == -7.f);
    CHECK(back.y == 2.f);

    const EquipArtChoice bow_hand = equip_art_choice(&table, bow, EquipPart::MainHand, Facing::SouthEast, "small");
    REQUIRE(bow_hand.sprite);
    CHECK(bow_hand.sprite->image.w == 32);
    const EquipArtChoice bow_away = equip_art_choice(&table, bow, EquipPart::MainHand, Facing::NorthEast, "medium");
    CHECK(bow_away.sprite == nullptr);
    const std::vector<EquipQuad> still = equip_quads(bow, EquipPart::MainHand, attach_points(shared_body_attach(), Facing::SouthEast).main_hand);
    CHECK(still.size() == 2);
    CHECK(still[0].color.r < 0.7f);
    fs::remove_all(art, ec);
}

namespace {
const EquipLayer* layer_part(const std::vector<EquipLayer>& ls, uint32_t id, EquipPart part) {
    for (const EquipLayer& l : ls)
        if (l.item && l.item->item == id && l.part == part) return &l;
    return nullptr;
}
bool layer_has(const std::vector<EquipLayer>& ls, uint32_t id) {
    for (const EquipLayer& l : ls)
        if (l.item && l.item->item == id) return true;
    return false;
}
uint32_t back_winner(const ActorView& a) {
    for (const EquipLayer& l : equip_layers(a))
        if (l.part == EquipPart::Back && l.item) return l.item->item;
    return 0;
}
EquipVisual gear(uint32_t id, AttachSlot slot, EquipKind kind, const char* cid) {
    return equip_scene::item(id, slot, kind, cid);
}
} // namespace

TEST_CASE("equip attach: per-body table, shared fallback, west mirrors x") {
    const fs::path root = fs::temp_directory_path() / ("isoforge_attach_" + run_suffix());
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path wolf = root / "srd" / "wolf" / "medium";
    const fs::path goblin = root / "srd" / "goblin" / "medium";
    const fs::path bin = root / "frames" / "medium";
    fs::create_directories(wolf);
    fs::create_directories(goblin);
    fs::create_directories(bin);
    std::ofstream(wolf / "attach.json") << R"({
        "se": {"main": [3, 5], "off": [-4, 6], "back": [-1, 2], "hip-main": [8, 9], "hip-off": [-2, 9], "neck": [1, 2]},
        "ne": {"main": [4, 1], "back": [5, 3], "hip-off": [-3, 7]}
    })";
    std::ofstream(bin / "attach.json") << R"({"se": {"main": [20, 1], "neck": [0, 6]}})";
    std::ofstream(goblin / "attach.json") << "{not json";

    const BodyAttach wolf_body = load_body_attach(root, "srd51.monster.wolf", "medium");
    CHECK(wolf_body.loaded);
    const AttachPoints se = attach_points(wolf_body, Facing::SouthEast);
    CHECK(se.main_hand.x == 3.f);
    CHECK(se.main_hand.y == 5.f);
    CHECK(se.off_hand.x == -4.f);
    CHECK(se.off_hand.y == 6.f);
    CHECK(se.back.x == -1.f);
    CHECK(se.hip_main.x == 8.f);
    CHECK(se.hip_main.y == 9.f);
    CHECK(se.hip_off.x == -2.f);
    CHECK(se.neck.x == 1.f);
    CHECK(se.neck.y == 2.f);
    // A missing ne key keeps the shared point, not the custom se point.
    const AttachPoints ne = attach_points(wolf_body, Facing::NorthEast);
    CHECK(ne.main_hand.x == 4.f);
    CHECK(ne.main_hand.y == 1.f);
    CHECK(ne.off_hand.x == -9.f);
    CHECK(ne.back.x == 5.f);
    CHECK(ne.back.y == 3.f);
    CHECK(ne.hip_main.x == 6.f);
    CHECK(ne.hip_off.x == -3.f);
    CHECK(ne.hip_off.y == 7.f);
    CHECK(ne.neck.y == 2.f);
    const AttachPoints sw = attach_points(wolf_body, Facing::SouthWest);
    const AttachPoints nw = attach_points(wolf_body, Facing::NorthWest);
    CHECK(sw.main_hand.x == -se.main_hand.x);
    CHECK(sw.off_hand.x == -se.off_hand.x);
    CHECK(sw.back.x == -se.back.x);
    CHECK(sw.hip_main.x == -se.hip_main.x);
    CHECK(sw.hip_off.x == -se.hip_off.x);
    CHECK(sw.neck.x == -se.neck.x);
    CHECK(sw.main_hand.y == se.main_hand.y);
    CHECK(nw.main_hand.x == -ne.main_hand.x);
    CHECK(nw.back.x == -ne.back.x);
    CHECK(nw.hip_off.x == -ne.hip_off.x);
    CHECK(nw.hip_main.x == -ne.hip_main.x);

    // No per-monster file: the bin kit, then shared fill for the keys it omits.
    const BodyAttach bear = load_body_attach(root, "srd51.monster.brown_bear", "medium");
    CHECK(bear.loaded);
    CHECK(attach_points(bear, Facing::SouthEast).main_hand.x == 20.f);
    CHECK(attach_points(bear, Facing::SouthEast).off_hand.x == -9.f);
    CHECK(attach_points(bear, Facing::SouthEast).neck.y == 6.f);
    CHECK(attach_points(bear, Facing::SouthWest).main_hand.x == -20.f);

    // A broken per-monster file falls through to the bin kit.
    const BodyAttach broken = load_body_attach(root, "srd51.monster.goblin", "medium");
    CHECK(broken.loaded);
    CHECK(attach_points(broken, Facing::SouthEast).main_hand.x == 20.f);

    // Per-monster wins over the bin kit: wolf did not pick up main [20, 1].
    CHECK(se.main_hand.x == 3.f);

    CHECK_FALSE(load_body_attach(root, "srd51.monster.wolf", "huge").loaded);
    CHECK(attach_points(load_body_attach(root, "srd51.monster.wolf", "nope"), Facing::SouthEast).main_hand.x == 9.f);
    const BodyAttach none = load_body_attach(root / "missing", "srd51.monster.wolf", "medium");
    CHECK_FALSE(none.loaded);
    CHECK(attach_points(none, Facing::SouthEast).hip_main.x == 6.f);
    CHECK(attach_points(none, Facing::SouthEast).hip_off.x == -6.f);

    const fs::path shipped{SLICE_STANDIN_ROOT};
    CHECK(fs::is_directory(shipped / "srd" / "green-hag" / "medium"));
    CHECK_FALSE(fs::is_regular_file(shipped / "srd" / "green-hag" / "medium" / "attach.json"));
    const BodyAttach hag = load_body_attach(shipped, "srd51.monster.green_hag", "medium");
    CHECK_FALSE(hag.loaded);
    CHECK(attach_points(hag, Facing::SouthEast).back.x == -7.f);
    CHECK(attach_points(hag, Facing::SouthWest).hip_main.x == -6.f);
    fs::remove_all(root, ec);
}

TEST_CASE("equip attach: paperdoll uses the humanoid table; a medium stand-in does not") {
    const fs::path shipped{SLICE_STANDIN_ROOT};
    CHECK_FALSE(fs::is_regular_file(shipped / "frames" / "medium" / "attach.json"));
    ActorView pc;
    pc.visual_kind = "paperdoll";
    pc.standin_bin = "medium";
    const std::string bin = pc.standin_bin.empty() ? std::string("medium") : pc.standin_bin;
    const BodyAttach doll = load_body_attach(shipped, pc.content_id, bin, pc.visual_kind);
    CHECK(doll.loaded);
    const AttachPoints se = attach_points(doll, Facing::SouthEast);
    CHECK(se.main_hand.x == 4.f);
    CHECK(se.main_hand.y == 3.f);
    CHECK(se.off_hand.x == -4.f);
    CHECK(se.back.x == -2.f);
    CHECK(se.back.y == -9.f);
    CHECK(se.hip_main.x == 1.f);
    CHECK(se.hip_main.y == -3.f);
    CHECK(se.hip_off.x == -5.f);
    CHECK(se.neck.x == 1.f);
    CHECK(se.neck.y == -11.f);
    const AttachPoints ne = attach_points(doll, Facing::NorthEast);
    CHECK(ne.hip_off.x == -6.f);
    CHECK(ne.hip_off.y == -3.f);
    CHECK(ne.hip_main.x == 4.f);
    CHECK(ne.back.x == -1.f);
    CHECK(ne.back.y == -9.f);
    CHECK(ne.neck.x == 0.f);   // no ne neck in the file; the shared point remains
    CHECK(ne.neck.y == 2.f);
    pc.facing = Facing::SouthEast;
    pc.equipment = {equip_scene::item(4, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet")};
    const std::vector<EquipLayer> layers = equip_layers(pc, doll);
    const EquipLayer* neck = nullptr;
    for (const EquipLayer& l : layers)
        if (l.part == EquipPart::Neck) neck = &l;
    REQUIRE(neck);
    CHECK(neck->z == kZWornFront);
    CHECK(neck->at.x == 1.f);
    CHECK(neck->at.y == -11.f);

    ActorView hag;
    hag.visual_kind = "standin";
    hag.content_id = "srd51.monster.green_hag";
    hag.standin_bin = "medium";
    const BodyAttach standin = load_body_attach(shipped, hag.content_id, hag.standin_bin, hag.visual_kind);
    CHECK_FALSE(standin.loaded);
    CHECK(attach_points(standin, Facing::SouthEast).neck.y == 2.f);
    CHECK(attach_points(standin, Facing::SouthEast).main_hand.x == 9.f);
    CHECK(attach_points(standin, Facing::NorthEast).hip_off.x == -6.f);
    CHECK(attach_points(standin, Facing::NorthEast).hip_off.y == 8.f);

    const fs::path tree = fs::temp_directory_path() / ("isoforge_paperdoll_vs_blob_" + run_suffix());
    std::error_code ec;
    fs::remove_all(tree, ec);
    const fs::path standins = tree / "monster_standins";
    fs::create_directories(standins / "frames" / "medium");
    fs::create_directories(tree / "paperdoll" / "medium");
    std::ofstream(standins / "frames" / "medium" / "attach.json") << R"({"se":{"main":[30,1]}})";
    std::ofstream(tree / "paperdoll" / "medium" / "attach.json")
        << R"({"se":{"neck":[1,-11]},"ne":{"hip-off":[-6,-3]}})";
    const BodyAttach blob = load_body_attach(standins, "srd51.monster.green_hag", "medium", "standin");
    CHECK(blob.loaded);
    CHECK(attach_points(blob, Facing::SouthEast).main_hand.x == 30.f);
    CHECK(attach_points(blob, Facing::SouthEast).neck.y == 2.f);
    const BodyAttach human = load_body_attach(standins, "srd51.monster.green_hag", "medium", "paperdoll");
    CHECK(human.loaded);
    CHECK(attach_points(human, Facing::SouthEast).neck.y == -11.f);
    CHECK(attach_points(human, Facing::NorthEast).hip_off.x == -6.f);
    CHECK(attach_points(human, Facing::SouthEast).main_hand.x == 9.f);
    fs::remove_all(tree, ec);
}

TEST_CASE("equip hips: sheath, two-weapon fighting, pouch, and hidden extras") {
    const BodyAttach body = shared_body_attach();
    const AttachPoints se = attach_points(body, Facing::SouthEast);
    ActorView a;
    a.facing = Facing::SouthEast;
    a.equipment = {gear(2, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword"),
                   gear(5, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch")};
    {
        const auto ls = equip_layers(a, body);
        const EquipLayer* sheath = layer_part(ls, 2, EquipPart::HipMain);
        const EquipLayer* pouch = layer_part(ls, 5, EquipPart::HipOff);
        REQUIRE(sheath);
        REQUIRE(pouch);
        CHECK(sheath->at.x == se.hip_main.x);
        CHECK(pouch->at.x == se.hip_off.x);
        CHECK(sheath->z == kZWornFront);
        CHECK(pouch->z == kZWornFront);
        CHECK(equip_quads(*sheath->item, sheath->part, sheath->at).empty());
        CHECK(equip_quads(*pouch->item, pouch->part, pouch->at).size() == 1);
    }
    // Two weapons in hand: both sheaths, the pouch is hidden.
    a.equipment.push_back(gear(3, AttachSlot::OffHand, EquipKind::Weapon, "srd51.item.dagger"));
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 2, EquipPart::HipMain));
        REQUIRE(layer_part(ls, 3, EquipPart::HipOff));
        CHECK(layer_part(ls, 3, EquipPart::HipOff)->at.x == se.hip_off.x);
        CHECK_FALSE(layer_has(ls, 5));
        CHECK(layer_part(ls, 2, EquipPart::MainHand));
        CHECK(layer_part(ls, 3, EquipPart::OffHand));
    }
    // No weapon in either hand: the pouch takes hip-main. An off-hand-only weapon is not a sheath.
    a.equipment = {gear(3, AttachSlot::OffHand, EquipKind::Weapon, "srd51.item.dagger"),
                   gear(5, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch")};
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 5, EquipPart::HipMain));
        CHECK(layer_part(ls, 5, EquipPart::HipMain)->at.x == se.hip_main.x);
        CHECK(layer_part(ls, 3, EquipPart::HipOff) == nullptr);
        CHECK(layer_part(ls, 3, EquipPart::OffHand));
    }
    // Component pouch is preferred over the belt pouch. The belt pouch and any other hip item are hidden.
    // Two component pouches: the lower instance id wins.
    a.equipment = {gear(9, AttachSlot::Hip, EquipKind::Other, "srd51.item.component_pouch"),
                   gear(2, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch"),
                   gear(4, AttachSlot::Hip, EquipKind::Other, "srd51.item.rope")};
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 9, EquipPart::HipMain));
        CHECK_FALSE(layer_has(ls, 2));
        CHECK_FALSE(layer_has(ls, 4));
    }
    a.equipment = {gear(8, AttachSlot::Hip, EquipKind::Other, "srd51.item.component_pouch"),
                   gear(3, AttachSlot::Hip, EquipKind::Other, "srd51.item.component_pouch")};
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 3, EquipPart::HipMain));
        CHECK_FALSE(layer_has(ls, 8));
    }
    // No component pouch: the belt pouch is the pouch, and a lower id wins a tie.
    a.equipment = {gear(7, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch"),
                   gear(6, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch"),
                   gear(1, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch")};
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 6, EquipPart::HipMain));   // no sheath
        CHECK_FALSE(layer_has(ls, 7));
    }
    // One sheath and two pouches: component pouch on hip-off, belt hidden.
    a.equipment = {gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.mace"),
                   gear(11, AttachSlot::Hip, EquipKind::Other, "srd51.item.pouch"),
                   gear(10, AttachSlot::Hip, EquipKind::Other, "srd51.item.component_pouch")};
    {
        const auto ls = equip_layers(a, body);
        REQUIRE(layer_part(ls, 1, EquipPart::HipMain));
        REQUIRE(layer_part(ls, 10, EquipPart::HipOff));
        CHECK_FALSE(layer_has(ls, 11));
    }
    // Both hips use the hip z rule on every facing. West mirrors x.
    a.equipment = {gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword"),
                   gear(2, AttachSlot::OffHand, EquipKind::Weapon, "srd51.item.dagger")};
    for (Facing f : {Facing::SouthEast, Facing::SouthWest, Facing::NorthEast, Facing::NorthWest}) {
        a.facing = f;
        const auto ls = equip_layers(a, body);
        const EquipLayer* main = layer_part(ls, 1, EquipPart::HipMain);
        const EquipLayer* off = layer_part(ls, 2, EquipPart::HipOff);
        REQUIRE(main);
        REQUIRE(off);
        CHECK(main->z == worn_z(AttachSlot::Hip, f));
        CHECK(off->z == worn_z(AttachSlot::Hip, f));
        const AttachPoints ap = attach_points(body, f);
        CHECK(main->at.x == ap.hip_main.x);
        CHECK(off->at.x == ap.hip_off.x);
        CHECK(main->z == (facing_shows_back(f) ? kZBack : kZWornFront));
    }
}

TEST_CASE("equip back: one item, fixed priority, lower instance id wins a tie") {
    CHECK(container_back_role("srd51.item.quiver") == BackRole::Quiver);
    CHECK(container_back_role("srd51.item.case_crossbow_bolt") == BackRole::BoltCase);
    CHECK(container_back_role("srd51.item.backpack") == BackRole::Pack);
    CHECK(container_back_role("srd51.item.shield") == BackRole::None);
    CHECK(container_back_role("srd51.item.longsword") == BackRole::None);

    ActorView a;
    auto bow = gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longbow");
    bow.hands = 2;
    auto quiver_hi = gear(10, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver");
    auto quiver_lo = gear(4, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver");
    auto bolts = gear(3, AttachSlot::Back, EquipKind::Other, "srd51.item.case_crossbow_bolt");
    auto shield = gear(2, AttachSlot::Back, EquipKind::Shield, "srd51.item.shield");
    auto held_shield = gear(2, AttachSlot::OffHand, EquipKind::Shield, "srd51.item.shield");
    auto great = gear(8, AttachSlot::Back, EquipKind::Weapon, "srd51.item.greatsword");
    great.hands = 2;
    auto versatile = gear(5, AttachSlot::Back, EquipKind::Weapon, "srd51.item.longsword");
    versatile.versatile = true;
    auto pack = gear(9, AttachSlot::Back, EquipKind::Other, "srd51.item.backpack");
    auto dagger = gear(7, AttachSlot::Back, EquipKind::Weapon, "srd51.item.dagger");
    auto rope = gear(6, AttachSlot::Back, EquipKind::Other, "srd51.item.rope");

    a.equipment = {bow, quiver_hi, bolts, shield, great, pack};
    CHECK(back_winner(a) == 10);   // 1. quiver matching the wielded bow, ahead of a lower-id shield

    a.equipment = {bow, quiver_hi, quiver_lo};
    CHECK(back_winner(a) == 4);    // tie inside rank 1: lower instance id

    auto xbow = gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.light_crossbow");
    xbow.hands = 2;
    a.equipment = {xbow, quiver_lo, bolts};
    CHECK(back_winner(a) == 3);    // crossbow matches the bolt case, not the quiver

    auto stowed_bow = gear(12, AttachSlot::Back, EquipKind::Weapon, "srd51.item.longbow");
    stowed_bow.hands = 2;
    a.equipment = {stowed_bow, quiver_lo, bolts, shield, great, pack};
    CHECK(back_winner(a) == 2);    // 2. stowed shield. A bow on the back is not wielded, so the quiver stays rank 5.

    a.equipment = {held_shield, pack, dagger};
    CHECK(back_winner(a) == 9);    // a shield in the hand is not stowed
    CHECK(layer_part(equip_layers(a), 2, EquipPart::OffHand));

    a.equipment = {great, versatile, pack};
    CHECK(back_winner(a) == 5);    // 3. two-handed and versatile tie; lower id is the versatile sword

    a.equipment = {pack, quiver_lo, bolts, dagger};
    CHECK(back_winner(a) == 9);    // 4. backpack ahead of a quiver that matches nothing

    a.equipment = {quiver_hi, bolts, dagger, rope};
    CHECK(back_winner(a) == 3);    // 5. other quiver / bolt case, lower id, ahead of other gear

    a.equipment = {dagger, rope};
    CHECK(back_winner(a) == 6);    // 6. other back gear, lower id

    // The loser is not drawn.
    a.equipment = {bow, quiver_lo, pack, shield};
    const auto ls = equip_layers(a);
    CHECK(layer_part(ls, 4, EquipPart::Back));
    CHECK_FALSE(layer_has(ls, 9));
    CHECK_FALSE(layer_has(ls, 2));
}

TEST_CASE("equip neck: visible on toward facings, hidden on away, z5") {
    ActorView a;
    a.equipment = {gear(4, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet"),
                   gear(8, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet"),
                   gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.mace")};
    for (Facing f : {Facing::SouthEast, Facing::SouthWest}) {
        a.facing = f;
        const auto ls = equip_layers(a);
        const EquipLayer* neck = layer_part(ls, 4, EquipPart::Neck);
        REQUIRE(neck);
        CHECK_FALSE(layer_has(ls, 8));   // tie: lower instance id
        CHECK(neck->z == kZWornFront);
        CHECK(neck->z < kZFarHand);
        CHECK(neck->z > kZBody);
        const AttachPoints ap = attach_points(shared_body_attach(), f);
        CHECK(neck->at.x == ap.neck.x);
        CHECK(neck->at.y == ap.neck.y);
        CHECK(equip_quads(*neck->item, EquipPart::Neck, neck->at).size() == 1);
    }
    for (Facing f : {Facing::NorthEast, Facing::NorthWest}) {
        a.facing = f;
        const auto ls = equip_layers(a);
        CHECK(layer_part(ls, 4, EquipPart::Neck) == nullptr);
        CHECK_FALSE(layer_has(ls, 8));
        CHECK(layer_part(ls, 1, EquipPart::MainHand));
    }

    const fs::path art = fs::temp_directory_path() / ("isoforge_equip_neck_" + run_suffix());
    std::error_code ec;
    fs::remove_all(art, ec);
    fs::create_directories(art / "srd51.item.holy_symbol_amulet");
    fs::create_directories(art / "srd51.item.longsword");
    const fs::path png{SLICE_STANDIN_ROOT};
    fs::copy_file(png / "frames/tiny/se/mon_tiny_se_idle_00.png", art / "srd51.item.holy_symbol_amulet" / "neck.se.png");
    fs::copy_file(png / "frames/small/ne/mon_small_ne_idle_00.png", art / "srd51.item.holy_symbol_amulet" / "neck.ne.png");
    fs::copy_file(png / "frames/tiny/se/mon_tiny_se_idle_01.png", art / "srd51.item.longsword" / "hip.se.png");
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(art);
    const EquipVisual amulet = gear(4, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet");
    const EquipVisual sword = gear(1, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword");
    const EquipArtChoice se = equip_art_choice(&table, amulet, EquipPart::Neck, Facing::SouthEast, "medium");
    const EquipArtChoice sw = equip_art_choice(&table, amulet, EquipPart::Neck, Facing::SouthWest, "medium");
    const EquipArtChoice ne = equip_art_choice(&table, amulet, EquipPart::Neck, Facing::NorthEast, "medium");
    const EquipArtChoice nw = equip_art_choice(&table, amulet, EquipPart::Neck, Facing::NorthWest, "medium");
    CHECK(table.size() == 2);   // neck.ne is not an entry
    eng::presentation::EquipArtQuery neck_q;
    neck_q.content_id = amulet.content_id;
    neck_q.kind = "other";
    neck_q.part = eng::presentation::EquipArtPart::Neck;
    neck_q.view = eng::presentation::EquipArtView::Away;
    CHECK(table.find(neck_q) == nullptr);
    neck_q.view = eng::presentation::EquipArtView::Toward;
    CHECK(table.find(neck_q) != nullptr);
    REQUIRE(se.sprite);
    CHECK_FALSE(se.mirror_x);
    CHECK(sw.sprite == se.sprite);
    CHECK(sw.mirror_x);
    CHECK(ne.sprite == nullptr);
    CHECK(nw.sprite == nullptr);
    CHECK(se.sprite->image.w == 16);
    const EquipArtChoice hip = equip_art_choice(&table, sword, EquipPart::HipMain, Facing::SouthEast, "");
    const EquipArtChoice hip_off = equip_art_choice(&table, sword, EquipPart::HipOff, Facing::SouthEast, "");
    REQUIRE(hip.sprite);
    CHECK(hip_off.sprite == hip.sprite);   // one hip file; the anchor is chosen here, not in the name
    fs::remove_all(art, ec);
}

TEST_CASE("equip draw order: unmapped items on a default body match main") {
    // equip_scene on main: one back item per actor, no pouch, no neck. Sheaths add no quads, so the
    // sprites submitted are the same list main drew. The golden hash below is that frame.
    const ViewModel vm = equip_scene::build(true);
    struct Drawn { EquipPart part; int32_t z; uint32_t item; float x, y; size_t quads; };
    auto drawn = [](const ActorView& a) {
        std::vector<Drawn> out;
        for (const EquipLayer& l : equip_layers(a)) {
            if (!l.item) continue;
            const size_t n = equip_quads(*l.item, l.part, l.at).size();
            if (n == 0) continue;
            out.push_back({l.part, l.z, l.item->item, l.at.x, l.at.y, n});
        }
        return out;
    };
    const AttachPoints se = attach_points(shared_body_attach(), Facing::SouthEast);
    const AttachPoints sw = attach_points(shared_body_attach(), Facing::SouthWest);
    const std::vector<Drawn> fighter = {
        {EquipPart::Back, kZBack, 12, se.back.x, se.back.y, 1},
        {EquipPart::OffHand, kZFarHand, 11, se.off_hand.x, se.off_hand.y, 2},
        {EquipPart::MainHand, kZNearHand, 10, se.main_hand.x, se.main_hand.y, 2},
    };
    const std::vector<Drawn> cleric = {
        {EquipPart::Back, kZBack, 21, sw.back.x, sw.back.y, 1},
        {EquipPart::MainHand, kZFarHand, 20, sw.main_hand.x, sw.main_hand.y, 2},
        {EquipPart::HeldLight, kZLight, 22, sw.off_hand.x, sw.off_hand.y, 2},
    };
    const std::vector<Drawn> rogue = {
        {EquipPart::OffHand, kZFarHand, 31, se.off_hand.x, se.off_hand.y, 2},
        {EquipPart::HeldLight, kZLight, 30, se.main_hand.x, se.main_hand.y, 2},
    };
    auto same = [](const std::vector<Drawn>& got, const std::vector<Drawn>& exp) {
        if (got.size() != exp.size()) return false;
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i].part != exp[i].part || got[i].z != exp[i].z || got[i].item != exp[i].item) return false;
            if (got[i].x != exp[i].x || got[i].y != exp[i].y || got[i].quads != exp[i].quads) return false;
        }
        return true;
    };
    CHECK(same(drawn(vm.actors[0]), fighter));
    CHECK(same(drawn(vm.actors[1]), cleric));
    CHECK(same(drawn(vm.actors[2]), rogue));

    ActorView hag = vm.actors[0];
    hag.content_id = "srd51.monster.green_hag";
    hag.standin_bin = "medium";
    hag.visual_kind = "standin";
    const BodyAttach hag_body = load_body_attach(fs::path(SLICE_STANDIN_ROOT), hag.content_id, hag.standin_bin);
    CHECK_FALSE(hag_body.loaded);
    CHECK(same(drawn(hag), fighter));
    const auto hag_layers = equip_layers(hag, hag_body);
    const auto pawn_layers = equip_layers(vm.actors[0]);
    REQUIRE(hag_layers.size() == pawn_layers.size());
    for (size_t i = 0; i < hag_layers.size(); ++i) {
        CHECK(hag_layers[i].part == pawn_layers[i].part);
        CHECK(hag_layers[i].z == pawn_layers[i].z);
        CHECK(hag_layers[i].at.x == pawn_layers[i].at.x);
        CHECK(hag_layers[i].at.y == pawn_layers[i].at.y);
    }
    CHECK(render_frame(vm).hash == 0xcfd377e872395730ull);
}

#ifndef SLICE_EQUIP_ART_ROOT
#define SLICE_EQUIP_ART_ROOT ""
#endif
#ifndef SLICE_H4_EQUIP_FIXTURE
#define SLICE_H4_EQUIP_FIXTURE ""
#endif

namespace {
fs::path write_empty_equip_kit(const std::string& name) {
    const fs::path kit = fs::temp_directory_path() / (name + "_" + run_suffix());
    std::error_code ec;
    fs::remove_all(kit, ec);
    fs::create_directories(kit);
    std::ofstream(kit / "kit.json") << R"({"$schema":"equip_kit@1","cell":[24,24],"items":[]})";
    return kit;
}

eng::presentation::EquipArtPart art_part_of(std::string_view name) {
    if (name == "back") return eng::presentation::EquipArtPart::Back;
    if (name == "hip") return eng::presentation::EquipArtPart::Hip;
    if (name == "neck") return eng::presentation::EquipArtPart::Neck;
    if (name == "off_hand") return eng::presentation::EquipArtPart::OffHand;
    if (name == "held_light") return eng::presentation::EquipArtPart::HeldLight;
    return eng::presentation::EquipArtPart::MainHand;
}
} // namespace

TEST_CASE("shipped equipment kit: loads, and every sprite it declares resolves") {
    const fs::path root{SLICE_EQUIP_ART_ROOT};
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(root);
    std::ifstream in(root / "kit.json");
    REQUIRE(in);
    const assets::Json doc = assets::Json::parse(in);
    CHECK(doc.at("$schema") == "equip_kit@1");
    CHECK(doc.at("cell") == assets::Json::array({24, 24}));
    const assets::Json items = doc.value("items", assets::Json::array());
    REQUIRE(items.is_array());
    for (const assets::Json& item : items) {
        const std::string cid = item.at("content_id").get<std::string>();
        if (item.contains("back_role")) {
            const auto role = table.back_role(cid);
            REQUIRE(role);
            CHECK(std::string(*role) == item.at("back_role").get<std::string>());
        } else {
            CHECK_FALSE(table.back_role(cid));
        }
        for (const assets::Json& part : item.at("parts")) {
            const std::string part_name = part.at("part").get<std::string>();
            for (const assets::Json& view : part.at("views")) {
                const std::string view_name = view.at("view").get<std::string>();
                const fs::path png = root / cid / (part_name + "." + view_name + ".png");
                if (!fs::is_regular_file(png)) continue;
                eng::presentation::EquipArtQuery q;
                q.content_id = cid;
                q.part = art_part_of(part_name);
                q.view = view_name == "ne" ? eng::presentation::EquipArtView::Away
                                           : eng::presentation::EquipArtView::Toward;
                const eng::presentation::EquipArtSprite* sprite = table.find(q);
                REQUIRE(sprite);
                if (view.contains("pivot")) {
                    CHECK(sprite->pivot_x == view.at("pivot").at(0).get<int>());
                    CHECK(sprite->pivot_y == view.at("pivot").at(1).get<int>());
                }
            }
        }
    }
    const EquipVisual other = equip_scene::item(99, AttachSlot::Back, EquipKind::Other, "srd51.item.no_such_pack");
    CHECK(equip_art_choice(&table, other, EquipPart::Back, Facing::SouthEast, "medium").sprite == nullptr);
    const std::vector<EquipQuad> bag = equip_quads(other, EquipPart::Back, {0.f, 0.f});
    CHECK(bag.size() == 1);
    CHECK(bag[0].size.x == 14.f);
    CHECK(bag[0].size.y == 16.f);
}

TEST_CASE("equip back: a manifest back_role drives the pick, and a missing field keeps the content id") {
    const fs::path empty_dir = write_empty_equip_kit("isoforge_empty_back_role");
    const eng::presentation::EquipArtTable empty = eng::presentation::EquipArtTable::load(empty_dir);
    CHECK(empty.size() == 0);
    CHECK_FALSE(empty.back_role("srd51.item.quiver"));
    CHECK_FALSE(empty.back_role("srd51.item.backpack"));
    CHECK_FALSE(empty.back_role("srd51.item.case_crossbow_bolt"));
    CHECK(container_back_role("srd51.item.quiver", &empty) == BackRole::Quiver);
    CHECK(container_back_role("srd51.item.case_crossbow_bolt", &empty) == BackRole::BoltCase);
    CHECK(container_back_role("srd51.item.backpack", &empty) == BackRole::Pack);

    const fs::path kit = fs::temp_directory_path() / ("isoforge_back_role_" + run_suffix());
    std::error_code ec;
    fs::remove_all(kit, ec);
    fs::create_directories(kit);
    {
        std::ofstream out(kit / "kit.json");
        out << R"({
            "$schema": "equip_kit@1",
            "cell": [24, 24],
            "items": [
                {"content_id": "srd51.item.rope", "back_role": "pack",
                 "parts": [{"part": "back", "views": [{"view": "se", "pivot": [12, 20]}]}]},
                {"content_id": "srd51.item.quiver", "back_role": "other",
                 "parts": [{"part": "back", "views": [{"view": "se", "pivot": [12, 20]}]}]}
            ]
        })";
    }
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(kit);
    CHECK(table.size() == 0);
    CHECK(container_back_role("srd51.item.rope", &table) == BackRole::Pack);
    CHECK(container_back_role("srd51.item.quiver", &table) == BackRole::Other);
    CHECK(container_back_role("srd51.item.backpack", &table) == BackRole::Pack);

    ActorView a;
    a.equipment = {gear(1, AttachSlot::Back, EquipKind::Other, "srd51.item.rope"),
                   gear(2, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver")};
    CHECK(back_winner(a) == 2);   // no manifest: the quiver outranks other gear
    uint32_t won = 0;
    for (const EquipLayer& l : equip_layers(a, shared_body_attach(), &table))
        if (l.part == EquipPart::Back && l.item) won = l.item->item;
    CHECK(won == 1);   // rope is a pack; the quiver's manifest role is other
    fs::remove_all(kit, ec);
    fs::remove_all(empty_dir, ec);
}

TEST_CASE("equip art: missing H4 parts fall back to quads, and the neck quad shows only toward") {
    const fs::path empty_dir = write_empty_equip_kit("isoforge_empty_h4");
    const eng::presentation::EquipArtTable shipped = eng::presentation::EquipArtTable::load(empty_dir);
    const EquipVisual quiver = gear(6, AttachSlot::Back, EquipKind::Other, "srd51.item.quiver");
    const EquipVisual bolts = gear(7, AttachSlot::Back, EquipKind::Other, "srd51.item.case_crossbow_bolt");
    const EquipVisual pack = gear(4, AttachSlot::Back, EquipKind::Other, "srd51.item.backpack");
    const EquipVisual amulet = gear(8, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet");
    const EquipVisual pouch = gear(5, AttachSlot::Hip, EquipKind::Other, "srd51.item.component_pouch");
    const EquipVisual shield = gear(3, AttachSlot::Back, EquipKind::Shield, "srd51.item.shield");
    auto missing = [&](const EquipVisual& item, EquipPart part) {
        const EquipArtChoice choice = equip_art_choice(&shipped, item, part, Facing::SouthEast, "medium");
        CHECK(choice.sprite == nullptr);
        const EquipArtChoice away = equip_art_choice(&shipped, item, part, Facing::NorthEast, "medium");
        CHECK(away.sprite == nullptr);
        const std::vector<EquipQuad> quads = equip_quads(item, part, {0.f, 0.f});
        CHECK_FALSE(quads.empty());
    };
    missing(quiver, EquipPart::Back);
    missing(bolts, EquipPart::Back);
    missing(pack, EquipPart::Back);
    missing(amulet, EquipPart::Neck);
    missing(pouch, EquipPart::HipMain);
    missing(shield, EquipPart::Back);

    ActorView a;
    a.facing = Facing::SouthEast;
    a.equipment = {amulet};
    const std::vector<EquipLayer> toward = equip_layers(a, shared_body_attach(), &shipped);
    const EquipLayer* neck = layer_part(toward, 8, EquipPart::Neck);
    REQUIRE(neck);
    CHECK(neck->z == kZWornFront);
    const AttachPoints se = attach_points(shared_body_attach(), Facing::SouthEast);
    CHECK(neck->at.x == se.neck.x);
    CHECK(neck->at.y == se.neck.y);
    CHECK(equip_quads(*neck->item, neck->part, neck->at).size() == 1);
    a.facing = Facing::SouthWest;
    const std::vector<EquipLayer> west = equip_layers(a, shared_body_attach(), &shipped);
    REQUIRE(layer_part(west, 8, EquipPart::Neck));
    a.facing = Facing::NorthEast;
    const std::vector<EquipLayer> away_e = equip_layers(a, shared_body_attach(), &shipped);
    CHECK(layer_part(away_e, 8, EquipPart::Neck) == nullptr);
    a.facing = Facing::NorthWest;
    const std::vector<EquipLayer> away_w = equip_layers(a, shared_body_attach(), &shipped);
    CHECK(layer_part(away_w, 8, EquipPart::Neck) == nullptr);

    const fs::path kit = fs::temp_directory_path() / ("isoforge_h4_gap_" + run_suffix());
    std::error_code ec;
    fs::remove_all(kit, ec);
    fs::create_directories(kit);
    {
        std::ofstream out(kit / "kit.json");
        out << R"({
            "$schema": "equip_kit@1",
            "cell": [24, 24],
            "items": [
                {"content_id": "srd51.item.quiver", "back_role": "quiver",
                 "parts": [{"part": "back", "views": [{"view": "se", "pivot": [12, 20]}, {"view": "ne", "pivot": [12, 20]}]}]},
                {"content_id": "srd51.item.holy_symbol_amulet",
                 "parts": [{"part": "neck", "views": [{"view": "se", "pivot": [12, 20]}]}]},
                {"content_id": "srd51.item.component_pouch",
                 "parts": [{"part": "hip", "views": [{"view": "se", "pivot": [12, 20]}]}]},
                {"content_id": "srd51.item.shield",
                 "parts": [{"part": "back", "views": [{"view": "se", "pivot": [12, 20]}]}]}
            ]
        })";
    }
    const eng::presentation::EquipArtTable named = eng::presentation::EquipArtTable::load(kit);
    CHECK(named.size() == 0);
    CHECK(equip_art_choice(&named, quiver, EquipPart::Back, Facing::SouthEast, "medium").sprite == nullptr);
    CHECK(equip_art_choice(&named, amulet, EquipPart::Neck, Facing::SouthEast, "").sprite == nullptr);
    CHECK(equip_art_choice(&named, pouch, EquipPart::HipOff, Facing::SouthEast, "").sprite == nullptr);
    CHECK(equip_quads(quiver, EquipPart::Back, se.back).size() == 1);
    CHECK(equip_quads(amulet, EquipPart::Neck, se.neck).size() == 1);
    CHECK(equip_quads(pouch, EquipPart::HipMain, se.hip_main).size() == 1);
    fs::remove_all(kit, ec);
    fs::remove_all(empty_dir, ec);
}

TEST_CASE("equip art: the H4 fixture kit loads all 13 sprites at pivot [12, 12]") {
    const fs::path root{SLICE_H4_EQUIP_FIXTURE};
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(root);
    CHECK(table.size() == 13);
    struct Row { const char* id; const char* part; const char* view; const char* role; };
    const Row rows[] = {
        {"srd51.item.quiver", "back", "se", "quiver"},
        {"srd51.item.quiver", "back", "ne", "quiver"},
        {"srd51.item.case_crossbow_bolt", "back", "se", "bolt_case"},
        {"srd51.item.case_crossbow_bolt", "back", "ne", "bolt_case"},
        {"srd51.item.backpack", "back", "se", "pack"},
        {"srd51.item.backpack", "back", "ne", "pack"},
        {"srd51.item.shield", "back", "se", nullptr},
        {"srd51.item.shield", "back", "ne", nullptr},
        {"srd51.item.pouch", "hip", "se", nullptr},
        {"srd51.item.pouch", "hip", "ne", nullptr},
        {"srd51.item.component_pouch", "hip", "se", nullptr},
        {"srd51.item.component_pouch", "hip", "ne", nullptr},
        {"srd51.item.holy_symbol_amulet", "neck", "se", nullptr},
    };
    int seen = 0;
    for (const Row& row : rows) {
        eng::presentation::EquipArtQuery q;
        q.content_id = row.id;
        q.part = art_part_of(row.part);
        q.view = std::string_view(row.view) == "ne" ? eng::presentation::EquipArtView::Away
                                                     : eng::presentation::EquipArtView::Toward;
        const eng::presentation::EquipArtSprite* sprite = table.find(q);
        REQUIRE(sprite);
        CHECK(sprite->pivot_x == 12);
        CHECK(sprite->pivot_y == 12);
        CHECK(sprite->image.w == 24);
        CHECK(sprite->image.h == 24);
        ++seen;
        if (row.role) {
            const auto role = table.back_role(row.id);
            REQUIRE(role);
            CHECK(std::string(*role) == row.role);
        } else {
            CHECK_FALSE(table.back_role(row.id));
        }
    }
    CHECK(seen == 13);
    eng::presentation::EquipArtQuery neck_away;
    neck_away.content_id = "srd51.item.holy_symbol_amulet";
    neck_away.part = eng::presentation::EquipArtPart::Neck;
    neck_away.view = eng::presentation::EquipArtView::Away;
    CHECK(table.find(neck_away) == nullptr);
    const fs::path pouch_se = root / "srd51.item.pouch" / "hip.se.png";
    const fs::path component_se = root / "srd51.item.component_pouch" / "hip.se.png";
    std::ifstream a(pouch_se, std::ios::binary), b(component_se, std::ios::binary);
    const std::string pouch_bytes{std::istreambuf_iterator<char>(a), {}};
    const std::string component_bytes{std::istreambuf_iterator<char>(b), {}};
    CHECK(pouch_bytes == component_bytes);
    CHECK_FALSE(pouch_bytes.empty());
}

#ifndef SLICE_SMALL_BODY_EQUIP_KIT
#define SLICE_SMALL_BODY_EQUIP_KIT ""
#endif

namespace {
Frame render_kit(const ViewModel& vm, const fs::path& kit) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev, kit.string());
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(4.5f, 4.5f);
    cam.position = {c.x - 320.f, c.y - 180.f};
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

ViewModel one_pawn(ActorView actor) {
    ViewModel vm;
    TileView t;
    t.tile = actor.tile;
    t.kind = TileKind::Floor;
    vm.tiles.push_back(t);
    vm.actors.push_back(std::move(actor));
    return vm;
}

ActorView paperdoll_at(BodySize size, bool short_flag) {
    ActorView a;
    a.id = 1;
    a.name = "PAWN";
    a.tile = {3, 4};
    a.hp = a.hp_max = 12;
    a.visual_kind = "paperdoll";
    a.kit = "paperdoll";
    a.facing = Facing::SouthEast;
    a.body_size = size;
    a.size_short = short_flag;
    a.equipment = {gear(10, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword")};
    return a;
}
} // namespace

TEST_CASE("paperdoll short body: hook stays off, and a forced seam picks the small table and override") {
    CHECK_FALSE(eng::sim::kSizeShortRuntime);
    CHECK_FALSE(short_body_hook_on());

    const ActorView small = paperdoll_at(BodySize::Small, false);
    const ActorView flagged = paperdoll_at(BodySize::Medium, true);
    const ActorView medium = paperdoll_at(BodySize::Medium, false);
    CHECK(std::string(attach_table_bin(small)) == "medium");
    CHECK(std::string(attach_table_bin(flagged)) == "medium");
    CHECK(equip_size_bin(small).empty());
    CHECK(equip_size_bin(flagged).empty());

    ActorView wolf;
    wolf.visual_kind = "standin";
    wolf.standin_bin = "large";
    wolf.body_size = BodySize::Small;
    wolf.size_short = true;
    CHECK(std::string(attach_table_bin(wolf)) == "large");
    CHECK(std::string(equip_size_bin(wolf)) == "large");

    const fs::path standins{SLICE_STANDIN_ROOT};
    const BodyAttach shipped_small = load_body_attach(standins, small.content_id, "small", "paperdoll");
    REQUIRE(shipped_small.loaded);
    const AttachPoints se = attach_points(shipped_small, Facing::SouthEast);
    CHECK(se.main_hand.x == 4.f);
    CHECK(se.main_hand.y == 6.f);
    CHECK(se.off_hand.x == -4.f);
    CHECK(se.off_hand.y == 6.f);
    CHECK(se.back.x == -2.f);
    CHECK(se.back.y == -6.f);
    CHECK(se.hip_main.x == 1.f);
    CHECK(se.hip_main.y == -3.f);
    CHECK(se.hip_off.x == -5.f);
    CHECK(se.hip_off.y == -3.f);
    CHECK(se.neck.x == 1.f);
    CHECK(se.neck.y == -8.f);
    const AttachPoints sw = attach_points(shipped_small, Facing::SouthWest);
    CHECK(sw.neck.x == -1.f);
    CHECK(sw.neck.y == -8.f);
    CHECK(sw.main_hand.x == -4.f);
    const AttachPoints ne = attach_points(shipped_small, Facing::NorthEast);
    CHECK(ne.main_hand.y == 6.f);
    CHECK(ne.back.x == -1.f);
    CHECK(ne.back.y == -6.f);
    CHECK(ne.hip_main.x == 4.f);
    CHECK(ne.hip_off.x == -6.f);
    CHECK(ne.neck.x == -1.f);   // stored; away facings still draw no neck
    CHECK(ne.neck.y == -8.f);
    const AttachPoints nw = attach_points(shipped_small, Facing::NorthWest);
    CHECK(nw.neck.x == 1.f);
    CHECK(nw.main_hand.x == -4.f);

    ActorView necked = small;
    necked.equipment = {gear(8, AttachSlot::Neck, EquipKind::Other, "srd51.item.holy_symbol_amulet")};
    const std::vector<EquipLayer> toward = equip_layers(necked, shipped_small);
    const EquipLayer* neck = layer_part(toward, 8, EquipPart::Neck);
    REQUIRE(neck);
    CHECK(neck->z == kZWornFront);
    CHECK(neck->at.x == 1.f);
    CHECK(neck->at.y == -8.f);
    necked.facing = Facing::NorthEast;
    CHECK(layer_part(equip_layers(necked, shipped_small), 8, EquipPart::Neck) == nullptr);
    necked.facing = Facing::NorthWest;
    CHECK(layer_part(equip_layers(necked, shipped_small), 8, EquipPart::Neck) == nullptr);

    const fs::path kit{SLICE_SMALL_BODY_EQUIP_KIT};
    const eng::presentation::EquipArtTable table = eng::presentation::EquipArtTable::load(kit);
    REQUIRE(table.size() == 4);
    const EquipVisual sword = gear(10, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword");
    const EquipArtChoice medium_art = equip_art_choice(&table, sword, EquipPart::MainHand, Facing::SouthEast, equip_size_bin(small));
    REQUIRE(medium_art.sprite);
    CHECK(medium_art.sprite->pivot_x == 12);
    CHECK(medium_art.sprite->pivot_y == 12);

    const Frame bare_off = render_kit(one_pawn([&] {
        ActorView a = paperdoll_at(BodySize::Small, true);
        a.equipment.clear();
        return a;
    }()), kit);
    const Frame armed_off = render_kit(one_pawn(small), kit);
    const Frame medium_off = render_kit(one_pawn(medium), kit);
    CHECK(armed_off.hash == medium_off.hash);

    {
        ShortBodyHookForce force;
        CHECK(short_body_hook_on());
        CHECK(std::string(attach_table_bin(small)) == "small");
        CHECK(std::string(attach_table_bin(flagged)) == "small");
        CHECK(std::string(equip_size_bin(small)) == "small");
        CHECK(std::string(equip_size_bin(flagged)) == "small");
        for (BodySize size : {BodySize::Tiny, BodySize::Medium, BodySize::Large, BodySize::Huge, BodySize::Gargantuan}) {
            const ActorView other = paperdoll_at(size, false);
            CHECK(std::string(attach_table_bin(other)) == "medium");
            CHECK(std::string(equip_size_bin(other)) == "medium");
        }
        CHECK(std::string(attach_table_bin(wolf)) == "large");
        CHECK(std::string(equip_size_bin(wolf)) == "large");

        const std::string bin(attach_table_bin(small));
        const BodyAttach chosen = load_body_attach(standins, small.content_id, bin, small.visual_kind);
        CHECK(attach_points(chosen, Facing::SouthEast).main_hand.y == 6.f);
        CHECK(attach_points(chosen, Facing::SouthEast).neck.y == -8.f);
        const BodyAttach still_medium = load_body_attach(standins, medium.content_id, std::string(attach_table_bin(medium)), medium.visual_kind);
        CHECK(attach_points(still_medium, Facing::SouthEast).main_hand.y == 3.f);
        CHECK(attach_points(still_medium, Facing::SouthEast).neck.y == -11.f);

        const EquipArtChoice small_art = equip_art_choice(&table, sword, EquipPart::MainHand, Facing::SouthEast, equip_size_bin(small));
        REQUIRE(small_art.sprite);
        CHECK(small_art.sprite->pivot_x == 4);
        CHECK(small_art.sprite->pivot_y == 8);
        CHECK(small_art.sprite != medium_art.sprite);
        const EquipArtChoice medium_still = equip_art_choice(&table, sword, EquipPart::MainHand, Facing::SouthEast, equip_size_bin(medium));
        CHECK(medium_still.sprite == medium_art.sprite);

        const Frame bare_on = render_kit(one_pawn([&] {
            ActorView a = paperdoll_at(BodySize::Small, true);
            a.equipment.clear();
            return a;
        }()), kit);
        CHECK(bare_on.hash == bare_off.hash);
        const Frame armed_on = render_kit(one_pawn(small), kit);
        const Frame flagged_on = render_kit(one_pawn(flagged), kit);
        const Frame medium_on = render_kit(one_pawn(medium), kit);
        const Frame tiny_on = render_kit(one_pawn(paperdoll_at(BodySize::Tiny, false)), kit);
        CHECK(armed_on.hash != armed_off.hash);
        CHECK(flagged_on.hash == armed_on.hash);
        CHECK(medium_on.hash == armed_off.hash);
        CHECK(tiny_on.hash == armed_off.hash);
    }
    CHECK_FALSE(short_body_hook_on());
    CHECK(std::string(attach_table_bin(small)) == "medium");
    CHECK(render_kit(one_pawn(small), kit).hash == armed_off.hash);
}
