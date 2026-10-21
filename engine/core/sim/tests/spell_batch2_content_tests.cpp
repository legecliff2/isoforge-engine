// VERSION_BUMP_BATCH 7.2zd: spell batch 2 content (T&C agreed shapes). Preserve Life: heal_pool with per_class_level compiles to
// the split FeatureSpec fields (and its refusals); a feature apply_modifier scope from_types with applies_to attackers.
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
Json cleric(const std::string& id, Json effect, const char* fid = "preserve_life") {
    Json d = load_json("content/packs/srd51.core/characters/cleric_level_2.json");
    d["id"] = id;
    d["resources"] = Json::parse(R"J([{"id": "channel_divinity", "name": "Channel Divinity", "max": 1, "recharge": "short_rest"}])J");
    d["features"].push_back({{"id", fid}, {"name", "Feature"}, {"description", "x"}, {"effect", effect}});
    return d;
}
ContentLibrary lib0() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
std::string err_of(Json d) {
    auto lib = lib0();
    const std::string id = d.value("id", "");
    auto e = lib.add(d, "character");
    if (!e) return std::string();
    return e->rfind(id + ": ", 0) == 0 ? e->substr(id.size() + 2) : *e;   // the document id prefix is the library's
}
const Json kPreserve = Json::parse(R"J({"kind": "heal_pool", "cost": "action", "usage": {"kind": "pool", "pool": "channel_divinity"},
    "per_class_level": 5, "range_ft": 30, "cap": "half_max", "excludes_types": ["undead", "construct"]})J");
const Json kWard = Json::parse(R"J({"kind": "apply_modifier", "cost": "action", "scope": {"from_types": ["fiend", "undead"]},
    "modifier": {"stat": "attack_roll", "mode": "disadvantage", "applies_to": "attackers", "duration": {"kind": "rounds", "n": 10}}})J");
uint32_t fid_of(const Archetype& a, const std::string& id) {
    for (const auto& f : a.active) if (f.id == id) return f.feature_id;
    return 0;
}
}  // namespace

TEST_CASE("7.2zd content: Preserve Life heal_pool per_class_level compiles to the split FeatureSpec") {
    auto lib = lib0();
    const auto e = lib.add(cleric("t.cleric", kPreserve), "character");
    REQUIRE_MESSAGE(!e, (e ? *e : ""));
    const Archetype* a = lib.find("t.cleric");
    REQUIRE(a);
    const uint32_t fid = fid_of(*a, "preserve_life");
    REQUIRE(fid != 0);
    const auto& spec = a->feature_specs.at(fid);
    CHECK(spec.split_pool == 10);   // 5 x cleric level 2
    CHECK(spec.split_range_ft == 30);
    CHECK(spec.split_cap_half);
    CHECK(spec.split_excluded == (rules5e::bit(rules5e::CreatureType::Undead) | rules5e::bit(rules5e::CreatureType::Construct)));
    CHECK(spec.pool == rules5e::spell_id_of("channel_divinity"));
    CHECK_FALSE(spec.heal_pool);
}

TEST_CASE("7.2zd content: heal_pool split refusals") {
    auto with = [](const char* key, Json v) { Json e = kPreserve; if (v.is_null()) e.erase(key); else e[key] = v; return e; };
    CHECK(err_of(cleric("t.c1", with("pool", "channel_divinity"))) == "preserve_life: heal_pool: pool and per_class_level don't mix");
    CHECK(err_of(cleric("t.c2", with("usage", nullptr))) == "preserve_life: heal_pool per_class_level needs a usage pool (Channel Divinity)");
    CHECK(err_of(cleric("t.c3", with("per_class_level", 11))) == "preserve_life: heal_pool per_class_level must be an integer 1..10");
    CHECK(err_of(cleric("t.c4", with("range_ft", 32))) == "preserve_life: heal_pool range_ft must be a multiple of 5 in 5..120");
    CHECK(err_of(cleric("t.c5", with("cap", "full"))) == "preserve_life: heal_pool cap must be half_max");
    CHECK(err_of(cleric("t.c6", with("excludes_types", Json::parse(R"(["undead", "undead"])")))) ==
          "preserve_life: heal_pool excludes_types must be a list of unique creature types");
    CHECK(err_of(cleric("t.c7", with("excludes_types", nullptr))).empty());   // the validator warns; the sim accepts it
}

TEST_CASE("7.2zd content: a feature scope from_types needs applies_to attackers rows") {
    auto lib = lib0();
    const auto e = lib.add(cleric("t.ward", kWard, "ward"), "character");
    REQUIRE_MESSAGE(!e, (e ? *e : ""));
    const Archetype* a = lib.find("t.ward");
    REQUIRE(a);
    const auto& rows = a->feature_modifiers.at(fid_of(*a, "ward"));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].applies_to == rules5e::ModAppliesTo::Attackers);
    CHECK(rows[0].from_types == (rules5e::bit(rules5e::CreatureType::Fiend) | rules5e::bit(rules5e::CreatureType::Undead)));
    Json bad = kWard; bad["modifier"]["applies_to"] = "self";
    CHECK(err_of(cleric("t.w2", bad, "ward")) == "ward: scope from_types needs applies_to attackers");
    bad = kWard; bad["scope"]["from_types"] = Json::parse(R"(["fiend", "fiend"])");
    CHECK(err_of(cleric("t.w3", bad, "ward")) == "ward: scope from_types items must be unique");
    bad = kWard; bad["modifier"]["stat"] = "save";
    CHECK(err_of(cleric("t.w4", bad, "ward")) == "ward: modifier applies_to attackers: attack_roll modifiers only");
}
