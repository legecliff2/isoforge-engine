// Combat::hash folds the v7 state tables (action_uses tag 0xA1, pools tag 0xA3) only when non-empty.
// CombatTestAccess is a test-only friend of Combat: it registers rows (nothing in sim does yet, so
// no public API can); it is not part of the sim contract.
#include <doctest/doctest.h>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;

namespace {
world::Chunk arena() {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {"srd51.character.fighter_1", "srd51.monster.goblin"};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + 3 * i), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
}  // namespace

TEST_CASE("Combat::hash: v7 sections are folded only when non-empty, each under its own tag") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    auto make = [&](ecs::Registry& reg) { auto c = Combat::create(reg, ch, l, 9); REQUIRE(c); return std::move(*c); };
    ecs::Registry r0, r1, r2, r3, r4;
    Combat base = make(r0), au = make(r1), pl = make(r2), both = make(r3), again = make(r4);
    const CombatantId a = ch.spawns[0].stable_id;
    const uint64_t h0 = base.hash();
    CHECK(again.hash() == h0);   // deterministic, and empty tables fold nothing

    rules5e::register_action_use(CombatTestAccess::state(au), a, 77);
    rules5e::register_pool(CombatTestAccess::state(pl), a, 5, 3, 3);
    rules5e::register_action_use(CombatTestAccess::state(both), a, 77);
    rules5e::register_pool(CombatTestAccess::state(both), a, 5, 3, 3);

    const uint64_t hau = au.hash(), hpl = pl.hash(), hboth = both.hash();
    CHECK(hau != h0);
    CHECK(hpl != h0);
    CHECK(hau != hpl);               // the tag bytes keep "action uses only" and "pools only" apart
    CHECK(hboth != hau);
    CHECK(hboth != hpl);
    // Every field of a row is covered.
    CHECK_FALSE(hau == [&] { rules5e::register_action_use(CombatTestAccess::state(au), a, 77, true); return au.hash(); }());
    rules5e::register_action_use(CombatTestAccess::state(au), a, 77, false);
    CHECK(au.hash() == hau);          // restoring the row restores the hash
    rules5e::register_pool(CombatTestAccess::state(pl), a, 5, 3, 2);
    CHECK(pl.hash() != hpl);
    // Emptying a table returns to the baseline: nothing is folded for an empty table.
    CombatTestAccess::state(both).action_uses.clear();
    CombatTestAccess::state(both).pools.clear();
    CHECK(both.hash() == h0);
}

TEST_CASE("Combat::hash: timed lasting effects fold under tag 0xA5 only when present") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    ecs::Registry r0, r1, r2;
    auto c0 = Combat::create(r0, ch, l, 9), c1 = Combat::create(r1, ch, l, 9), c2 = Combat::create(r2, ch, l, 9);
    REQUIRE(c0); REQUIRE(c1); REQUIRE(c2);
    const CombatantId a = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
    const uint64_t h0 = c0->hash();
    auto effect = [&](Combat& c, rules5e::Timed t) {
        CombatTestAccess::state(c).effects.push_back(rules5e::ActiveSpellEffect{
            5, a, b, rules5e::Condition::Frightened, rules5e::Ability::Wis, 10, rules5e::RepeatSave::None, false, t});
    };
    effect(*c1, {});   // an untimed effect folds nothing (effects were never hashed)
    CHECK(c1->hash() == h0);
    effect(*c1, {rules5e::Expiry::Rounds, 3});
    effect(*c2, {rules5e::Expiry::Rounds, 2});
    CHECK(c1->hash() != h0);
    CHECK(c2->hash() != h0);
    CHECK(c1->hash() != c2->hash());   // rounds_left is covered
}

TEST_CASE("Combat::hash: R-G15 spell-kind effect state folds under tag 0xAC only for ends-on-damage / damage effects") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    ecs::Registry r0, r1, r2, r3;
    auto c0 = Combat::create(r0, ch, l, 9), c1 = Combat::create(r1, ch, l, 9), c2 = Combat::create(r2, ch, l, 9),
         c3 = Combat::create(r3, ch, l, 9);
    REQUIRE(c0); REQUIRE(c1); REQUIRE(c2); REQUIRE(c3);
    const CombatantId a = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
    auto push = [&](Combat& c) -> rules5e::ActiveSpellEffect& {
        auto& fx = CombatTestAccess::state(c).effects;
        fx.push_back(rules5e::ActiveSpellEffect{7, a, b, rules5e::Condition::Blinded, rules5e::Ability::Wis, 0,
                                                rules5e::RepeatSave::None, true});
        return fx.back();
    };
    // An untimed plain effect (and the R-G15 fields left at their defaults, e.g. slot_level) folds nothing: older content hashes
    // as before.
    push(*c1).slot_level = 3;
    const uint64_t h0 = c0->hash();
    CHECK(c1->hash() == h0);
    // Heat Metal-style sustained damage: folded, and each field is covered.
    auto& e2 = push(*c2);
    e2.no_condition = true; e2.damage_when = 2; e2.damage = {2, 8, 0}; e2.damage_type = rules5e::DamageType::Fire; e2.cast_stamp = 0x10002;
    const uint64_t h2 = c2->hash();
    CHECK(h2 != h0);
    e2.cast_stamp = 0x20002;
    CHECK(c2->hash() != h2);
    e2.cast_stamp = 0x10002;
    CHECK(c2->hash() == h2);
    e2.damage.count = 3;
    CHECK(c2->hash() != h2);
    e2.damage.count = 2;
    e2.slot_level = 3;
    CHECK(c2->hash() != h2);
    e2.slot_level = 0;
    e2.damage_when = 1;
    CHECK(c2->hash() != h2);
    // Sleep-style ends_on_damage alone is folded too.
    push(*c3).ends_on_damage = true;
    CHECK(c3->hash() != h0);
    CombatTestAccess::state(*c3).effects.back().ends_on_damage = false;
    CHECK(c3->hash() == h0);
}

TEST_CASE("Combat::hash: small-leftover state (rules #111) folds under tag 0xAA only when present") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    ecs::Registry r0, r1;
    auto c0 = Combat::create(r0, ch, l, 9), c1 = Combat::create(r1, ch, l, 9);
    REQUIRE(c0); REQUIRE(c1);
    const CombatantId a = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
    const uint64_t h0 = c0->hash();
    CHECK(c1->hash() == h0);
    CHECK_FALSE(rules5e::has_leftover_state(c1->state()));
    auto who = [&](CombatantId id) -> rules5e::Combatant& {
        for (auto& k : CombatTestAccess::state(*c1).combatants) if (k.id == id) return k;
        FAIL("no such combatant");
        return CombatTestAccess::state(*c1).combatants.front();
    };
    // Each field is covered, and clearing it returns to the baseline (nothing is folded without leftover state).
    who(a).hp_max_reduction = 4;
    const uint64_t hd = c1->hash();
    CHECK(hd != h0);
    who(a).hp_max_reduction = 5;
    CHECK(c1->hash() != hd);
    who(a).hp_max_reduction = 0;
    CHECK(c1->hash() == h0);
    who(b).lethargy = 1;
    const uint64_t hl = c1->hash();
    CHECK(hl != h0);
    who(b).lethargy = 2;
    CHECK(c1->hash() != hl);
    who(b).lethargy = 0;
    who(a).reckless_active = true;
    const uint64_t hr = c1->hash();
    CHECK(hr != h0);
    who(a).reckless_active = false;
    who(b).reckless_active = true;
    CHECK(c1->hash() != hr);   // the combatant id is covered
    who(b).reckless_active = false;
    CHECK(c1->hash() == h0);
    // An escalating (Petrifying Gaze) effect alone: folded under 0xAA (it is untimed, so 0xA5 adds nothing).
    CombatTestAccess::state(*c1).effects.push_back(rules5e::ActiveSpellEffect{
        9, b, a, rules5e::Condition::Restrained, rules5e::Ability::Con, 12, rules5e::RepeatSave::EndOfTurn, false});
    const uint64_t he0 = c1->hash();
    CombatTestAccess::state(*c1).effects.back().escalates = true;
    const uint64_t he = c1->hash();
    CHECK(he != he0);
    CombatTestAccess::state(*c1).effects.back().escalate_to = rules5e::Condition::Paralyzed;
    CHECK(c1->hash() != he);
    CHECK(rules5e::has_leftover_state(c1->state()));
    CombatTestAccess::state(*c1).effects.back().escalates = false;
    CHECK(c1->hash() == he0);
}

TEST_CASE("Combat::log_hash: small-leftover request flags (rules #111) fold under tag 0xB3 only when set") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    ecs::Registry r0;
    auto c0 = Combat::create(r0, ch, l, 9);
    REQUIRE(c0);
    for (int i = 0; i < 3; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c0->state()), rules5e::ActionKind::EndTurn};
        REQUIRE(c0->submit(e).ok);
    }
    auto& log = CombatTestAccess::log(*c0);
    REQUIRE(log.size() == 3);
    const uint64_t h0 = c0->log_hash();
    rules5e::ActionRequest& q = log[1].step.request;
    std::vector<uint64_t> seen{h0};
    for (int bits = 1; bits < 8; ++bits) {   // every combination of bit0 haste_action, bit1 reckless, bit2 aggressive differs
        q.haste_action = (bits & 1) != 0;
        q.reckless = (bits & 2) != 0;
        q.aggressive = (bits & 4) != 0;
        const uint64_t h = c0->log_hash();
        for (uint64_t o : seen) CHECK(h != o);
        seen.push_back(h);
    }
    q.haste_action = q.reckless = q.aggressive = false;
    CHECK(c0->log_hash() == h0);   // the byte is not folded when zero: older logs hash as before
    // Which entry carries the flag is covered too.
    q.aggressive = true;
    const uint64_t h1 = c0->log_hash();
    q.aggressive = false;
    log[2].step.request.aggressive = true;
    CHECK(c0->log_hash() != h1);
}
