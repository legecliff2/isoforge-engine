// SRD swarms (swarm of rats): the "swarm" tag -> Archetype::swarm (shared spaces both ways, Combat::occupied_except) and
// Combatant::no_regain_hp (no healing, no temp HP); weapon action `half_hp_damage` (bite 2d6, 1d6 at half HP or fewer);
// condition immunities / BPS resistances through the existing loader keys.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "rules5e/healing.hpp"
#include "sim/combat.hpp"
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
// A goblin re-authored as a swarm of rats (SRD: Medium swarm of Tiny beasts, 24 HP, bite +2 2d6 / 1d6 at half HP or fewer).
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
SubmitResult step(Combat& c, Vec2i to) {
    Command x;
    x.kind = CommandKind::Step;
    x.to = to;
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
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
// one bite against `t` until it hits (a natural 1 misses); returns the target's HP loss
int bite(Combat& c, CombatantId a, CombatantId t) {
    for (int turn = 0; turn < 6; ++turn) {
        to_actor(c, a);
        const int before = rules5e::find(c.state(), t)->hp;
        const auto r = submit(c, {a, ActionKind::Attack, t, 0});
        REQUIRE_MESSAGE(r.ok, r.error);
        const int lost = before - rules5e::find(c.state(), t)->hp;
        end_turn(c);
        if (lost > 0) return lost;
    }
    FAIL("no hit in 6 turns");
    return 0;
}
}  // namespace

TEST_CASE("swarm loader: tag -> swarm + no_regain_hp, half_hp_damage, condition immunities and BPS resistances; errors") {
    auto lib = lib_with({rats(), rats("t.plain", false, "2d1", nullptr)});
    const auto* a = lib.find("t.rats");
    CHECK(a->swarm);
    CHECK(a->base.no_regain_hp);
    REQUIRE(a->base.attacks.size() == 1);
    CHECK(a->base.attacks[0].half_hp_damage == rules5e::DiceExpr{1, 1, 0});
    for (Condition k : {Condition::Charmed, Condition::Frightened, Condition::Grappled, Condition::Paralyzed, Condition::Petrified,
                        Condition::Prone, Condition::Restrained, Condition::Stunned})
        CHECK((a->base.condition_immunities & rules5e::bit(k)) != 0);
    for (auto d : {rules5e::DamageType::Bludgeoning, rules5e::DamageType::Piercing, rules5e::DamageType::Slashing})
        CHECK((a->base.resistances & (1u << unsigned(d))) != 0);
    const auto* p = lib.find("t.plain");
    CHECK_FALSE(p->swarm);
    CHECK_FALSE(p->base.no_regain_hp);
    CHECK(p->base.attacks[0].half_hp_damage.count == 0);
    CHECK(lib.add(rats("t.bad", true, "2d6", "x"), "monster").value_or("").find("bad half_hp_damage") != std::string::npos);
    CHECK(lib.add(rats("t.bad2", true, "2d6", "5"), "monster").value_or("").find("bad half_hp_damage") != std::string::npos);
}

TEST_CASE("swarm shares spaces both ways; non-swarms still block each other; replay") {
    auto lib = lib_with({rats(), rats("t.plain", false, "2d1", nullptr)});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.rats", Vec2i{7, 5}}, {"t.plain", Vec2i{5, 7}}});
    const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id, g = ch.spawns[2].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    // the swarm moves into, through and ends in the fighter's space
    to_actor(*c, s);
    REQUIRE(step(*c, {6, 5}).ok);
    auto r = step(*c, {5, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    r = step(*c, {4, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    r = step(*c, {5, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->pos(s) == c->pos(f));
    end_turn(*c);
    // a plain goblin cannot enter the fighter's space, but can enter the swarm's
    to_actor(*c, g);
    REQUIRE(step(*c, {5, 6}).ok);
    CHECK_FALSE(step(*c, {5, 5}).ok);   // fighter (+ swarm) tile: the fighter still blocks
    end_turn(*c);
    // the fighter leaves and re-enters the shared tile: the swarm does not block it
    to_actor(*c, f);
    r = step(*c, {6, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    r = step(*c, {5, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->pos(s) == c->pos(f));
    end_turn(*c);
    // once the fighter has gone, the plain goblin can end in the swarm's space
    to_actor(*c, f);
    REQUIRE(step(*c, {4, 4}).ok);
    end_turn(*c);
    to_actor(*c, g);
    r = step(*c, {5, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->pos(g) == c->pos(s));
    end_turn(*c);
    same_on_replay(*c, ch, lib, 4);
}

TEST_CASE("swarm bite: 2d6 above half HP, half_hp_damage at half HP or fewer; a plain creature keeps its dice; replay") {
    auto lib = lib_with({rats("t.rats", true, "4d1", "1d1")});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.rats", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    CHECK(bite(*c, s, f) == 4);   // 24 / 24
    // the fighter hits the swarm (slashing, resisted) until it is at 12 or fewer
    for (int i = 0; i < 30 && rules5e::find(c->state(), s)->hp > 12 && !rules5e::find(c->state(), s)->dead; ++i) {
        to_actor(*c, f);
        REQUIRE(submit(*c, {f, ActionKind::Attack, s, 0}).ok);
        end_turn(*c);
    }
    const int hp = rules5e::find(c->state(), s)->hp;
    REQUIRE(hp > 0);
    REQUIRE(hp <= 12);
    CHECK(bite(*c, s, f) == 1);
    same_on_replay(*c, ch, lib, 9);
    // boundary: exactly half (12 / 24) uses the variant, 13 does not
    for (int at : {13, 12}) {
        ecs::Registry r2;
        auto d = Combat::create(r2, ch, lib, 9);
        who(*d, s).hp = int16_t(at);
        CHECK(bite(*d, s, f) == (at == 12 ? 1 : 4));
    }
}

TEST_CASE("swarm: no healing and no temp HP (damage_dealt heal_self / temp_hp riders, apply_healing); a non-swarm twin gains both") {
    for (bool swarm : {true, false}) {
        CAPTURE(swarm);
        Json m = rats("t.r", swarm);
        m["traits"].push_back({{"id", "feed"}, {"name", "F"}, {"description", "x"},
                               {"effect", {{"kind", "on_event"}, {"on", "damage_dealt"},
                                           {"effects", Json::array({{{"kind", "heal_self"}, {"value", 5}}, {{"kind", "temp_hp"}, {"value", 7}}})}}}});
        auto lib = lib_with({m});
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.r", Vec2i{6, 5}}});
        const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 2);
        REQUIRE(c);
        who(*c, s).hp = 10;
        bite(*c, s, f);
        const auto* r = rules5e::find(c->state(), s);
        CHECK(r->hp == (swarm ? 10 : 15));
        CHECK(r->temp_hp == (swarm ? 0 : 7));
        // rules Heal request straight on the state
        auto st = c->state();
        mut(st, s).hp = 10;
        const auto h = rules5e::apply_healing(mut(st, s), 6);
        CHECK(h.healed == (swarm ? 0 : 6));
    }
}
