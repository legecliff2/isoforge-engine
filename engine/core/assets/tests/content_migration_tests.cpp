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
    CHECK(make().current_version("monster") == 2u);
}

TEST_CASE("monster 1->2: presentation.ai moves to behavior.ai") {
    auto r = make().load_text(R"({"$schema":"monster@1","id":"x.monster.a",
        "presentation":{"ai":"scripts/ai/a.luau","sprite":"s.png"}})");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["$schema"] == "monster@2");
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
    CHECK_FALSE(make().load_text(R"({"$schema":"monster@3"})").ok);
}

TEST_CASE("frozen fl1 goblin (monster@1, no ai) migrates to monster@2 unchanged otherwise") {
    const std::string path = kRoot + "/tests/compat/fl1/sample.vertical_slice/monsters/goblin.json";
    auto r = make().load_file(path);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.declared == SchemaRef{"monster", 1});
    CHECK(r.data["$schema"] == "monster@2");
    CHECK(r.data["id"] == "srd51.monster.goblin");
    CHECK_FALSE(monster_ai_script(r.data));
    // Everything except $schema is identical to the source.
    SchemaRegistry plain;
    plain.register_type("monster", 1);
    auto raw = plain.load_file(path);
    REQUIRE(raw.ok);
    raw.data.erase("$schema");
    r.data.erase("$schema");
    CHECK(raw.data == r.data);
}

TEST_CASE("sample pack goblin (monster@1 with presentation.ai) resolves behavior.ai") {
    auto r = make().load_file(kRoot + "/content/packs/sample.vertical_slice/monsters/goblin.json");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(monster_ai_script(r.data) == "scripts/ai/skirmisher.luau");
    if (r.data.contains("presentation")) CHECK_FALSE(r.data["presentation"].contains("ai"));
}
