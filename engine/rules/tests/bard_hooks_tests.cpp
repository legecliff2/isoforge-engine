// VERSION_BUMP_BATCH 7.2za: bard hooks. Held die (Bardic Inspiration: grant, spend on attack / save / check, lapse, replace),
// subtract_roll applies (Cutting Words on checks and damage), half proficiency (Jack of All Trades), digest and replay.
#include <doctest/doctest.h>

#include <algorithm>
#include "rules5e/checks.hpp"
#include "rules5e/initiative.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kInspire = 7301;
const uint32_t kHeld = 7302;
const uint32_t kWords = 7303;
const uint32_t kPool = 7304;
const uint32_t kBlastB = spell_id_of("test.b#blast");

ModifierSpec held_row(uint8_t applies, uint16_t rounds = 100, DiceExpr die = {1, 6, 0}) {
    ModifierSpec m; m.id = kHeld; m.source = kInspire; m.stat = ModStat::HeldDie; m.applies_to = ModAppliesTo::Target;
    m.die = die; m.expiry = 1; m.rounds = rounds; m.held_applies = applies; m.range_ft = 60;
    return m;
}
EncounterHooks hk(uint8_t applies = kRollAttack | kRollSave | kRollCheck, uint16_t rounds = 100) {
    auto h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kBlastB; d.content_id = "test.b#blast"; d.level = 0; d.range_ft = 60; d.kind = SpellEffectKind::Save;
    d.save_ability = Ability::Wis; d.fixed_dc = 14; d.on_save = OnSave::None; d.dice = DiceExpr{1, 4, 0};
    d.damage_type = DamageType::Psychic; d.targets = 1;
    b->add(d);
    h.spells = b;
    h.feature_modifiers = [applies, rounds](const EncounterState&, const Combatant&, uint32_t f) {
        std::vector<ModifierSpec> v;
        if (f == kInspire) v.push_back(held_row(applies, rounds));
        return v;
    };
    h.feature_spec = [](const EncounterState&, const Combatant&, uint32_t f) -> std::optional<FeatureSpec> {
        if (f != kInspire) return std::nullopt;
        FeatureSpec fs; fs.cost = 2; fs.pool = kPool; fs.pool_cost = 1; return fs;
    };
    return h;
}
Combatant hero(CombatantId id, uint8_t team = 0) {
    auto c = pc(id, 200);
    c.team = team;
    c.proficiency_bonus = 3;
    c.attacks = {Attack{"sword", 3, {1, 8, 0}, DamageType::Slashing, 5, false, false}};
    c.reaction_policy.fill(ReactionPolicy::Auto);
    return c;
}
Combatant dummy(CombatantId id, int ac = 14) { auto m = monster(id); m.hp = m.hp_max = 5000; m.ac = int8_t(ac); return m; }
Combatant cutter(CombatantId id, uint8_t applies) {
    auto b = monster(id); b.hp = b.hp_max = 500;
    Reaction r; r.id = kWords; r.window = ReactionTrigger::AttackRolled; r.kind = ReactionEffectKind::SubtractRoll;
    r.dice = DiceExpr{1, 6, 0}; r.pool = kPool; r.pool_cost = 1; r.applies = applies;
    b.reactions = {r};
    b.reaction_policy.fill(ReactionPolicy::Auto);
    return b;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed, EncounterHooks h = hk()) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    for (const auto& c : s.combatants) register_pool(s, c.id, kPool, 5, 5);
    end_turns_until(s, 1);
    return s;
}
ActionRequest inspire(CombatantId target) {
    ActionRequest r{1, ActionKind::UseFeature, target}; r.feature_id = kInspire; r.as_bonus_action = true; return r;
}
ActionRequest check(CombatantId who, int dc) {
    ActionRequest r{who, ActionKind::AbilityCheck}; r.skill = Skill::Arcana; r.dc = int16_t(dc); return r;
}
ActionRequest swing(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
bool holds(const EncounterState& s, CombatantId id) {
    return std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& m) {
        return m.target == id && m.spec.stat == ModStat::HeldDie; });
}
struct Rolled { int natural = 0, total = 0; bool success = false, spent = false; };
Rolled first(const std::vector<Event>& ev, EventKind k) {
    Rolled o;
    for (const auto& e : ev) {
        if (e.kind == k && o.natural == 0) { o.natural = e.a; o.total = e.b; o.success = (e.c & 1) != 0; }
        if (e.kind == EventKind::ModifierExpired && e.a == int32_t(kHeld) && e.b == 9) o.spent = true;
    }
    return o;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}

}  // namespace

TEST_CASE("7.2za grant_die: bonus action, pool cost; an ally other than the user, in range, who can hear") {
    auto s = start({hero(1), hero(2), dummy(3)}, 1);
    CHECK_FALSE(step(s, inspire(1)).ok);   // self
    CHECK_FALSE(step(s, inspire(3)).ok);   // hostile
    CHECK_FALSE(step(s, inspire(0)).ok);   // no target
    auto deaf = hero(2); deaf.conditions |= bit(Condition::Deafened);
    auto d = start({hero(1), deaf, dummy(3)}, 1);
    CHECK_FALSE(step(d, inspire(2)).ok);
    auto far_h = hk(); far_h.targets_within = [](const EncounterState&, const std::vector<CombatantId>&, int ft) { return ft > 60; };
    auto f = start({hero(1), hero(2), dummy(3)}, 1, far_h);
    CHECK_FALSE(step(f, inspire(2)).ok);
    CHECK(find_pool(s, 1, kPool)->cur == 5);   // refused before any state change
    REQUIRE(step(s, inspire(2)).ok);
    CHECK(holds(s, 2));
    CHECK(find_pool(s, 1, kPool)->cur == 4);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);
}

TEST_CASE("7.2za held die on a check: Auto adds it only when it could turn a failure into a success; then it is gone") {
    int taken = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto a = start({hero(1), hero(2), dummy(3)}, seed), b = start({hero(1), hero(2), dummy(3)}, seed);
        REQUIRE(step(a, inspire(2)).ok); REQUIRE(step(b, inspire(2)).ok);
        b.modifiers.clear();
        const Rolled x = first(step(a, check(2, 14)).events, EventKind::CheckRolled);
        const Rolled y = first(step(b, check(2, 14)).events, EventKind::CheckRolled);
        CHECK(x.natural == y.natural);
        if (y.total < 14 && y.total + 6 >= 14) {
            ++taken;
            CHECK(x.spent);
            CHECK(x.total - y.total >= 1); CHECK(x.total - y.total <= 6);
            CHECK(x.success == (x.total >= 14));
            CHECK_FALSE(holds(a, 2));
            CHECK_FALSE(find(a, 2)->reaction_used);   // no reaction spent
        } else {
            CHECK(x.total == y.total); CHECK_FALSE(x.spent);
            CHECK(holds(a, 2));
        }
    }
    CHECK(taken > 0);
}

TEST_CASE("7.2za held die: applies_to filters the roll kind; attack and save rolls") {
    int atk = 0, sav = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto only_atk = start({hero(1), hero(2), dummy(3)}, seed, hk(kRollAttack));
        REQUIRE(step(only_atk, inspire(2)).ok);
        CHECK_FALSE(first(step(only_atk, check(2, 14)).events, EventKind::CheckRolled).spent);
        CHECK(holds(only_atk, 2));
        // attack: the holder's own attack roll on its turn
        auto a = start({hero(1), dummy(3, 15)}, seed, hk(kRollAttack));
        auto b = a;
        std::vector<Event> sink;
        REQUIRE(apply_modifier(a, 2, 1, held_row(kRollAttack), sink));
        const Rolled x = first(step(a, swing(1, 3)).events, EventKind::AttackResolved);
        const Rolled y = first(step(b, swing(1, 3)).events, EventKind::AttackResolved);
        if (y.natural != 1 && y.natural != 20 && y.total < 15 && y.total + 6 >= 15) {
            ++atk; CHECK(x.spent); CHECK(x.total > y.total); CHECK(x.success == (x.total >= 15));
        } else CHECK(x.total == y.total);
        // save: an enemy's Wis save spell against the holder
        auto w = wizard(1); w.team = 1; w.spells_known = {kBlastB};
        auto c = start({w, hero(2)}, seed, hk(kRollSave));
        auto e = c;
        REQUIRE(apply_modifier(c, 2, 2, held_row(kRollSave), sink));
        const Rolled p = first(step(c, cast(1, kBlastB, 2, 0)).events, EventKind::SavingThrowRolled);
        const Rolled q = first(step(e, cast(1, kBlastB, 2, 0)).events, EventKind::SavingThrowRolled);
        REQUIRE(q.natural > 0);
        if (q.total < 14 && q.total + 6 >= 14) { ++sav; CHECK(p.spent); CHECK(p.total > q.total); CHECK(p.success == (p.total >= 14)); }
        else CHECK(p.total == q.total);
    }
    CHECK(atk > 0); CHECK(sav > 0);
}

TEST_CASE("7.2za held die: a new grant replaces the old one; it lapses at expiry; Never and Ask") {
    auto s = start({hero(1), hero(2), dummy(3)}, 3, hk(kRollCheck, 1));
    std::vector<Event> sink;
    ModifierSpec other = held_row(kRollCheck, 50); other.id = kHeld + 50;   // another bard's die
    REQUIRE(apply_modifier(s, 3, 2, other, sink));
    REQUIRE(step(s, inspire(2)).ok);
    CHECK(std::count_if(s.modifiers.begin(), s.modifiers.end(), [](const ActiveModifier& m) {
        return m.target == 2 && m.spec.stat == ModStat::HeldDie; }) == 1);
    CHECK(s.modifiers.back().spec.id == kHeld);
    for (int i = 0; i < 6 && holds(s, 2); ++i) step(s, ActionRequest{current_actor(s), ActionKind::EndTurn});
    CHECK_FALSE(holds(s, 2));   // n = 1 round
    bool paused = false;
    for (uint64_t seed = 0; seed < 40 && !paused; ++seed) {
        auto n = hero(2); n.reaction_policy.fill(ReactionPolicy::Never);
        auto t = start({hero(1), n, dummy(3)}, seed, hk(kRollCheck));
        REQUIRE(step(t, inspire(2)).ok);
        CHECK_FALSE(first(step(t, check(2, 30)).events, EventKind::CheckRolled).spent);
        auto k = hero(2); k.reaction_policy.fill(ReactionPolicy::Ask);
        auto u = start({hero(1), k, dummy(3)}, seed, hk(kRollCheck));
        REQUIRE(step(u, inspire(2)).ok);
        const auto r = step(u, check(2, 30));
        REQUIRE(r.ok);
        if (r.status != StepStatus::NeedsDecision) continue;
        paused = true;
        REQUIRE(r.needs);
        CHECK(r.needs->window.trigger == ReactionTrigger::AttackRolled);
        CHECK(r.needs->window.roll_kind == kRollCheck);
        REQUIRE(r.needs->options.size() == 1);
        CHECK(r.needs->options[0].kind == ReactionOptionKind::AddHeldDie);
        CHECK_FALSE(r.needs->options[0].uses_reaction);
        REQUIRE(step(u, decision_request(*r.needs, true, 0)).ok);
        CHECK_FALSE(holds(u, 2));
    }
    CHECK(paused);
}

TEST_CASE("7.2za subtract_roll applies: Cutting Words on ability checks and damage rolls; absent = attack only") {
    int chk = 0, dmg = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto a = start({hero(1), cutter(2, kRollCheck | kRollDamage)}, seed), b = start({hero(1), cutter(2, 0)}, seed);
        const Rolled x = first(step(a, check(1, 12)).events, EventKind::CheckRolled);
        const Rolled y = first(step(b, check(1, 12)).events, EventKind::CheckRolled);
        CHECK(x.natural == y.natural);
        if (y.total >= 12 && y.total - 6 < 12) {
            ++chk; CHECK(x.total < y.total); CHECK(find_pool(a, 2, kPool)->cur == 4); CHECK(find(a, 2)->reaction_used);
        } else CHECK(x.total == y.total);
        CHECK(find_pool(b, 2, kPool)->cur == 5);   // attack-only: not offered on a check
        // damage: Auto always takes it on a damage roll; a check-only Cutting Words stays out of it
        auto c = start({hero(1), cutter(2, kRollDamage)}, seed), e = start({hero(1), cutter(2, kRollCheck)}, seed);
        for (auto* st : {&c, &e}) for (auto& m : st->combatants) if (m.id == 2) m.ac = 1;
        const int dc_ = dealt_to(step(c, swing(1, 2)).events, 2), de = dealt_to(step(e, swing(1, 2)).events, 2);
        if (de > 0) { CHECK(dc_ <= de); if (dc_ < de) ++dmg; CHECK(find_pool(c, 2, kPool)->cur == 4); }
        CHECK(find_pool(e, 2, kPool)->cur == 5);
    }
    CHECK(chk > 0); CHECK(dmg > 0);
}

TEST_CASE("7.2za half proficiency: checks without proficiency (rounded down) and initiative") {
    auto c = hero(1);
    c.skill_proficiencies = bit(Skill::Athletics);
    const int prof = skill_modifier(c, Skill::Athletics), plain = skill_modifier(c, Skill::Arcana);
    c.half_proficiency = 1;
    CHECK(skill_modifier(c, Skill::Athletics) == prof);        // proficient: unchanged
    CHECK(skill_modifier(c, Skill::Arcana) == plain + 1);      // 3 / 2 = 1
    for (uint64_t seed = 0; seed < 10; ++seed) {
        auto a = hero(1), b = hero(1); b.half_proficiency = 2;
        rng::Rng r1(seed, 7), r2(seed, 7);
        const auto i1 = roll_initiative(r1, std::vector<Combatant>{a}), i2 = roll_initiative(r2, std::vector<Combatant>{b});
        CHECK(i2[0].total == i1[0].total + 1);
        CHECK(i2[0].dex_mod == i1[0].dex_mod);
    }
}

TEST_CASE("7.2za digest folds the held die; replay is deterministic") {
    auto s = start({hero(1), hero(2), dummy(3)}, 4);
    const uint64_t d0 = modifiers_digest(s);
    REQUIRE(step(s, inspire(2)).ok);
    CHECK(modifiers_digest(s) != d0);
    auto once = [](uint64_t seed) {
        auto s2 = start({hero(1), hero(2), dummy(3)}, seed);
        std::vector<Event> all;
        for (const auto& q : {inspire(2), check(2, 14), check(2, 14)}) { auto r = step(s2, q); all.insert(all.end(), r.events.begin(), r.events.end()); }
        return std::make_pair(all.size(), modifiers_digest(s2) ^ uint64_t(s2.next_window_seq));
    };
    for (uint64_t seed = 0; seed < 5; ++seed) CHECK(once(seed) == once(seed));
}
