// Row 9a: triggers on Hit / Crit with extra_damage riders (Sneak Attack, smite-style, Charge/Pounce),
// predicates, limits and the FeatureTriggered emit site. See docs/proposals/VERSION_BUMP_BATCH.md section 3.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSneak = spell_id_of("test.rogue#sneak_attack#0");
const uint32_t kPounce = spell_id_of("test.tiger#pounce#0");

Predicate pred(PredicateKind k, int a = 0, int b = 0, uint32_t mask = 0) {
    Predicate p; p.kind = k; p.a = int16_t(a); p.b = int16_t(b); p.mask = mask; return p;
}
RiderEffect extra(DiceExpr d, bool same_as_weapon = true, DamageType t = DamageType::Force) {
    RiderEffect e; e.kind = RiderKind::ExtraDamage; e.dice = d; e.same_as_weapon = same_as_weapon; e.type = t; return e;
}
Trigger sneak(TriggerLimit lim = TriggerLimit::OncePerTurn) {
    Trigger t; t.id = kSneak; t.on = TriggerOn::Hit; t.limit = lim;
    t.when = {pred(PredicateKind::Weapon, int(WeaponIs::FinesseOrRanged)), pred(PredicateKind::AdvantageOrAllyAdjacent)};
    t.effects = {extra({3, 6, 0})};
    return t;
}

// Rogue: finesse dagger that always hits (d4 damage), trigger list as given.
Combatant rogue(CombatantId id, std::vector<Trigger> trig) {
    auto c = pc(id, 50);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    c.triggers = std::move(trig);
    return c;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = 10;
    m.reaction_policy.fill(ReactionPolicy::Never);
    return m;
}
ActionRequest stab(CombatantId a, CombatantId t, bool ally = false) {
    ActionRequest r{a, ActionKind::Attack, t, 0, 0, false, true};
    r.ally_near_target = ally;
    return r;
}
const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
int dealt(const std::vector<Event>& ev) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt) n += e.a; return n;
}
}  // namespace

TEST_CASE("defaults: no triggers, nothing emitted, no state") {
    Combatant c{};
    CHECK(c.triggers.empty());
    auto s = begin_encounter({rogue(1, {}), dummy(2)}, 3, nullptr, spell_hooks());
    CHECK_FALSE(has_trigger_state(s));
    end_turns_until(s, 1);
    auto r = step(s, stab(1, 2, true));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::FeatureTriggered) == 0);
    CHECK(dealt(r.events) <= 8);
}

TEST_CASE("Sneak Attack: needs advantage or an ally; extra dice with the hit; event before the damage") {
    int extra_seen = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = begin_encounter({rogue(1, {sneak()}), dummy(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto plain = step(s, stab(1, 2, false));
        REQUIRE(plain.ok);
        CHECK(count(plain.events, EventKind::FeatureTriggered) == 0);   // no advantage, no ally
        CHECK(dealt(plain.events) <= 8);   // 1d4, or 2d4 on a crit
        auto s2 = begin_encounter({rogue(1, {sneak()}), dummy(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s2, 1);
        auto r = step(s2, stab(1, 2, true));
        REQUIRE(r.ok);
        if (!(first(r.events, EventKind::AttackResolved)->c & 1)) continue;   // natural 1
        const Event* ft = first(r.events, EventKind::FeatureTriggered);
        REQUIRE(ft);
        CHECK(ft->actor == 1); CHECK(ft->target == 2); CHECK(ft->a == int32_t(kSneak));
        CHECK(ft->b == int32_t(TriggerOn::Hit)); CHECK(ft->c == 1);
        auto idx = [&](EventKind k) { return std::find_if(r.events.begin(), r.events.end(), [&](const Event& e) { return e.kind == k; }) - r.events.begin(); };
        CHECK(idx(EventKind::AttackResolved) < idx(EventKind::FeatureTriggered));
        CHECK(idx(EventKind::FeatureTriggered) < idx(EventKind::DamageDealt));
        const bool crit = first(r.events, EventKind::AttackResolved)->c & 2;
        const int d = dealt(r.events);
        CHECK(d >= (crit ? 8 : 4));                 // 1d4 + 3d6 (doubled dice on a crit: 2d4 + 6d6)
        CHECK(d <= (crit ? 44 : 22));
        extra_seen += d > 4;
    }
    CHECK(extra_seen > 0);
}

TEST_CASE("Sneak Attack: advantage from the roll mode counts; disadvantage with an ally does not") {
    auto s = begin_encounter({rogue(1, {sneak()}), dummy(2)}, 5, nullptr, spell_hooks());
    mut(s, 2).conditions |= bit(Condition::Restrained);   // attackers have advantage against a restrained target
    end_turns_until(s, 1);
    CHECK(count(step(s, stab(1, 2, false)).events, EventKind::FeatureTriggered) == 1);
    auto s2 = begin_encounter({rogue(1, {sneak()}), dummy(2)}, 5, nullptr, spell_hooks());
    mut(s2, 1).conditions |= bit(Condition::Poisoned);    // poisoned attacker: disadvantage
    end_turns_until(s2, 1);
    CHECK(count(step(s2, stab(1, 2, true)).events, EventKind::FeatureTriggered) == 0);
}

TEST_CASE("limits: once per turn, once per round, none; next turn resets") {
    auto run = [&](TriggerLimit lim, int expect_second) {
        auto s = begin_encounter({rogue(1, {sneak(lim)}), dummy(2)}, 9, nullptr, spell_hooks());
        end_turns_until(s, 1);
        s.budget.attacks_remaining = 3;   // extra attacks this turn
        CHECK(count(step(s, stab(1, 2, true)).events, EventKind::FeatureTriggered) == 1);
        CHECK(count(step(s, stab(1, 2, true)).events, EventKind::FeatureTriggered) == expect_second);
        // A later turn of the owner: the limit is fresh again.
        end_turns_until(s, 2); end_turns_until(s, 1);
        CHECK(count(step(s, stab(1, 2, true)).events, EventKind::FeatureTriggered) == 1);
    };
    run(TriggerLimit::OncePerTurn, 0);
    run(TriggerLimit::OncePerRound, 0);
    run(TriggerLimit::OncePerAttack, 1);
    run(TriggerLimit::None, 1);
}

TEST_CASE("Crit trigger fires only on a critical hit; Hit trigger fires on both") {
    Trigger on_crit; on_crit.id = 77; on_crit.on = TriggerOn::Crit; on_crit.effects = {extra({2, 8, 0}, false, DamageType::Radiant)};
    int crits = 0, hits = 0;
    for (uint64_t seed = 0; seed < 300; ++seed) {
        auto s = begin_encounter({rogue(1, {on_crit}), dummy(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        const Event* ar = first(r.events, EventKind::AttackResolved);
        REQUIRE(ar);
        const bool crit = ar->c & 2;
        CHECK(count(r.events, EventKind::FeatureTriggered) == (crit ? 1 : 0));
        crits += crit; hits += (ar->c & 1);
    }
    CHECK(crits > 0);
    CHECK(hits > crits);
}

TEST_CASE("predicates: target creature type, not crit, damage type, self hp, extra damage type") {
    auto make = [&](std::vector<Predicate> when, DamageType extra_type = DamageType::Radiant, bool same = false) {
        Trigger t; t.id = 5; t.on = TriggerOn::Hit; t.when = std::move(when); t.effects = {extra({4, 8, 0}, same, extra_type)};
        return rogue(1, {t});
    };
    auto fires = [&](Combatant r, Combatant target) {
        auto s = begin_encounter({r, target}, 3, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto res = step(s, stab(1, 2));
        REQUIRE(res.ok);
        return count(res.events, EventKind::FeatureTriggered) == 1;
    };
    auto undead = dummy(2); undead.creature_type = CreatureType::Undead;
    CHECK(fires(make({pred(PredicateKind::TargetCreatureType, 0, 0, bit(CreatureType::Undead))}), undead));
    CHECK_FALSE(fires(make({pred(PredicateKind::TargetCreatureType, 0, 0, bit(CreatureType::Undead))}), dummy(2)));
    CHECK_FALSE(fires(make({pred(PredicateKind::NotCreatureType, 0, 0, bit(CreatureType::Undead))}), undead));
    CHECK(fires(make({pred(PredicateKind::NotCreatureType, 0, 0, bit(CreatureType::Undead))}), dummy(2)));
    CHECK(fires(make({pred(PredicateKind::DamageTypeIs, 0, 0, bit(DamageType::Piercing))}), dummy(2)));
    CHECK_FALSE(fires(make({pred(PredicateKind::DamageTypeIs, 0, 0, bit(DamageType::Slashing))}), dummy(2)));
    CHECK(fires(make({pred(PredicateKind::DamageTypeNot, 0, 0, bit(DamageType::Slashing))}), dummy(2)));
    CHECK_FALSE(fires(make({pred(PredicateKind::DamageTypeNot, 0, 0, bit(DamageType::Piercing))}), dummy(2)));
    auto hurt = make({pred(PredicateKind::SelfHpBelow, 1, 2)});   // below half
    CHECK_FALSE(fires(hurt, dummy(2)));
    hurt.hp = 20;                                                  // 20/50 < 1/2
    CHECK(fires(hurt, dummy(2)));
    // extra damage type: an immune target takes only the base damage; same_as_weapon follows the weapon.
    for (bool same : {false, true}) {
        auto imm = dummy(2); imm.immunities = bit(same ? DamageType::Piercing : DamageType::Radiant);
        auto s = begin_encounter({make({}, DamageType::Radiant, same), imm}, 3, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::FeatureTriggered) == 1);
        const int d = dealt(r.events);
        if (same) CHECK(d == 0);        // the whole hit is piercing: immune
        else { CHECK(d >= 1); CHECK(d <= 8); }   // d4 piercing or 2d4 on a crit; the radiant dice are immune
    }
}

TEST_CASE("Charge / Pounce: moved_at_least_ft uses moved_ft of the owner's own turn") {
    Trigger pounce; pounce.id = kPounce; pounce.on = TriggerOn::Hit; pounce.limit = TriggerLimit::OncePerTurn;
    pounce.when = {pred(PredicateKind::MovedAtLeastFt, 20)};
    pounce.effects = {extra({2, 6, 0})};
    auto walk = [&](int tiles) {
        auto s = begin_encounter({rogue(1, {pounce}), dummy(2)}, 4, nullptr, spell_hooks());
        s.combatants[0].speed_ft = 60;
        end_turns_until(s, 1);
        s.budget.movement_ft = 60;
        for (int i = 0; i < tiles; ++i) {
            ActionRequest m{1, ActionKind::Move}; m.move_ft = 5; m.move_tiles = 1;
            REQUIRE(step(s, m).ok);
        }
        return count(step(s, stab(1, 2)).events, EventKind::FeatureTriggered);
    };
    CHECK(walk(3) == 0);   // 15 ft
    CHECK(walk(4) == 1);   // 20 ft
    CHECK(walk(6) == 1);
    CHECK(walk(0) == 0);
}

TEST_CASE("weapon predicates do not apply to spell attacks (no Sneak Attack with Fire Bolt)") {
    auto r = rogue(1, {sneak()});
    r.spellcasting_ability = Ability::Int; r.caster_level = 5; r.proficiency_bonus = 3;
    r.spells_known = {kFireBolt};
    auto s = begin_encounter({r, dummy(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto req = cast(1, kFireBolt, 2); req.ally_near_target = true;
    auto res = step(s, req);
    REQUIRE(res.ok);
    CHECK(count(res.events, EventKind::FeatureTriggered) == 0);
}

TEST_CASE("reserved: other TriggerOn values, ask/never choice and non-9a effects do not fire yet") {
    auto base = [&] { Trigger t = sneak(TriggerLimit::None); t.when.clear(); return t; };
    auto fires = [&](Trigger t) {
        auto s = begin_encounter({rogue(1, {t}), dummy(2)}, 3, nullptr, spell_hooks());
        end_turns_until(s, 1);
        return count(step(s, stab(1, 2)).events, EventKind::FeatureTriggered);
    };
    CHECK(fires(base()) == 1);
    Trigger t = base(); t.on = TriggerOn::ReduceToZero; CHECK(fires(t) == 0);
    t = base(); t.on = TriggerOn::DropToZero; CHECK(fires(t) == 0);
    t = base(); t.choice = 1; CHECK(fires(t) == 0);
    t = base(); t.choice = 2; CHECK(fires(t) == 0);
    t = base(); t.effects[0].kind = RiderKind::SetHp; CHECK(fires(t) == 0);
}

TEST_CASE("RNG invariant: a rogue whose trigger never fires draws exactly as one without triggers") {
    auto run = [&](bool with) {
        auto s = begin_encounter({rogue(1, with ? std::vector<Trigger>{sneak()} : std::vector<Trigger>{}), dummy(2)}, 7, nullptr, spell_hooks());
        end_turns_until(s, 1);
        (void)step(s, stab(1, 2, false));   // no advantage / ally: the trigger does not fire
        return s.rng;
    };
    auto a = run(false), b = run(true);
    CHECK(a.d(1000) == b.d(1000));
}

TEST_CASE("digest: covers ids, limits and the used masks; empty without triggers") {
    EncounterState s;
    s.combatants = {rogue(1, {}), dummy(2)};
    CHECK_FALSE(has_trigger_state(s));
    s.combatants[0].triggers = {sneak()};
    CHECK(has_trigger_state(s));
    const uint64_t d0 = triggers_digest(s);
    s.combatants[0].trigger_turn_mask = 1;
    const uint64_t d1 = triggers_digest(s);
    CHECK(d1 != d0);
    s.combatants[0].triggers[0].limit = TriggerLimit::OncePerRound;
    CHECK(triggers_digest(s) != d1);
}
