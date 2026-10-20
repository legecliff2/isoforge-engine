// VERSION_BUMP_BATCH 7.2u: next-roll spell riders (SRD 5.1): Guiding Bolt, Vicious Mockery, Ray of Frost, Shocking Grasp,
// Chill Touch. New: Expiry::EndOfCasterNextTurn / StartOfTargetNextTurn (ModifierSpec::expiry 3 / 4), the NoReactions and
// NoHealing markers, and the SubjectIsCaster / OwnerCreatureType predicates.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

Combatant striker(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"dagger", 4, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    return c;
}
Combatant dummy(CombatantId id, int ac = 14) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = int8_t(ac);
    m.attacks = {Attack{"claw", 4, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
ModifierSpec mod(uint32_t id, ModStat st) {
    ModifierSpec m; m.id = id; m.source = id; m.stat = st; m.applies_to = ModAppliesTo::Target; return m;
}
Predicate pred(PredicateKind k, uint32_t mask = 0) { Predicate p; p.kind = k; p.mask = mask; return p; }
ActionRequest hit(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
bool has_mod(const EncounterState& s, uint32_t id) {
    return std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& m) { return m.spec.id == id; });
}
const Event* find_ev(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
EncounterHooks hooks_in_range() {
    EncounterHooks h = spell_hooks();
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&, std::span<const ReactionOption>) {
        return ReactionDecision{0, true, 0}; };
    return h;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 4, EncounterHooks h = spell_hooks()) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    return s;
}
void apply(EncounterState& s, CombatantId caster, CombatantId target, const ModifierSpec& m) {
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, caster, target, m, sink));
}
void end_turn(EncounterState& s) { REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok); }

ModifierSpec guiding() {   // advantage on the next attack against the target, before the end of the caster's next turn
    ModifierSpec m = mod(9100, ModStat::AttacksAgainst); m.mode = RollMode::Advantage; m.consume = 2; m.expiry = 3; return m;
}

}  // namespace

TEST_CASE("Guiding Bolt: lasts through the caster's next turn start and ends at the end of the caster's next turn") {
    auto s = start({striker(1), dummy(2)});
    apply(s, 1, 2, guiding());
    CHECK(s.modifiers[0].timed.kind == Expiry::EndOfCasterNextTurn);
    end_turn(s);                       // the caster's current turn does not count
    CHECK(has_mod(s, 9100));
    REQUIRE(current_actor(s) == 2);
    end_turn(s);                       // the target's turn: no attack against it
    REQUIRE(current_actor(s) == 1);
    CHECK(has_mod(s, 9100));           // was dropped here with until_start_of_caster_turn
    auto r = step(s, {1, ActionKind::EndTurn});
    REQUIRE(r.ok);
    CHECK_FALSE(has_mod(s, 9100));
    const Event* x = find_ev(r.events, EventKind::ModifierExpired);
    REQUIRE(x); CHECK(x->b == 0);
}

TEST_CASE("Guiding Bolt: spent by the first attack roll against the target (reason 3)") {
    auto s = start({striker(1), dummy(2)});
    apply(s, 1, 2, guiding());
    auto r = step(s, hit(1, 2));
    REQUIRE(r.ok);
    CHECK_FALSE(has_mod(s, 9100));
    const Event* x = find_ev(r.events, EventKind::ModifierExpired);
    REQUIRE(x); CHECK(x->b == 3);
}

TEST_CASE("Guiding Bolt applied outside the caster's turn ends at the end of the caster's next (current) turn") {
    auto s = start({striker(1), dummy(2)});
    end_turn(s);
    REQUIRE(current_actor(s) == 2);
    apply(s, 1, 2, guiding());         // e.g. a reaction-time application
    end_turn(s);
    CHECK(has_mod(s, 9100));
    end_turn(s);                       // the caster's next turn ends
    CHECK_FALSE(has_mod(s, 9100));
}

TEST_CASE("Vicious Mockery: disadvantage on the target's next attack roll, spent by it, else gone at the end of its next turn") {
    ModifierSpec m = mod(9200, ModStat::AttackRoll); m.mode = RollMode::Disadvantage; m.consume = 1; m.expiry = 2;
    {
        auto s = start({striker(1), dummy(2)});
        apply(s, 1, 2, m);
        end_turn(s);
        CHECK(has_mod(s, 9200));
        auto r = step(s, hit(2, 1));
        REQUIRE(r.ok);
        CHECK_FALSE(has_mod(s, 9200));
        const Event* x = find_ev(r.events, EventKind::ModifierExpired);
        REQUIRE(x); CHECK(x->b == 3);
    }
    {
        auto s = start({striker(1), dummy(2)});
        apply(s, 1, 2, m);
        end_turn(s);
        end_turn(s);                   // no attack: the end of its next turn
        CHECK_FALSE(has_mod(s, 9200));
    }
    int with = 0, without = 0;         // disadvantage lowers the hit rate
    for (uint64_t seed = 0; seed < 300; ++seed) {
        for (bool on : {true, false}) {
            auto s = start({striker(1), dummy(2)}, seed);
            if (on) apply(s, 1, 2, m);
            end_turn(s);
            auto r = step(s, hit(2, 1));
            REQUIRE(r.ok);
            (on ? with : without) += int(find_ev(r.events, EventKind::AttackResolved)->c & 1);
        }
    }
    CHECK(with + 30 < without);
}

TEST_CASE("Ray of Frost: -10 ft speed in the target's movement budget until the start of the caster's next turn") {
    ModifierSpec m = mod(9300, ModStat::Speed); m.speed_sub = 10; m.until_caster_turn = true;
    auto s = start({striker(1), dummy(2)});
    apply(s, 1, 2, m);
    end_turn(s);
    REQUIRE(current_actor(s) == 2);
    CHECK(s.budget.movement_ft == 20);   // speed 30
    end_turn(s);
    CHECK_FALSE(has_mod(s, 9300));
    end_turn(s);
    REQUIRE(current_actor(s) == 2);
    CHECK(s.budget.movement_ft == 30);
}

TEST_CASE("Shocking Grasp: no reactions until the start of the target's next turn") {
    ModifierSpec m = mod(9400, ModStat::NoReactions); m.expiry = 4;
    auto run = [](bool marked) {
        auto c1 = striker(1);
        auto s = start({c1, dummy(2)}, 4, hooks_in_range());
        if (marked) { ModifierSpec q = mod(9400, ModStat::NoReactions); q.expiry = 4; apply(s, 1, 2, q); }
        ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5; mv.leaves_reach = true;
        auto r = step(s, mv);
        REQUIRE(r.ok);
        return std::pair<int, bool>{count(r.events, EventKind::ReactionWindowOpened), find(s, 2)->reaction_used};
    };
    CHECK(run(false).first == 1);
    CHECK(run(true).first == 0);
    CHECK_FALSE(run(true).second);
    auto s = start({striker(1), dummy(2)});
    apply(s, 1, 2, m);
    CHECK(s.modifiers[0].timed.kind == Expiry::StartOfTargetNextTurn);
    auto r = step(s, {1, ActionKind::EndTurn});   // the target's turn starts: the marker goes
    REQUIRE(r.ok);
    CHECK_FALSE(has_mod(s, 9400));
}

TEST_CASE("Chill Touch: no hit point regain on any path until the start of the caster's next turn") {
    ModifierSpec m = mod(9500, ModStat::NoHealing); m.until_caster_turn = true;
    auto s = start({striker(1), dummy(2)});
    mut(s, 2).hp = 100;
    mut(s, 2).regeneration.amount = 10;
    apply(s, 1, 2, m);
    ActionRequest heal{0, ActionKind::Heal, 2}; heal.amount = 20;
    auto r = step(s, heal);
    REQUIRE(r.ok);
    CHECK(find(s, 2)->hp == 100);
    const Event* h = find_ev(r.events, EventKind::Healed);
    REQUIRE(h); CHECK(h->a == 0);
    end_turn(s);                       // the target's turn start: regeneration blocked too
    CHECK(find(s, 2)->hp == 100);
    end_turn(s);                       // the caster's turn starts: the marker goes
    CHECK_FALSE(has_mod(s, 9500));
    auto r2 = step(s, heal);
    REQUIRE(r2.ok);
    CHECK(find(s, 2)->hp == 120);
}

TEST_CASE("Chill Touch: a dying creature's natural 20 death save regains no hit point while marked") {
    for (uint64_t seed = 0; seed < 400; ++seed) {
        auto s = start({striker(1), dummy(2), striker(3)}, seed);
        make_dying(mut(s, 3));
        ModifierSpec m = mod(9500, ModStat::NoHealing); m.expiry = 1; m.rounds = 5;
        apply(s, 1, 3, m);
        while (current_actor(s) != 3) end_turn(s);
        CHECK(find(s, 3)->hp == 0);
    }
}

TEST_CASE("Chill Touch: an undead target has disadvantage on attacks against the caster only") {
    ModifierSpec m = mod(9600, ModStat::AttackRoll); m.mode = RollMode::Disadvantage; m.expiry = 3;
    m.when = {pred(PredicateKind::OwnerCreatureType, bit(CreatureType::Undead)), pred(PredicateKind::SubjectIsCaster)};
    auto run = [&](uint64_t seed, bool on, bool undead, CombatantId victim) {
        auto d = dummy(2); if (undead) d.creature_type = CreatureType::Undead;
        auto s = start({striker(1), d, striker(3)}, seed);
        if (on) apply(s, 1, 2, m);
        while (current_actor(s) != 2) end_turn(s);
        auto r = step(s, hit(2, victim));
        REQUIRE(r.ok);
        return *find_ev(r.events, EventKind::AttackResolved);
    };
    int with = 0, without = 0;
    for (uint64_t seed = 0; seed < 300; ++seed) {
        with += int(run(seed, true, true, 1).c & 1);
        without += int(run(seed, false, true, 1).c & 1);
        const Event other = run(seed, true, true, 3), base3 = run(seed, false, true, 3);   // another creature: no effect
        CHECK(other.a == base3.a); CHECK(other.b == base3.b);
        const Event living = run(seed, true, false, 1), base1 = run(seed, false, false, 1);   // not undead: no effect
        CHECK(living.a == base1.a); CHECK(living.b == base1.b);
    }
    CHECK(with + 30 < without);
}

TEST_CASE("spell riders: replay recomputes the same state (cast-based)") {
    const uint32_t kRay = spell_id_of("test.ray_of_frost"), kChill = spell_id_of("test.chill_touch");
    auto make = [&] {
        EncounterHooks h = spell_hooks();
        auto b = std::make_shared<SpellBook>(*h.spells);
        SpellDef ray; ray.id = kRay; ray.content_id = "test.ray_of_frost"; ray.level = 0; ray.range_ft = 60;
        ray.components = kComponentV; ray.kind = SpellEffectKind::SpellAttack; ray.dice = {1, 8, 0}; ray.damage_type = DamageType::Cold;
        SpellEffect fx; fx.kind = SpellEffectKind::ApplyModifier; fx.affected_only = true;
        fx.modifier = mod(9300, ModStat::Speed); fx.modifier.speed_sub = 10; fx.modifier.until_caster_turn = true;
        ray.extra_effects.push_back(fx);
        REQUIRE(b->add(ray));
        SpellDef ch = ray; ch.id = kChill; ch.content_id = "test.chill_touch"; ch.damage_type = DamageType::Necrotic;
        ch.extra_effects.clear();
        SpellEffect nh; nh.kind = SpellEffectKind::ApplyModifier; nh.affected_only = true;
        nh.modifier = mod(9500, ModStat::NoHealing); nh.modifier.until_caster_turn = true;
        SpellEffect dis; dis.kind = SpellEffectKind::ApplyModifier; dis.affected_only = false;
        dis.modifier = mod(9600, ModStat::AttackRoll); dis.modifier.mode = RollMode::Disadvantage; dis.modifier.expiry = 3;
        dis.modifier.when = {pred(PredicateKind::OwnerCreatureType, bit(CreatureType::Undead)), pred(PredicateKind::SubjectIsCaster)};
        ch.extra_effects = {nh, dis};
        REQUIRE(b->add(ch));
        h.spells = b;
        auto w = wizard(1); w.spells_known.push_back(kRay); w.spells_known.push_back(kChill);
        auto d = dummy(2, 8); d.creature_type = CreatureType::Undead;
        auto s = begin_encounter({w, d}, 13, nullptr, std::move(h));
        end_turns_until(s, 1);
        return s;
    };
    auto a = make(), b = make();
    ActionLog log;
    REQUIRE(step_logged(a, cast(1, kChill, 2), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    REQUIRE(step_logged(a, hit(2, 1), log).ok);
    REQUIRE(step_logged(a, {2, ActionKind::EndTurn}, log).ok);
    REQUIRE(step_logged(a, cast(1, kRay, 2), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    for (const auto& ls : log) { auto r = replay_step(b, ls); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    CHECK(modifiers_digest(a) == modifiers_digest(b));
    for (std::size_t i = 0; i < a.combatants.size(); ++i) CHECK(a.combatants[i].hp == b.combatants[i].hp);
    CHECK(a.budget.movement_ft == b.budget.movement_ft);
}
