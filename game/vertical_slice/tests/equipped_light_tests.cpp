// Equipment as one producer of entity-attached light sources (#233 generic list): a light-capable item
// held by a creature glows on its holder and follows it; dropped, it glows on its ground tile; stowed
// (covered), it doesn't glow. Keyed by the sim's light id. Visual styles via light.preset.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "assets/schema_registry.hpp"
#include "rules5e/equipment.hpp"
#include "rules5e/item.hpp"
#include "session.hpp"
#include "sim/content.hpp"
#include "sim/combat.hpp"
#include "sim/equipment.hpp"
#include "sim/light.hpp"
#include <utility>
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
namespace fs = std::filesystem;
constexpr const char* kTorch = "test.item.torch";
constexpr const char* kLantern = "test.item.lantern";
constexpr const char* kLanternPreset = "test.item.lantern";   // == the lantern def's preset (its id; no doc)

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}

// Temp pack "test.equip": the sample pack, with the fighter replaced by a character@4 (same id) carrying
// `light_item` lit in the off hand, plus an item doc that styles kLanternPreset (id differs from the preset).
fs::path make_pack(const std::string& tag, const char* light_item) {
    const fs::path root = fs::temp_directory_path() / ("isoforge_equip_" + tag);
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path pack = root / "test.equip";
    fs::copy(fs::path(SLICE_PACK_ROOT) / SLICE_PACK, pack, fs::copy_options::recursive);
    std::string man = slurp(pack / "manifest.json");
    man.replace(man.find("\"sample.vertical_slice\""), 23, "\"test.equip\"");
    man.replace(man.find("\"chunks/0_0_0.json\""), 19, "\"chunks/0_0_0.json\",\n    \"items/lantern_style.json\"");
    std::ofstream(pack / "manifest.json") << man;
    assets::Json f = assets::Json::parse(slurp(fs::path(SLICE_PACK_ROOT) / "srd51.core/characters/fighter_level_1.json"));
    f["id"] = "srd51.character.fighter_1";
    f["equipment"] = assets::Json::array({assets::Json{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                          assets::Json{{"item", "srd51.item.longsword"}, {"equipped", true}},
                                          assets::Json{{"item", light_item}, {"equipped", true}}});
    std::ofstream(pack / "characters/fighter_1.json") << f.dump(2);
    fs::create_directories(pack / "items");
    // item@2 gear light (schema-valid): its light.preset names the lantern def's preset; its visual styles it.
    std::ofstream(pack / "items/lantern_style.json")
        << R"({"$schema": "item@2", "id": "test.equip.item.lantern_style", "name": "Blue lantern", "category": "gear",
              "cost_cp": 500, "weight_oz": 32, "hands": 1,
              "light": {"bright_ft": 30, "dim_ft": 30, "shape": "radius", "lit_by_default": false, "preset": ")"
        << kLanternPreset << R"("},
              "visual": {"color": {"r": 51, "g": 102, "b": 255}, "intensity": 400, "flicker": "candle"}})";
    return root;
}

// Lit copies of the real srd51 gear (srd51.core/items/*.json, item@2): same light block, only re-id'd and
// lit_by_default true, since there is no light / hood op yet (Rules). The real items stay loaded, unlit.
void add_light_defs(sim::ContentLibrary& l) {
    for (auto [id, file] : {std::pair{kTorch, "torch.json"}, std::pair{kLantern, "lantern_hooded.json"}}) {
        assets::Json d = assets::Json::parse(slurp(fs::path(SLICE_PACK_ROOT) / "srd51.core/items" / file));
        d["id"] = id;
        d["light"]["lit_by_default"] = true;
        d["light"].erase("preset");   // the preset then falls back to the item id (styled below by light.preset)
        REQUIRE_FALSE(l.items()->add(d));
    }
}

std::unique_ptr<Session> open_equip(const fs::path& root, bool torches, std::string* err) {
    Session::Options o{{root.string(), SLICE_PACK_ROOT}, {"test.equip"}, 1, true};
    o.pc_torches = torches;
    o.equipment = true;
    o.content_hook = add_light_defs;
    return Session::open(o, err);
}

const LightSourceView* source_with_index(const ViewModel& vm, uint32_t index) {
    for (const auto& s : vm.light.sources) if (s.attached && s.index == index) return &s;
    return nullptr;
}
uint32_t pc_id(const ViewModel& vm) {
    for (const auto& a : vm.actors) if (a.is_pc) return a.id;
    return 0;
}
Vec2i tile_of(const ViewModel& vm, uint32_t id) {
    for (const auto& a : vm.actors) if (a.id == id) return a.tile;
    return {};
}
} // namespace

TEST_CASE("equipped torch: glow keyed by the sim light id follows the holder; dropped it lies on the ground; stowed it is gone") {
    const fs::path root = make_pack("torch", kTorch);
    std::string err;
    auto s = open_equip(root, false, &err);
    REQUIRE_MESSAGE(s, err);
    auto lights = s->equipment_lights();
    REQUIRE(lights.size() == 1);
    const uint32_t lid = lights[0].light_id, item = lights[0].item;
    CHECK(lid < kInterimLightIdBase);
    ViewModel vm = s->view();
    const uint32_t pc = pc_id(vm);
    CHECK(lights[0].holder_id == pc);
    REQUIRE(vm.light.present);
    const LightSourceView* src = source_with_index(vm, lid);
    REQUIRE(src);
    CHECK(src->carrier == 1);
    CHECK(src->carrier_id == pc);
    CHECK(src->bright_ft == 20);
    CHECK(src->lit);
    const iso2d::GlowPalette pal;
    auto glows = glows_from(vm.light, pal);
    REQUIRE(glows.size() == 1);
    CHECK(glows[0].anchor == iso2d::GlowAnchorKind::Entity);
    CHECK(glows[0].anchor_id == pc);
    CHECK(glows[0].key == src->sort_key());

    CHECK(src->tile == tile_of(vm, pc));   // the published source sits on the holder
    // Render side: the glow tracks the holder's drawn (interpolated) position, not the last published tile.
    glows = glows_from(vm.light, pal);
    const iso2d::GlowAnchor drawn[] = {{pc, {7.5f, 3.25f}}};
    iso2d::follow_anchors(glows, drawn);
    REQUIRE_FALSE(glows.empty());
    CHECK(glows[0].tile.x == 7.5f);
    CHECK(glows[0].tile.y == 3.25f);

    // Drop it on the holder's turn: same light id, holder 0, a ground glow at the item's tile (not anchored).
    REQUIRE_FALSE(s->ended());
    REQUIRE(s->end_turns_until(pc));
    {
        const Vec2i at = tile_of(s->view(), pc);
        REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::Drop), item));
        lights = s->equipment_lights();
        REQUIRE(lights.size() == 1);
        CHECK(lights[0].light_id == lid);
        CHECK(lights[0].holder_id == 0);
        vm = s->view();
        src = source_with_index(vm, lid);
        REQUIRE(src);
        CHECK(src->carrier == 0);   // Carrier::Tile
        CHECK(src->tile == at);
        glows = glows_from(vm.light, pal);
        REQUIRE(glows.size() == 1);
        CHECK(glows[0].anchor == iso2d::GlowAnchorKind::None);
        CHECK(glows[0].tile.x == float(at.x));
        CHECK(glows[0].tile.y == float(at.y));
        // Pick it up again and stow it: covered, so no glow (the source stays, unlit).
        REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::PickUp), item, 2));
        vm = s->view();
        src = source_with_index(vm, lid);
        REQUIRE(src);
        CHECK(src->carrier == 1);   // back on the holder, same id
        CHECK(src->carrier_id == pc);
        CHECK(glows_from(vm.light, pal).at(0).anchor == iso2d::GlowAnchorKind::Entity);
        REQUIRE(s->end_turns_until(pc));
        REQUIRE(s->submit_equip(pc, uint8_t(rules5e::EquipOp::Stow), item));
        vm = s->view();
        src = source_with_index(vm, lid);
        REQUIRE(src);
        CHECK_FALSE(src->lit);
        CHECK(glows_from(vm.light, pal).empty());
    }
    fs::remove_all(root);
}

TEST_CASE("equipped lantern: visual via light.preset; pc_torches still a producer alongside equipment") {
    const fs::path root = make_pack("lantern", kLantern);
    std::string err;
    // The lantern def has no item doc, so its glow preset is its item id; the styling item (another id) names that
    // preset in light.preset.
    auto s = open_equip(root, true, &err);
    REQUIRE_MESSAGE(s, err);
    ViewModel vm = s->view();
    const auto lights = s->equipment_lights();
    REQUIRE(lights.size() == 1);
    const LightSourceView* lantern = source_with_index(vm, lights[0].light_id);
    REQUIRE(lantern);
    CHECK(lantern->bright_ft == 30);
    CHECK(lantern->preset == kLantern);
    size_t torches = 0;
    for (const auto& v : vm.light.sources) if (v.index >= kInterimLightIdBase) ++torches;
    CHECK(torches == 1);   // pc_torches unchanged, ids disjoint from the sim's
    REQUIRE(vm.light.visuals.count(kLanternPreset) == 1);   // registered under light.preset, not the styling item's id
    CHECK(vm.light.visuals.count("test.equip.item.lantern_style") == 0);

    // The lantern's glow takes the visual registered under its preset by a different item (lantern_style).
    iso2d::GlowPalette pal;
    for (const auto& [id, st] : vm.light.visuals) pal.set_override(id, st);
    bool styled = false, torch_default = false;
    for (const auto& g : glows_from(vm.light, pal)) {
        if (g.radius_tiles == doctest::Approx(iso2d::glow_radius_tiles(30, 30)))
            styled = g.color.b == doctest::Approx(1.f) && g.intensity == doctest::Approx(0.4f);
        else torch_default = g.color.r == doctest::Approx(pal.style_for("srd51.core.light.torch", false).color.r);
    }
    CHECK(styled);
    CHECK(torch_default);
    // Without the visual the lantern keeps the built-in "lantern" default.
    for (const auto& g : glows_from(vm.light, iso2d::GlowPalette{}))
        if (g.radius_tiles == doctest::Approx(iso2d::glow_radius_tiles(30, 30)))
            CHECK(g.intensity == doctest::Approx(iso2d::GlowPalette{}.style_for("x.lantern", false).intensity));
    fs::remove_all(root);
}

TEST_CASE("real srd51 gear: equip lights with state radii and presets, dark until a light op exists") {
    const fs::path root = make_pack("real", "srd51.item.lantern_hooded");
    std::string err;
    auto s = open_equip(root, false, &err);
    REQUIRE_MESSAGE(s, err);
    const auto lights = s->equipment_lights();
    REQUIRE(lights.size() == 1);
    CHECK_FALSE(lights[0].lit);   // lit_by_default false
    const ViewModel vm = s->view();
    const LightSourceView* src = source_with_index(vm, lights[0].light_id);
    REQUIRE(src);
    CHECK(src->preset == "srd51.core.light.lantern_hooded");   // from item@2 light.preset
    CHECK(src->bright_ft == 30);
    CHECK(src->dim_ft == 30);
    CHECK_FALSE(src->lit);
    CHECK(glows_from(vm.light, iso2d::GlowPalette{}).empty());
    fs::remove_all(root);
}

TEST_CASE("equipment_source: state radii (hooded = small dim glow), 0/0 draws nothing, cone drawn as radius") {
    auto view_of = [](const sim::LightSource& ls) {
        LightSourceView v;
        v.attached = true;
        v.index = ls.id.index;
        v.tile = ls.tile;
        v.carrier = ls.carrier == sim::Carrier::Creature ? 1 : ls.carrier == sim::Carrier::Item ? 2 : 0;
        v.carrier_id = ls.carrier_id;
        v.bright_ft = ls.bright_ft;
        v.dim_ft = ls.dim_ft;
        v.lit = ls.lit;
        v.preset = ls.preset;
        LightView L;
        L.present = true;
        L.sources = {v};
        return L;
    };
    const iso2d::GlowPalette pal;
    sim::EquipmentLight e;
    e.light_id = 3; e.item = 9; e.holder_id = 5; e.tile = {2, 2}; e.lit = true;
    e.state = 0; e.bright_ft = 30; e.dim_ft = 30;
    const auto open = glows_from(view_of(equipment_source(e, {4, 4}, "srd51.core.light.lantern_hooded")), pal);
    REQUIRE(open.size() == 1);
    CHECK(open[0].anchor_id == 5);
    CHECK(open[0].radius_tiles == doctest::Approx(9.f));
    e.state = 1; e.bright_ft = 0; e.dim_ft = 5;   // hooded
    const sim::LightSource hs = equipment_source(e, {4, 4}, "srd51.core.light.lantern_hooded");
    CHECK(hs.bright_ft == 0);
    CHECK(hs.dim_ft == 5);
    const auto hooded = glows_from(view_of(hs), pal);
    REQUIRE(hooded.size() == 1);
    CHECK(hooded[0].radius_tiles == doctest::Approx(0.5f));
    CHECK(hooded[0].intensity < open[0].intensity);
    e.bright_ft = 0; e.dim_ft = 0;   // lit, but emits nothing
    CHECK(glows_from(view_of(equipment_source(e, {4, 4}, "x")), pal).empty());
    e.state = 0; e.bright_ft = 60; e.dim_ft = 60; e.shape = rules5e::LightShape::Cone;   // bullseye: radius for now
    const auto cone = glows_from(view_of(equipment_source(e, {4, 4}, "srd51.core.light.lantern_bullseye")), pal);
    REQUIRE(cone.size() == 1);
    CHECK(cone[0].radius_tiles == doctest::Approx(iso2d::glow_radius_tiles(60, 60)));
}
