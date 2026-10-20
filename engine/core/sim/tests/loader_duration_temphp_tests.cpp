// Engine Core fix/loader-duration-temphp: condition durations until_end_of_caster_next_turn / until_start_of_target_turn
// (rules5e Expiry since #183) on condition effects, and temp_hp riders with amount_from (ability mod + class level, min 1)
// gated by subject_hostile (warlock Dark One's Blessing); temp HP never stacks.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "rules5e/healing.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// A goblin re-authored (from the swarm tests) as a test creature: bite +30 of rats (SRD: Medium swarm of Tiny beasts, 24 HP, bite +2 2d6 / 1d6 at half HP or fewer).
Json rats(const std::string& id = "t.rats", bool swarm = true, const char* bite = "2d1", const char* half = "1d1") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["type"] = "beast";
    if (swarm) m["tags"] = Json::array({"swarm"});
    m["hit_points"]["average"] = 24;
    m["damage_resistances"] = Json::array({"bludgeoning", "piercing", "slashing"});
    m["condition_immunities"] = Json::array({"charmed", "frightened", "grappled", "paralyzed", "petrified", "prone", "restrained", "stunned"});
    m["traits"] = Json::array();
    Json b = m["actions"][0];
    b["id"] = "bite";
    b["to_hit"] = 30;
    b["damage"] = Json::array({{{"dice", bite}, {"type", "piercing"}}});
    if (half) b["half_hp_damage"] = half;
    m["actions"] = Json::array({b});
    return m;
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
ContentLibrary lib_with(const std::vector<Json>& monsters) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
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
SubmitResult submit(Combat& c, const rules5e::ActionRequest& q) {
    Command x;
    x.request = q;
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
}
void same_on_replay(const Combat& c, const world::Chunk& ch, const ContentLibrary& lib, uint64_t seed) {
    ecs::Registry reg;
    std::string err;
    auto r = replay(reg, ch, lib, seed, c.log(), &err, c.surprised_at_start());
    REQUIRE_MESSAGE(r, err);
    CHECK(r->hash() == c.hash());
    CHECK(r->log_hash() == c.log_hash());
}
rules5e::Combatant& mut(rules5e::EncounterState& st, CombatantId id) {
    for (auto& x : st.combatants) if (x.id == id) return x;
    FAIL("no combatant");
    return st.combatants.front();
}
rules5e::Combatant& who(Combat& c, CombatantId id) { return mut(CombatTestAccess::state(c), id); }
}  // namespace

namespace {
// a goblin whose bite applies `cond` for `kind` (until_end_of_caster_next_turn / until_start_of_target_turn)
Json timed_biter(const std::string& id, const char* kind, const char* cond = "blinded") {
    Json m = rats(id, false, "1d1", nullptr);
    m["actions"][0]["on_hit"] = Json::array({{{"kind", "apply_condition"},
                                              {"apply", Json::array({{{"condition", cond}, {"duration", {{"kind", kind}}}}})}}});
    return m;
}
bool has(const Combat& c, CombatantId id, Condition k) { return rules5e::find(c.state(), id)->has(k); }
// bite until it hits, without ending the biter's turn
void hit_once(Combat& c, CombatantId a, CombatantId t) {
    for (int turn = 0; turn < 8; ++turn) {
        to_actor(c, a);
        const int before = rules5e::find(c.state(), t)->hp;
        const auto r = submit(c, {a, ActionKind::Attack, t, 0});
        REQUIRE_MESSAGE(r.ok, r.error);
        if (rules5e::find(c.state(), t)->hp < before) return;
        end_turn(c);
    }
    FAIL("no hit in 8 turns");
}
// Dark One's Blessing as an on_event trait
Json blessed(const std::string& id, Json amount_from, bool hostile_only = true) {
    Json m = rats(id, false, "2d1", nullptr);
    m["abilities"]["cha"] = 18;   // +4
    Json t = {{"kind", "on_event"}, {"on", "reduce_to_zero"},
              {"effects", Json::array({{{"kind", "temp_hp"}, {"amount_from", amount_from}}})}};
    if (hostile_only) t["when"] = Json::array({{{"kind", "subject_hostile"}}});
    m["traits"] = Json::array({{{"id", "dark_ones_blessing"}, {"name", "D"}, {"description", "x"}, {"effect", t}}});
    return m;
}
}  // namespace

TEST_CASE("condition duration until_end_of_caster_next_turn: loads, lasts through the target's turn, ends at the end of the applier's next turn; replay") {
    auto lib = lib_with({timed_biter("t.cnt", "until_end_of_caster_next_turn")});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.cnt", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    hit_once(*c, g, f);
    CHECK(has(*c, f, Condition::Blinded));
    end_turn(*c);                            // the applier's own turn does not count
    CHECK(has(*c, f, Condition::Blinded));
    to_actor(*c, f);
    CHECK(has(*c, f, Condition::Blinded));
    end_turn(*c);                            // the target's turn end does not end it
    CHECK(has(*c, f, Condition::Blinded));
    to_actor(*c, g);
    CHECK(has(*c, f, Condition::Blinded));
    end_turn(*c);                            // the applier's next turn ends: gone
    CHECK_FALSE(has(*c, f, Condition::Blinded));
    same_on_replay(*c, ch, lib, 5);
}

TEST_CASE("condition duration until_start_of_target_turn: ends as the target's next turn starts; replay") {
    auto lib = lib_with({timed_biter("t.sot", "until_start_of_target_turn")});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.sot", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 6);
    REQUIRE(c);
    hit_once(*c, g, f);
    CHECK(has(*c, f, Condition::Blinded));
    end_turn(*c);
    if (rules5e::current_actor(c->state()) != f) CHECK(has(*c, f, Condition::Blinded));
    to_actor(*c, f);
    CHECK_FALSE(has(*c, f, Condition::Blinded));
    same_on_replay(*c, ch, lib, 6);
}

TEST_CASE("condition durations: the other schema-legal kinds stay refused as unsupported") {
    for (const char* k : {"until_start_of_caster_turn", "until_end_of_target_turn", "concentration", "until_rest"}) {
        CAPTURE(k);
        ContentLibrary lib;
        REQUIRE(lib.load(sample_set()).empty());
        CHECK(lib.add(timed_biter("t.x", k), "monster").value_or("").find("unsupported apply_condition duration") != std::string::npos);
    }
}

TEST_CASE("temp_hp amount_from: ability mod + class level, min 1; loader errors") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(blessed("t.a", {{"ability", "cha"}}), "monster"));
    CHECK(lib.find("t.a")->base.triggers.at(0).effects.at(0).value == 4);
    REQUIRE_FALSE(lib.add(blessed("t.b", {{"ability", "cha"}, {"class", "warlock"}}), "monster"));   // no classes[]: level 1
    CHECK(lib.find("t.b")->base.triggers.at(0).effects.at(0).value == 5);
    Json low = blessed("t.c", {{"ability", "str"}});
    low["abilities"]["str"] = 3;   // -4 -> min 1
    REQUIRE_FALSE(lib.add(low, "monster"));
    CHECK(lib.find("t.c")->base.triggers.at(0).effects.at(0).value == 1);
    Json z = blessed("t.d", {{"ability", "str"}, {"min", 0}});
    z["abilities"]["str"] = 9;   // -1 -> min 0
    REQUIRE_FALSE(lib.add(z, "monster"));
    CHECK(lib.find("t.d")->base.triggers.at(0).effects.at(0).value == 0);
    CHECK(lib.add(blessed("t.e", {{"ability", "luck"}}), "monster").value_or("").find("bad amount_from ability") != std::string::npos);
    // a character: the class level comes from classes[]
    Json w = load_json("content/packs/srd51.core/characters/fighter_level_1.json");
    w["id"] = "t.lock";
    w["abilities"]["cha"] = 16;   // +3
    w["classes"] = Json::array({{{"class", "warlock"}, {"level", 4}, {"hit_die", "d8"}}});
    w["features"].push_back({{"id", "dob"}, {"name", "D"}, {"description", "x"},
                             {"effect", {{"kind", "on_event"}, {"on", "reduce_to_zero"}, {"when", Json::array({{{"kind", "subject_hostile"}}})},
                                         {"effects", Json::array({{{"kind", "temp_hp"}, {"amount_from", {{"ability", "cha"}, {"class", "warlock"}}}}})}}}});
    const auto e = lib.add(w, "character");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    const auto& tr = lib.find("t.lock")->base.triggers;
    REQUIRE_FALSE(tr.empty());
    CHECK(tr.back().effects.at(0).value == 7);
    CHECK(tr.back().when.at(0).kind == rules5e::PredicateKind::SubjectHostile);
}

TEST_CASE("Dark One's Blessing: reducing a hostile creature to 0 HP grants temp HP; no stacking (higher kept); not for an ally; replay") {
    for (int pre : {0, 9}) {
        CAPTURE(pre);
        auto lib = lib_with({blessed("t.w", {{"ability", "cha"}, {"class", "warlock"}})});   // 5
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.w", Vec2i{6, 5}}});
        const CombatantId f = ch.spawns[0].stable_id, w = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        to_actor(*c, w);
        who(*c, f).hp = 1;
        who(*c, w).temp_hp = int16_t(pre);
        hit_once(*c, w, f);
        CHECK(rules5e::find(c->state(), f)->hp <= 0);
        CHECK(rules5e::find(c->state(), w)->temp_hp == (pre > 5 ? pre : 5));
    }
    // an ally reduced to 0: no temp HP with subject_hostile; without the predicate it fires
    for (bool hostile_only : {true, false}) {
        CAPTURE(hostile_only);
        auto lib = lib_with({blessed("t.w", {{"ability", "cha"}}, hostile_only), rats("t.ally", false, "1d1", nullptr)});
        const auto ch = arena({{kFighter, Vec2i{2, 2}}, {"t.w", Vec2i{6, 5}}, {"t.ally", Vec2i{7, 5}}});
        const CombatantId w = ch.spawns[1].stable_id, a = ch.spawns[2].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        to_actor(*c, w);
        who(*c, a).hp = 1;
        hit_once(*c, w, a);
        CHECK(rules5e::find(c->state(), a)->hp <= 0);
        CHECK(rules5e::find(c->state(), w)->temp_hp == (hostile_only ? 0 : 4));
    }
}

TEST_CASE("Dark One's Blessing: a real kill through the log replays to the same hash") {
    auto lib = lib_with({blessed("t.w", {{"ability", "cha"}, {"class", "warlock"}})});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.w", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, w = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    for (int i = 0; i < 40 && rules5e::find(c->state(), f)->hp > 0; ++i) {
        to_actor(*c, w);
        REQUIRE(submit(*c, {w, ActionKind::Attack, f, 0}).ok);
        if (rules5e::find(c->state(), f)->hp <= 0) break;   // the last hostile is down: the combat ends
        end_turn(*c);
    }
    REQUIRE(rules5e::find(c->state(), f)->hp <= 0);
    CHECK(rules5e::find(c->state(), w)->temp_hp >= 5);
    same_on_replay(*c, ch, lib, 11);
}
