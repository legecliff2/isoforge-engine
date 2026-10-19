// R-G6, sim side: bonus-action attack options compile from content (two-weapon fighting from "light" properties, Martial Arts /
// Flurry of Blows / Frenzy by feature id), the AI uses them with ActionRequest::bonus_option, and log_hash folds the option only
// when it is set.
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

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json weapon(const char* id, const char* dice, std::vector<std::string> props) {
    return {{"id", id}, {"name", id}, {"kind", "melee_weapon_attack"}, {"to_hit", 5}, {"reach", 5}, {"targets", 1},
            {"properties", props}, {"damage", Json::array({{{"dice", dice}, {"type", "piercing"}}})}};
}
Json feat(const char* id) { return {{"id", id}, {"name", id}, {"description", "x"}}; }
Json fighter(const std::string& id, std::vector<Json> actions, std::vector<Json> feats) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    c["hit_points"]["average"] = 2000;
    c["actions"] = Json(actions);
    for (const Json& f : feats) c["features"].push_back(f);
    return c;
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

TEST_CASE("bonus attacks: two-weapon fighting, Martial Arts, Flurry and Frenzy compile; errors") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json dagger = weapon("dagger", "1d4+3", {"finesse", "light"}), sword = weapon("shortsword", "1d6+3", {"finesse", "light"});
    REQUIRE_FALSE(lib.add(fighter("t.twf", {dagger, sword}, {}), "character"));
    const auto* a = lib.find("t.twf");
    REQUIRE(a);
    const auto& bo = a->base.bonus_attacks;
    REQUIRE(bo.size() == 2);
    CHECK(bo[0].attack_index == 0);
    CHECK(bo[1].attack_index == 1);
    CHECK(bo[0].requires_attacks == 3u);
    CHECK(bo[0].different_weapon);
    REQUIRE(bo[0].has_damage);
    CHECK(bo[0].damage.sides == 4);
    CHECK(bo[0].damage.bonus == 0);   // no ability modifier without the style
    CHECK(bo[0].id != bo[1].id);
    // the style puts the modifier back (rules use the attack's own dice)
    REQUIRE_FALSE(lib.add(fighter("t.twf2", {dagger, sword}, {feat("two_weapon_fighting")}), "character"));
    for (const auto& b : lib.find("t.twf2")->base.bonus_attacks) CHECK_FALSE(b.has_damage);
    // one light weapon is not enough; an older file never gets options
    REQUIRE_FALSE(lib.add(fighter("t.one", {dagger}, {}), "character"));
    CHECK(lib.find("t.one")->base.bonus_attacks.empty());
    REQUIRE_FALSE(lib.add(fighter("t.old", {dagger, sword}, {}), "character", "", 3));
    CHECK(lib.find("t.old")->base.bonus_attacks.empty());
    // monk
    const Json fist = weapon("unarmed_strike", "1d4+3", {"unarmed"}), staff = weapon("quarterstaff", "1d6+3", {"monk"});
    CHECK(lib.add(fighter("t.noki", {staff, fist}, {feat("flurry_of_blows")}), "character"));   // no ki resource: load error
    Json monk = fighter("t.monk", {staff, fist}, {feat("martial_arts"), feat("flurry_of_blows")});
    monk["resources"] = Json::array({{{"id", "ki"}, {"max", 2}, {"recharge", "short_rest"}}});
    REQUIRE_FALSE(lib.add(monk, "character"));
    const auto& mo = lib.find("t.monk")->base.bonus_attacks;
    REQUIRE(mo.size() == 2);
    CHECK(mo[0].attack_index == 1);
    CHECK(mo[0].requires_attacks == 3u);
    CHECK(mo[1].count == 2);
    CHECK(mo[1].pool == rules5e::spell_id_of("ki"));
    CHECK(mo[1].pool_cost == 1);
    // frenzy
    REQUIRE_FALSE(lib.add(fighter("t.zerk", {sword}, {feat("rage"), feat("frenzy")}), "character"));
    const auto& fo = lib.find("t.zerk")->base.bonus_attacks;
    REQUIRE(fo.size() == 1);
    CHECK(fo[0].any_melee);
    CHECK(fo[0].requires_modifier == rules5e::spell_id_of("t.zerk#rage#m0"));
    // errors
    CHECK(lib.add(fighter("t.bad1", {sword}, {feat("martial_arts")}), "character"));
    CHECK(lib.add(fighter("t.bad2", {sword}, {feat("frenzy")}), "character"));
}

TEST_CASE("bonus attacks: the AI makes the off-hand attack; bonus_option folds into log_hash; replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(fighter("t.twf", {weapon("dagger", "1d4+3", {"light"}), weapon("shortsword", "1d6+3", {"light"})}, {}), "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 600;
    REQUIRE_FALSE(lib.add(gob, "monster"));
    auto ch = arena({{"t.twf", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
    for (int i = 0; i < 40; ++i) {
        const CombatantId cur = rules5e::current_actor(c->state());
        Command cmd;
        if (cur == me) cmd = next_command(*c, foe);
        else cmd.request = {cur, ActionKind::EndTurn};
        REQUIRE(c->submit(cmd).ok);
        c->settle(nullptr);
    }
    int bonus = 0;
    for (const auto& e : c->log())
        if (e.step.request.bonus_option != 0) {
            ++bonus;
            CHECK(e.step.request.as_bonus_action);
            CHECK(e.step.request.kind == ActionKind::Attack);
            CHECK(e.step.request.bonus_option == e.step.request.attack_index + 1);
        }
    CHECK(bonus >= 1);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 4, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
}
