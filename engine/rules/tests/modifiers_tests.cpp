// Row 12: timed modifiers (ModifierSpec / ActiveModifier / apply_modifier, ModifierApplied / ModifierExpired).
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBless = spell_id_of("test.spell#bless");
const uint32_t kBlast = spell_id_of("test.spell#blast");

Combatant striker(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    return c;
}
Combatant dummy(CombatantId id, int ac = 10) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = int16_t(ac);
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
ModifierSpec mod(uint32_t id, ModStat st) {
    ModifierSpec m; m.id = id; m.source = id; m.stat = st; m.applies_to = ModAppliesTo::Target; return m;
}
ModifierSpec flat(uint32_t id, ModStat st, int bonus) {
    ModifierSpec m = mod(id, st); m.bonus = int16_t(bonus); return m;
}
SpellDef bless_def() {
    SpellDef d; d.id = kBless; d.content_id = "test.spell#bless"; d.level = 1; d.range_ft = 30; d.concentration = true;
    d.components = kComponentV | kComponentS;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier = mod(9001, ModStat::AttackRoll);
    d.modifier.die = DiceExpr{1, 4, 0};
    return d;
}
SpellDef blast_def() {
    SpellDef d; d.id = kBlast; d.content_id = "test.spell#blast"; d.level = 0; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 15; d.on_save = OnSave::None;
    d.dice = DiceExpr{1, 6, 0}; d.damage_type = DamageType::Fire;
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(bless_def());
    book->add(blast_def());
    h.spells = book;
    return h;
}
ActionRequest stab(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
const Event* find_ev(const std::vector<Event>& ev, EventKind k, int nth = 0) {
    for (const auto& e : ev) if (e.kind == k && nth-- == 0) return &e;
    return nullptr;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed, EncounterHooks h = hooks()) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    return s;
}
// Same seed with and without `m` on combatant `on` (applied before the attack), returns both attack events.
struct Pair { std::vector<Event> base, with; };
Pair attack_pair(uint64_t seed, CombatantId on, const ModifierSpec& m, int def_ac = 10) {
    Pair p;
    auto a = start({striker(1), dummy(2, def_ac)}, seed);
    p.base = step(a, stab(1, 2)).events;
    auto b = start({striker(1), dummy(2, def_ac)}, seed);
    std::vector<Event> sink;
    apply_modifier(b, 2, on, m, sink);
    p.with = step(b, stab(1, 2)).events;
    return p;
}
}  // namespace

TEST_CASE("enum values and defaults") {
    CHECK(uint8_t(ModStat::AttackRoll) == 0);
    CHECK(uint8_t(ModStat::AttacksAgainst) == 8);
    CHECK(uint8_t(SpellEffectKind::ApplyModifier) > uint8_t(SpellEffectKind::Counter));
    ModifierSpec m;
    CHECK(m.die_mult == 1); CHECK(m.speed_num == 1); CHECK(m.speed_den == 1); CHECK_FALSE(m.stack);
    Combatant c{};
    CHECK(c.mod_resist == 0);
    auto s = start({striker(1), dummy(2)}, 1);
    CHECK(s.modifiers.empty());
    CHECK_FALSE(has_modifier_state(s));
}

TEST_CASE("flat attack_roll bonus changes the total by exactly the bonus and draws no extra rng") {
    for (uint64_t seed = 0; seed < 10; ++seed) {
        auto p = attack_pair(seed, 1, flat(1, ModStat::AttackRoll, 3));
        const Event* b = find_ev(p.base, EventKind::AttackResolved);
        const Event* w = find_ev(p.with, EventKind::AttackResolved);
        REQUIRE(b); REQUIRE(w);
        CHECK(w->a == b->a);
        CHECK(w->b == b->b + 3);
    }
}

TEST_CASE("Bless / Bane dice are rolled at the roll, in the stated direction") {
    bool varied = false; int first = -1;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        ModifierSpec bless = mod(2, ModStat::AttackRoll); bless.die = DiceExpr{1, 4, 0};
        auto p = attack_pair(seed, 1, bless);
        const int d = find_ev(p.with, EventKind::AttackResolved)->b - find_ev(p.base, EventKind::AttackResolved)->b;
        CHECK(d >= 1); CHECK(d <= 4);
        if (first < 0) first = d; else if (d != first) varied = true;
        ModifierSpec bane = bless; bane.die_mult = -1;
        auto q = attack_pair(seed, 1, bane);
        const int e = find_ev(q.base, EventKind::AttackResolved)->b - find_ev(q.with, EventKind::AttackResolved)->b;
        CHECK(e >= 1); CHECK(e <= 4);
    }
    CHECK(varied);
}

TEST_CASE("ac bonus turns hits into misses; modified_ac_bonus sums") {
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto p = attack_pair(seed, 2, flat(3, ModStat::Ac, 100));
        const Event* w = find_ev(p.with, EventKind::AttackResolved);
        REQUIRE(w);
        if (w->c & 1) CHECK(w->a == 20);   // only a natural 20 still lands
    }
    auto s = start({striker(1), dummy(2)}, 4);
    std::vector<Event> sink;
    apply_modifier(s, 1, 2, flat(3, ModStat::Ac, 2), sink);
    apply_modifier(s, 1, 2, flat(4, ModStat::Ac, 3), sink);
    CHECK(modified_ac_bonus(s, 2) == 5);
    CHECK(modified_ac_bonus(s, 1) == 0);
}

TEST_CASE("modes: attack_roll advantage, attacks_against advantage / disadvantage") {
    auto avg = [](ModStat st, RollMode mode, CombatantId on) {
        int sum = 0;
        for (uint64_t seed = 0; seed < 80; ++seed) {
            auto s = start({striker(1), dummy(2)}, seed);
            if (mode != RollMode::Normal) {
                ModifierSpec m = mod(5, st); m.mode = mode;
                std::vector<Event> sink; apply_modifier(s, 2, on, m, sink);
            }
            sum += find_ev(step(s, stab(1, 2)).events, EventKind::AttackResolved)->a;
        }
        return sum;
    };
    const int normal = avg(ModStat::AttackRoll, RollMode::Normal, 1);
    CHECK(avg(ModStat::AttackRoll, RollMode::Advantage, 1) > normal);
    CHECK(avg(ModStat::AttacksAgainst, RollMode::Advantage, 2) > normal);
    CHECK(avg(ModStat::AttacksAgainst, RollMode::Disadvantage, 2) < normal);
    CHECK(avg(ModStat::AttackRoll, RollMode::Disadvantage, 1) < normal);
}

TEST_CASE("damage modifiers: flat bonus, die, melee scope, weapon type") {
    int checked = 0;
    for (uint64_t seed = 0; seed < 40 && checked < 6; ++seed) {
        auto p = attack_pair(seed, 1, flat(6, ModStat::Damage, 2));
        const Event* b = find_ev(p.base, EventKind::AttackResolved);
        if (!(b->c & 1) || (b->c & 2)) continue;
        CHECK(dealt_to(p.with, 2) == dealt_to(p.base, 2) + 2);
        ++checked;
    }
    CHECK(checked > 0);
    // melee-only scope does not apply to a ranged weapon
    auto ranged = striker(1);
    ranged.attacks = {Attack{"bow", 40, {1, 4, 0}, DamageType::Piercing, 60, true, false}};
    int ok = 0;
    for (uint64_t seed = 0; seed < 40 && ok < 4; ++seed) {
        auto a = start({ranged, dummy(2)}, seed);
        auto r0 = step(a, stab(1, 2));
        auto b = start({ranged, dummy(2)}, seed);
        ModifierSpec m = flat(7, ModStat::Damage, 5); m.scope = 1;
        std::vector<Event> sink; apply_modifier(b, 2, 1, m, sink);
        auto r1 = step(b, stab(1, 2));
        CHECK(dealt_to(r1.events, 2) == dealt_to(r0.events, 2));
        ++ok;
    }
    // die: damage grows by 1..4 (non-crit hits)
    for (uint64_t seed = 0; seed < 40; ++seed) {
        ModifierSpec m = mod(8, ModStat::Damage); m.die = DiceExpr{1, 4, 0};
        auto p = attack_pair(seed, 1, m);
        const Event* b = find_ev(p.base, EventKind::AttackResolved);
        const Event* w = find_ev(p.with, EventKind::AttackResolved);
        if (!(b->c & 1) || (b->c & 2) || !(w->c & 1)) continue;
        const int d = dealt_to(p.with, 2) - dealt_to(p.base, 2);
        CHECK(d >= 1); CHECK(d <= 4);
        break;
    }
}

TEST_CASE("resistance modifier feeds effective resistances and clears on expiry") {
    auto s = start({striker(1), dummy(2)}, 3);
    ModifierSpec m = mod(10, ModStat::Resistance);
    m.damage_type = DamageType::Fire; m.has_damage_type = true; m.expiry = 1; m.rounds = 1;
    std::vector<Event> ev;
    apply_modifier(s, 1, 2, m, ev);
    CHECK((effective_resistances(s.combatants[1]) & bit(DamageType::Fire)) != 0);
    CHECK((effective_resistances(s.combatants[0]) & bit(DamageType::Fire)) == 0);
    for (int i = 0; i < 6 && !s.modifiers.empty(); ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        for (const auto& e : r.events) ev.push_back(e);
    }
    CHECK(s.modifiers.empty());
    CHECK((effective_resistances(s.combatants[1]) & bit(DamageType::Fire)) == 0);
    CHECK(count(ev, EventKind::ModifierExpired) == 1);
}

TEST_CASE("speed modifier scales the movement budget at the start of the target's turn") {
    auto s = start({striker(1), dummy(2)}, 5);
    ModifierSpec half = mod(11, ModStat::Speed); half.speed_num = 1; half.speed_den = 2;
    std::vector<Event> ev; apply_modifier(s, 1, 2, half, ev);
    CHECK(modified_speed(s, s.combatants[1]) == 15);
    CHECK(modified_speed(s, s.combatants[0]) == 30);
    ModifierSpec dbl = mod(12, ModStat::Speed); dbl.speed_num = 2; dbl.speed_den = 1;
    apply_modifier(s, 1, 1, dbl, ev);
    CHECK(modified_speed(s, s.combatants[0]) == 60);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 15);
}

TEST_CASE("save and ability-check modifiers, with ability scope") {
    // Save +100 always passes; scoped to Str it does not help a Dex save
    for (uint64_t seed = 0; seed < 6; ++seed) {
        auto caster = wizard(1); caster.spells_known.push_back(kBlast);
        auto s = start({caster, dummy(2)}, seed);
        ModifierSpec m = flat(13, ModStat::Save, 100); m.has_ability = true; m.ability = Ability::Dex;
        std::vector<Event> sink; apply_modifier(s, 2, 2, m, sink);
        auto r = step(s, cast(1, kBlast, 2));
        REQUIRE(r.ok);
        const Event* sv = find_ev(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv);
        CHECK((sv->c & 1) == 1);
        CHECK(dealt_to(r.events, 2) == 0);
        auto t = start({caster, dummy(2)}, seed);
        ModifierSpec wrong = flat(14, ModStat::Save, 100); wrong.has_ability = true; wrong.ability = Ability::Str;
        apply_modifier(t, 2, 2, wrong, sink);
        auto q = step(t, cast(1, kBlast, 2));
        const Event* sv2 = find_ev(q.events, EventKind::SavingThrowRolled);
        REQUIRE(sv2);
        CHECK(sv2->b < 50);
    }
    // check bonus
    for (uint64_t seed = 0; seed < 6; ++seed) {
        auto s = start({striker(1), dummy(2)}, seed);
        std::vector<Event> sink;
        auto base = step(s, check_request(2, Skill::Athletics, 40));
        auto t = start({striker(1), dummy(2)}, seed);
        apply_modifier(t, 1, 2, flat(15, ModStat::AbilityCheck, 50), sink);
        auto r = step(t, check_request(2, Skill::Athletics, 40));
        const Event* b = find_ev(base.events, EventKind::CheckRolled);
        const Event* w = find_ev(r.events, EventKind::CheckRolled);
        REQUIRE(b); REQUIRE(w);
        CHECK(w->b == b->b + 50);
        CHECK((w->c & 1) == 1);
    }
}

TEST_CASE("replace vs stack; temp_hp is instantaneous") {
    auto s = start({striker(1), dummy(2)}, 6);
    std::vector<Event> ev;
    apply_modifier(s, 1, 2, flat(20, ModStat::Ac, 2), ev);
    apply_modifier(s, 1, 2, flat(20, ModStat::Ac, 3), ev);
    CHECK(s.modifiers.size() == 1);
    CHECK(modified_ac_bonus(s, 2) == 3);
    const Event* x = find_ev(ev, EventKind::ModifierExpired);
    REQUIRE(x);
    CHECK(x->a == 20); CHECK(x->b == 2);
    ModifierSpec st = flat(20, ModStat::Ac, 1); st.stack = true;
    apply_modifier(s, 1, 2, st, ev);
    CHECK(s.modifiers.size() == 2);
    CHECK(modified_ac_bonus(s, 2) == 4);
    ModifierSpec th = flat(21, ModStat::TempHp, 8);
    ev.clear();
    apply_modifier(s, 1, 2, th, ev);
    CHECK(s.combatants[1].temp_hp == 8);
    CHECK(s.modifiers.size() == 2);
    apply_modifier(s, 1, 2, flat(21, ModStat::TempHp, 5), ev);
    CHECK(s.combatants[1].temp_hp == 8);
}

TEST_CASE("ModifierApplied payload") {
    auto s = start({striker(1), dummy(2)}, 7);
    std::vector<Event> ev;
    ModifierSpec d = mod(30, ModStat::AttackRoll); d.die = DiceExpr{1, 4, 0};
    apply_modifier(s, 1, 2, d, ev);
    apply_modifier(s, 1, 2, flat(31, ModStat::Ac, 2), ev);
    ModifierSpec sp = mod(32, ModStat::Speed); sp.speed_num = 1; sp.speed_den = 2;
    apply_modifier(s, 1, 2, sp, ev);
    REQUIRE(count(ev, EventKind::ModifierApplied) == 3);
    const Event* a0 = find_ev(ev, EventKind::ModifierApplied, 0);
    CHECK(a0->actor == 1); CHECK(a0->target == 2); CHECK(a0->a == 30);
    CHECK(a0->b == int(ModStat::AttackRoll)); CHECK(a0->c == 104);
    CHECK(find_ev(ev, EventKind::ModifierApplied, 1)->c == 2);
    CHECK(find_ev(ev, EventKind::ModifierApplied, 2)->c == 102);
}

TEST_CASE("expiry: rounds, end of the target's next turn, caster's next turn") {
    {
        auto s = start({striker(1), dummy(2)}, 8);
        ModifierSpec m = flat(40, ModStat::Ac, 1); m.expiry = 1; m.rounds = 2;
        std::vector<Event> ev; apply_modifier(s, 1, 2, m, ev);
        int rounds = 0;
        while (!s.modifiers.empty() && rounds < 12) {
            auto r = step(s, {current_actor(s), ActionKind::EndTurn});
            for (const auto& e : r.events) ev.push_back(e);
            ++rounds;
        }
        CHECK(s.modifiers.empty());
        const Event* x = find_ev(ev, EventKind::ModifierExpired);
        REQUIRE(x);
        CHECK(x->b == 0);
    }
    {
        auto s = start({striker(1), dummy(2)}, 9);   // combatant 1 acting
        ModifierSpec m = flat(41, ModStat::Ac, 1); m.expiry = 2;
        std::vector<Event> ev; apply_modifier(s, 1, 2, m, ev);
        auto r = step(s, {1, ActionKind::EndTurn});   // 2 starts its turn
        CHECK(s.modifiers.size() == 1);
        r = step(s, {2, ActionKind::EndTurn});
        CHECK(s.modifiers.empty());
        CHECK(count(r.events, EventKind::ModifierExpired) == 1);
    }
    {
        auto s = start({striker(1), dummy(2)}, 10);
        ModifierSpec m = flat(42, ModStat::Ac, 2); m.until_caster_turn = true;
        std::vector<Event> ev; apply_modifier(s, 1, 1, m, ev);
        (void)step(s, {1, ActionKind::EndTurn});
        CHECK(s.modifiers.size() == 1);
        auto r = step(s, {2, ActionKind::EndTurn});   // 1's turn starts
        CHECK(s.modifiers.empty());
        CHECK(count(r.events, EventKind::ModifierExpired) == 1);
    }
}

TEST_CASE("ApplyModifier spell: Bless, concentration, ends when concentration is replaced") {
    auto w = wizard(1);
    w.spells_known.push_back(kBless);
    auto ally = striker(3);
    auto s = start({w, dummy(2), ally}, 11);
    auto r = step(s, cast(1, kBless, 3, 1));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ModifierApplied) == 1);
    CHECK(s.modifiers.size() == 1);
    CHECK(s.modifiers[0].target == 3);
    CHECK(s.modifiers[0].caster == 1);
    CHECK(s.combatants[0].concentration_spell == kBless);
    CHECK(s.combatants[0].spell_slots[1] == 3);
    // casting Hold Person (concentration) at level 2 ends Bless
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    auto r2 = step(s, cast(1, kHoldPerson, 2, 2));
    INFO(std::string(r2.error));
    REQUIRE(r2.ok);
    CHECK(s.modifiers.empty());
    const Event* x = find_ev(r2.events, EventKind::ModifierExpired);
    REQUIRE(x);
    CHECK(x->a == 9001); CHECK(x->b == 1);
}

TEST_CASE("UseFeature applies hooks.feature_modifiers (Self, Target, AlliesWithin30)") {
    const uint32_t kRage = 777;
    auto h = hooks();
    h.feature_modifiers = [](const EncounterState&, const Combatant&, uint32_t f) {
        std::vector<ModifierSpec> v;
        if (f == kRage) {
            ModifierSpec m = flat(50, ModStat::Damage, 2); m.applies_to = ModAppliesTo::Self; m.scope = 1; v.push_back(m);
            ModifierSpec r = mod(51, ModStat::Resistance); r.damage_type = DamageType::Slashing; r.has_damage_type = true;
            r.applies_to = ModAppliesTo::Self; v.push_back(r);
        }
        if (f == 778) { ModifierSpec m = flat(52, ModStat::AbilityCheck, 3); m.applies_to = ModAppliesTo::Target; v.push_back(m); }
        return v;
    };
    auto s = start({striker(1), dummy(2), striker(3)}, 12, h);
    ActionRequest u{1, ActionKind::UseFeature};
    u.feature_id = kRage; u.as_bonus_action = true;
    auto r = step(s, u);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ModifierApplied) == 2);
    CHECK(s.modifiers.size() == 2);
    CHECK((effective_resistances(s.combatants[0]) & bit(DamageType::Slashing)) != 0);
    ActionRequest t{1, ActionKind::UseFeature};
    t.feature_id = 778; t.target = 3;
    auto r2 = step(s, t);
    REQUIRE(r2.ok);
    CHECK(s.modifiers.back().target == 3);
    CHECK(modifier_flat(s, 3, ModStat::AbilityCheck) == 3);
}

TEST_CASE("digest: stable, changes with state, folded only when non-empty") {
    auto s = start({striker(1), dummy(2)}, 13);
    CHECK_FALSE(has_modifier_state(s));
    std::vector<Event> ev;
    const uint64_t empty = modifiers_digest(s);
    apply_modifier(s, 1, 2, flat(60, ModStat::Ac, 2), ev);
    CHECK(has_modifier_state(s));
    const uint64_t one = modifiers_digest(s);
    CHECK(one != empty);
    CHECK(one == modifiers_digest(s));
    auto t = start({striker(1), dummy(2)}, 13);
    apply_modifier(t, 1, 2, flat(60, ModStat::Ac, 2), ev);
    CHECK(modifiers_digest(t) == one);
    apply_modifier(t, 1, 2, flat(60, ModStat::Ac, 3), ev);
    CHECK(modifiers_digest(t) != one);
}
