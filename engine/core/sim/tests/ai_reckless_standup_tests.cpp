// Director-approved AI policy in driver.cpp (no new request fields, no new tags): a creature with Combatant::reckless_attack
// declares ActionRequest::reckless on its turn's first melee attack (never on a ranged one), and a prone creature stands up
// (rules5e StandUp) at the start of its turn unless it is grappled, its speed is 0 or its movement is short. AI-driven fights
// replay with Combat::hash and Combat::log_hash equal.
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
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json trait(const char* id, const char* kind) {
    return {{"id", id}, {"name", id}, {"description", "x"}, {"effect", {{"kind", kind}}}};
}
// A goblin stat block (scimitar, shortbow; no Nimble Escape) as monster@4 with the given traits.
Json goblin_with(const std::string& id, std::vector<Json> traits, int hp = 400, int speed = 30) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    m["speed"]["walk"] = speed;
    m["traits"] = Json::array();
    for (const Json& t : traits) m["traits"].push_back(t);
    return m;
}
ContentLibrary lib_with(const std::vector<Json>& monsters, int fighter_hp = 2000) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = fighter_hp;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
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
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
Command special(CombatantId who, CombatantId target, uint8_t kind) {
    Command x;
    x.request = {who, ActionKind::Attack, target};
    x.request.special = kind;
    return x;
}
Command step(Vec2i to) {
    Command s;
    s.kind = CommandKind::Step;
    s.to = to;
    return s;
}
bool has(const Combat& c, CombatantId id, Condition k) { return rules5e::find(c.state(), id)->has(k); }
// Repeats the fighter's special request (1 = grapple, 2 = shove prone) on its turns until `k` is on the target.
void land(Combat& c, CombatantId f, CombatantId t, uint8_t kind, Condition k) {
    for (int round = 0; round < 40 && !has(c, t, k); ++round) {
        to_actor(c, f);
        auto r = c.submit(special(f, t, kind));
        REQUIRE_MESSAGE(r.ok, r.error);
        if (r.needs) REQUIRE(c.settle(nullptr).ok);
        if (!has(c, t, k)) end_turn(c);
    }
    REQUIRE(has(c, t, k));
}
int stood_up(const Combat& c, CombatantId who) {
    int n = 0;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::StoodUp && e.rules.actor == who) ++n;
    return n;
}
void check_replay(const world::Chunk& ch, const ContentLibrary& lib, uint64_t seed, const Combat& c) {
    ecs::Registry reg;
    std::string err;
    auto again = replay(reg, ch, lib, seed, c.log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c.hash());
    CHECK(again->log_hash() == c.log_hash());
}
}  // namespace

TEST_CASE("AI Reckless: a berserker declares reckless on its first melee attack, never on a ranged one; others never") {
    const auto lib = lib_with({goblin_with("t.berserker", {trait("reckless", "reckless_attack")}), goblin_with("t.plain", {})});
    {   // adjacent: the scimitar attack opens the turn with reckless; rules accept it and Reckless is active
        const auto ch = arena({{kFighter, {5, 5}}, {"t.berserker", {6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 33);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
        to_actor(*c, b);
        const Command a = next_command(*c, f);
        REQUIRE(a.kind == CommandKind::Rules);
        CHECK(a.request.kind == ActionKind::Attack);
        CHECK(a.request.attack_index == 0);
        CHECK(a.request.reckless);
        auto r = c->submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (r.needs) REQUIRE(c->settle(nullptr).ok);
        CHECK(rules5e::find(c->state(), b)->reckless_active);
        // the action is spent: no second declaration, and the turn ends
        CHECK_FALSE(next_command(*c, f).request.reckless);
        // the fighter (no Reckless Attack) never declares it
        end_turn(*c);
        to_actor(*c, f);
        const Command fa = next_command(*c, b);
        CHECK(fa.request.kind == ActionKind::Attack);
        CHECK_FALSE(fa.request.reckless);
    }
    {   // out of reach for this turn: it shoots the shortbow, without reckless
        const auto ch = arena({{kFighter, {2, 5}}, {"t.berserker", {20, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 33);
        REQUIRE(c);
        to_actor(*c, ch.spawns[1].stable_id);
        const Command a = next_command(*c, ch.spawns[0].stable_id);
        CHECK(a.request.kind == ActionKind::Attack);
        CHECK(a.request.attack_index == 1);
        CHECK_FALSE(a.request.reckless);
        REQUIRE(c->submit(a).ok);
    }
    {   // a plain goblin in melee does not declare it
        const auto ch = arena({{kFighter, {5, 5}}, {"t.plain", {6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 33);
        REQUIRE(c);
        to_actor(*c, ch.spawns[1].stable_id);
        const Command a = next_command(*c, ch.spawns[0].stable_id);
        CHECK(a.request.kind == ActionKind::Attack);
        CHECK_FALSE(a.request.reckless);
    }
}

TEST_CASE("AI Reckless: an AI-driven fight declares it on melee only, rules accept every request; replay reproduces both hashes") {
    const auto lib = lib_with({goblin_with("t.berserker", {trait("reckless", "reckless_attack")}, 40)}, 60);
    const auto ch = arena({{kFighter, {2, 5}}, {"t.berserker", {20, 5}}});   // it shoots first, then meets the fighter
    const CombatantId b = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    NearestEnemyPolicy nearest;
    auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &nearest; });
    REQUIRE_MESSAGE(rr.ended, rr.error);
    const auto* me = rules5e::find(c->state(), b);
    REQUIRE(me);
    int reckless = 0, ranged = 0;
    for (const auto& le : c->log()) {
        const auto& q = le.step.request;
        if (q.actor != b || q.kind != ActionKind::Attack || q.special != 0) continue;
        const bool is_ranged = q.attack_index < me->attacks.size() && me->attacks[q.attack_index].ranged;
        if (is_ranged) { ++ranged; CHECK_FALSE(q.reckless); }
        if (q.reckless) { ++reckless; CHECK_FALSE(is_ranged); }
    }
    CHECK(ranged >= 1);
    CHECK(reckless >= 1);
    check_replay(ch, lib, 9, *c);
}

TEST_CASE("AI stand up: a creature shoved prone stands on its next turn (half its speed, StoodUp), then attacks or moves") {
    const auto lib = lib_with({goblin_with("t.gob", {})});
    for (bool walk_away : {false, true}) {
        const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
        land(*c, f, g, 2, Condition::Prone);   // Shove: knocked prone (no timer)
        if (walk_away) {   // the fighter backs off 15 ft (the goblin may take its opportunity attack)
            for (Vec2i p : {Vec2i{4, 5}, Vec2i{3, 5}, Vec2i{2, 5}}) {
                auto r = c->submit(step(p));
                REQUIRE_MESSAGE(r.ok, r.error);
                if (r.needs) REQUIRE(c->settle(nullptr).ok);
            }
        }
        end_turn(*c);
        to_actor(*c, g);
        REQUIRE(has(*c, g, Condition::Prone));
        const Command up = next_command(*c, f);
        REQUIRE(up.kind == CommandKind::Rules);
        CHECK(up.request.kind == ActionKind::StandUp);
        auto r = c->submit(up);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK_FALSE(has(*c, g, Condition::Prone));
        CHECK(c->state().budget.movement_ft == 15);
        CHECK(stood_up(*c, g) == 1);
        const Command next = next_command(*c, f);
        if (walk_away) {
            CHECK(next.kind == CommandKind::Step);
        } else {
            CHECK(next.kind == CommandKind::Rules);
            CHECK(next.request.kind == ActionKind::Attack);
            CHECK(next.request.attack_index == 0);
        }
        REQUIRE(c->submit(next).ok);
        // not prone any more: it does not try to stand again
        CHECK(next_command(*c, f).request.kind != ActionKind::StandUp);
    }
}

TEST_CASE("AI stand up: an AI-driven fight after a Shove stands up and plays on; replay reproduces both hashes") {
    const auto lib = lib_with({goblin_with("t.gob", {}, 30)}, 60);
    const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    land(*c, f, g, 2, Condition::Prone);
    end_turn(*c);
    NearestEnemyPolicy nearest;
    auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &nearest; });
    REQUIRE_MESSAGE(rr.ended, rr.error);
    CHECK(stood_up(*c, g) >= 1);
    bool stand_logged = false;
    for (const auto& le : c->log()) stand_logged = stand_logged || (le.step.request.actor == g && le.step.request.kind == ActionKind::StandUp);
    CHECK(stand_logged);
    check_replay(ch, lib, 11, *c);
}

TEST_CASE("AI stand up: a grappled prone creature stays prone and fights on; AI-driven fight replays") {
    const auto lib = lib_with({goblin_with("t.gob", {})});
    const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    land(*c, f, g, 1, Condition::Grappled);
    end_turn(*c);
    land(*c, f, g, 2, Condition::Prone);
    REQUIRE(has(*c, g, Condition::Grappled));
    end_turn(*c);
    to_actor(*c, g);
    const Command x = next_command(*c, f);
    CHECK(x.request.kind != ActionKind::StandUp);
    CHECK(x.request.kind == ActionKind::Attack);   // in reach: attacks (with disadvantage), as before
    NearestEnemyPolicy nearest;
    auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &nearest; }, 60);
    CHECK(rr.error == "command limit reached");
    CHECK(has(*c, g, Condition::Grappled));
    CHECK(has(*c, g, Condition::Prone));
    CHECK(stood_up(*c, g) == 0);
    check_replay(ch, lib, 7, *c);
}

TEST_CASE("AI stand up: a 0-speed prone creature stays prone and fights on; AI-driven fight replays") {
    const auto lib = lib_with({goblin_with("t.rooted", {}, 400, 0)});
    REQUIRE(lib.find("t.rooted")->base.speed_ft == 0);
    const auto ch = arena({{kFighter, {5, 5}}, {"t.rooted", {6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    land(*c, f, g, 2, Condition::Prone);
    end_turn(*c);
    to_actor(*c, g);
    const Command x = next_command(*c, f);
    CHECK(x.request.kind == ActionKind::Attack);
    NearestEnemyPolicy nearest;
    auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &nearest; }, 60);
    CHECK(rr.error == "command limit reached");
    CHECK(has(*c, g, Condition::Prone));
    CHECK(stood_up(*c, g) == 0);
    check_replay(ch, lib, 5, *c);
}
