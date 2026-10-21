// VERSION_BUMP_BATCH 7.2zc: spell / feature gap batch 1 content (T&C agreed shapes): Disciple of Life (static heal_bonus) and
// the loader's refusals; a feature apply_modifier refuses the spell-only mark keys (scope target marked, ability chosen).
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
Json cleric(const std::string& id, Json effect) {
    Json d = load_json("content/packs/srd51.core/characters/cleric_level_1.json");
    d["id"] = id;
    d["features"].push_back({{"id", "disciple_of_life"}, {"name", "Disciple of Life"}, {"description", "x"}, {"effect", effect}});
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
const Json kDisciple = Json::parse(R"J({"kind": "heal_bonus", "flat": 2, "per_spell_level": true})J");
}  // namespace

TEST_CASE("7.2zc content: Disciple of Life heal_bonus compiles to the static Combatant fields") {
    auto lib = lib0();
    const auto e = lib.add(cleric("t.cleric", kDisciple), "character");
    REQUIRE_MESSAGE(!e, (e ? *e : ""));
    const Archetype* a = lib.find("t.cleric");
    REQUIRE(a);
    CHECK(a->base.has_heal_bonus);
    CHECK(a->base.heal_bonus_flat == 2);
    CHECK(a->base.heal_bonus_per_level);
    Json zero = Json::parse(R"J({"kind": "heal_bonus", "per_spell_level": true})J");
    REQUIRE_FALSE(lib.add(cleric("t.cleric0", zero), "character"));
    const Archetype* z = lib.find("t.cleric0");
    REQUIRE(z);
    CHECK(z->base.has_heal_bonus); CHECK(z->base.heal_bonus_flat == 0);
    REQUIRE_FALSE(lib.add(load_json("content/packs/srd51.core/characters/cleric_level_1.json"), "character"));
    CHECK_FALSE(lib.find("srd51.character.cleric_level_1")->base.has_heal_bonus);
}

TEST_CASE("7.2zc content: heal_bonus in an older file is inert") {
    auto lib = lib0();
    Json d = cleric("t.old", kDisciple);
    d["$schema"] = "character@3";
    const auto e = lib.add(d, "character");
    if (e) { CHECK(e->find("heal_bonus") == std::string::npos); return; }   // an @3 document may fail for an unrelated key
    const Archetype* a = lib.find("t.old");
    REQUIRE(a);
    CHECK_FALSE(a->base.has_heal_bonus);
    CHECK(std::find(a->inert_features.begin(), a->inert_features.end(), "disciple_of_life (heal_bonus)") != a->inert_features.end());
}

TEST_CASE("7.2zc loader refusals (heal_bonus; spell-only mark keys on a feature)") {
    auto bad = [](Json eff, const char* want) {
        const std::string e = err_of(cleric("t.bad", eff));
        INFO(e); CHECK(e.find(want) != std::string::npos);
    };
    bad(Json::parse(R"J({"kind": "heal_bonus", "flat": 2})J"), "disciple_of_life: heal_bonus needs per_spell_level (true or false)");
    bad(Json::parse(R"J({"kind": "heal_bonus", "flat": 2, "per_spell_level": 1})J"), "heal_bonus needs per_spell_level (true or false)");
    bad(Json::parse(R"J({"kind": "heal_bonus", "flat": 11, "per_spell_level": true})J"), "heal_bonus: flat must be an integer 0..10");
    bad(Json::parse(R"J({"kind": "heal_bonus", "flat": -1, "per_spell_level": true})J"), "heal_bonus: flat must be an integer 0..10");
    bad(Json::parse(R"J({"kind": "heal_bonus", "per_spell_level": true, "range": 30})J"), "heal_bonus: unknown key 'range'");
    bad(Json::parse(R"J({"kind": "apply_modifier", "scope": {"target": "marked"},
        "modifier": {"stat": "damage", "die": "1d6", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}}})J"),
        "scope: target marked is for spells only");
    bad(Json::parse(R"J({"kind": "apply_modifier",
        "modifier": {"stat": "ability_check", "mode": "advantage", "ability": "chosen", "applies_to": "self", "duration": {"kind": "rounds", "n": 10}}})J"),
        "modifier ability chosen needs scope target marked");
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4"; m["id"] = "t.heal_goblin";
    m["traits"] = Json::array({{{"id", "disciple"}, {"name", "D"}, {"description", "x"}, {"effect", kDisciple}}});
    const std::string e = err_of(m, "monster");
    CHECK(e.find("disciple: heal_bonus: not valid on monsters") != std::string::npos);
}
