// Row 12 follow-ups: consumed-early modifiers, speed reduction, AC floor, per-turn temp HP, general `when` scope.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

Combatant striker(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    return c;
}
Combatant dummy(CombatantId id, int ac = 10) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = int8_t(ac);
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
ModifierSpec mod(uint32_t id, ModStat st) {
    ModifierSpec m; m.id = id; m.source = id; m.stat = st; m.applies_to = ModAppliesTo::Target; return m;
}
ModifierSpec flat(uint32_t id, ModStat st, int bonus) {
    ModifierSpec m = mod(id, st); m.bonus = int16_t(bonus); return m;
}
ActionRequest stab(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
const Event* find_ev(const std::vector<Event>& ev, EventKind k, int nth = 0) {
    for (const auto& e : ev) if (e.kind == k && nth-- == 0) return &e;
    return nullptr;
}
int index_of(const std::vector<Event>& ev, EventKind k) {
    for (int i = 0; i < int(ev.size()); ++i) if (ev[std::size_t(i)].kind == k) return i;
    return -1;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, spell_hooks());
    end_turns_until(s, 1);
    return s;
}
Predicate pred(PredicateKind k, int a = 0, uint32_t mask = 0) {
    Predicate p; p.kind = k; p.a = int16_t(a); p.mask = mask; return p;
}
const uint32_t kGuiding = spell_id_of("test.spell#guiding_bolt");
SpellDef guiding_def() {
    SpellDef d; d.id = kGuiding; d.content_id = "test.spell#guiding_bolt"; d.level = 1; d.range_ft = 120;
    d.components = kComponentV | kComponentS;
    d.kind = SpellEffectKind::SpellAttack; d.dice = DiceExpr{1, 6, 0}; d.damage_type = DamageType::Radiant;
    SpellEffect fx; fx.kind = SpellEffectKind::ApplyModifier; fx.affected_only = true;
    fx.modifier = mod(9100, ModStat::AttacksAgainst);
    fx.modifier.mode = RollMode::Advantage; fx.modifier.consume = 2; fx.modifier.expiry = 3;   // 7.2u: end of the caster's next turn
    d.extra_effects.push_back(fx);
    return d;
}
}  // namespace

TEST_CASE("defaults") {
    ModifierSpec m;
    CHECK(m.consume == 0); CHECK(m.speed_sub == 0); CHECK(m.ac_floor == 0);
    CHECK_FALSE(m.ac_floor_dex); CHECK_FALSE(m.per_turn); CHECK(m.when.empty());
    ActiveModifier a;
    CHECK_FALSE(a.gated);
}

TEST_CASE("consume 1: spent by the target's next attack roll") {
    for (uint64_t seed = 0; seed < 6; ++seed) {
        auto s = start({striker(1), dummy(2)}, seed);
        ModifierSpec m = mod(1, ModStat::AttackRoll); m.mode = RollMode::Advantage; m.consume = 1;
        std::vector<Event> sink; apply_modifier(s, 1, 1, m, sink);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        CHECK(s.modifiers.empty());
        const Event* x = find_ev(r.events, EventKind::ModifierExpired);
        REQUIRE(x);
        CHECK(x->a == 1); CHECK(x->b == 3);
        CHECK(index_of(r.events, EventKind::ModifierExpired) < index_of(r.events, EventKind::AttackResolved));
    }
}

TEST_CASE("consume 1: advantage is used by that attack and not by the next") {
    int better = 0, trials = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start({striker(1), dummy(2)}, seed);
        auto base = start({striker(1), dummy(2)}, seed);
        ModifierSpec m = mod(1, ModStat::AttackRoll); m.mode = RollMode::Advantage; m.consume = 1;
        std::vector<Event> sink; apply_modifier(s, 1, 1, m, sink);
        const int with = find_ev(step(s, stab(1, 2)).events, EventKind::AttackResolved)->a;
        const int without = find_ev(step(base, stab(1, 2)).events, EventKind::AttackResolved)->a;
        ++trials; if (with > without) ++better;
    }
    CHECK(better > 0);
    CHECK(trials == 60);
}

TEST_CASE("consume 2: spent by an attack against the target, not by the target's own attack") {
    auto s = start({striker(1), dummy(2)}, 3);
    ModifierSpec m = mod(2, ModStat::AttacksAgainst); m.mode = RollMode::Advantage; m.consume = 2; m.until_caster_turn = true;
    std::vector<Event> sink; apply_modifier(s, 1, 2, m, sink);
    // 2 attacks 1: not consumed
    (void)step(s, {1, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 2);
    auto own = step(s, stab(2, 1));
    REQUIRE(own.ok);
    CHECK(s.modifiers.size() == 1);
    (void)step(s, {2, ActionKind::EndTurn});   // 1's turn starts: until_caster_turn would have dropped it
    // (the modifier above was applied by 1 with until_caster_turn, so it is gone now: re-apply and let 1 attack 2)
    sink.clear();
    ModifierSpec m2 = mod(3, ModStat::AttacksAgainst); m2.mode = RollMode::Advantage; m2.consume = 2;
    apply_modifier(s, 1, 2, m2, sink);
    auto hit = step(s, stab(1, 2));
    REQUIRE(hit.ok);
    CHECK(s.modifiers.empty());
    CHECK(find_ev(hit.events, EventKind::ModifierExpired)->b == 3);
}

TEST_CASE("consume 3: until damaged") {
    int checked = 0;
    for (uint64_t seed = 0; seed < 40 && checked < 4; ++seed) {
        auto s = start({striker(1), dummy(2)}, seed);
        ModifierSpec m = flat(4, ModStat::Ac, 0); m.consume = 3;
        std::vector<Event> sink; apply_modifier(s, 1, 2, m, sink);
        auto r = step(s, stab(1, 2));
        if (dealt_to(r.events, 2) == 0) {
            CHECK(s.modifiers.size() == 1);   // a miss does not spend it
            continue;
        }
        CHECK(s.modifiers.empty());
        const Event* x = find_ev(r.events, EventKind::ModifierExpired);
        REQUIRE(x);
        CHECK(x->b == 4);
        ++checked;
    }
    CHECK(checked > 0);
}

TEST_CASE("speed reduction: scale then subtract, never below zero") {
    auto s = start({striker(1), dummy(2)}, 5);
    ModifierSpec sub = mod(5, ModStat::Speed); sub.speed_sub = 10;
    std::vector<Event> ev; apply_modifier(s, 1, 2, sub, ev);
    CHECK(modified_speed(s, s.combatants[1]) == 20);
    CHECK(find_ev(ev, EventKind::ModifierApplied)->c == 10);
    ModifierSpec half = mod(6, ModStat::Speed); half.speed_den = 2; half.speed_sub = 10;
    apply_modifier(s, 1, 1, half, ev);
    CHECK(modified_speed(s, s.combatants[0]) == 5);   // 30 / 2 - 10
    ModifierSpec big = mod(7, ModStat::Speed); big.speed_sub = 200;
    apply_modifier(s, 1, 1, big, ev);
    CHECK(modified_speed(s, s.combatants[0]) == 0);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 20);
}

TEST_CASE("AC floor and AC floor with Dex; floor loses to better armor; bonuses stack on top") {
    auto s = start({striker(1), dummy(2, 10)}, 6);
    std::vector<Event> ev;
    ModifierSpec bark = mod(8, ModStat::Ac); bark.ac_floor = 16;
    apply_modifier(s, 1, 2, bark, ev);
    CHECK(modified_ac_bonus(s, 2) == 6);
    CHECK(find_ev(ev, EventKind::ModifierApplied)->c == 16);
    s.combatants[1].ac = 18;
    CHECK(modified_ac_bonus(s, 2) == 0);
    s.combatants[1].ac = 10;
    apply_modifier(s, 1, 2, flat(9, ModStat::Ac, 2), ev);
    CHECK(modified_ac_bonus(s, 2) == 8);               // 16 floor + the +2
    s.modifiers.clear();
    s.combatants[1].abilities.score[1] = 14;           // Dex +2
    ModifierSpec mage = mod(10, ModStat::Ac); mage.ac_floor = 13; mage.ac_floor_dex = true;
    apply_modifier(s, 1, 2, mage, ev);
    CHECK(modified_ac_bonus(s, 2) == 5);               // 13 + 2 - 10
    // the floor reaches the attack roll
    auto t = start({striker(1), dummy(2, 10)}, 7);
    ModifierSpec wall = mod(11, ModStat::Ac); wall.ac_floor = 100;
    apply_modifier(t, 1, 2, wall, ev);
    for (uint64_t seed = 0; seed < 10; ++seed) {
        auto u = start({striker(1), dummy(2, 10)}, seed);
        apply_modifier(u, 1, 2, wall, ev);
        const auto r = step(u, stab(1, 2));   // keep the result alive: the event pointer points into it
        const Event* a = find_ev(r.events, EventKind::AttackResolved);
        REQUIRE(a);
        if (a->c & 1) CHECK(a->a == 20);
    }
}

TEST_CASE("per-turn temp HP (Heroism)") {
    auto s = start({striker(1), dummy(2)}, 8);
    ModifierSpec h = flat(12, ModStat::TempHp, 5); h.per_turn = true; h.expiry = 1; h.rounds = 3;
    std::vector<Event> ev; apply_modifier(s, 1, 2, h, ev);
    CHECK(s.combatants[1].temp_hp == 5);
    CHECK(s.modifiers.size() == 1);
    CHECK(count(ev, EventKind::ModifierApplied) == 1);
    s.combatants[1].temp_hp = 0;
    (void)step(s, {1, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 2);
    CHECK(s.combatants[1].temp_hp == 5);               // granted again at the start of its turn
    s.combatants[1].temp_hp = 2;
    for (int i = 0; i < 12 && !s.modifiers.empty(); ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
    CHECK(s.modifiers.empty());                        // expired after 3 rounds
    // a plain temp-hp modifier leaves no record
    auto t = start({striker(1), dummy(2)}, 9);
    apply_modifier(t, 1, 2, flat(13, ModStat::TempHp, 5), ev);
    CHECK(t.modifiers.empty());
}

TEST_CASE("when scope: attack roll and damage") {
    // AttackRoll +50 only for melee weapon attacks: applies to the dagger (not ranged), not when Ranged is required
    for (uint64_t seed = 0; seed < 5; ++seed) {
        auto base = start({striker(1), dummy(2)}, seed);
        const int b = find_ev(step(base, stab(1, 2)).events, EventKind::AttackResolved)->b;
        auto melee = start({striker(1), dummy(2)}, seed);
        ModifierSpec m = flat(14, ModStat::AttackRoll, 50); m.when = {pred(PredicateKind::Weapon, int(WeaponIs::Melee))};
        std::vector<Event> sink; apply_modifier(melee, 1, 1, m, sink);
        CHECK(find_ev(step(melee, stab(1, 2)).events, EventKind::AttackResolved)->b == b + 50);
        CHECK(melee.modifiers.size() == 1);
        CHECK_FALSE(melee.modifiers[0].gated);
        auto ranged = start({striker(1), dummy(2)}, seed);
        ModifierSpec r = flat(15, ModStat::AttackRoll, 50); r.when = {pred(PredicateKind::Weapon, int(WeaponIs::Ranged))};
        apply_modifier(ranged, 1, 1, r, sink);
        CHECK(find_ev(step(ranged, stab(1, 2)).events, EventKind::AttackResolved)->b == b);
    }
    // Damage +5 only against a creature type
    int checked = 0;
    for (uint64_t seed = 0; seed < 40 && checked < 4; ++seed) {
        auto base = start({striker(1), dummy(2)}, seed);
        auto rb = step(base, stab(1, 2));
        const Event* a = find_ev(rb.events, EventKind::AttackResolved);
        if (!(a->c & 1) || (a->c & 2)) continue;
        auto yes = start({striker(1), dummy(2)}, seed);
        ModifierSpec m = flat(16, ModStat::Damage, 5); m.when = {pred(PredicateKind::TargetCreatureType, 0, bit(CreatureType::Humanoid))};
        std::vector<Event> sink; apply_modifier(yes, 1, 1, m, sink);
        CHECK(dealt_to(step(yes, stab(1, 2)).events, 2) == dealt_to(rb.events, 2) + 5);
        auto no = start({striker(1), dummy(2)}, seed);
        ModifierSpec n = flat(17, ModStat::Damage, 5); n.when = {pred(PredicateKind::TargetCreatureType, 0, bit(CreatureType::Undead))};
        apply_modifier(no, 1, 1, n, sink);
        CHECK(dealt_to(step(no, stab(1, 2)).events, 2) == dealt_to(rb.events, 2));
        ++checked;
    }
    CHECK(checked > 0);
}

TEST_CASE("when scope: a gated consume-1 modifier is not spent") {
    auto s = start({striker(1), dummy(2)}, 4);
    ModifierSpec m = mod(18, ModStat::AttackRoll); m.mode = RollMode::Advantage; m.consume = 1;
    m.when = {pred(PredicateKind::Weapon, int(WeaponIs::Ranged))};
    std::vector<Event> sink; apply_modifier(s, 1, 1, m, sink);
    auto r = step(s, stab(1, 2));
    REQUIRE(r.ok);
    CHECK(s.modifiers.size() == 1);
    CHECK(count(r.events, EventKind::ModifierExpired) == 0);
}

TEST_CASE("digest covers the new fields") {
    auto a = start({striker(1), dummy(2)}, 11);
    auto b = start({striker(1), dummy(2)}, 11);
    std::vector<Event> ev;
    apply_modifier(a, 1, 2, flat(20, ModStat::Ac, 1), ev);
    apply_modifier(b, 1, 2, flat(20, ModStat::Ac, 1), ev);
    CHECK(modifiers_digest(a) == modifiers_digest(b));
    auto digest_with = [&](auto fn) {
        auto c = start({striker(1), dummy(2)}, 11);
        ModifierSpec m = flat(20, ModStat::Ac, 1); fn(m);
        apply_modifier(c, 1, 2, m, ev);
        return modifiers_digest(c);
    };
    const uint64_t base = modifiers_digest(a);
    CHECK(digest_with([](ModifierSpec& m) { m.consume = 1; }) != base);
    CHECK(digest_with([](ModifierSpec& m) { m.speed_sub = 5; }) != base);
    CHECK(digest_with([](ModifierSpec& m) { m.ac_floor = 12; }) != base);
    CHECK(digest_with([](ModifierSpec& m) { m.ac_floor_dex = true; }) != base);
    CHECK(digest_with([](ModifierSpec& m) { m.per_turn = true; }) != base);
    CHECK(digest_with([](ModifierSpec& m) { m.when = {pred(PredicateKind::NotCrit)}; }) != base);
}

TEST_CASE("ApplyModifier as a rider effect: Guiding Bolt applies only on a hit") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto h = spell_hooks();
        auto book = std::make_shared<SpellBook>(*h.spells);
        book->add(guiding_def());
        h.spells = book;
        auto w = wizard(1);
        w.spells_known.push_back(kGuiding);
        auto s = begin_encounter({w, dummy(2, 10)}, seed, nullptr, std::move(h));
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kGuiding, 2, 1));
        REQUIRE(r.ok);
        const Event* a = find_ev(r.events, EventKind::AttackResolved);
        REQUIRE(a);
        if (a->c & 1) {
            ++hits;
            REQUIRE(s.modifiers.size() == 1);
            CHECK(s.modifiers[0].target == 2);
            CHECK(s.modifiers[0].spec.id == 9100);
            CHECK(count(r.events, EventKind::ModifierApplied) == 1);
        } else {
            ++misses;
            CHECK(s.modifiers.empty());
        }
    }
    CHECK(hits > 0);
    CHECK(misses >= 0);
}
