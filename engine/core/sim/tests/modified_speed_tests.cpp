// fix/sim-modified-speed (7.2u follow-up): the sim budgets movement from rules5e::modified_speed (Ray of Frost-style slows,
// grappled 0, exhaustion): the Aggressive Dash bookkeeping and the AI's Dash planning, the AI's stand-up check (and rules'
// StandUp), and a readied Move's length. Plus the 7.2u NoReactions marker in the sim: no opportunity attack, no readied action.
// Modifiers and conditions are poked into the state directly (test-only, as ally_near_tests), so no replay checks here.
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
using rules5e::ModStat;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json goblin_with(const std::string& id, bool aggressive) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["traits"] = Json::array();
    if (aggressive)
        m["traits"].push_back({{"id", "aggressive"}, {"name", "aggressive"}, {"description", "x"}, {"effect", {{"kind", "aggressive"}}}});
    return m;
}
ContentLibrary make_lib() {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(lib.add(goblin_with("t.orc", true), "monster"));
    REQUIRE_FALSE(lib.add(goblin_with("t.gob", false), "monster"));
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
Command step(Vec2i to) {
    Command s;
    s.kind = CommandKind::Step;
    s.to = to;
    return s;
}
// Test-only state pokes.
rules5e::EncounterState& poke_state(Combat& c) { return const_cast<rules5e::EncounterState&>(c.state()); }
rules5e::Combatant& poke(Combat& c, CombatantId id) { return *const_cast<rules5e::Combatant*>(rules5e::find(c.state(), id)); }
void add_mod(Combat& c, CombatantId target, ModStat st, uint8_t speed_sub = 0) {
    rules5e::ActiveModifier m;
    m.spec.id = 0x7E570000u + uint32_t(poke_state(c).modifiers.size());
    m.spec.stat = st;
    m.spec.speed_sub = speed_sub;
    m.target = target;
    poke_state(c).modifiers.push_back(m);
}
// Re-open the current turn's movement as start_turn would (budget = modified speed) after a poke.
void reset_movement(Combat& c, CombatantId who) {
    poke_state(c).budget.movement_ft = int16_t(rules5e::modified_speed(c.state(), *rules5e::find(c.state(), who)));
}
}  // namespace

TEST_CASE("modified speed: Aggressive Dash plans and books with the slowed speed; grappled never dashes") {
    const auto lib = make_lib();
    {   // 45 ft to reach, slowed to 20: 20 + 20 < 45, so it shoots (with the raw 30 it would have dashed: 20 + 30 >= 45)
        const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {15, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, o = ch.spawns[1].stable_id;
        to_actor(*c, o);
        add_mod(*c, o, ModStat::Speed, 10);
        reset_movement(*c, o);
        CHECK(c->state().budget.movement_ft == 20);
        const Command a = next_command(*c, f);
        CHECK(a.request.kind == ActionKind::Attack);
        CHECK(a.request.attack_index == 1);   // shortbow
    }
    {   // 35 ft to reach, slowed to 20: the Dash adds 20 (40 total); 20 ft of its own go anywhere, then only toward
        const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {13, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, o = ch.spawns[1].stable_id;
        to_actor(*c, o);
        add_mod(*c, o, ModStat::Speed, 10);
        reset_movement(*c, o);
        const Command d = next_command(*c, f);
        REQUIRE(d.request.kind == ActionKind::Dash);
        CHECK(d.request.aggressive);
        auto r = c->submit(d);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(c->state().budget.movement_ft == 40);
        for (int x = 14; x <= 17; ++x) REQUIRE(c->submit(step({x, 5})).ok);   // its own 20 ft, away (raw speed booked only 10)
        CHECK_FALSE(c->aggressive_step_ok(o, {18, 5}));
        CHECK(c->aggressive_step_ok(o, {16, 5}));
    }
    {   // grappled: speed 0, no movement; the Dash would add nothing, so it shoots
        const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {15, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, o = ch.spawns[1].stable_id;
        to_actor(*c, o);
        rules5e::add_condition(poke(*c, o), Condition::Grappled);
        reset_movement(*c, o);
        CHECK(c->state().budget.movement_ft == 0);
        const Command a = next_command(*c, f);
        CHECK(a.request.kind == ActionKind::Attack);
        CHECK(a.request.attack_index == 1);
    }
}

TEST_CASE("modified speed: standing up costs half the modified speed (exhaustion, slowed); a slow to 0 can't stand") {
    const auto lib = make_lib();
    struct Case { uint8_t exhaustion, sub; int left; bool stands; };
    for (const Case k : {Case{2, 0, 8, true}, Case{0, 10, 10, true}, Case{0, 25, 3, true}, Case{0, 30, 0, false}}) {
        CAPTURE(int(k.exhaustion)); CAPTURE(int(k.sub));
        const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {15, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
        to_actor(*c, g);
        rules5e::add_condition(poke(*c, g), Condition::Prone);
        poke(*c, g).exhaustion_level = k.exhaustion;
        if (k.sub) add_mod(*c, g, ModStat::Speed, k.sub);
        reset_movement(*c, g);
        const Command up = next_command(*c, f);
        if (k.stands) {
            REQUIRE(up.request.kind == ActionKind::StandUp);
            auto r = c->submit(up);
            REQUIRE_MESSAGE(r.ok, r.error);
            CHECK_FALSE(rules5e::find(c->state(), g)->has(Condition::Prone));
            CHECK(c->state().budget.movement_ft == k.left);
        } else {
            CHECK(up.request.kind != ActionKind::StandUp);
            Command s;
            s.request = {g, ActionKind::StandUp};
            CHECK_FALSE(c->submit(s).ok);
            CHECK(rules5e::find(c->state(), g)->has(Condition::Prone));
        }
    }
}

TEST_CASE("modified speed: a readied Move is limited to the modified speed") {
    const auto lib = make_lib();
    const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {20, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 8);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id;
    to_actor(*c, f);
    add_mod(*c, f, ModStat::Speed, 10);   // 30 -> 20
    reset_movement(*c, f);
    Command rd;
    rd.request = {f, ActionKind::Ready};
    rd.request.ready_kind = ActionKind::Move;
    rd.request.trigger_id = ready_trigger_id(ReadyTriggerKind::HostileWithin, 3);
    rd.to = {0, 5};   // 25 ft: within the raw 30, beyond the slowed 20
    CHECK(c->submit(rd).error == "readied move longer than speed");
    rd.to = {1, 5};   // 20 ft
    auto r = c->submit(rd);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->log().back().step.request.move_ft == 20);
}

TEST_CASE("NoReactions marker (7.2u) in the sim: no opportunity attack, no readied action") {
    const auto lib = make_lib();
    for (bool marked : {false, true}) {
        CAPTURE(marked);
        {   // opportunity attack: the goblin walks out of the fighter's reach
            const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {6, 5}}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, 3);
            REQUIRE(c);
            const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
            to_actor(*c, g);
            if (marked) add_mod(*c, f, ModStat::NoReactions);
            auto r = c->submit(step({7, 5}));
            REQUIRE_MESSAGE(r.ok, r.error);
            const bool asked = r.status == StepStatus::NeedsDecision && r.needs && r.needs->reactor == f &&
                               r.needs->trigger == ReactionTrigger::LeavingReach;
            CHECK(asked == !marked);
        }
        {   // readied Move: the goblin comes within 3 tiles
            const auto ch = arena({{kFighter, {5, 5}}, {"t.gob", {10, 5}}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, 8);
            REQUIRE(c);
            const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
            to_actor(*c, f);
            Command rd;
            rd.request = {f, ActionKind::Ready};
            rd.request.ready_kind = ActionKind::Move;
            rd.request.trigger_id = ready_trigger_id(ReadyTriggerKind::HostileWithin, 3);
            rd.to = {2, 5};
            REQUIRE(c->submit(rd).ok);
            to_actor(*c, g);
            if (marked) add_mod(*c, f, ModStat::NoReactions);
            REQUIRE(c->submit(step({9, 5})).ok);   // 4 tiles: no trigger yet
            auto r = c->submit(step({8, 5}));      // 3 tiles: the readied Move's trigger
            REQUIRE_MESSAGE(r.ok, r.error);
            const bool asked = r.status == StepStatus::NeedsDecision && r.needs && r.needs->reactor == f &&
                               r.needs->trigger == ReactionTrigger::ReadiedTrigger;
            CHECK(asked == !marked);
            if (asked) r = c->decide({r.needs->window_seq, true, 0});
            CHECK((c->pos(f) == Vec2i{5, 5}) == marked);
        }
    }
}
