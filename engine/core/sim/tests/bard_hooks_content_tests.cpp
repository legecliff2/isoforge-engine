// VERSION_BUMP_BATCH 7.2za: bard hooks content (T&C #202 shapes): grant_die (Bardic Inspiration), half_proficiency (Jack of
// All Trades), subtract_roll applies_to (Cutting Words), load errors, and a sim replay with a held die.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::ModStat;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
const Json kInspiration = Json::parse(R"J({"kind": "grant_die", "die": "1d6", "class": "bard", "scale": [{"level": 5, "die": "1d8"}, {"level": 10, "die": "1d10"}, {"level": 15, "die": "1d12"}], "range_ft": 60, "duration": {"kind": "rounds", "n": 100}, "applies_to": ["attack", "save", "check"], "cost": "bonus_action", "usage": {"kind": "pool", "pool": "bardic_inspiration"}})J");
const Json kJack = Json::parse(R"J({"kind": "half_proficiency", "on": ["checks", "initiative"]})J");
Json bard(const std::string& id, int level = 5) {
    Json d = load_json("content/packs/srd51.core/characters/bard_level_5.json");
    d["id"] = id;
    d["level"] = level;
    d["classes"][0]["level"] = level;
    for (auto& f : d["features"]) {
        if (f["id"] == "bardic_inspiration") f["effect"] = kInspiration;
        if (f["id"] == "jack_of_all_trades") f["effect"] = kJack;
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
const rules5e::ModifierSpec& held(const Archetype& a) {
    REQUIRE(a.feature_modifiers.size() == 1);
    const auto& rows = a.feature_modifiers.begin()->second;
    REQUIRE(rows.size() == 1);
    return rows[0];
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
}  // namespace

TEST_CASE("7.2za content: grant_die compiles one HeldDie row; the die follows the bard level") {
    auto lib = lib0();
    for (auto [lv, sides] : {std::pair{1, 6}, {4, 6}, {5, 8}, {9, 8}, {10, 10}, {15, 12}, {20, 12}}) {
        const std::string id = "t.bard" + std::to_string(lv);
        const auto e = lib.add(bard(id, lv), "character");
        REQUIRE_MESSAGE(!e, e.value_or(""));
        const Archetype* a = lib.find(id);
        REQUIRE(a);
        const auto& m = held(*a);
        CHECK(m.stat == ModStat::HeldDie);
        CHECK(m.die.count == 1);
        CHECK(int(m.die.sides) == sides);
        CHECK(m.held_applies == (rules5e::kRollAttack | rules5e::kRollSave | rules5e::kRollCheck));
        CHECK(m.range_ft == 60);
        CHECK(m.expiry == 1); CHECK(m.rounds == 100);
        CHECK(m.applies_to == rules5e::ModAppliesTo::Target);
        REQUIRE(a->feature_specs.size() == 1);
        CHECK(a->feature_specs.begin()->second.cost == 2);
        CHECK(a->feature_specs.begin()->second.pool == rules5e::spell_id_of("bardic_inspiration"));
        CHECK(a->base.half_proficiency == 3);
    }
}

TEST_CASE("7.2za content: load errors") {
    auto lib = lib0();
    auto bad = [&](const char* id, auto edit, const char* expect) {
        Json d = bard(id); edit(d);
        const auto e = lib.add(d, "character");
        REQUIRE_MESSAGE(e, std::string(id));
        CHECK_MESSAGE(e->find(expect) != std::string::npos, *e);
    };
    bad("b1", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["scale"] = Json::parse(R"([{"level": 10, "die": "1d10"}, {"level": 5, "die": "1d8"}])"); }, "scale levels must ascend");
    bad("b2", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["applies_to"] = Json::array({"save", "save"}); }, "bad applies_to");
    bad("b3", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["applies_to"] = Json::array({"initiative"}); }, "bad applies_to");
    bad("b4", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["duration"] = {{"kind", "minutes"}, {"n", 10}}; }, "duration must be");
    bad("b5", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["bonus"] = 1; }, "unknown key 'bonus'");
    bad("b6", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["cost"] = "free"; }, "cost must be action or bonus_action");
    bad("b7", [](Json& d) { feature(d, "bardic_inspiration")["effect"].erase("range_ft"); }, "range_ft must be");
    bad("b8", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["die"] = "1d6+1"; }, "grant_die needs die");   // #214 follow-up: plain NdM
    bad("b9", [](Json& d) { feature(d, "bardic_inspiration")["effect"]["scale"][0]["die"] = "1d8+2"; }, "scale entries are {level, die}");
    bad("b8", [](Json& d) { feature(d, "jack_of_all_trades")["effect"]["on"] = Json::array({"checks", "checks"}); }, "half_proficiency: bad on");
    bad("b9", [](Json& d) { feature(d, "jack_of_all_trades")["effect"]["on"] = Json::array({"saves"}); }, "half_proficiency: bad on");
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4"; m["id"] = "t.jack_goblin";
    m["traits"] = Json::array({{{"id", "jack"}, {"name", "J"}, {"description", "x"}, {"effect", kJack}}});
    const auto e = lib.add(m, "monster");
    REQUIRE(e);
    CHECK(e->find("half_proficiency: not valid on monsters") != std::string::npos);
}

TEST_CASE("7.2za content: subtract_roll applies_to compiles to roll-kind bits; absent = attack only") {
    auto goblin = [](const std::string& id, Json applies) {
        Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
        m["$schema"] = "monster@4"; m["id"] = id;
        m["resources"] = Json::array({{{"id", "bardic_inspiration"}, {"name", "B"}, {"max", 3}, {"recharge", "long_rest"}}});
        Json r = {{"id", "cutting_words"}, {"name", "Cutting Words"}, {"window", {{"kind", "roll_attack"}}},
                  {"effect", {{"kind", "subtract_roll"}, {"dice", "1d6"}}}, {"usage", {{"kind", "pool"}, {"pool", "bardic_inspiration"}}}};
        if (!applies.is_null()) r["effect"]["applies_to"] = applies;
        m["reactions"] = Json::array({r});
        return m;
    };
    auto lib = lib0();
    REQUIRE_FALSE(lib.add(goblin("t.cw0", Json()), "monster"));
    REQUIRE_FALSE(lib.add(goblin("t.cw1", Json::array({"attack", "check", "damage"})), "monster"));
    REQUIRE(lib.find("t.cw0")->base.reactions.size() == 1);
    CHECK(lib.find("t.cw0")->base.reactions[0].applies == 0);
    CHECK(lib.find("t.cw1")->base.reactions[0].applies == (rules5e::kRollAttack | rules5e::kRollCheck | rules5e::kRollDamage));
    CHECK(lib.add(goblin("t.cw2", Json::array({"check", "check"})), "monster"));
    CHECK(lib.add(goblin("t.cw3", Json::array({"initiative"})), "monster"));
}

TEST_CASE("7.2za sim: Bardic Inspiration on an ally through Combat; the log replays to the same hash") {
    auto lib = lib0();
    REQUIRE_FALSE(lib.add(bard("t.bard"), "character"));
    Json ally = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    ally["id"] = "t.ally";
    REQUIRE_FALSE(lib.add(ally, "character"));
    const auto ch = arena({{"t.bard", {4, 4}}, {"t.ally", {6, 4}}, {"srd51.monster.goblin", {12, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId b = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != b; ++i) {
        Command e; e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c->state()) == b);
    const Archetype* a = lib.find("t.bard");
    Command u;
    u.request = {b, ActionKind::UseFeature, f};
    u.request.feature_id = a->feature_modifiers.begin()->first;
    u.request.as_bonus_action = true;
    Command self = u; self.request.target = b;
    CHECK_FALSE(c->submit(self).ok);
    auto r = c->submit(u);
    REQUIRE_MESSAGE(r.ok, r.error);
    bool held = false;
    for (const auto& m : c->state().modifiers) held = held || (m.target == f && m.spec.stat == ModStat::HeldDie && m.spec.die.sides == 8);
    CHECK(held);
    for (int i = 0; i < 4; ++i) {
        Command e; e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        if (!c->submit(e).ok) break;
    }
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 11, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
}
