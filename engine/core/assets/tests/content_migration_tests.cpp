#include <doctest/doctest.h>

#include <string>
#include "assets/content_migrations.hpp"

using namespace eng::assets;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
SchemaRegistry make() {
    SchemaRegistry r;
    register_content_schemas(r);
    return r;
}
} // namespace

TEST_CASE("content schemas: chains are complete") {
    CHECK(make().check_chains().empty());
    CHECK(make().current_version("monster") == 4u);
    CHECK(make().current_version("character") == 4u);
}

TEST_CASE("monster 1->2: presentation.ai moves to behavior.ai") {
    auto r = make().load_text(R"({"$schema":"monster@1","id":"x.monster.a",
        "presentation":{"ai":"scripts/ai/a.luau","sprite":"s.png"}})");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["$schema"] == "monster@4");
    CHECK(r.data["behavior"] == Json{{"ai", "scripts/ai/a.luau"}});
    CHECK(r.data["presentation"] == Json{{"sprite", "s.png"}});
    CHECK(monster_ai_script(r.data) == "scripts/ai/a.luau");

    auto only_ai = make().load_text(R"({"$schema":"monster@1","presentation":{"ai":"scripts/ai/a.luau"}})");
    REQUIRE(only_ai.ok);
    CHECK_FALSE(only_ai.data.contains("presentation"));   // emptied presentation dropped
}

TEST_CASE("monster 1->2: no ai is a no-op; v2 passes through; conflicts rejected") {
    auto none = make().load_text(R"({"$schema":"monster@1","presentation":{"sprite":"s.png"}})");
    REQUIRE(none.ok);
    CHECK_FALSE(none.data.contains("behavior"));
    CHECK_FALSE(monster_ai_script(none.data));
    auto v2 = make().load_text(R"({"$schema":"monster@2","behavior":{"ai":"scripts/b.luau"}})");
    REQUIRE(v2.ok);
    CHECK(monster_ai_script(v2.data) == "scripts/b.luau");
    CHECK_FALSE(make().load_text(R"({"$schema":"monster@1","behavior":{},"presentation":{"ai":"x"}})").ok);
    CHECK_FALSE(make().load_text(R"({"$schema":"monster@5"})").ok);
}

TEST_CASE("frozen fl1 goblin (monster@1, no ai) migrates to monster@4; only effects marked inert") {
    const std::string path = kRoot + "/tests/compat/fl1/sample.vertical_slice/monsters/goblin.json";
    auto r = make().load_file(path);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.declared == SchemaRef{"monster", 1});
    CHECK(r.data["$schema"] == "monster@4");
    CHECK(r.data["id"] == "srd51.monster.goblin");
    CHECK_FALSE(monster_ai_script(r.data));
    // Everything except $schema and the 2 -> 3 inert markers is identical to the source.
    SchemaRegistry plain;
    plain.register_type("monster", 1);
    auto raw = plain.load_file(path);
    REQUIRE(raw.ok);
    raw.data.erase("$schema");
    r.data.erase("$schema");
    for (auto& t : raw.data["traits"])
        if (t.contains("effect")) t["effect"]["inert"] = true;
    // ... and the 3 -> 4 markers (stamps on reactions / loadable bonus actions, merged inert_mechanics).
    r.data.erase("inert_mechanics");
    for (const char* key : {"reactions", "bonus_actions"})
        if (r.data.contains(key))
            for (auto& e : r.data[key]) e.erase("inert");
    CHECK(raw.data == r.data);
}

TEST_CASE("monster 2->3: features/traits effects marked inert; @3 passes through; actions untouched") {
    auto r = make().load_text(R"({"$schema":"monster@2","traits":[{"id":"t","effect":{"kind":"damage_bonus","amount":2}},
        {"id":"plain"}],"features":[{"id":"f","effect":{"kind":"self_heal"}}],"actions":[{"id":"a"}]})");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["$schema"] == "monster@4");
    CHECK(r.data["traits"][0]["effect"]["inert"] == true);
    CHECK_FALSE(r.data["traits"][1].contains("effect"));
    CHECK(r.data["features"][0]["effect"]["inert"] == true);
    CHECK(r.data["actions"][0] == Json{{"id", "a"}});
    auto v3 = make().load_text(R"({"$schema":"monster@3","traits":[{"id":"t","effect":{"kind":"bonus_action_grant"}}]})");
    REQUIRE(v3.ok);
    CHECK_FALSE(v3.data["traits"][0]["effect"].contains("inert"));
}

TEST_CASE("sample pack goblin (monster@1 with presentation.ai) resolves behavior.ai") {
    auto r = make().load_file(kRoot + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(monster_ai_script(r.data) == "scripts/ai/skirmisher.luau");
    if (r.data.contains("presentation")) CHECK_FALSE(r.data["presentation"].contains("ai"));
}

TEST_CASE("3 -> 4 migration: markers, frozen effect kinds, merged inert_mechanics, purity") {
    const char* kDoc = R"({"$schema":"monster@3","id":"t.m","inert_mechanics":["damage[1..]","spellcasting"],
        "reactions":[{"id":"parry","kind":"special"},{"id":"r2"}],
        "bonus_actions":[{"id":"b1","kind":"special"},{"id":"b2","kind":"attack"}],
        "traits":[{"id":"a","effect":{"kind":"on_event","on":"bogus"}},{"id":"b","effect":{"kind":"damage_bonus","amount":1}},
                  {"id":"c","effect":{"kind":"multiattack","attacks":["x"]}},{"id":"d","effect":{"kind":"reaction"}},{"id":"e"}],
        "features":[{"id":"f","effect":{"kind":"save_effect"}},{"id":"g","effect":{"kind":"magic_weapons"}}],
        "actions":[{"id":"x","kind":"special"}],"legendary_actions":[{"id":"l"}]})";
    auto r = make().load_text(kDoc);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.declared == SchemaRef{"monster", 3});
    const Json& d = r.data;
    CHECK(d["$schema"] == "monster@4");
    CHECK(d["inert_mechanics"] == Json::array({"bonus_actions", "damage[1..]", "reactions", "spellcasting"}));   // merged, sorted
    CHECK(d["reactions"][0]["inert"] == true);   // every reactions[] entry, special or not
    CHECK(d["reactions"][1]["inert"] == true);
    CHECK_FALSE(d["bonus_actions"][0].contains("inert"));   // special stays descriptive
    CHECK(d["bonus_actions"][1]["inert"] == true);
    CHECK(d["traits"][0]["effect"]["inert"] == true);
    CHECK_FALSE(d["traits"][1]["effect"].contains("inert"));   // live at @3: never marked
    CHECK_FALSE(d["traits"][2]["effect"].contains("inert"));
    CHECK(d["traits"][3]["effect"]["inert"] == true);
    CHECK_FALSE(d["traits"][4].contains("effect"));
    CHECK(d["features"][0]["effect"]["inert"] == true);
    CHECK_FALSE(d["features"][1]["effect"].contains("inert"));
    CHECK(d["actions"][0] == Json::parse(R"({"id":"x","kind":"special"})"));   // actions[] never touched
    CHECK(d["legendary_actions"][0] == Json{{"id", "l"}});
    // @4 passes through unchanged (idempotent: the migration only runs on @3 input).
    auto again = make().load_text(d.dump());
    REQUIRE(again.ok);
    CHECK(again.data == d);
    // nothing to mark: no inert_mechanics key is invented
    auto clean = make().load_text(R"({"$schema":"monster@3","id":"t.c","reactions":[],"bonus_actions":[{"id":"s","kind":"special"}]})");
    REQUIRE(clean.ok);
    CHECK_FALSE(clean.data.contains("inert_mechanics"));
    // characters: a non-empty equipment list is marked; monsters never are
    auto ch = make().load_text(R"({"$schema":"character@3","id":"t.ch","equipment":["longsword"]})");
    REQUIRE(ch.ok);
    CHECK(ch.data["inert_mechanics"] == Json::array({"equipment"}));
    auto mo = make().load_text(R"({"$schema":"monster@3","id":"t.mo","equipment":["x"]})");
    REQUIRE(mo.ok);
    CHECK_FALSE(mo.data.contains("inert_mechanics"));
    // the frozen list the sim registry must equal
    CHECK(effect_kinds_introduced_at(4) == std::vector<std::string>{"on_event", "apply_modifier", "grant_action", "heal_pool", "save_effect", "reaction"});
    CHECK(effect_kinds_introduced_at(3).empty());
}
