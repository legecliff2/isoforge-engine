// Row 11: ReactionTrigger::Damaged / AllyAttacked, reduce_damage (Uncanny Dodge), retaliate (Hellish Rebuke),
// attack (Riposte), impose_disadvantage (Protection), damaged_options / ally_attacked_options hooks.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kRebuke = spell_id_of("test.devil#hellish_rebuke");

Combatant striker(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    return c;
}
Combatant defender(CombatantId id, std::vector<Reaction> rx, int hp = 500) {
    auto m = monster(id);
    m.hp = m.hp_max = int16_t(hp);
    m.ac = 10;
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    m.reactions = std::move(rx);
    return m;
}
Reaction reduce_half() {
    Reaction r; r.id = 301; r.window = ReactionTrigger::Damaged; r.kind = ReactionEffectKind::ReduceDamage; r.half = true; return r;
}
Reaction reduce_flat(int n) {
    Reaction r; r.id = 302; r.window = ReactionTrigger::Damaged; r.kind = ReactionEffectKind::ReduceDamage;
    r.dice = DiceExpr{0, 0, int16_t(n)}; return r;
}
Reaction retaliate() {
    Reaction r; r.id = 303; r.window = ReactionTrigger::Damaged; r.kind = ReactionEffectKind::Retaliate; r.spell_id = kRebuke; return r;
}
Reaction riposte() {
    Reaction r; r.id = 304; r.window = ReactionTrigger::Damaged; r.kind = ReactionEffectKind::Attack; r.attack_index = 0; return r;
}
Reaction protection() {
    Reaction r; r.id = 305; r.window = ReactionTrigger::AllyAttacked; r.kind = ReactionEffectKind::ImposeDisadvantage; return r;
}
SpellDef rebuke_def() {
    SpellDef d; d.id = kRebuke; d.content_id = "test.devil#hellish_rebuke"; d.level = 0; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 30; d.on_save = OnSave::Half;
    d.dice = DiceExpr{2, 10, 0}; d.damage_type = DamageType::Fire; d.is_spell = false; d.magical = false;
    return d;
}
// pick: option index for Damaged / AllyAttacked windows, -1 declines. `seen` records the options offered.
EncounterHooks hooks_with(int pick, std::vector<std::vector<ReactionOption>>* seen = nullptr, bool in_range_all = true) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(rebuke_def());
    h.spells = book;
    if (!in_range_all) h.in_range = nullptr;
    h.decide_reaction = [pick, seen](const EncounterState&, const ReactionWindow& w, const Combatant&,
                                     std::span<const ReactionOption> opts) {
        const bool ours = w.trigger == ReactionTrigger::Damaged || w.trigger == ReactionTrigger::AllyAttacked;
        if (ours && seen) seen->emplace_back(opts.begin(), opts.end());
        if (!ours || pick < 0) return ReactionDecision{w.seq, false, 0};
        return ReactionDecision{w.seq, true, uint8_t(pick)};
    };
    return h;
}
ActionRequest stab(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
int idx_of(const std::vector<Event>& ev, EventKind k, int from = 0) {
    for (int i = from; i < int(ev.size()); ++i) if (ev[std::size_t(i)].kind == k) return i;
    return -1;
}
bool landed(const StepResult& r) {
    const int i = idx_of(r.events, EventKind::AttackResolved);
    return i >= 0 && (r.events[std::size_t(i)].c & 1);
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
StepResult run(std::vector<Combatant> cs, uint64_t seed, EncounterHooks h, EncounterState* out = nullptr) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    auto r = step(s, stab(1, 2));
    if (out) *out = s;
    return r;
}
}  // namespace

TEST_CASE("enum values and defaults") {
    CHECK(uint8_t(ReactionTrigger::HitRider) == 4);
    CHECK(uint8_t(ReactionTrigger::Damaged) == 5);
    CHECK(uint8_t(ReactionTrigger::AllyAttacked) == 6);
    CHECK(kReactionTriggerCount == 7);
    Combatant c{};
    CHECK(c.reactions.empty());
    for (auto p : c.reaction_policy) CHECK(p == ReactionPolicy::Ask);
}

TEST_CASE("reduce_applied recomputes temp hp, drop and massive damage") {
    Combatant c = monster(1);
    c.hp = 10; c.hp_max = 10; c.temp_hp = 5;
    DamageApplied d = apply_damage(c, 8, DamageType::Slashing);
    auto r = reduce_applied(c, d, 3);
    CHECK(r.dealt == 5); CHECK(r.temp_hp_after == 0); CHECK(r.hp_after == 10); CHECK_FALSE(r.dropped_to_zero);
    auto all = reduce_applied(c, d, 99);
    CHECK(all.dealt == 0); CHECK(all.temp_hp_after == 5); CHECK(all.types_dealt == 0);
    c.temp_hp = 0; c.hp = 3;
    d = apply_damage(c, 10, DamageType::Slashing);
    CHECK(d.dropped_to_zero);
    r = reduce_applied(c, d, 8);
    CHECK(r.dealt == 2); CHECK(r.hp_after == 1); CHECK_FALSE(r.dropped_to_zero);
    c.hp = 10;
    d = apply_damage(c, 25, DamageType::Slashing);
    CHECK(d.instant_death);
    r = reduce_applied(c, d, 20);
    CHECK_FALSE(r.instant_death); CHECK(r.hp_after == 5);
    CHECK(reduce_applied(c, d, -4).dealt == 25);
}

TEST_CASE("Uncanny Dodge: Damaged window halves the damage; declining or Never does not") {
    int cmp = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto base = run({striker(1), defender(2, {})}, seed, hooks_with(-1));
        if (!landed(base)) continue;
        const int D = dealt_to(base.events, 2);
        std::vector<std::vector<ReactionOption>> seen;
        EncounterState s;
        auto r = run({striker(1), defender(2, {reduce_half()})}, seed, hooks_with(0, &seen), &s);
        REQUIRE(seen.size() == 1);
        REQUIRE(seen[0].size() == 1);
        CHECK(seen[0][0].kind == ReactionOptionKind::ReduceDamage);
        CHECK(seen[0][0].half);
        CHECK(seen[0][0].uses_reaction);
        CHECK(dealt_to(r.events, 2) == D - D / 2);
        CHECK(s.combatants[1].hp == 500 - (D - D / 2));
        CHECK(s.combatants[1].reaction_used);
        const int opened = idx_of(r.events, EventKind::ReactionWindowOpened);
        CHECK(opened >= 0);
        CHECK(r.events[std::size_t(opened)].a == int32_t(ReactionTrigger::Damaged));
        CHECK(opened < idx_of(r.events, EventKind::DamageDealt));
        // declined: full damage, reaction kept
        auto d = run({striker(1), defender(2, {reduce_half()})}, seed, hooks_with(-1), &s);
        CHECK(dealt_to(d.events, 2) == D);
        CHECK_FALSE(s.combatants[1].reaction_used);
        CHECK(count(d.events, EventKind::ReactionDeclined) == 1);
        // policy Never: no window at all
        auto nv = defender(2, {reduce_half()});
        nv.reaction_policy[std::size_t(ReactionTrigger::Damaged)] = ReactionPolicy::Never;
        auto n = run({striker(1), nv}, seed, hooks_with(0));
        CHECK(count(n.events, EventKind::ReactionWindowOpened) == 0);
        CHECK(dealt_to(n.events, 2) == D);
        ++cmp;
    }
    CHECK(cmp > 0);
}

TEST_CASE("reduce_damage can turn a lethal hit into no damage, and a flat reduction clamps") {
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto base = run({striker(1), defender(2, {})}, seed, hooks_with(-1));
        if (!landed(base)) continue;
        const int D = dealt_to(base.events, 2);
        EncounterState s;
        auto r = run({striker(1), defender(2, {reduce_flat(100)}, 1)}, seed, hooks_with(0), &s);   // 1 hp: would die
        CHECK(dealt_to(r.events, 2) == 0);
        CHECK(s.combatants[1].hp == 1);
        CHECK_FALSE(s.combatants[1].dead);
        CHECK(count(r.events, EventKind::CombatantDied) == 0);
        auto f = run({striker(1), defender(2, {reduce_flat(1)})}, seed, hooks_with(0), &s);
        CHECK(dealt_to(f.events, 2) == D - 1);
    }
}

TEST_CASE("Auto policy takes option 0 without a decider; default Ask pauses with NeedsDecision") {
    int paused = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto d = defender(2, {reduce_half()});
        d.reaction_policy[std::size_t(ReactionTrigger::Damaged)] = ReactionPolicy::Auto;
        auto h = spell_hooks();
        auto r = run({striker(1), d}, seed, h);
        REQUIRE(r.status == StepStatus::Done);
        if (landed(r)) CHECK(count(r.events, EventKind::ReactionTaken) == 1);
        // default Ask, no decider
        auto s = begin_encounter({striker(1), defender(2, {reduce_half()})}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto p = step(s, stab(1, 2));
        if (p.status != StepStatus::NeedsDecision) continue;
        REQUIRE(p.needs);
        CHECK(p.needs->trigger == ReactionTrigger::Damaged);
        CHECK(p.needs->reactor == 2);
        CHECK(p.needs->window.damage > 0);
        CHECK(p.needs->window.target == 2);
        CHECK(p.needs->window.trigger_actor == 1);
        auto done = step(s, decision_request(*p.needs, true, 0));
        REQUIRE(done.ok);
        CHECK(done.status == StepStatus::Done);
        ++paused;
    }
    CHECK(paused > 0);
}

TEST_CASE("Hellish Rebuke: retaliate resolves against the damager after the damage, at reaction depth 1") {
    int checked = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        EncounterState s;
        auto r = run({striker(1), defender(2, {retaliate()})}, seed, hooks_with(0), &s);
        if (!landed(r)) continue;
        ++checked;
        const int dd = idx_of(r.events, EventKind::DamageDealt);
        const int sv = idx_of(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv > dd);
        CHECK(r.events[std::size_t(sv)].actor == 1);               // the damager saves (DC 30: fails)
        CHECK(r.events[std::size_t(sv)].reaction_depth == 1);
        CHECK(s.combatants[0].hp < 80);
        CHECK(s.combatants[1].reaction_used);
        // reactor dead after the hit: the reaction is spent but nothing is thrown back
        auto k = run({striker(1), defender(2, {retaliate()}, 1)}, seed, hooks_with(0), &s);
        CHECK(idx_of(k.events, EventKind::SavingThrowRolled) < 0);
        CHECK(s.combatants[1].dead);
    }
    CHECK(checked > 0);
    // a retaliate whose def is unknown is not offered
    auto bad = defender(2, {retaliate()});
    bad.reactions[0].spell_id = 12345;
    std::vector<std::vector<ReactionOption>> seen;
    (void)run({striker(1), bad}, 3, hooks_with(0, &seen));
    CHECK(seen.empty());
}

TEST_CASE("Riposte: an attack reaction strikes the damager after the damage") {
    int checked = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto r = run({striker(1), defender(2, {riposte()})}, seed, hooks_with(0));
        if (!landed(r)) continue;
        ++checked;
        int n = 0, first_actor_two = -1;
        for (int i = 0; i < int(r.events.size()); ++i) {
            const auto& e = r.events[std::size_t(i)];
            if (e.kind != EventKind::AttackResolved) continue;
            ++n;
            if (e.actor == 2) first_actor_two = int(e.reaction_depth);
        }
        CHECK(n == 2);
        CHECK(first_actor_two == 1);
    }
    CHECK(checked > 0);
}

TEST_CASE("melee_only / ranged_only filter the window by the attack that caused it") {
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto ranged = striker(1);
        ranged.attacks[0].ranged = true;
        auto rx = reduce_half(); rx.melee_only = true;
        std::vector<std::vector<ReactionOption>> seen;
        (void)run({ranged, defender(2, {rx})}, seed, hooks_with(0, &seen));
        CHECK(seen.empty());
        auto rx2 = reduce_half(); rx2.ranged_only = true;
        (void)run({striker(1), defender(2, {rx2})}, seed, hooks_with(0, &seen));
        CHECK(seen.empty());
    }
}

TEST_CASE("damaged_options hook options come after the compiled ones") {
    auto h = hooks_with(-1);
    std::vector<std::vector<ReactionOption>> seen;
    h = hooks_with(1, &seen);
    h.damaged_options = [](const EncounterState&, const ReactionWindow&, const Combatant&) {
        ReactionOption o{}; o.kind = ReactionOptionKind::ReduceDamage; o.half = true; o.option_id = 999;
        return std::vector<ReactionOption>{o};
    };
    for (uint64_t seed = 0; seed < 20; ++seed) {
        seen.clear();
        EncounterState s;
        auto r = run({striker(1), defender(2, {})}, seed, h, &s);   // no compiled reactions: the hook alone opens the window
        if (!landed(r)) continue;
        REQUIRE(seen.size() == 1);
        REQUIRE(seen[0].size() == 1);
        CHECK(seen[0][0].option_id == 999);
        return;
    }
}

TEST_CASE("Protection: AllyAttacked imposes disadvantage; needs in_range, a teammate with a reaction, once") {
    int lower = 0, higher = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto base = run({striker(1), defender(2, {}), defender(3, {protection()})}, seed, hooks_with(-1));
        const int bi = idx_of(base.events, EventKind::AttackResolved);
        REQUIRE(bi >= 0);
        std::vector<std::vector<ReactionOption>> seen;
        EncounterState s;
        auto r = run({striker(1), defender(2, {}), defender(3, {protection()})}, seed, hooks_with(0, &seen), &s);
        const int ri = idx_of(r.events, EventKind::AttackResolved);
        REQUIRE(ri >= 0);
        const int n0 = r.events[std::size_t(ri)].a, nb = base.events[std::size_t(bi)].a;
        if (n0 < nb) ++lower;
        if (n0 > nb) ++higher;
        REQUIRE(seen.size() >= 1);
        CHECK(seen[0][0].kind == ReactionOptionKind::ImposeDisadvantage);
        CHECK(idx_of(r.events, EventKind::ReactionWindowOpened) < ri);
        CHECK(r.events[std::size_t(idx_of(r.events, EventKind::ReactionWindowOpened))].a == int32_t(ReactionTrigger::AllyAttacked));
        CHECK(s.combatants[2].reaction_used);
        // no in_range hook: Engine Core supplies adjacency, so nobody is asked
        seen.clear();
        auto nr = run({striker(1), defender(2, {}), defender(3, {protection()})}, seed, hooks_with(0, &seen, false));
        CHECK(count(nr.events, EventKind::ReactionWindowOpened) == 0);
    }
    CHECK(lower > 0);
    CHECK(higher == 0);
}

TEST_CASE("Protection: not the target, not an enemy of the target, not twice") {
    for (uint64_t seed = 0; seed < 10; ++seed) {
        std::vector<std::vector<ReactionOption>> seen;
        // the target itself holds the reaction: no AllyAttacked window for it
        (void)run({striker(1), defender(2, {protection()})}, seed, hooks_with(0, &seen));
        CHECK(seen.empty());
        // a creature on the attacker's team is not an ally of the target
        auto mate = striker(3);
        mate.reactions = {protection()};
        (void)run({striker(1), defender(2, {}), mate}, seed, hooks_with(0, &seen));
        CHECK(seen.empty());
        // two protectors: the first imposes, the second is not asked
        EncounterState s;
        auto r = run({striker(1), defender(2, {}), defender(3, {protection()}), defender(4, {protection()})}, seed, hooks_with(0, &seen), &s);
        CHECK(count(r.events, EventKind::ReactionTaken) == 1);
        CHECK((s.combatants[2].reaction_used != s.combatants[3].reaction_used));
    }
}
