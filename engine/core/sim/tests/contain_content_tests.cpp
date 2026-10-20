// VERSION_BUMP_BATCH 7.2w: content keys for grab and contain (apply[] `contain` {kind, damage}, `max_size`; save_effect
// `escape`). The compiled synthetic SpellDefs carry rules5e's contain / escape / size fields.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;

namespace {
Json goblin() {
    std::ifstream f(std::string(ENGINE_SOURCE_ROOT) + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE(f.good());
    Json m = Json::parse(f);
    m["$schema"] = "monster@4";
    m["traits"] = Json::array();
    return m;
}
Json toad(Json apply) {
    Json m = goblin(); m["id"] = "t.toad";
    m["actions"].push_back({{"id", "swallow"}, {"name", "Swallow"}, {"kind", "attack_effect"}, {"attack", "melee"}, {"to_hit", 4},
                            {"range_ft", 5}, {"apply", Json::array({apply})}});
    return m;
}
Json cube(Json apply) {
    Json m = goblin(); m["id"] = "t.cube";
    m["actions"].push_back({{"id", "engulf"}, {"name", "Engulf"}, {"kind", "save_effect"}, {"save", {{"ability", "dex"}, {"dc", 12}}},
                            {"damage", Json::array({{{"dice", "3d6"}, {"type", "acid"}}})}, {"on_success", "none"},
                            {"apply", Json::array({apply})}});
    return m;
}
}  // namespace

TEST_CASE("content 7.2w: Swallow (attack_effect, no escape) and Engulf (save_effect with escape) compile to contain fields") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json sw = {{"condition", "restrained"}, {"max_size", "medium"},
               {"contain", {{"kind", "swallow"}, {"damage", {{"dice", "3d6"}, {"type", "acid"}}}}}};
    REQUIRE(lib.add(toad(sw), "monster").value_or("") == "");
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.toad#swallow"));
    REQUIRE(d); REQUIRE(d->extra_effects.size() == 1);
    CHECK(d->extra_effects[0].contain == 2);
    CHECK(d->extra_effects[0].escape_dc == 0);
    CHECK(d->extra_effects[0].contain_dice == rules5e::DiceExpr{3, 6, 0});
    CHECK(d->extra_effects[0].condition_max_size == int8_t(rules5e::Size::Medium));

    Json en = {{"condition", "restrained"}, {"escape", {{"ability", "str"}, {"dc", 12}}}, {"max_size", "large"},
               {"contain", {{"kind", "engulf"}, {"damage", {{"dice", "6d6"}, {"type", "acid"}}}}}};
    REQUIRE(lib.add(cube(en), "monster").value_or("") == "");
    const auto* c = lib.spells()->find(rules5e::spell_id_of("t.cube#engulf"));
    REQUIRE(c);
    CHECK(c->contain == 1); CHECK(c->escape_dc == 12); CHECK(c->contain_dice == rules5e::DiceExpr{6, 6, 0});
    CHECK(c->condition_max_size == int8_t(rules5e::Size::Large));

    ContentLibrary bad;
    REQUIRE(bad.load(sample_set()).empty());
    Json b1 = sw; b1["contain"]["kind"] = "absorb";
    CHECK(bad.add(toad(b1), "monster").value_or("").find("contain kind must be engulf or swallow") != std::string::npos);
    Json b2 = sw; b2["max_size"] = "enormous";
    CHECK(bad.add(toad(b2), "monster").value_or("").find("bad max_size") != std::string::npos);
    Json b3 = en; b3["contain"].erase("damage");
    CHECK(bad.add(cube(b3), "monster").value_or("").find("contain needs damage") != std::string::npos);
}

TEST_CASE("content 7.2w (EC review): regurgitate, excludes_formless, formless tag, max_size needs contain on save_effect") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json sw = {{"condition", "restrained"},
               {"contain", {{"kind", "swallow"}, {"damage", {{"dice", "3d6"}, {"type", "acid"}}}, {"regurgitate", {{"dc", 14}}}}}};
    REQUIRE(lib.add(toad(sw), "monster").value_or("") == "");
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.toad#swallow"));
    REQUIRE(d);
    CHECK(d->extra_effects[0].regurgitate_dc == 14); CHECK(d->extra_effects[0].regurgitate_damage == 30);
    Json g = goblin(); g["id"] = "t.ooze"; g["tags"] = Json::array({"Formless"});
    REQUIRE(lib.add(g, "monster").value_or("") == "");
    CHECK(lib.find("t.ooze")->base.formless);
    CHECK_FALSE(lib.find("t.toad")->base.formless);

    auto err = [](Json m) { ContentLibrary l; REQUIRE(l.load(sample_set()).empty()); return l.add(m, "monster").value_or(""); };
    Json net = {{"condition", "restrained"}, {"escape", {{"ability", "str"}, {"dc", 10}}}, {"max_size", "large"}, {"excludes_formless", true}};
    {
        ContentLibrary l; REQUIRE(l.load(sample_set()).empty());
        REQUIRE(l.add(toad(net), "monster").value_or("") == "");
        CHECK(l.spells()->find(rules5e::spell_id_of("t.toad#swallow"))->extra_effects[0].excludes_formless);
    }
    Json b = net; b["excludes_formless"] = 1; CHECK(err(toad(b)).find("bad excludes_formless") != std::string::npos);
    Json r1 = sw; r1["contain"]["regurgitate"] = {{"dc", 0}}; CHECK(err(toad(r1)).find("bad regurgitate") != std::string::npos);
    Json r2 = sw; r2["contain"]["kind"] = "engulf"; CHECK(err(toad(r2)).find("regurgitate is a swallow option") != std::string::npos);
    Json ms = {{"condition", "restrained"}, {"escape", {{"ability", "str"}, {"dc", 12}}}, {"max_size", "large"}};
    CHECK(err(cube(ms)).find("max_size on a save_effect needs contain") != std::string::npos);
}

TEST_CASE("content 7.2w: transparent {dc} trait (monsters only, dc 1..30, highest wins)") {
    auto with = [](Json eff, Json more = nullptr) {
        Json m = goblin(); m["id"] = "t.cube2";
        m["traits"].push_back({{"id", "transparent"}, {"name", "Transparent"}, {"effect", eff}});
        if (!more.is_null()) m["traits"].push_back({{"id", "transparent_2"}, {"name", "Transparent"}, {"effect", more}});
        return m;
    };
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE(lib.add(with({{"kind", "transparent"}, {"dc", 15}}, {{"kind", "transparent"}, {"dc", 12}}), "monster").value_or("") == "");
    CHECK(lib.find("t.cube2")->base.transparent_dc == 15);
    auto err = [](Json m) { ContentLibrary l; REQUIRE(l.load(sample_set()).empty()); return l.add(m, "monster").value_or(""); };
    CHECK(err(with({{"kind", "transparent"}, {"dc", 31}})).find("transparent: dc must be an integer 1..30") != std::string::npos);
    CHECK(err(with({{"kind", "transparent"}})).find("transparent: dc must be an integer 1..30") != std::string::npos);
    CHECK(err(with({{"kind", "transparent"}, {"dc", 15}, {"seen", true}})).find("transparent: unknown key 'seen'") != std::string::npos);
    std::ifstream f(std::string(ENGINE_SOURCE_ROOT) + "/content/packs/srd51.core/characters/fighter_level_5.json");
    Json ch = Json::parse(f);
    ch["features"].push_back({{"id", "transparent"}, {"name", "Transparent"}, {"effect", {{"kind", "transparent"}, {"dc", 15}}}});
    ContentLibrary l; REQUIRE(l.load(sample_set()).empty());
    CHECK(l.add(ch, "character").value_or("").find("transparent: only valid on monsters") != std::string::npos);
}
