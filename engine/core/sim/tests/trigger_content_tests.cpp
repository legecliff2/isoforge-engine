// Row 9a, sim side: `on_event` effects and `on_hit[]` riders compile into rules5e::Trigger rows on the archetype
// (Combatant::triggers via the base combatant), duplicate ids are load errors, and Combat::hash folds the trigger
// bookkeeping under 0xA6 only when a combatant has triggers.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::PredicateKind;
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json sneak(Json extra = Json::object()) {
    Json e = {{"kind", "on_event"}, {"on", "hit"}, {"limit", "once_per_turn"},
              {"when", Json::array({{{"kind", "weapon"}, {"is", "finesse_or_ranged"}}, {{"kind", "advantage_or_ally_adjacent"}},
                                    {{"kind", "target_creature_type"}, {"creature_types", Json::array({"undead", "fiend"})}},
                                    {{"kind", "damage_type_not"}, {"types", Json::array({"radiant", "fire"})}},
                                    {{"kind", "moved_at_least_ft"}, {"ft", 20}}, {{"kind", "self_hp_below"}, {"num", 1}, {"den", 2}},
                                    {{"kind", "not_crit"}}})},
              {"effects", Json::array({{{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "1d6"}, {"type", "same_as_weapon"}}})},
                                        {"scale", {{"per_levels", {{"class", "rogue"}, {"every", 2}, {"add_dice", "1d6"}}}}}}})}};
    for (auto& [k, v] : extra.items()) e[k] = v;
    return e;
}
Json goblin(const std::string& id, std::vector<Json> traits = {}, Json bite_on_hit = Json()) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    for (auto& a : m["actions"]) if (a.contains("to_hit")) a["to_hit"] = 30;   // always hits
    for (std::size_t i = 0; i < traits.size(); ++i)
        m["traits"].push_back({{"id", "t" + std::to_string(i)}, {"name", "T"}, {"description", "x"}, {"effect", traits[i]}});
    if (!bite_on_hit.is_null()) m["actions"][1]["on_hit"] = bite_on_hit;   // the shortbow (attack index 1)
    return m;
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
ContentLibrary lib_with(const std::vector<Json>& docs) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;   // survives the whole scripted fight
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : docs) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
// Plays `turns` goblin turns attacking with `attack_index`; returns the trigger ids seen in FeatureTriggered events.
std::vector<uint32_t> play(Combat& c, CombatantId g, CombatantId f, uint8_t attack_index, int turns) {
    for (int t = 0; t < turns; ++t) {
        for (int i = 0; i < 6 && rules5e::current_actor(c.state()) != g; ++i) end_turn(c);
        REQUIRE(rules5e::current_actor(c.state()) == g);
        Command a;
        a.request = {g, ActionKind::Attack, f};
        a.request.attack_index = attack_index;
        auto r = c.submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        r = c.settle(nullptr);
        end_turn(c);
    }
    std::vector<uint32_t> ids;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered) ids.push_back(uint32_t(e.rules.a));
    return ids;
}
} // namespace

TEST_CASE("triggers content: on_event and on_hit[] compile to Trigger rows") {
    Json rider = Json::array({{{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "1d4"}, {"type", "fire"}}})}},
                              {{"kind", "set_hp"}, {"value", 1}}});
    auto lib = lib_with({goblin("t.gob", {sneak({{"choice", "ask"}})}, rider)});
    const auto& tr = lib.find("t.gob")->base.triggers;
    REQUIRE(tr.size() == 3);   // the trait, then the two shortbow riders
    CHECK(tr[0].id == spell_id_of("t.gob#t0#0"));
    CHECK(tr[0].on == rules5e::TriggerOn::Hit);
    CHECK(tr[0].limit == rules5e::TriggerLimit::OncePerTurn);
    CHECK(tr[0].choice == 1);
    REQUIRE(tr[0].when.size() == 7);
    CHECK(tr[0].when[0].kind == PredicateKind::Weapon);
    CHECK(tr[0].when[0].a == int16_t(rules5e::WeaponIs::FinesseOrRanged));
    CHECK(tr[0].when[1].kind == PredicateKind::AdvantageOrAllyAdjacent);
    CHECK(tr[0].when[2].mask == (rules5e::bit(rules5e::CreatureType::Undead) | rules5e::bit(rules5e::CreatureType::Fiend)));
    CHECK(tr[0].when[3].kind == PredicateKind::DamageTypeNot);
    CHECK(tr[0].when[3].mask == (rules5e::bit(rules5e::DamageType::Radiant) | rules5e::bit(rules5e::DamageType::Fire)));
    CHECK(tr[0].when[4].a == 20);
    CHECK((tr[0].when[5].a == 1 && tr[0].when[5].b == 2));
    CHECK(tr[0].when[6].kind == PredicateKind::NotCrit);
    REQUIRE(tr[0].effects.size() == 1);
    CHECK(tr[0].effects[0].same_as_weapon);
    CHECK(tr[0].effects[0].dice == rules5e::DiceExpr{1, 6, 0});   // a monster has no level: no scaling
    CHECK(tr[1].id == spell_id_of("t.gob#shortbow#0"));
    REQUIRE(tr[1].when.size() == 1);
    CHECK(tr[1].when[0].kind == PredicateKind::AttackIs);
    CHECK(tr[1].when[0].a == 1);                                   // bound to the shortbow
    CHECK(tr[1].effects[0].type == rules5e::DamageType::Fire);
    CHECK(tr[1].effects[0].dice == rules5e::DiceExpr{1, 4, 0});
    CHECK(tr[2].id == spell_id_of("t.gob#shortbow#1"));
    CHECK(tr[2].effects[0].kind == rules5e::RiderKind::SetHp);     // stored for 9b, not fired yet
    CHECK(tr[2].effects[0].value == 1);
    CHECK(lib.find("t.gob")->unsupported_features.empty());
}

TEST_CASE("triggers content: scale.per_levels resolves against the owner's class level") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json ch = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    ch["$schema"] = "character@4";
    ch["id"] = "t.rogue";
    ch["classes"] = Json::array({{{"class", "rogue"}, {"level", 5}, {"hit_die", "d8"}}});
    ch["features"].push_back({{"id", "sneak"}, {"name", "Sneak"}, {"description", "x"}, {"effect", sneak()}});
    const auto e = lib.add(ch, "character");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    const auto& t = lib.find("t.rogue")->base.triggers;
    REQUIRE(t.size() == 1);
    CHECK(t[0].effects[0].dice == rules5e::DiceExpr{3, 6, 0});   // 1d6 + (5 - 1) / 2 x 1d6
    Json bad = ch;
    bad["id"] = "t.rogue2";
    bad["features"].back()["effect"]["effects"][0]["scale"]["per_levels"]["add_dice"] = "1d8";   // sides differ
    CHECK(lib.add(bad, "character"));
}

TEST_CASE("triggers content: load errors and listed-not-compiled cases") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    // A trait id equal to an attack id with on_hit riders collides: 'arch#owner#index' is the same string.
    Json clash = goblin("t.clash", {}, Json::array({{{"kind", "set_hp"}, {"value", 1}}}));
    clash["traits"].push_back({{"id", "shortbow"}, {"name", "T"}, {"description", "x"}, {"effect", sneak()}});
    const auto e = lib.add(clash, "monster");
    REQUIRE(e);
    CHECK(e->find("collision") != std::string::npos);
    CHECK(e->find("t.clash#shortbow#0") != std::string::npos);
    CHECK_FALSE(lib.find("t.clash"));
    for (const auto& patch : std::vector<std::pair<const char*, Json>>{
             {"on", {{"on", "sometimes"}}}, {"limit", {{"limit", "twice"}}}, {"choice", {{"choice", "maybe"}}},
             {"empty effects", {{"effects", Json::array()}}},
             {"predicate", {{"when", Json::array({{{"kind", "vibes"}}})}}},
             {"rider kind", {{"effects", Json::array({{{"kind", "grapple"}}})}}}}) {
        CAPTURE(patch.first);
        Json g = goblin("t.bad", {sneak(patch.second)});
        const auto er = lib.add(g, "monster");
        REQUIRE(er);
        CHECK(er->find("t.bad") != std::string::npos);
        CHECK_FALSE(lib.find("t.bad"));
    }
    // usage (monster predicates PR): at_will compiles with no cost; recharge compiles too (deferred-monsters PR); the file still loads
    REQUIRE_FALSE(lib.add(goblin("t.usage", {sneak({{"usage", {{"kind", "at_will"}}}})}), "monster"));
    CHECK(lib.find("t.usage")->base.triggers.size() == 1);
    CHECK(lib.find("t.usage")->unsupported_features.empty());
    REQUIRE_FALSE(lib.add(goblin("t.usage2", {sneak({{"usage", {{"kind", "recharge"}, {"min", 5}}}})}), "monster"));
    CHECK(lib.find("t.usage2")->base.triggers.size() == 1);   // recharge compiles since the deferred-monsters PR
    CHECK(lib.find("t.usage2")->unsupported_features.empty());
    // a file that declared < 4 carries no on_hit[]
    Json old = goblin("t.old", {}, Json::array({{{"kind", "set_hp"}, {"value", 1}}}));
    REQUIRE_FALSE(lib.add(old, "monster", "", 3));
    CHECK(lib.find("t.old")->base.triggers.empty());
}

TEST_CASE("triggers in a fight: an on_hit rider fires only on its own attack; hash folds 0xA6 only with triggers") {
    Json rider = Json::array({{{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "1d4"}, {"type", "fire"}}})}}});
    auto lib = lib_with({goblin("t.gob", {}, rider), goblin("t.plain")});
    const uint32_t bound = spell_id_of("t.gob#shortbow#0");
    for (uint8_t attack : {uint8_t(0), uint8_t(1)}) {
        auto ch = arena({{"t.gob", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 8);
        REQUIRE(c);
        CHECK(rules5e::has_trigger_state(c->state()));
        const auto ids = play(*c, ch.spawns[0].stable_id, ch.spawns[1].stable_id, attack, 6);
        const auto n = std::count(ids.begin(), ids.end(), bound);
        if (attack == 1) CHECK(n >= 1); else CHECK(n == 0);
        // replay reproduces hash and log_hash
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, ch, lib, 8, c->log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->hash() == c->hash());
        CHECK(rep->log_hash() == c->log_hash());
    }
    auto ch = arena({{"t.plain", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 8);
    REQUIRE(c);
    CHECK_FALSE(rules5e::has_trigger_state(c->state()));   // no triggers: no fold, goldens unchanged
}
