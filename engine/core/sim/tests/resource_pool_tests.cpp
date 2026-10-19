// Row 13, sim side: resources[] resolve into pools registered by Combat::create (Legendary Resistance sets
// Combatant::legendary_pool), grant_action / heal_pool features hand rules a FeatureSpec, and the AI uses them.
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
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json hero(const std::string& id, std::vector<Json> resources, std::vector<Json> effects = {}) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    c["hit_points"]["average"] = 2000;
    c["resources"] = Json(resources);
    for (std::size_t i = 0; i < effects.size(); ++i)
        c["features"].push_back({{"id", "f" + std::to_string(i)}, {"name", "F"}, {"description", "x"}, {"effect", effects[i]}});
    return c;
}
Json pool(const char* id, Json max, const char* recharge = "long_rest") { return {{"id", id}, {"max", max}, {"recharge", recharge}}; }
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("resources: integer, per_level / plus_ability, tables and unlimited entries resolve at load") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json table = {{"table", {{"class", "fighter"}, {"by_level", Json::array({{{"level", 1}, {"value", 2}}, {{"level", 3}, {"value", 3}}})}}}};
    Json unl = {{"table", {{"class", "fighter"}, {"by_level", Json::array({{{"level", 1}, {"unlimited", true}}})}}}};
    REQUIRE_FALSE(lib.add(hero("t.h", {pool("flat", 4), pool("ki", {{"per_level", 3}, {"class", "fighter"}}), pool("bard", {{"plus_ability", "str"}, {"min", 1}}),
                                       pool("rage", table), pool("endless", unl), pool("zero", {{"table", {{"class", "wizard"}, {"by_level", Json::array({{{"level", 1}, {"value", 2}}})}}}})}), "character"));
    const auto& rs = lib.find("t.h")->resources;
    REQUIRE(rs.size() == 5);   // endless registers no pool
    auto find = [&](const char* id) -> const Archetype::Resource& {
        for (const auto& r : rs) if (r.id == id) return r;
        FAIL("missing resource");
        return rs[0];
    };
    CHECK(find("flat").max == 4);
    CHECK(find("ki").max == 3);              // 3 per level x fighter level 1
    CHECK(find("bard").max >= 1);
    CHECK(find("rage").max == 2);
    CHECK(find("zero").max == 0);            // no wizard levels: below the first entry
    CHECK(lib.find("t.h")->unlimited_resources == std::vector<std::string>{"endless"});
    // monster: Legendary Resistance sets legendary_pool; only integer maxes
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["$schema"] = "monster@4";
    gob["id"] = "t.lr";
    gob["resources"] = Json::array({pool("legendary_resistance", 3)});
    REQUIRE_FALSE(lib.add(gob, "monster"));
    CHECK(lib.find("t.lr")->base.legendary_pool == spell_id_of("legendary_resistance"));
    gob["id"] = "t.lr2";
    gob["resources"] = Json::array({pool("x", {{"per_level", 1}})});
    REQUIRE(lib.add(gob, "monster"));
    // load errors
    REQUIRE(lib.add(hero("t.d", {pool("a", 1), pool("a", 2)}), "character"));
    Json bs = pool("a", 3);
    bs["start"] = 4;
    REQUIRE(lib.add(hero("t.s", {bs}), "character"));
    // features: unknown pool, unlimited pool leaves spec.pool 0
    REQUIRE(lib.add(hero("t.u", {pool("a", 1)}, {{{"kind", "grant_action"}, {"cost", "free"}, {"usage", {{"kind", "pool"}, {"pool", "nope"}}}}}), "character"));
    REQUIRE_FALSE(lib.add(hero("t.v", {pool("endless", unl)}, {{{"kind", "grant_action"}, {"cost", "free"}, {"usage", {{"kind", "pool"}, {"pool", "endless"}}}}}), "character"));
    REQUIRE(lib.find("t.v")->feature_specs.size() == 1);
    CHECK(lib.find("t.v")->feature_specs.begin()->second.pool == 0);
    CHECK(lib.find("t.v")->feature_specs.begin()->second.grant_actions == 1);
}

TEST_CASE("pools in a fight: Lay on Hands spends the amount; Action Surge grants an extra action the AI takes") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json loh = {{"kind", "heal_pool"}, {"pool", "loh"}};
    Json surge = {{"kind", "grant_action"}, {"cost", "free"}, {"usage", {{"kind", "per_rest"}, {"uses", 1}, {"rest", "short"}}}};
    REQUIRE_FALSE(lib.add(hero("t.pal", {pool("loh", 5)}, {loh, surge}), "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(gob, "monster"));
    auto ch = arena({{"t.pal", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
    const auto* p = rules5e::find_pool(c->state(), me, spell_id_of("loh"));
    REQUIRE(p);
    CHECK(p->max == 5);
    CHECK(p->cur == 5);
    to_actor(*c, me);
    const Archetype* arch = c->archetype(me);
    uint32_t loh_id = 0, surge_id = 0;
    for (const auto& [fid, spec] : arch->feature_specs) (spec.heal_pool ? loh_id : surge_id) = fid;
    REQUIRE(loh_id != 0);
    REQUIRE(surge_id != 0);
    // Lay on Hands: amount and the default target (self); the pool is charged the amount
    Command h;
    h.request = {me, ActionKind::UseFeature};
    h.request.feature_id = loh_id;
    h.request.amount = 2;
    auto r = c->submit(h);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find_pool(c->state(), me, spell_id_of("loh"))->cur == 3);
    // Action Surge: the action is spent by an attack; the AI then takes the free feature and attacks again
    to_actor(*c, foe);
    to_actor(*c, me);
    Command a;
    a.request = {me, ActionKind::Attack, foe};
    a.request.attack_index = 0;
    REQUIRE(c->submit(a).ok);
    c->settle(nullptr);
    CHECK_FALSE(c->state().budget.action);
    CHECK_FALSE(c->submit(a).ok);   // no action left
    const Command nx = next_command(*c, foe);
    CHECK(nx.request.kind == ActionKind::UseFeature);
    CHECK(nx.request.feature_id == surge_id);
    REQUIRE(c->submit(nx).ok);
    CHECK(c->state().budget.extra_actions == 1);
    const Command again = next_command(*c, foe);
    CHECK(again.request.kind == ActionKind::Attack);
    REQUIRE(c->submit(again).ok);
    c->settle(nullptr);
    CHECK(c->state().budget.extra_actions == 0);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 4, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
}
