// VERSION_BUMP_BATCH 7.2zb: monk / warlock hooks content (T&C #207 / #210 shapes): Patient Defense / Step of the Wind
// (bonus_action_grant usage), Open Hand Technique (attack_from_feature, pick_one, forced_move / apply_modifier riders),
// Deflect Missiles (reduce_damage applies_to_attack), Agonizing / Repelling Blast (spell_mod), and the loader's refusals.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/content.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
const Json kOpenHand = Json::parse(R"J({"kind": "on_event", "on": "hit", "choice": "ask", "pick_one": true,
  "when": [{"kind": "attack_from_feature", "feature": "flurry_of_blows"}],
  "effects": [{"kind": "save_condition", "save": {"ability": "dex", "dc": 13}, "apply": [{"condition": "prone"}]},
              {"kind": "forced_move", "save": {"ability": "str", "dc": 13}, "mode": "push", "feet": 15},
              {"kind": "apply_modifier", "modifier": {"stat": "no_reactions", "duration": {"kind": "until_end_of_caster_next_turn"}}}]})J");
Json& feature(Json& d, const char* id) {
    for (auto& f : d["features"]) if (f["id"] == id) return f;
    FAIL("no feature");
    return d;
}
Json monk(const std::string& id) {
    Json d = load_json("content/packs/srd51.core/characters/monk_level_5.json");
    d["id"] = id;
    feature(d, "patient_defense")["effect"] = {{"kind", "bonus_action_grant"}, {"actions", {"dodge"}}, {"usage", {{"kind", "pool"}, {"pool", "ki"}}}};
    feature(d, "step_of_the_wind")["effect"] = {{"kind", "bonus_action_grant"}, {"actions", {"dash", "disengage"}}, {"usage", {{"kind", "pool"}, {"pool", "ki"}, {"cost", 1}}}};
    feature(d, "open_hand_technique")["effect"] = kOpenHand;
    d["reactions"][0]["effect"]["applies_to_attack"] = {"ranged_weapon"};
    return d;
}
Json warlock(const std::string& id) {
    Json d = load_json("content/packs/srd51.core/characters/warlock_level_5.json");
    d["id"] = id;
    feature(d, "agonizing_blast")["effect"] = {{"kind", "spell_mod"}, {"spell", "srd51.spell.eldritch_blast"}, {"damage_bonus_ability", "cha"}};
    feature(d, "repelling_blast")["effect"] = {{"kind", "spell_mod"}, {"spell", "srd51.spell.eldritch_blast"}, {"on_hit_push_ft", 10}};
    return d;
}
ContentLibrary lib0() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
std::string err_of(Json d, const char* type = "character") {
    auto lib = lib0();
    auto e = lib.add(d, type);
    return e ? *e : std::string();
}
}  // namespace

TEST_CASE("7.2zb content: Patient Defense / Step of the Wind compile to ki-costed bonus-action grants") {
    auto lib = lib0();
    const auto e = lib.add(monk("t.monk"), "character");
    REQUIRE_MESSAGE(!e, (e ? *e : ""));
    const Archetype* a = lib.find("t.monk");
    REQUIRE(a);
    using AK = rules5e::ActionKind;
    for (AK k : {AK::Dodge, AK::Dash, AK::Disengage}) CHECK((a->base.bonus_action_grants & rules5e::bit(k)) != 0);
    REQUIRE(a->base.bonus_action_costs.size() == 3);
    for (const auto& c : a->base.bonus_action_costs) { CHECK(c.pool == rules5e::spell_id_of("ki")); CHECK(c.cost == 1); }
    CHECK(a->base.bonus_action_costs[0].kind == AK::Dodge);
}

TEST_CASE("7.2zb content: Open Hand Technique: AttackFromFeature on Flurry's bonus attack id, pick_one, three riders") {
    auto lib = lib0();
    REQUIRE_FALSE(lib.add(monk("t.monk"), "character"));
    const Archetype* a = lib.find("t.monk");
    REQUIRE(a);
    const rules5e::Trigger* oh = nullptr;
    for (const auto& t : a->base.triggers) if (t.pick_one) oh = &t;
    REQUIRE(oh);
    CHECK(oh->choice == 1);
    REQUIRE(oh->when.size() == 1);
    CHECK(oh->when[0].kind == rules5e::PredicateKind::AttackFromFeature);
    CHECK(oh->when[0].mask == rules5e::spell_id_of("t.monk#bonus#flurry_of_blows"));
    bool flurry = false;
    for (const auto& b : a->base.bonus_attacks) flurry = flurry || b.id == oh->when[0].mask;
    CHECK(flurry);
    REQUIRE(oh->effects.size() == 3);
    CHECK(oh->effects[0].kind == rules5e::RiderKind::SaveCondition);
    CHECK(oh->effects[1].kind == rules5e::RiderKind::Effect);
    CHECK(oh->effects[2].kind == rules5e::RiderKind::Effect);
}

TEST_CASE("7.2zb content: Deflect Missiles applies_to_attack and spell_mod rows") {
    auto lib = lib0();
    REQUIRE_FALSE(lib.add(monk("t.monk"), "character"));
    const Archetype* m = lib.find("t.monk");
    REQUIRE(m);
    REQUIRE(m->base.reactions.size() == 1);
    CHECK(m->base.reactions[0].attack_kinds == rules5e::kAttackKindRangedWeapon);
    const auto e = lib.add(warlock("t.lock"), "character");
    REQUIRE_MESSAGE(!e, (e ? *e : ""));
    const Archetype* w = lib.find("t.lock");
    REQUIRE(w);
    REQUIRE(w->base.spell_mods.size() == 2);
    const uint32_t eb = rules5e::spell_id_of("srd51.spell.eldritch_blast");
    CHECK(w->base.spell_mods[0].spell_id == eb); CHECK(w->base.spell_mods[0].damage_ability == uint8_t(rules5e::Ability::Cha));
    CHECK(w->base.spell_mods[0].push_ft == 0);
    CHECK(w->base.spell_mods[1].spell_id == eb); CHECK(w->base.spell_mods[1].damage_ability == 0xFF);
    CHECK(w->base.spell_mods[1].push_ft == 10);
}

TEST_CASE("7.2zb content: the unchanged pack still loads (empty effects stay descriptive, no costs, no mods)") {
    auto lib = lib0();
    const auto e = lib.add(load_json("content/packs/srd51.core/characters/monk_level_5.json"), "character");
    REQUIRE_FALSE(e);
}

TEST_CASE("7.2zb loader refusals") {
    {   Json d = monk("t.m1"); feature(d, "patient_defense")["effect"]["usage"]["pool"] = "nope";
        CHECK(err_of(d).find("unknown (or unlimited) pool 'nope'") != std::string::npos); }
    {   Json d = monk("t.m2"); feature(d, "patient_defense")["effect"]["actions"] = {"hide"};
        CHECK(err_of(d).find("usage covers dash, disengage and dodge only") != std::string::npos); }
    {   Json d = monk("t.m3"); feature(d, "patient_defense")["effect"]["usage"] = {{"kind", "per_day"}, {"uses", 1}};
        CHECK(err_of(d).find("usage must be {kind: pool") != std::string::npos); }
    {   Json d = monk("t.m4"); feature(d, "open_hand_technique")["effect"]["when"][0]["feature"] = "nope";
        CHECK(err_of(d).find("attack_from_feature names unknown feature 'nope'") != std::string::npos); }
    {   Json d = monk("t.m5"); feature(d, "open_hand_technique")["effect"]["choice"] = "auto";
        CHECK(err_of(d).find("pick_one needs choice ask on a hit or crit trigger") != std::string::npos); }
    {   Json d = monk("t.m4b"); feature(d, "open_hand_technique")["effect"]["when"][0]["feature"] = "patient_defense";
        CHECK(err_of(d).find("attack_from_feature 'patient_defense' is not a bonus-attack feature of this document") != std::string::npos); }
    {   Json d = monk("t.m6"); feature(d, "open_hand_technique")["effect"]["effects"][1]["feet"] = 12;
        CHECK(err_of(d).find("feet must be 5..60") != std::string::npos); }
    {   Json d = monk("t.m7"); feature(d, "open_hand_technique")["effect"]["effects"][1]["mode"] = "pull";
        CHECK(err_of(d).find("mode must be push") != std::string::npos); }
    {   Json d = monk("t.m8"); d["reactions"][0]["effect"]["applies_to_attack"] = {"ranged_weapon", "ranged_weapon"};
        CHECK(err_of(d).find("bad or repeated 'ranged_weapon'") != std::string::npos); }
    {   Json d = warlock("t.w1"); feature(d, "repelling_blast")["effect"] = {{"kind", "spell_mod"}, {"spell", "srd51.spell.eldritch_blast"}};
        CHECK(err_of(d).find("spell_mod needs damage_bonus_ability or on_hit_push_ft") != std::string::npos); }
    {   Json d = warlock("t.w2"); feature(d, "repelling_blast")["effect"]["on_hit_push_ft"] = 35;
        CHECK(err_of(d).find("on_hit_push_ft must be 5..30") != std::string::npos); }
    {   Json d = warlock("t.w3"); feature(d, "agonizing_blast")["effect"]["extra"] = 1;
        CHECK(err_of(d).find("spell_mod: unknown key 'extra'") != std::string::npos); }
}
