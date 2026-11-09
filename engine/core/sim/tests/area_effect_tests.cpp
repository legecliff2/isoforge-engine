// monster@5 area_effect (docs/proposals/AOE_MONSTER_ABILITIES.md, PR 2): a synthetic Save SpellDef on the
// existing Combat::area_targets path. Recharge and OnSave::Half are the save_effect machinery. Existing
// save_effect breaths are not rewritten.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::AreaShape;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json monster(const std::string& id, Json action, const char* schema = "monster@5") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["hit_points"]["average"] = 200;
    m["actions"].push_back(std::move(action));
    return m;
}
Json area(const char* shape, int size_ft, Json extra = Json::object()) {
    Json a = {{"kind", "area_effect"}, {"id", "breath"}, {"name", "Breath"},
              {"save", {{"ability", "dex"}, {"dc", 14}}},
              {"area", {{"shape", shape}, {"size_ft", size_ft}}}, {"origin", "self"},
              {"damage", Json::array({{{"dice", "6d6"}, {"type", "fire"}}})}, {"on_success", "half"},
              {"usage", {{"kind", "recharge"}, {"min", 5}}}};
    if (std::string(shape) == "line") a["area"]["width_ft"] = 5;
    if (std::string(shape) == "cylinder") a["area"]["height_ft"] = 20;
    for (auto& [k, v] : extra.items()) a[k] = v;
    return a;
}
const rules5e::SpellDef* spell_of(const ContentLibrary& lib, const char* arch, const char* act = "breath") {
    return lib.spells()->find(rules5e::spell_id_of(std::string(arch) + "#" + act));
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
} // namespace

TEST_CASE("area_effect compiles cone, line, cube and sphere onto the spell area path") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    struct Row { const char* id; const char* shape; int size; AreaShape want; int width; };
    const Row rows[] = {{"t.cone", "cone", 30, AreaShape::Cone, 0},
                        {"t.line", "line", 30, AreaShape::Line, 5},
                        {"t.cube", "cube", 15, AreaShape::Cube, 0},
                        {"t.sphere", "sphere", 5, AreaShape::Sphere, 0}};
    for (const Row& row : rows) {
        Json extra = row.shape == std::string("line") ? Json{{"damage", Json::array({{{"dice", "11d8"}, {"type", "acid"}}})},
                                                               {"usage", {{"kind", "recharge"}, {"min", 6}}}}
                                                      : row.shape == std::string("sphere") ? Json{{"save", {{"ability", "dex"}, {"dc", 11}}},
                                                                                                   {"damage", Json::array({{{"dice", "2d6"}, {"type", "fire"}}})},
                                                                                                   {"usage", {{"kind", "recharge"}, {"min", 6}}}}
                                                                                           : Json::object();
        REQUIRE_FALSE(lib.add(monster(row.id, area(row.shape, row.size, extra)), "monster"));
        const Archetype* a = lib.find(row.id);
        REQUIRE(a);
        REQUIRE(a->save_actions.size() == 1);
        CHECK(a->save_actions[0].area);
        CHECK(a->save_actions[0].usage_kind == 1);
        CHECK(a->save_actions[0].range_ft == 0);
        const auto* d = spell_of(lib, row.id);
        REQUIRE(d);
        CHECK_FALSE(d->is_spell);
        CHECK(d->kind == rules5e::SpellEffectKind::Save);
        CHECK(d->area == row.want);
        CHECK(d->area_size_ft == row.size);
        CHECK(d->area_width_ft == row.width);
        CHECK(d->range_ft == 0);
        CHECK(d->on_save == rules5e::OnSave::Half);
        CHECK(d->fixed_dc == (row.shape == std::string("sphere") ? 11 : 14));
        CHECK(d->save_ability == rules5e::Ability::Dex);
        CHECK(d->recharge_min == (row.shape == std::string("line") || row.shape == std::string("sphere") ? 6 : 5));
        CHECK(std::count(a->base.spells_known.begin(), a->base.spells_known.end(), d->id) == 1);
    }
    // A point cube is centred on the aim tile (range_ft stays). A cylinder stores height and does not use it.
    REQUIRE_FALSE(lib.add(monster("t.point", area("cube", 15, {{"origin", "point"}, {"range_ft", 30}})), "monster"));
    const auto* point = spell_of(lib, "t.point");
    REQUIRE(point);
    CHECK(point->area == AreaShape::Cube);
    CHECK(point->range_ft == 30);
    REQUIRE_FALSE(lib.add(monster("t.cyl", area("cylinder", 10, {{"on_success", "none"}})), "monster"));
    const auto* cyl = spell_of(lib, "t.cyl");
    REQUIRE(cyl);
    CHECK(cyl->area == AreaShape::Cylinder);
    CHECK(cyl->area_size_ft == 10);
    CHECK(cyl->area_height_ft == 20);
    CHECK(cyl->on_save == rules5e::OnSave::None);
    // Condition breath: no damage, OnSave::None, the condition rides the save. Recharge 6.
    Json blind = {{"kind", "area_effect"}, {"id", "blinding_breath"}, {"name", "Blinding Breath"},
                  {"save", {{"ability", "dex"}, {"dc", 10}}},
                  {"area", {{"shape", "cone"}, {"size_ft", 15}}}, {"origin", "self"},
                  {"apply", Json::array({{{"condition", "blinded"},
                                           {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                           {"repeat_save", "end_of_turn"}}})},
                  {"usage", {{"kind", "recharge"}, {"min", 6}}}};
    REQUIRE_FALSE(lib.add(monster("t.blind", blind), "monster"));
    const auto* bd = spell_of(lib, "t.blind", "blinding_breath");
    REQUIRE(bd);
    CHECK(bd->dice.count == 0);
    CHECK(bd->dice.bonus == 0);
    CHECK(bd->on_save == rules5e::OnSave::None);
    CHECK(bd->applies_condition);
    CHECK(bd->condition == rules5e::Condition::Blinded);
    CHECK(bd->repeat_save == rules5e::RepeatSave::EndOfTurn);
    CHECK(bd->expiry == uint8_t(rules5e::Expiry::Rounds));
    CHECK(bd->duration_rounds == 10);
    CHECK(bd->recharge_min == 6);
    CHECK(bd->area == AreaShape::Cone);
    // Per-day uses take the FeatureUses path.
    REQUIRE_FALSE(lib.add(monster("t.day", area("cone", 15, {{"usage", {{"kind", "per_day"}, {"uses", 1}}}})), "monster"));
    CHECK(lib.find("t.day")->save_actions[0].usage_kind == 2);
    CHECK(lib.find("t.day")->save_actions[0].uses == 1);
    CHECK(spell_of(lib, "t.day")->usage_kind == 2);
    CHECK(spell_of(lib, "t.day")->recharge_min == 0);
}

TEST_CASE("area_effect refuses a bad shape, a pre-@5 file, and a character") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    auto err = lib.add(monster("t.noarea", area("cone", 15, {{"area", nullptr}})), "monster");
    REQUIRE(err);
    CHECK(err->find("needs an area") != std::string::npos);
    err = lib.add(monster("t.shape", area("cone", 15, {{"area", {{"shape", "wall"}, {"size_ft", 15}}}})), "monster");
    REQUIRE(err);
    CHECK(err->find("bad area shape") != std::string::npos);
    err = lib.add(monster("t.wide", area("cone", 15, {{"area", {{"shape", "cone"}, {"size_ft", 15}, {"width_ft", 5}}}})), "monster");
    REQUIRE(err);
    CHECK(err->find("width_ft is line only") != std::string::npos);
    Json line = area("line", 30);
    line["area"].erase("width_ft");
    err = lib.add(monster("t.nowidth", line), "monster");
    REQUIRE(err);
    CHECK(err->find("line needs width_ft") != std::string::npos);
    err = lib.add(monster("t.min", area("cone", 15, {{"usage", {{"kind", "recharge"}, {"min", 1}}}})), "monster");
    REQUIRE(err);
    CHECK(err->find("bad recharge min") != std::string::npos);
    err = lib.add(monster("t.half", area("cone", 15, {{"on_success", "miss"}})), "monster");
    REQUIRE(err);
    CHECK(err->find("on_success half or none") != std::string::npos);
    err = lib.add(monster("t.old", area("cone", 15), "monster@4"), "monster");
    REQUIRE(err);
    CHECK(err->find("monster@5") != std::string::npos);
    CHECK_FALSE(lib.find("t.old"));
    Json fighter = load_json("content/packs/srd51.core/characters/fighter_level_1.json");
    fighter["id"] = "t.hero";
    fighter["actions"].push_back(area("cone", 15));
    err = lib.add(fighter, "character");
    REQUIRE(err);
    CHECK(err->find("monster@5") != std::string::npos);
    // A refused condition duration is listed, not compiled. The archetype still loads.
    REQUIRE_FALSE(lib.add(monster("t.dur", area("cone", 15, {{"apply", Json::array({{{"condition", "stunned"}, {"duration", {{"kind", "concentration"}}}}})}})), "monster"));
    CHECK(lib.find("t.dur")->save_actions.empty());
    REQUIRE(lib.find("t.dur")->unsupported_features.size() == 1);
    CHECK(lib.find("t.dur")->unsupported_features[0].find("area_effect") != std::string::npos);
    CHECK_FALSE(spell_of(lib, "t.dur"));
}

TEST_CASE("area_effect cast uses area_targets, half on a success, and the recharge row") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(monster("t.breath", area("cone", 15, {{"damage", Json::array({{{"dice", "1d1+7"}, {"type", "fire"}}})},
                                                           {"save", {{"ability", "dex"}, {"dc", 30}}},
                                                           {"usage", {{"kind", "recharge"}, {"min", 5}}}})), "monster"));
    auto ch = arena({{kFighter, {7, 5}}, {"t.breath", {5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.breath#breath");
    REQUIRE(rules5e::find_action_use(c->state(), g, sid));
    CHECK_FALSE(rules5e::find_action_use(c->state(), g, sid)->expended);
    CHECK(rules5e::find_action_use(c->state(), f, sid) == nullptr);
    advance_to(*c, g);
    const std::size_t from = c->events().size();
    Command cmd;
    cmd.aim = true;
    cmd.to = {9, 5};
    cmd.request = {g, ActionKind::CastSpell};
    cmd.request.spell_id = sid;
    auto r = c->submit(cmd);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find_action_use(c->state(), g, sid)->expended);
    int damage = -1, saves = 0;
    bool used = false, spell_cast = false;
    for (std::size_t i = from; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::ActionUsed && e.rules.actor == g) used = true;
        if (e.rules.kind == EventKind::SpellCast) spell_cast = true;
        if (e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == f) ++saves;
        if (e.rules.kind == EventKind::DamageDealt && e.rules.target == f) damage = e.rules.a;
    }
    CHECK(used);
    CHECK_FALSE(spell_cast);
    CHECK(saves == 1);
    CHECK(damage == 8);   // DC 30 fails; 1d1+7 is 8. Half is a later cast.
    // The same cone at DC 1 halves that total. A second creature proves the row was spent.
    REQUIRE_FALSE(lib.add(monster("t.half", area("cone", 15, {{"damage", Json::array({{{"dice", "1d1+7"}, {"type", "fire"}}})},
                                                          {"save", {{"ability", "dex"}, {"dc", 1}}}})), "monster"));
    auto ch2 = arena({{kFighter, {7, 5}}, {"t.half", {5, 5}}});
    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, ch2, lib, 9);
    REQUIRE(c2);
    advance_to(*c2, ch2.spawns[1].stable_id);
    Command cmd2;
    cmd2.aim = true;
    cmd2.to = {9, 5};
    cmd2.request = {ch2.spawns[1].stable_id, ActionKind::CastSpell};
    cmd2.request.spell_id = rules5e::spell_id_of("t.half#breath");
    const std::size_t from2 = c2->events().size();
    auto r2 = c2->submit(cmd2);
    while (r2.needs) r2 = c2->decide({r2.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r2.ok, r2.error);
    int halved = -1, saves2 = 0;
    for (std::size_t i = from2; i < c2->events().size(); ++i) {
        const auto& e = c2->events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::SavingThrowRolled) ++saves2;
        if (e.rules.kind == EventKind::DamageDealt && e.rules.target == ch2.spawns[0].stable_id) halved = e.rules.a;
    }
    CHECK(saves2 == 1);
    CHECK(halved == 4);
}

TEST_CASE("existing save_effect breaths are unchanged by the area_effect loader") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const auto* ice = spell_of(lib, "srd51.monster.ice_mephit", "frost_breath");
    REQUIRE(ice);
    CHECK(ice->area == AreaShape::Cone);
    CHECK(ice->area_size_ft == 15);
    CHECK(ice->on_save == rules5e::OnSave::Half);
    CHECK(ice->recharge_min == 6);
    CHECK(ice->fixed_dc == 10);
    CHECK(ice->save_ability == rules5e::Ability::Dex);
    CHECK(ice->dice == rules5e::DiceExpr{2, 4, 0});
    CHECK_FALSE(ice->is_spell);
    const auto* spray = spell_of(lib, "srd51.monster.ankheg", "acid_spray");
    REQUIRE(spray);
    CHECK(spray->area == AreaShape::Line);
    CHECK(spray->area_size_ft == 30);
    CHECK(spray->area_width_ft == 5);
    CHECK(spray->on_save == rules5e::OnSave::Half);
    CHECK(spray->recharge_min == 6);
    CHECK(spray->fixed_dc == 13);
    const Archetype* mephit = lib.find("srd51.monster.ice_mephit");
    REQUIRE(mephit);
    bool found = false;
    for (const auto& sa : mephit->save_actions)
        if (sa.id == "frost_breath") { found = true; CHECK(sa.area); CHECK(sa.usage_kind == 1); }
    CHECK(found);
}
