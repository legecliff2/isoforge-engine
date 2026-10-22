// Equipment drawn on character sprites (equip_visuals.hpp): fixed layer order around the body, determinism, unequip, and
// byte-exact frames through the null backend; plus the live path Session (sim loadout) -> ViewModel -> layers.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
        a.facing = f;
        CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::MainHand, P::OffHand, P::HeldLight});
        CHECK(attach_points(f).main_hand.x < 0.f);
    }
    a.facing = Facing::NorthEast;
    CHECK(parts(a) == std::vector<P>{P::Back, P::Body, P::OffHand, P::MainHand, P::HeldLight});
    CHECK(attach_points(Facing::SouthEast).main_hand.x > 0.f);
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
    if (std::getenv("EQUIP_PRINT_GOLDEN")) std::printf("equip golden: 0x%016llxull 0x%016llxull\n",
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
fs::path make_kit_pack() {
    const fs::path root = fs::temp_directory_path() / "isoforge_equip_visuals";
    fs::remove_all(root);
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
}
