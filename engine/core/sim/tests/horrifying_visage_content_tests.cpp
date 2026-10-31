// B17 content: Horrifying Visage save_effect. No ghost.json.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json visage(int dc = 13, int margin = 5) {
    return {{"id", "horrifying_visage"}, {"name", "Horrifying Visage"}, {"kind", "save_effect"},
            {"origin", "self"}, {"area", {{"shape", "sphere"}, {"size_ft", 60}}},
            {"save", {{"ability", "wis"}, {"dc", dc}}}, {"target_must_see_source", true},
            {"exclude_creature_types", Json::array({"undead"})},
            {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}}, {"immune_on_end", true},
            {"age_on_fail_by", {{"margin", margin}, {"dice", "1d4"}, {"years_per", 10}}},
            {"apply", Json::array({{{"condition", "frightened"},
                                    {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                    {"repeat_save", "end_of_turn"}}})}};
}
Json body(const std::string& id, Json action, const char* type = "undead", int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["type"] = type;
    m["hit_points"]["average"] = 80;
    m["actions"].push_back(std::move(action));
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) {
    return lib.add(m, "monster", "", version);
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
    for (int i = 0; i < 12 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("horrifying visage loader: the save shape, and the refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, body("t.ghost", visage())));
    const auto* arch = lib.find("t.ghost");
    REQUIRE(arch);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].area);
    CHECK(arch->save_actions[0].range_ft == 0);
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.ghost#horrifying_visage"));
    REQUIRE(d);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
    CHECK(d->area == rules5e::AreaShape::Sphere);
    CHECK(d->area_size_ft == 60);
    CHECK(d->range_ft == 0);
    CHECK(d->save_ability == rules5e::Ability::Wis);
    CHECK(d->fixed_dc == 13);
    CHECK(d->condition == Condition::Frightened);
    CHECK(d->repeat_save == rules5e::RepeatSave::EndOfTurn);
    CHECK(d->duration_rounds == 10);
    CHECK(d->target_sees_source);
    CHECK(d->excluded_types == rules5e::bit(rules5e::CreatureType::Undead));
    CHECK(d->success_immunity_hours == 24);
    CHECK(d->success_immunity_scope == 1);
    CHECK(d->immune_on_effect_end);
    CHECK(d->age_margin == 5);
    CHECK(d->age_dice.count == 1);
    CHECK(d->age_dice.sides == 4);
    CHECK(d->age_years_per == 10);

    auto err = [&](const char* id, Json act) {
        const auto r = add(lib, body(id, std::move(act)));
        REQUIRE(r);
        return *r;
    };
    Json bad = visage();
    bad["area"]["shape"] = "cone";
    CHECK(err("t.cone", bad).find("age_on_fail_by needs a sphere area") != std::string::npos);
    bad = visage();
    bad.erase("immune_on_end");
    CHECK(err("t.noend", bad).find("age_on_fail_by needs immune_on_end") != std::string::npos);
    bad = visage();
    bad["age_on_fail_by"]["dice"] = "0d4";
    CHECK(err("t.dice", bad).find("bad age_on_fail_by dice") != std::string::npos);
    bad = visage();
    bad["exclude_creature_types"] = Json::array({"not-a-type"});
    CHECK(err("t.type", bad).find("bad exclude_creature_types") != std::string::npos);
    bad = visage();
    bad["also_on_fail_by"] = {{"margin", 5}, {"condition", "paralyzed"}};
    CHECK(err("t.also", bad).find("also_on_fail_by takes no area") != std::string::npos);
    const auto old = add(lib, body("t.old", visage(), "undead", 4), 4);
    REQUIRE(old);
    CHECK(old->find("need a monster@5 file") != std::string::npos);
}

TEST_CASE("horrifying visage: within 60 feet, non-undead, fail ages and frightens") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 40;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(add(lib, body("t.ghost", visage(30))));
    auto typed = [&](const char* id, const char* type) {
        Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
        m["$schema"] = "monster@5";
        m["id"] = id;
        m["type"] = type;
        m["hit_points"]["average"] = 80;
        return m;
    };
    REQUIRE_FALSE(add(lib, typed("t.skeleton", "undead")));
    REQUIRE_FALSE(add(lib, typed("t.human", "humanoid")));
    // Ghost at (2,2). Fighter 10 ft away. Skeleton 5 ft away. Humanoid at exactly 60 ft. One at 65 ft.
    auto ch = arena({{kFighter, {4, 2}}, {"t.ghost", {2, 2}}, {"t.skeleton", {3, 2}},
                     {"t.human", {2, 14}}, {"t.human", {2, 15}}});
    ecs::Registry reg;
    auto combat = Combat::create(reg, ch, lib, 3);
    REQUIRE(combat);
    const CombatantId near = ch.spawns[0].stable_id;
    const CombatantId who = ch.spawns[1].stable_id;
    const CombatantId skel = ch.spawns[2].stable_id;
    const CombatantId edge = ch.spawns[3].stable_id;
    const CombatantId out = ch.spawns[4].stable_id;
    advance_to(*combat, who);
    Command cast;
    cast.aim = true;
    cast.to = {4, 2};
    cast.request = {who, ActionKind::CastSpell};
    cast.request.spell_id = rules5e::spell_id_of("t.ghost#horrifying_visage");
    auto r = combat->submit(cast);
    while (r.needs) r = combat->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    auto aged = [](uint16_t y) { return y == 10 || y == 20 || y == 30 || y == 40; };
    const auto* n = rules5e::find(combat->state(), near);
    const auto* e = rules5e::find(combat->state(), edge);
    const auto* u = rules5e::find(combat->state(), skel);
    const auto* o = rules5e::find(combat->state(), out);
    REQUIRE(n); REQUIRE(e); REQUIRE(u); REQUIRE(o);
    CHECK(n->has(Condition::Frightened));
    CHECK(e->has(Condition::Frightened));
    CHECK(aged(n->aged_years));
    CHECK(aged(e->aged_years));
    CHECK_FALSE(u->has(Condition::Frightened));
    CHECK(u->aged_years == 0);
    CHECK_FALSE(o->has(Condition::Frightened));
    CHECK(o->aged_years == 0);
    int saves = 0;
    for (const auto& ev : combat->events()) {
        if (ev.kind != SimEvent::Kind::Rules) continue;
        if (ev.rules.kind == EventKind::SavingThrowRolled) ++saves;
    }
    CHECK(saves == 2);
    CHECK(combat->state().action_immunities.empty());
}
