// Debug equip / inventory view (debug_equip.hpp): a script drives the same DebugEquipView::handle path the F3 keys use,
// headless, against a live session: equip, unequip, light / hood / douse, and checks inventory, the sprite overlay
// (ActorView::equipment), the published light and the drawn panel (null backend).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "assets/schema_registry.hpp"
#include "debug_equip.hpp"
#include "equip_visuals.hpp"
#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;
using Cmd = DebugEquipView::Cmd;

namespace {
namespace fs = std::filesystem;
std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}
// The sample pack; the fighter wears chain mail, holds a longsword, and carries a hooded lantern and a shield.
fs::path make_pack() {
    const fs::path root = fs::temp_directory_path() / "isoforge_debug_equip";
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
                                          assets::Json{{"item", "srd51.item.lantern_hooded"}, {"equipped", false}},
                                          assets::Json{{"item", "srd51.item.shield"}, {"equipped", false}}});
    std::ofstream(pack / "characters/fighter_1.json") << f.dump(2);
    return root;
}
// Moves the cursor onto the row of `content_id` through Up / Down commands only (as a player would).
bool select(DebugEquipView& v, Session& s, const std::string& content_id) {
    for (int i = 0; i < 16; ++i) v.handle(Cmd::Up, s);
    const auto inv = s.inventory(v.selected_pc(s));
    for (size_t i = 0; i < inv.size(); ++i) {
        if (inv[i].content_id == content_id) return v.cursor() == i;
        v.handle(Cmd::Down, s);
    }
    return false;
}
const Session::InventoryItem* item(const Session& s, uint32_t pc, const std::string& id) {
    static std::vector<Session::InventoryItem> inv;
    inv = s.inventory(pc);
    for (const auto& it : inv) if (it.content_id == id) return &it;
    return nullptr;
}
const EquipVisual* drawn(const Session& s, uint32_t pc, const std::string& id) {
    static ViewModel vm;
    vm = s.view();
    for (const ActorView& a : vm.actors)
        if (a.id == pc) for (const EquipVisual& e : a.equipment) if (e.content_id == id) return &e;
    return nullptr;
}
// Ends turns until pc acts again (fresh free object interaction).
void next_turn(Session& s, uint32_t pc) {
    for (const ActorView& a : s.view().actors)
        if (a.id != pc && s.end_turns_until(a.id)) break;
    REQUIRE(s.end_turns_until(pc));
}
} // namespace

TEST_CASE("debug equip view: scripted equip, unequip and light / hood / douse through the key command path") {
    const fs::path root = make_pack();
    Session::Options o{{root.string(), SLICE_PACK_ROOT}, {"test.equip"}, 1, true};
    o.pc_torches = false;
    o.equipment = true;
    std::string err;
    auto sp = Session::open(o, &err);
    REQUIRE_MESSAGE(sp, err);
    Session& s = *sp;
    DebugEquipView v;
    CHECK(v.lines(s).empty());                     // closed: no panel
    CHECK_FALSE(v.handle(Cmd::Equip, s));          // closed: commands are ignored
    v.handle(Cmd::Toggle, s);
    REQUIRE(v.open());
    const uint32_t pc = v.selected_pc(s);
    REQUIRE(pc != 0);
    const auto inv = s.inventory(pc);
    REQUIRE(inv.size() == 4);
    CHECK(inv[0].where == Session::InventoryItem::Worn);   // chain mail
    CHECK(inv[1].content_id == "srd51.item.longsword");
    CHECK(inv[1].where == Session::InventoryItem::MainHand);
    const auto lines = v.lines(s);
    REQUIRE(lines.size() >= 6);
    CHECK(lines[0].find("DEBUG EQUIP") == 0);
    CHECK(lines[1].find("> WORN CHAIN MAIL") == 0);

    next_turn(s, pc);
    // Equip the lantern (Draw, any free hand): off hand, drawn on the sprite, unlit.
    REQUIRE(select(v, s, "srd51.item.lantern_hooded"));
    REQUIRE(v.handle(Cmd::Equip, s));
    REQUIRE(item(s, pc, "srd51.item.lantern_hooded"));
    CHECK(item(s, pc, "srd51.item.lantern_hooded")->where == Session::InventoryItem::OffHand);
    REQUIRE(drawn(s, pc, "srd51.item.lantern_hooded"));
    CHECK(drawn(s, pc, "srd51.item.lantern_hooded")->slot == AttachSlot::OffHand);
    CHECK_FALSE(drawn(s, pc, "srd51.item.lantern_hooded")->lit);

    // Light cycle: unlit -> lit 30/30 -> hooded 0/5 -> doused. One interaction per turn.
    struct Step { bool lit; uint8_t state; uint16_t bright, dim; };
    for (const Step st : {Step{true, 0, 30, 30}, Step{true, 1, 0, 5}, Step{false, 1, 0, 0}}) {
        next_turn(s, pc);
        REQUIRE(select(v, s, "srd51.item.lantern_hooded"));
        REQUIRE(v.handle(Cmd::CycleLight, s));
        const auto* it = item(s, pc, "srd51.item.lantern_hooded");
        REQUIRE(it);
        CHECK(it->lit == st.lit);
        if (st.lit) CHECK(it->state == st.state);
        CHECK(it->bright_ft == st.bright);
        CHECK(it->dim_ft == st.dim);
        const EquipVisual* e = drawn(s, pc, "srd51.item.lantern_hooded");
        REQUIRE(e);
        CHECK(sheds_light(*e) == (st.bright + st.dim > 0));
        bool published = false;   // the sim light the glow comes from agrees
        for (const auto& l : s.equipment_lights()) if (l.holder_id == pc && l.lit == st.lit && l.dim_ft == st.dim) published = true;
        CHECK(published);
    }

    // Unequip the longsword (Stow): carried, slung on the back.
    next_turn(s, pc);
    REQUIRE(select(v, s, "srd51.item.longsword"));
    REQUIRE(v.handle(Cmd::Unequip, s));
    CHECK(item(s, pc, "srd51.item.longsword")->where == Session::InventoryItem::Carried);
    CHECK(drawn(s, pc, "srd51.item.longsword")->slot == AttachSlot::Back);
    CHECK(v.message() == "LONGSWORD UNEQUIPPED");

    // Refusals do not submit: unequip a carried item, light a non-light, act off-turn.
    REQUIRE(select(v, s, "srd51.item.shield"));
    CHECK_FALSE(v.handle(Cmd::Unequip, s));
    CHECK(v.message() == "NOT HELD");
    CHECK_FALSE(v.handle(Cmd::CycleLight, s));
    CHECK(v.message() == "NOT A LIGHT");
    for (const ActorView& a : s.view().actors)
        if (a.id != pc && s.end_turns_until(a.id)) break;
    if (s.current_actor() != pc) {
        CHECK_FALSE(v.handle(Cmd::Equip, s));
        CHECK(v.message() == "NOT ITS TURN");
    }

    // The panel draws through SliceView (null backend), and only while open.
    render::NullDevice dev;
    SliceView view(dev);
    iso2d::Camera cam;
    auto ui_with = [&](bool open) {
        if (v.open() != open) v.handle(Cmd::Toggle, s);
        ViewModel vm = s.view();
        v.decorate(s, vm);
        CHECK(vm.debug_panel.empty() == !open);
        REQUIRE(dev.begin_frame());
        view.prepare(vm, cam, 1280, 720);
        auto& cl = dev.command_list();
        cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        return view.ui_sprites();
    };
    const uint32_t closed = ui_with(false), opened = ui_with(true);
    CHECK(opened > closed);
    CHECK(dev.errors() == 0);
}
