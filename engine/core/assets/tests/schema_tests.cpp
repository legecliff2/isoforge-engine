#include <doctest/doctest.h>

#include <charconv>
#include <stdexcept>
#include <string>
#include "assets/schema_registry.hpp"

using namespace eng::assets;

namespace {
const std::string kFixtures = ENGINE_CORE_FIXTURES;

// Test-only migration chain. monster@2 follows ARCHITECTURE.md §5.4 ("make damage[].dice
// structured {count, sides, bonus}"); monster@3 is a synthetic step (speed -> speed_ft)
// to prove multi-step chains. Neither is a released schema.
Json parse_dice(const std::string& s) {
    const auto d = s.find('d');
    const auto sign = s.find_first_of("+-", d);
    auto num = [](std::string_view v) {
        int out = 0;
        auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
        if (ec != std::errc{} || p != v.data() + v.size()) throw std::runtime_error("bad dice '" + std::string(v) + "'");
        return out;
    };
    if (d == std::string::npos) throw std::runtime_error("bad dice");
    std::string_view sv(s);
    const int count = num(sv.substr(0, d));
    const int sides = num(sv.substr(d + 1, sign == std::string::npos ? std::string::npos : sign - d - 1));
    int bonus = 0;
    if (sign != std::string::npos) bonus = (s[sign] == '-' ? -1 : 1) * num(sv.substr(sign + 1));
    return Json{{"count", count}, {"sides", sides}, {"bonus", bonus}};
}

void register_monster(SchemaRegistry& reg) {
    reg.register_type("monster", 3);
    reg.register_migration("monster", 1, 2, [](Json& j, MigrationCtx&) {
        for (auto& a : j.at("actions"))
            for (auto& dmg : a.at("damage")) dmg["dice"] = parse_dice(dmg.at("dice").get<std::string>());
    });
    reg.register_migration("monster", 2, 3, [](Json& j, MigrationCtx& ctx) {
        j["speed_ft"] = j.at("speed");
        j.erase("speed");
        ctx.warnings.push_back("speed renamed to speed_ft");
    });
    reg.register_validator("monster", 1, [](const Json& j) -> std::optional<std::string> {
        if (!j.contains("id") || !j["id"].is_string()) return "id required";
        if (!j.contains("speed")) return "speed required";
        return std::nullopt;
    });
    reg.register_validator("monster", 3, [](const Json& j) -> std::optional<std::string> {
        if (j.contains("speed")) return "speed was removed in v3";
        if (!j.at("actions").at(0).at("damage").at(0).at("dice").is_object()) return "dice must be structured";
        return std::nullopt;
    });
}
} // namespace

TEST_CASE("parse_schema_ref") {
    CHECK(parse_schema_ref("monster@3") == SchemaRef{"monster", 3});
    CHECK(parse_schema_ref("sprite_sheet@12") == SchemaRef{"sprite_sheet", 12});
    CHECK_FALSE(parse_schema_ref("monster"));
    CHECK_FALSE(parse_schema_ref("monster@0"));
    CHECK_FALSE(parse_schema_ref("monster@01"));
    CHECK_FALSE(parse_schema_ref("Monster@1"));
    CHECK_FALSE(parse_schema_ref("@1"));
    CHECK_FALSE(parse_schema_ref("monster@1x"));
}

TEST_CASE("v1 fixture migrates forward to current (v3)") {
    SchemaRegistry reg;
    register_monster(reg);
    CHECK(reg.check_chains().empty());
    LoadResult r = reg.load_file(kFixtures + "/monster/goblin.v1.json");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.declared == SchemaRef{"monster", 1});
    CHECK(r.current == SchemaRef{"monster", 3});
    CHECK(r.data["$schema"] == "monster@3");
    CHECK(r.data["actions"][0]["damage"][0]["dice"] == Json{{"count", 1}, {"sides", 6}, {"bonus", 2}});
    CHECK(r.data["speed_ft"]["walk"] == 30);
    CHECK_FALSE(r.data.contains("speed"));
    CHECK(r.warnings.size() == 1);
    // Untouched fields survive.
    CHECK(r.data["armor_class"]["value"] == 15);
}

TEST_CASE("migration output is canonical and identical regardless of key order in source") {
    SchemaRegistry reg;
    register_monster(reg);
    LoadResult a = reg.load_file(kFixtures + "/monster/goblin.v1.json");
    Json reordered = Json::parse(R"({"speed":{"walk":30},"actions":[{"damage":[{"type":"slashing","dice":"1d6+2"}],"id":"scimitar","kind":"melee_weapon_attack","reach":5,"to_hit":4},{"damage":[{"type":"piercing","dice":"1d6+2"}],"id":"shortbow","kind":"ranged_weapon_attack","range":[80,320],"to_hit":4}],"abilities":{"cha":8,"con":10,"dex":14,"int":10,"str":8,"wis":8},"hit_points":{"formula":"2d6","average":7},"armor_class":{"note":"leather armor, shield","value":15},"type":"humanoid","size":"small","source":{"license":"CC-BY-4.0","ref":"SRD 5.1"},"name":"Goblin","id":"srd51.monster.goblin","$schema":"monster@1"})");
    LoadResult b = reg.load(reordered);
    REQUIRE(a.ok); REQUIRE(b.ok);
    CHECK(a.data.dump() == b.data.dump());
}

TEST_CASE("intermediate version loads and only runs remaining steps") {
    SchemaRegistry reg;
    register_monster(reg);
    LoadResult r = reg.load_text(R"({"$schema":"monster@2","id":"x.y","speed":{"walk":25},
        "actions":[{"damage":[{"dice":{"count":2,"sides":4,"bonus":0}}]}]})");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["speed_ft"]["walk"] == 25);
    CHECK(r.data["actions"][0]["damage"][0]["dice"]["sides"] == 4);
}

TEST_CASE("load errors") {
    SchemaRegistry reg;
    register_monster(reg);
    CHECK_FALSE(reg.load_text("not json").ok);
    CHECK_FALSE(reg.load_text("[1,2]").ok);
    CHECK_FALSE(reg.load_text(R"({"id":"a"})").ok);                              // no $schema
    CHECK_FALSE(reg.load_text(R"({"$schema":"spell@1"})").ok);                   // unknown type
    CHECK(reg.load_text(R"({"$schema":"monster@4"})").error.find("newer") != std::string::npos);
    CHECK_FALSE(reg.load_text(R"({"$schema":"monster@1","id":"a"})").ok);        // v1 validator: no speed
    auto f = reg.load_text(R"({"$schema":"monster@3","id":"a","hp":7.5})");
    CHECK(f.error.find("floating-point") != std::string::npos);
    auto m = reg.load_text(R"({"$schema":"monster@1","id":"a","speed":{},"actions":[{"damage":[{"dice":"zzz"}]}]})");
    CHECK_FALSE(m.ok);
    CHECK(m.error.find("1 -> 2 failed") != std::string::npos);
    CHECK_FALSE(reg.load_file(kFixtures + "/does_not_exist.json").ok);
}

TEST_CASE("registry rejects malformed registrations and reports broken chains") {
    SchemaRegistry reg;
    CHECK_THROWS_AS(reg.register_migration("t", 1, 3, [](Json&, MigrationCtx&) {}), std::logic_error);
    CHECK_THROWS_AS(reg.register_type("t", 0), std::logic_error);
    reg.register_type("t", 3);
    CHECK_THROWS_AS(reg.register_type("t", 4), std::logic_error);
    reg.register_migration("t", 2, 3, [](Json&, MigrationCtx&) {});
    CHECK_THROWS_AS(reg.register_migration("t", 2, 3, [](Json&, MigrationCtx&) {}), std::logic_error);
    auto errs = reg.check_chains();
    REQUIRE(errs.size() == 1);
    CHECK(errs[0] == "t: missing migration 1 -> 2");
    CHECK(reg.load_text(R"({"$schema":"t@1"})").error.find("no migration t 1 -> 2") != std::string::npos);
    CHECK(reg.load_text(R"({"$schema":"t@2"})").ok);
}

namespace {
ASSET_MIGRATION("macro_test", 1, 2, [](Json& j, MigrationCtx&) { j["migrated"] = true; });
}

TEST_CASE("ASSET_MIGRATION registers into the global registry (§3.2 form)") {
    global_registry().register_type("macro_test", 2);
    LoadResult r = global_registry().load_text(R"({"$schema":"macro_test@1"})");
    REQUIRE(r.ok);
    CHECK(r.data["migrated"] == true);
}
