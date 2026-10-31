// B16 content: Charm and Draining Kiss save_effects. No succubus.json.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json charm(int dc = 30) {
    return {{"id", "charm"}, {"name", "Charm"}, {"kind", "save_effect"}, {"range_ft", 30},
            {"save", {{"ability", "wis"}, {"dc", dc}}}, {"magical", true}, {"caster_sees_target", true},
            {"target_creature_types", Json::array({"humanoid"})}, {"control", "command"}, {"charm_command", true},
            {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}},
            {"apply", Json::array({{{"condition", "charmed"},
                                    {"duration", {{"kind", "rounds"}, {"n", 14400}}},
                                    {"repeat_save", "on_damage"}}})}};
}
Json kiss(int dc = 30) {
    return {{"id", "draining_kiss"}, {"name", "Draining Kiss"}, {"kind", "save_effect"}, {"range_ft", 5},
            {"save", {{"ability", "con"}, {"dc", dc}}}, {"magical", true}, {"on_success", "half"},
            {"damage", Json::array({{{"dice", "5d10+5"}, {"type", "psychic"}}})}, {"drain_max_hp", true},
            {"target_requires_any", Json::array({"charmed_by_self", "willing"})}};
}
Json fiend(const std::string& id, std::vector<Json> actions, int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["hit_points"]["average"] = 80;
    for (auto& a : actions) m["actions"].push_back(std::move(a));
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) {
    return lib.add(m, "monster", "", version);
}
}  // namespace

TEST_CASE("charm and draining kiss loader: the save shapes, and the refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, fiend("t.fiend", {charm(), kiss()})));
    const auto* arch = lib.find("t.fiend");
    REQUIRE(arch);
    REQUIRE(arch->save_actions.size() == 2);
    const auto* ch = lib.spells()->find(rules5e::spell_id_of("t.fiend#charm"));
    const auto* dk = lib.spells()->find(rules5e::spell_id_of("t.fiend#draining_kiss"));
    REQUIRE(ch);
    REQUIRE(dk);
    CHECK(ch->charm_command);
    CHECK(ch->magical);
    CHECK(ch->caster_sees_target);
    CHECK(ch->control == rules5e::ControlKind::Command);
    CHECK(ch->condition == Condition::Charmed);
    CHECK(ch->repeat_save == rules5e::RepeatSave::OnDamage);
    CHECK(ch->duration_rounds == 14400);
    CHECK(ch->success_immunity_hours == 24);
    CHECK(ch->success_immunity_scope == 1);
    CHECK(ch->target_types == rules5e::bit(rules5e::CreatureType::Humanoid));
    CHECK(dk->save_drain_max_hp);
    CHECK(dk->on_save == rules5e::OnSave::Half);
    CHECK(dk->damage_type == rules5e::DamageType::Psychic);
    CHECK(dk->dice.count == 5);
    CHECK(dk->dice.sides == 10);
    CHECK(dk->dice.bonus == 5);
    CHECK(dk->target_requires_any == (rules5e::kTargetGateCharmedBySelf | rules5e::kTargetGateWilling));
    CHECK(dk->fixed_dc == 30);

    auto err = [&](const char* id, Json act) {
        const auto r = add(lib, fiend(id, {std::move(act)}));
        REQUIRE(r);
        return *r;
    };
    Json bad = charm();
    bad.erase("immune_on_success");
    CHECK(err("t.no_imm", bad).find("charm_command needs immune_on_success") != std::string::npos);
    bad = charm();
    bad["control"] = "turned";
    CHECK(err("t.turned", bad).find("charm_command needs control command") != std::string::npos);
    bad = kiss();
    bad.erase("damage");
    bad.erase("on_success");
    CHECK(err("t.nodmg", bad).find("drain_max_hp needs damage") != std::string::npos);
    bad = kiss();
    bad["target_requires_any"] = Json::array({"restrained"});
    CHECK(err("t.gate", bad).find("grappled_by_self, charmed_by_self, or willing") != std::string::npos);
    Json old_act = charm();
    old_act.erase("immune_on_success");
    old_act.erase("caster_sees_target");
    const auto old = add(lib, fiend("t.old", {std::move(old_act)}, 4), 4);
    REQUIRE(old);
    CHECK(old->find("charm_command needs a monster@5 file") != std::string::npos);
}

TEST_CASE("charm then kiss: the loaded actions charm a humanoid and reduce the maximum") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 40;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(add(lib, fiend("t.fiend", {charm(), kiss()})));
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    c.spawns.push_back({kFighter, {5, 5}, world::spawn_stable_id(c.coord, 0), ""});
    c.spawns.push_back({"t.fiend", {6, 5}, world::spawn_stable_id(c.coord, 1), ""});
    c.content_hash = world::compute_content_hash(c);
    ecs::Registry reg;
    auto combat = Combat::create(reg, c, lib, 3);
    REQUIRE(combat);
    const CombatantId pc = c.spawns[0].stable_id;
    const CombatantId who = c.spawns[1].stable_id;
    auto end_turn = [&] {
        Command x;
        x.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(x).ok);
    };
    auto to = [&](CombatantId id) {
        for (int i = 0; i < 8 && rules5e::current_actor(combat->state()) != id; ++i) end_turn();
        REQUIRE(rules5e::current_actor(combat->state()) == id);
    };
    to(who);
    const uint32_t charm_id = rules5e::spell_id_of("t.fiend#charm");
    Command cast;
    cast.request = {who, ActionKind::CastSpell, pc};
    cast.request.spell_id = charm_id;
    auto r = combat->submit(cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find(combat->state(), pc)->has(Condition::Charmed));
    CHECK(rules5e::charmed_by(combat->state(), pc, who));
    end_turn();
    to(who);
    Command kiss_cast;
    kiss_cast.request = {who, ActionKind::CastSpell, pc};
    kiss_cast.request.spell_id = rules5e::spell_id_of("t.fiend#draining_kiss");
    auto k = combat->submit(kiss_cast);
    REQUIRE_MESSAGE(k.ok, k.error);
    CHECK(rules5e::find(combat->state(), pc)->hp_max_reduction > 0);
    CHECK(rules5e::find(combat->state(), pc)->hp < 40);
}
