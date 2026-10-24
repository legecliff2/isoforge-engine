// monster@5 flat action damage (CONTENT_CONVENTIONS 19): `{flat N, type}` instead of `{dice, type}` (SRD 5.1 "Hit: 1 piercing
// damage": rat, cat, scorpion, ...). Loads as DiceExpr{0, 0, N}; rolls no dice (no rng draw) and a critical hit does not double
// it. No new state, no hash tag: existing goldens are untouched (trace_equivalence / hash_fold tests run unchanged).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rng/rng.hpp"
#include "rules5e/dice.hpp"
#include "sample_set.hpp"
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

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// A goblin re-authored as an SRD rat-like biter: one bite, damage components as given.
Json biter(const std::string& id, Json damage, const char* schema = "monster@5") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["traits"] = Json::array();
    Json b = m["actions"][0];
    b["id"] = "bite";
    b["to_hit"] = 30;
    b["damage"] = std::move(damage);
    m["actions"] = Json::array({b});
    return m;
}
Json flat1() { return Json::array({{{"flat", 1}, {"type", "piercing"}}}); }
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
rules5e::Combatant& who(Combat& c, CombatantId id) {
    for (auto& x : CombatTestAccess::state(c).combatants) if (x.id == id) return x;
    FAIL("no combatant");
    return CombatTestAccess::state(c).combatants.front();
}
// one bite against a paralyzed `t` (advantage, auto-crit within 5 ft; to_hit 30): the target's HP loss
int crit_bite(Combat& c, CombatantId a, CombatantId t) {
    for (int turn = 0; turn < 6; ++turn) {
        to_actor(c, a);
        who(c, t).conditions |= rules5e::bit(rules5e::Condition::Paralyzed);
        const int before = rules5e::find(c.state(), t)->hp;
        Command x;
        x.request = {a, ActionKind::Attack, t, 0};
        auto r = c.submit(x);
        if (r.ok) r = c.settle(nullptr);
        REQUIRE_MESSAGE(r.ok, r.error);
        const int lost = before - rules5e::find(c.state(), t)->hp;
        end_turn(c);
        if (lost > 0) return lost;
    }
    FAIL("no hit in 6 turns");
    return 0;
}
}  // namespace

TEST_CASE("flat damage loader: monster@5 {flat, type} -> DiceExpr{0,0,N}; dice unchanged; errors") {
    Json mixed = Json::array({{{"flat", 1}, {"type", "piercing"}}, {{"dice", "3d4"}, {"type", "poison"}}});   // flying snake
    auto lib = lib_with({biter("t.rat", flat1()), biter("t.snake", mixed),
                         biter("t.dice", Json::array({{{"dice", "1d4+2"}, {"type", "piercing"}}}))});
    const auto* rat = lib.find("t.rat");
    REQUIRE(rat->base.attacks.size() == 1);
    CHECK(rat->base.attacks[0].damage == rules5e::DiceExpr{0, 0, 1});
    CHECK(rat->base.attacks[0].type == rules5e::DamageType::Piercing);
    const auto* snake = lib.find("t.snake");
    CHECK(snake->base.attacks[0].damage == rules5e::DiceExpr{0, 0, 1});
    REQUIRE(snake->base.attacks[0].extra_damage.size() == 1);
    CHECK(snake->base.attacks[0].extra_damage[0].dice == rules5e::DiceExpr{3, 4, 0});
    CHECK(lib.find("t.dice")->base.attacks[0].damage == rules5e::DiceExpr{1, 4, 2});

    auto bad = [&](const std::string& id, Json dmg, const char* schema = "monster@5") {
        const auto e = lib.add(biter(id, std::move(dmg), schema), "monster");
        CHECK_MESSAGE(e.value_or("").find("bad damage") != std::string::npos, id);
    };
    bad("t.b0", Json::array({{{"flat", 0}, {"type", "piercing"}}}));
    bad("t.b1", Json::array({{{"flat", 256}, {"type", "piercing"}}}));
    bad("t.b2", Json::array({{{"flat", 1}, {"dice", "1d4"}, {"type", "piercing"}}}));
    bad("t.b3", Json::array({{{"flat", "1"}, {"type", "piercing"}}}));
    bad("t.b4", Json::array({{{"flat", 1}, {"type", "piercing"}, {"bonus_vs", {{"creature_types", {"undead"}}, {"dice", "1d6"}}}}}));
    bad("t.b5", flat1(), "monster@4");   // monster@5 only
    bad("t.b6", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}, {{"flat", 1}, {"type", "poison"}}}), "monster@4");
}

TEST_CASE("flat damage roll: no rng draw, a critical hit does not double the flat amount") {
    rng::Rng r(42, 7);
    const auto before = r.save();
    CHECK(rules5e::roll_dice(r, rules5e::DiceExpr{0, 0, 1}) == 1);
    CHECK(rules5e::roll_dice(r, rules5e::DiceExpr{0, 0, 1}, true) == 1);
    CHECK(rules5e::roll_dice_reroll_low(r, rules5e::DiceExpr{0, 0, 3}, true, 2) == 3);
    const auto after = r.save();
    CHECK(after.state == before.state);
    CHECK(after.inc == before.inc);
}

TEST_CASE("flat damage in combat: a crit deals 1 (dice would double); mixed flat + dice") {
    Json mixed = Json::array({{{"flat", 1}, {"type", "piercing"}}, {{"dice", "1d1"}, {"type", "poison"}}});
    auto lib = lib_with({biter("t.rat", flat1()), biter("t.die", Json::array({{{"dice", "1d1"}, {"type", "piercing"}}})),
                         biter("t.snake", mixed)});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.rat", Vec2i{6, 5}}, {"t.die", Vec2i{4, 5}}, {"t.snake", Vec2i{5, 6}}});
    const CombatantId f = ch.spawns[0].stable_id, rat = ch.spawns[1].stable_id, die = ch.spawns[2].stable_id,
                      snake = ch.spawns[3].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    CHECK(crit_bite(*c, die, f) == 2);     // control: 1d1 on a crit is 2d1
    CHECK(crit_bite(*c, rat, f) == 1);     // flat 1 stays 1
    CHECK(crit_bite(*c, snake, f) == 3);   // 1 flat + 2d1 poison
}

TEST_CASE("flat damage replay: every hit (natural 20 included) deals exactly 1; replay matches hash and log hash") {
    auto lib = lib_with({biter("t.rat", flat1())});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.rat", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, rat = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    for (int turn = 0; turn < 6; ++turn) {
        to_actor(*c, rat);
        const int before = rules5e::find(c->state(), f)->hp;
        Command x;
        x.request = {rat, ActionKind::Attack, f, 0};
        auto r = c->submit(x);
        if (r.ok) r = c->settle(nullptr);
        REQUIRE_MESSAGE(r.ok, r.error);
        const int lost = before - rules5e::find(c->state(), f)->hp;
        CHECK((lost == 0 || lost == 1));
        end_turn(*c);
    }
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 11, c->log(), &err, c->surprised_at_start());
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
}
