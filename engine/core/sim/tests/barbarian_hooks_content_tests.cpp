// VERSION_BUMP_BATCH 7.2z: barbarian hooks content (T&C #201 shapes): Rage, Danger Sense, Frenzy.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/content.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ModStat;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
const Json kRageEffect = Json::parse(R"J({"kind": "apply_modifier", "cost": "bonus_action", "usage": {"kind": "pool", "pool": "rage"}, "scope": {"attack": "melee_weapon", "ability": "str"}, "ends_early": ["no_attack_or_damage_since_last_turn", "unconscious"], "blocks": ["casting", "concentration"], "advantage_on": {"checks": ["str"], "saves": ["str"]}, "on_end": {"exhaustion": 1}, "modifiers": [{"stat": "damage", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}, "bonus_table": {"class": "barbarian", "by_level": [{"level": 1, "value": 2}, {"level": 9, "value": 3}]}}, {"stat": "resistance", "damage_type": "bludgeoning", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}}, {"stat": "resistance", "damage_type": "piercing", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}}, {"stat": "resistance", "damage_type": "slashing", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}}]})J");
const Json kDangerSense = Json::parse(R"J({"kind": "apply_modifier", "passive": true, "advantage_on": {"saves": ["dex"]}, "when": {"save": "dex", "source_seen": true, "not_conditions": ["blinded", "deafened", "incapacitated"]}})J");
Json barbarian(const std::string& id) {
    Json d = load_json("content/packs/srd51.core/characters/barbarian_level_5.json");
    d["id"] = id;
    d["resources"] = Json::array({{{"id", "rage"}, {"max", 3}, {"recharge", "long_rest"}}});
    for (auto& f : d["features"]) {
        if (f["id"] == "rage") f["effect"] = kRageEffect;
        if (f["id"] == "danger_sense") f["effect"] = kDangerSense;
        if (f["id"] == "frenzy") f["effect"] = {{"kind", "bonus_attack"}, {"requires_effect", "rage"}};
    }
    return d;
}
Json& feature(Json& d, const char* id) {
    for (auto& f : d["features"]) if (f["id"] == id) return f;
    FAIL("no feature");
    return d;
}
ContentLibrary lib0() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
}  // namespace

TEST_CASE("7.2z content: Rage rows carry scope / ends_early / blocks / on_end; advantage_on rows; Frenzy; Danger Sense") {
    auto lib = lib0();
    REQUIRE_FALSE(lib.add(barbarian("t.barb"), "character"));
    const Archetype* a = lib.find("t.barb");
    REQUIRE(a);
    REQUIRE(a->feature_modifiers.size() == 1);
    const auto& rows = a->feature_modifiers.begin()->second;
    REQUIRE(rows.size() == 6);   // damage, 3 resistances, Str check and Str save advantage
    CHECK(rows[0].stat == ModStat::Damage);
    CHECK(rows[0].bonus == 2);
    CHECK(rows[0].scope == 1);
    CHECK(rows[0].scope_ability == 1);
    CHECK(rows[1].scope_ability == 0);   // a resistance row ignores the attack scope
    for (const auto& m : rows) {
        CHECK(m.ends_early == 3); CHECK(m.blocks == 3); CHECK(m.on_end_exhaustion == 1);
        CHECK(m.source == rows[0].source);
    }
    CHECK(rows[4].stat == ModStat::AbilityCheck);
    CHECK(rows[5].stat == ModStat::Save);
    CHECK(rows[5].mode == rules5e::RollMode::Advantage);
    CHECK(rows[5].ability == rules5e::Ability::Str);
    CHECK(rows[5].rounds == 10);
    REQUIRE(a->feature_specs.size() == 1);
    CHECK(a->feature_specs.begin()->second.pool == rules5e::spell_id_of("rage"));
    REQUIRE(a->base.save_advantages.size() == 1);
    CHECK(a->base.save_advantages[0].ability == rules5e::Ability::Dex);
    CHECK(a->base.save_advantages[0].source_seen);
    CHECK(a->base.save_advantages[0].not_conditions == (rules5e::bit(rules5e::Condition::Blinded) |
          rules5e::bit(rules5e::Condition::Deafened) | rules5e::bit(rules5e::Condition::Incapacitated)));
    int frenzy = 0;
    for (const auto& b : a->base.bonus_attacks)
        if (b.requires_modifier == rules5e::spell_id_of("t.barb#rage#m0")) { ++frenzy; CHECK(b.any_melee); }
    CHECK(frenzy == 1);
}

TEST_CASE("7.2z content: load errors") {
    auto lib = lib0();
    auto bad = [&](const char* id, auto edit) { Json d = barbarian(id); edit(d); CHECK(lib.add(d, "character")); };
    bad("t.b1", [](Json& d) { feature(d, "frenzy")["effect"]["requires_effect"] = "danger_sense"; });   // passive, not activatable
    bad("t.b2", [](Json& d) { feature(d, "frenzy")["effect"]["requires_effect"] = "nope"; });
    bad("t.b3", [](Json& d) { feature(d, "rage")["effect"]["scope"] = Json::object(); });
    bad("t.b4", [](Json& d) { feature(d, "rage")["effect"]["scope"]["ability"] = "con"; });
    bad("t.b5", [](Json& d) { feature(d, "rage")["effect"]["ends_early"] = Json::array({"unconscious", "unconscious"}); });
    bad("t.b6", [](Json& d) { feature(d, "rage")["effect"]["on_end"]["exhaustion"] = 7; });
    bad("t.b7", [](Json& d) { feature(d, "danger_sense")["effect"]["cost"] = "action"; });
    bad("t.b8", [](Json& d) { feature(d, "danger_sense")["effect"]["when"]["save"] = "str"; });
    bad("t.b9", [](Json& d) { feature(d, "rage")["effect"]["when"] = {{"save", "dex"}}; });
    bad("t.b10", [](Json& d) { feature(d, "rage")["effect"]["blocks"] = Json::array({"flying"}); });
}
