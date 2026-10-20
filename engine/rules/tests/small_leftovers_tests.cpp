// Small leftovers: Haste and Slow, Life Drain, Reckless Attack, Aggressive, Petrifying Gaze progression.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "rules5e/rest.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSlow = spell_id_of("test.spell#slow");
const uint32_t kGaze = spell_id_of("test.spell#gaze");
const uint32_t kStare = spell_id_of("test.spell#stare");   // gaze with fail-by-5 petrification

ModifierSpec marker(uint32_t id, ModStat st, uint32_t source) {
    ModifierSpec m; m.id = id; m.source = source; m.stat = st; m.applies_to = ModAppliesTo::Target; return m;
}
ModifierSpec flat(uint32_t id, ModStat st, int bonus, uint32_t source) {
    ModifierSpec m = marker(id, st, source); m.bonus = int16_t(bonus); return m;
}
Combatant fighter(CombatantId id, int attacks = 1) {
    auto c = pc(id, 60);
    c.attacks = {Attack{"axe", 40, {1, 6, 2}, DamageType::Slashing, 5, false, false},
                 Attack{"bow", 40, {1, 6, 2}, DamageType::Piercing, 150, true, false}};
    c.attacks_per_action = uint8_t(attacks);
    c.bonus_action_grants = bit(ActionKind::Dash);
    return c;
}
Combatant dummy(CombatantId id, int hp = 300) {
    auto m = monster(id);
    m.hp = m.hp_max = int16_t(hp);
    m.attacks = {Attack{"claw", 40, {1, 6, 2}, DamageType::Slashing, 5, false, false}};
    return m;
}
ActionRequest atk(CombatantId a, CombatantId t, uint8_t index = 0) { return ActionRequest{a, ActionKind::Attack, t, index}; }
ActionRequest haste(ActionRequest r) { r.haste_action = true; return r; }
const Event* find_ev(const std::vector<Event>& ev, EventKind k, int nth = 0) {
    for (const auto& e : ev) if (e.kind == k && nth-- == 0) return &e;
    return nullptr;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef sl; sl.id = kSlow; sl.content_id = "test.spell#slow"; sl.level = 3; sl.range_ft = 120; sl.concentration = true;
    sl.kind = SpellEffectKind::Save; sl.save_ability = Ability::Wis; sl.fixed_dc = 30;
    SpellEffect m1; m1.kind = SpellEffectKind::ApplyModifier; m1.affected_only = true;
    m1.modifier = marker(7001, ModStat::Slow, kSlow); m1.modifier.concentration = true;
    m1.modifier.repeat_save = 1; m1.modifier.repeat_ability = Ability::Wis;
    SpellEffect m2 = m1; m2.modifier = flat(7002, ModStat::Ac, -2, kSlow); m2.modifier.concentration = true;
    sl.extra_effects = {m1, m2};
    book->add(sl);
    SpellDef g; g.id = kGaze; g.content_id = "test.spell#gaze"; g.level = 0; g.range_ft = 30;
    g.kind = SpellEffectKind::Save; g.save_ability = Ability::Con; g.fixed_dc = 30; g.applies_condition = true;
    g.condition = Condition::Restrained; g.repeat_save = RepeatSave::EndOfTurn; g.escalates = true;
    g.escalate_to = Condition::Petrified;
    book->add(g);
    SpellDef st = g; st.id = kStare; st.content_id = "test.spell#stare"; st.escalate_margin = 5;
    book->add(st);
    h.spells = book;
    return h;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 1, CombatantId first = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, first);
    return s;
}
// One full round: end turns until `id` acts again.
void next_turn_of(EncounterState& s, CombatantId id) {
    (void)step(s, {current_actor(s), ActionKind::EndTurn});
    end_turns_until(s, id);
}

}  // namespace

TEST_CASE("defaults: nothing new is in force") {
    Combatant c = pc(1);
    CHECK(c.hp_max_reduction == 0); CHECK(c.lethargy == 0);
    CHECK_FALSE(c.reckless_attack); CHECK_FALSE(c.reckless_active); CHECK_FALSE(c.aggressive);
    ModifierSpec m; CHECK(m.repeat_save == 0); CHECK(m.repeat_dc == 0);
    TurnBudget b; CHECK_FALSE(b.haste_action); CHECK_FALSE(b.slow_attacked); CHECK_FALSE(b.aggressive_dash);
    ActionRequest r{1, ActionKind::Attack};
    CHECK_FALSE(r.haste_action); CHECK_FALSE(r.reckless); CHECK_FALSE(r.aggressive);
    Attack a{"x", 0, {1, 4, 0}, DamageType::Slashing, 5, false, false};
    CHECK_FALSE(a.drain_max_hp); CHECK(a.drain_save_dc == 0);
    SpellDef d; CHECK_FALSE(d.escalates); CHECK(d.escalate_margin == 0);
    auto s = start({fighter(1), dummy(2)});
    CHECK_FALSE(has_leftover_state(s));
    CHECK_FALSE(s.budget.haste_action);
}

TEST_CASE("Haste: the extra action from the next turn on, beside the action") {
    auto s = start({fighter(1), dummy(2)});
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste, 7100), sink));
    CHECK_FALSE(s.budget.haste_action);   // granted when a turn starts
    CHECK_FALSE(step(s, haste(atk(1, 2))).ok);
    next_turn_of(s, 1);
    REQUIRE(s.budget.haste_action);
    auto r = step(s, haste(atk(1, 2)));
    REQUIRE(r.ok);
    CHECK(find_ev(r.events, EventKind::AttackResolved));
    CHECK(s.budget.action);                // the creature's own action is untouched
    CHECK_FALSE(s.budget.haste_action);
    CHECK(s.budget.attacks_remaining == 0);
    auto again = step(s, haste({1, ActionKind::Dash}));
    CHECK_FALSE(again.ok);
    CHECK(again.error == "no Haste action");
    CHECK(step(s, atk(1, 2)).ok);          // then the Attack action
    CHECK_FALSE(s.budget.action);
}

TEST_CASE("Haste: only one weapon attack, Dash, Disengage or Hide") {
    auto s = start({fighter(1, 2), dummy(2)});
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste, 7100), sink));
    next_turn_of(s, 1);
    REQUIRE(s.budget.haste_action);
    for (ActionKind k : {ActionKind::Dodge, ActionKind::Help, ActionKind::Ready}) {
        auto r = step(s, haste({1, k}));
        CHECK_FALSE(r.ok);
        CHECK(std::string(r.error).find("haste_action") != std::string::npos);
    }
    ActionRequest grapple = haste(atk(1, 2)); grapple.special = 1;
    CHECK_FALSE(step(s, grapple).ok);
    ActionRequest bonus = haste({1, ActionKind::Dash}); bonus.as_bonus_action = true;
    CHECK_FALSE(step(s, bonus).ok);
    // Extra Attack: the Attack action's second attack survives a Haste attack made in between
    REQUIRE(step(s, atk(1, 2)).ok);
    REQUIRE(s.budget.attacks_remaining == 1);
    REQUIRE(step(s, haste(atk(1, 2))).ok);
    CHECK(s.budget.attacks_remaining == 1);
    CHECK(step(s, atk(1, 2)).ok);
    CHECK(s.budget.attacks_remaining == 0);
    CHECK_FALSE(step(s, atk(1, 2)).ok);
}

TEST_CASE("Haste: Dash with the extra action adds movement; legal_actions lists the options") {
    auto s = start({fighter(1), dummy(2)});
    std::vector<Event> sink;
    ModifierSpec speed = marker(7101, ModStat::Speed, 7100); speed.speed_num = 2;
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste, 7100), sink));
    REQUIRE(apply_modifier(s, 1, 1, speed, sink));
    next_turn_of(s, 1);
    CHECK(s.budget.movement_ft == 60);     // doubled speed (existing Speed modifier)
    const auto legal = legal_actions(s);
    CHECK(std::count_if(legal.begin(), legal.end(), [](const ActionRequest& r) { return r.haste_action; }) == 2 + 3);
    REQUIRE(step(s, haste({1, ActionKind::Dash})).ok);
    CHECK(s.budget.movement_ft == 120);   // 7.2u: Dash adds the hasted (doubled) speed, SRD Haste; was 90 (unmodified speed)
    CHECK(s.budget.action);
}

TEST_CASE("Haste ends: lethargy for the next turn, then normal") {
    auto s = start({fighter(1), dummy(2)});
    std::vector<Event> sink;
    ModifierSpec h = marker(7100, ModStat::Haste, 7100); h.until_caster_turn = true;   // ends when 2's turn starts
    REQUIRE(apply_modifier(s, 2, 1, h, sink));
    (void)step(s, {1, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 2);
    CHECK(s.modifiers.empty());
    CHECK(mut(s, 1).lethargy == 1);
    CHECK(has_leftover_state(s));
    (void)step(s, {2, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 1);
    CHECK(mut(s, 1).lethargy == 2);
    CHECK_FALSE(s.budget.action); CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.movement_ft == 0);
    CHECK_FALSE(s.budget.haste_action);
    CHECK_FALSE(step(s, atk(1, 2)).ok);
    CHECK_FALSE(step(s, {1, ActionKind::Dash}).ok);
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5;
    CHECK_FALSE(step(s, mv).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK(mut(s, 1).lethargy == 0);
    CHECK_FALSE(has_leftover_state(s));
    end_turns_until(s, 1);
    CHECK(s.budget.action);
    CHECK(s.budget.movement_ft == 30);
}

TEST_CASE("Haste: a replaced Haste or a second one still in force leaves no lethargy") {
    auto s = start({fighter(1), dummy(2)});
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste, 7100), sink));
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste, 7100), sink));   // same id: replaced (reason 2)
    CHECK(mut(s, 1).lethargy == 0);
    ModifierSpec other = marker(7200, ModStat::Haste, 7200); other.until_caster_turn = true;
    REQUIRE(apply_modifier(s, 2, 1, other, sink));
    (void)step(s, {1, ActionKind::EndTurn});   // 7200 drops; 7100 still hastes
    CHECK(s.modifiers.size() == 1);
    CHECK(mut(s, 1).lethargy == 0);
}

TEST_CASE("Haste through concentration: losing it brings lethargy") {
    int dropped = 0;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        auto s = start({fighter(1), dummy(2)}, seed);
        std::vector<Event> sink;
        ModifierSpec h = marker(7100, ModStat::Haste, 7100); h.concentration = true;
        REQUIRE(apply_modifier(s, 2, 1, h, sink));
        mut(s, 2).concentration_spell = 7100;
        mut(s, 2).hp = 1;
        auto r = step(s, atk(1, 2));
        REQUIRE(r.ok);
        const Event* e = find_ev(r.events, EventKind::AttackResolved);
        REQUIRE(e);
        if (!(e->c & 1)) continue;
        ++dropped;   // the caster dropped: concentration (and Haste) ended
        CHECK(s.modifiers.empty());
        CHECK(mut(s, 1).lethargy == 1);
    }
    CHECK(dropped > 0);
}

TEST_CASE("Slow: action or bonus action, one attack, no reactions") {
    auto s = start({fighter(1, 2), dummy(2)});
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 2, 1, marker(7001, ModStat::Slow, kSlow), sink));
    REQUIRE(step(s, atk(1, 2)).ok);
    CHECK(s.budget.slow_attacked);
    CHECK(s.budget.attacks_remaining == 0);    // Extra Attack is cut to one
    auto second = step(s, atk(1, 2));
    CHECK_FALSE(second.ok);
    CHECK(second.error == "slowed: one attack per turn");
    CHECK_FALSE(s.budget.bonus_action);        // the action was spent: no bonus action
    ActionRequest dash{1, ActionKind::Dash}; dash.as_bonus_action = true;
    CHECK_FALSE(step(s, dash).ok);
    next_turn_of(s, 1);
    REQUIRE(step(s, dash).ok);                 // bonus action first: then no action
    CHECK_FALSE(s.budget.action);
    CHECK_FALSE(step(s, atk(1, 2)).ok);
    CHECK_FALSE(step(s, {1, ActionKind::Dodge}).ok);
    // reactions: an opportunity attack is never offered to a slowed creature
    (void)step(s, {1, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 2);
    ActionRequest mv{2, ActionKind::Move}; mv.move_ft = 5; mv.leaves_reach = true;
    set_policy(mut(s, 1), ReactionTrigger::LeavingReach, ReactionPolicy::Auto);
    auto r = step(s, mv);
    REQUIRE(r.ok);
    CHECK(find_ev(r.events, EventKind::AttackResolved) == nullptr);
    CHECK_FALSE(mut(s, 1).reaction_used);
}

TEST_CASE("Slow: repeat save at the end of each turn; success ends every modifier of the spell") {
    auto s = start({fighter(1), dummy(2)});
    std::vector<Event> sink;
    ModifierSpec m = marker(7001, ModStat::Slow, kSlow); m.repeat_save = 1; m.repeat_ability = Ability::Wis; m.repeat_dc = 50;
    REQUIRE(apply_modifier(s, 2, 1, m, sink));
    REQUIRE(apply_modifier(s, 2, 1, flat(7002, ModStat::Ac, -2, kSlow), sink));
    auto fail = step(s, {1, ActionKind::EndTurn});
    REQUIRE(fail.ok);
    const Event* sv = find_ev(fail.events, EventKind::SavingThrowRolled);
    REQUIRE(sv); CHECK(sv->target == CombatantId(Ability::Wis)); CHECK(sv->c == 0);
    CHECK(s.modifiers.size() == 2);
    end_turns_until(s, 1);
    for (auto& am : s.modifiers) if (am.spec.repeat_save) am.spec.repeat_dc = 1;
    auto ok = step(s, {1, ActionKind::EndTurn});
    REQUIRE(ok.ok);
    CHECK(s.modifiers.empty());
    int sixes = 0;
    for (const auto& e : ok.events) if (e.kind == EventKind::ModifierExpired && e.b == 6) ++sixes;
    CHECK(sixes == 2);
}

TEST_CASE("Slow as a spell: Wis save, riders carry the caster's DC for the repeat save") {
    auto w = wizard(1); w.spells_known.push_back(kSlow);
    auto s = start({w, dummy(2)});
    auto r = step(s, cast(1, kSlow, 2, 3));
    REQUIRE(r.ok);
    REQUIRE(s.modifiers.size() == 2);
    for (const auto& am : s.modifiers) {
        CHECK(am.target == 2);
        if (am.spec.stat == ModStat::Slow) CHECK(am.spec.repeat_dc == 30);
    }
    CHECK(mut(s, 1).concentration_spell == kSlow);
    const uint64_t h1 = modifiers_digest(s);
    for (auto& am : s.modifiers) if (am.spec.repeat_save) am.spec.repeat_dc = 29;
    CHECK(modifiers_digest(s) != h1);
}

TEST_CASE("Life Drain: maximum reduced by the damage taken, capped HP, long rest restores") {
    int hits = 0;
    for (uint64_t seed = 0; seed < 8; ++seed) {
        auto d = dummy(2); d.attacks[0].drain_max_hp = true;
        auto s = start({fighter(1), d}, seed, 2);
        auto r = step(s, atk(2, 1));
        REQUIRE(r.ok);
        const Event* ar = find_ev(r.events, EventKind::AttackResolved);
        REQUIRE(ar);
        if (!(ar->c & 1)) continue;
        ++hits;
        const Combatant& t = mut(s, 1);
        CHECK(t.hp_max_reduction == 60 - t.hp);
        CHECK(effective_hp_max(t) == t.hp);
        const Event* m = find_ev(r.events, EventKind::ModifierApplied);
        REQUIRE(m);
        CHECK(m->b == int32_t(ModStat::MaxHp)); CHECK(m->c == -t.hp_max_reduction);
        CHECK(has_leftover_state(s));
        Combatant c = t;
        long_rest(c);
        CHECK(c.hp_max_reduction == 0);
        CHECK(c.hp == c.hp_max);
    }
    CHECK(hits > 0);
}

TEST_CASE("Life Drain: a Con save avoids it; a maximum of 0 kills") {
    for (uint64_t seed = 0; seed < 8; ++seed) {
        auto d = dummy(2); d.attacks[0].drain_max_hp = true; d.attacks[0].drain_save_dc = 1;
        auto s = start({fighter(1), d}, seed, 2);
        auto r = step(s, atk(2, 1));
        REQUIRE(r.ok);
        CHECK(mut(s, 1).hp_max_reduction == 0);
        CHECK(find_ev(r.events, EventKind::ModifierApplied) == nullptr);
    }
    int deaths = 0;
    for (uint64_t seed = 0; seed < 8; ++seed) {
        auto d = dummy(2); d.attacks[0].drain_max_hp = true;
        auto f = fighter(1); f.hp = f.hp_max = 3;
        auto s = start({f, d}, seed, 2);
        auto r = step(s, atk(2, 1));
        REQUIRE(r.ok);
        if (!(find_ev(r.events, EventKind::AttackResolved)->c & 1)) continue;
        ++deaths;
        CHECK(mut(s, 1).dead);
        CHECK(find_ev(r.events, EventKind::CombatantDied));
    }
    CHECK(deaths > 0);
}

TEST_CASE("Reckless Attack: declared on the first attack; advantage both ways until its next turn") {
    auto f = fighter(1, 2); f.reckless_attack = true;
    auto s = start({f, dummy(2)});
    ActionRequest r = atk(1, 2); r.reckless = true;
    auto ranged_first = r; ranged_first.attack_index = 1;
    REQUIRE(step(s, r).ok);
    CHECK(mut(s, 1).reckless_active);
    auto again = step(s, r);
    CHECK_FALSE(again.ok);
    CHECK(std::string(again.error).find("first attack") != std::string::npos);
    CHECK(step(s, atk(1, 2)).ok);
    (void)step(s, {1, ActionKind::EndTurn});
    REQUIRE(current_actor(s) == 2);
    CHECK(mut(s, 1).reckless_active);          // attacks against it still have advantage
    CHECK(has_leftover_state(s));
    end_turns_until(s, 1);
    CHECK_FALSE(mut(s, 1).reckless_active);
    auto none = start({fighter(1), dummy(2)});
    auto refused = step(none, r);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "no Reckless Attack");
    CHECK(none.budget.action);
}

TEST_CASE("Reckless Attack: advantage on its melee attack and on attacks against it") {
    int better_own = 0, better_against = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto f = fighter(1); f.reckless_attack = true;
        auto s = start({f, dummy(2)}, seed);
        auto base = start({f, dummy(2)}, seed);
        ActionRequest r = atk(1, 2); r.reckless = true;
        const int with = find_ev(step(s, r).events, EventKind::AttackResolved)->a;
        const int without = find_ev(step(base, atk(1, 2)).events, EventKind::AttackResolved)->a;
        CHECK(with >= without);
        if (with > without) ++better_own;
        (void)step(s, {1, ActionKind::EndTurn}); (void)step(base, {1, ActionKind::EndTurn});
        if (current_actor(s) != 2 || current_actor(base) != 2) continue;
        const int w2 = find_ev(step(s, atk(2, 1)).events, EventKind::AttackResolved)->a;
        const int wo2 = find_ev(step(base, atk(2, 1)).events, EventKind::AttackResolved)->a;
        if (w2 > wo2) ++better_against;
    }
    CHECK(better_own > 0);
    CHECK(better_against > 0);
}

TEST_CASE("Aggressive: a bonus-action Dash") {
    auto m = dummy(1); m.aggressive = true; m.bonus_action_grants = 0;
    auto s = start({m, fighter(2)});
    ActionRequest r{1, ActionKind::Dash}; r.as_bonus_action = true; r.aggressive = true;
    const auto legal = legal_actions(s);
    CHECK(std::any_of(legal.begin(), legal.end(), [](const ActionRequest& q) { return q.aggressive; }));
    auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK(s.budget.movement_ft == 60);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);
    CHECK(s.budget.aggressive_dash);
    CHECK_FALSE(step(s, r).ok);
    auto plain = start({dummy(1), fighter(2)});
    auto refused = step(plain, r);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "no Aggressive");
    ActionRequest as_action = r; as_action.as_bonus_action = false;
    auto s2 = start({m, fighter(2)});
    CHECK_FALSE(step(s2, as_action).ok);
}

TEST_CASE("Petrifying Gaze: restrained, then petrified on the failed repeat save") {
    auto w = wizard(1); w.spells_known.push_back(kGaze);
    auto s = start({w, dummy(2)});
    REQUIRE(step(s, cast(1, kGaze, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Restrained));
    CHECK_FALSE(mut(s, 2).has(Condition::Petrified));
    CHECK(has_leftover_state(s));
    const uint64_t before = leftover_digest(s);
    end_turns_until(s, 2);
    auto r = step(s, {2, ActionKind::EndTurn});
    REQUIRE(r.ok);
    CHECK(mut(s, 2).has(Condition::Petrified));
    CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
    CHECK(find_ev(r.events, EventKind::ConditionRemoved));
    CHECK(find_ev(r.events, EventKind::ConditionAdded));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].repeat_save == RepeatSave::None);
    CHECK_FALSE(s.effects[0].escalates);
    CHECK(leftover_digest(s) != before);
    end_turns_until(s, 2);
    auto later = step(s, {2, ActionKind::EndTurn});   // no more saves
    CHECK(find_ev(later.events, EventKind::SavingThrowRolled) == nullptr);
    CHECK(mut(s, 2).has(Condition::Petrified));
}

TEST_CASE("Petrifying Gaze: failing by 5 or more petrifies at once") {
    auto w = wizard(1); w.spells_known.push_back(kStare);
    auto s = start({w, dummy(2)});
    REQUIRE(step(s, cast(1, kStare, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Petrified));
    CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].repeat_save == RepeatSave::None);
    CHECK_FALSE(has_leftover_state(s));
}

TEST_CASE("leftover_digest: zero state folds nothing new; each field moves it") {
    auto s = start({fighter(1), dummy(2)});
    const uint64_t h0 = leftover_digest(s);
    mut(s, 1).hp_max_reduction = 3;
    const uint64_t h1 = leftover_digest(s);
    CHECK(h1 != h0);
    mut(s, 1).lethargy = 1;
    const uint64_t h2 = leftover_digest(s);
    CHECK(h2 != h1);
    mut(s, 1).reckless_active = true;
    CHECK(leftover_digest(s) != h2);
}
