#include <doctest/doctest.h>

#include "golden_hash.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}

// Goblin with 60 HP so it survives a few spells.
Combatant tough_goblin(CombatantId id) {
    auto g = monster(id);
    g.hp = g.hp_max = 60;
    return g;
}

} // namespace

TEST_CASE("spell save DC, spell attack bonus, slots and rests") {
    auto w = wizard(1);
    CHECK(spell_save_dc(w) == 14);       // 8 + 3 + 3
    CHECK(spell_attack_bonus(w) == 6);   // 3 + 3
    CHECK(has_slot(w, 0));
    CHECK(has_slot(w, 3));
    CHECK_FALSE(has_slot(w, 4));
    CHECK(spend_slot(w, 3)); CHECK(spend_slot(w, 3));
    CHECK_FALSE(spend_slot(w, 3));
    w.pact_slot_level = 3; w.pact_slots_max = 1;
    recover_slots_short_rest(w);          // Pact Magic comes back on a short rest ...
    CHECK(w.spell_slots[3] == 0);
    CHECK(w.pact_slots == 1);
    CHECK(spend_slot(w, 3));              // ... and is used when no regular slot is left
    CHECK(w.pact_slots == 0);
    recover_slots_long_rest(w);           // everything on a long rest
    CHECK(w.spell_slots == w.spell_slots_max);
    CHECK(w.pact_slots == 1);
}

TEST_CASE("CastSpell validation") {
    auto s = begin_encounter({wizard(1), tough_goblin(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    CHECK_FALSE(step(s, cast(1, kCureWounds, 2, 1)).ok);     // not known
    CHECK_FALSE(step(s, cast(1, kFireBolt, 2, 1)).ok);       // cantrips use no slot
    CHECK_FALSE(step(s, cast(1, kHoldPerson, 2, 1)).ok);     // below the spell's level
    CHECK_FALSE(step(s, cast(1, kHoldPerson, 2, 4)).ok);     // no 4th-level slot
    CHECK_FALSE(step(s, cast(1, kShield, 1, 1)).ok);         // reaction spell
    CHECK_FALSE(step(s, cast(1, kFireBolt, 1)).ok);          // spell attack at self
    auto mm = cast(1, kMagicMissile, 2, 1); mm.targets = {2, 2};
    CHECK_FALSE(step(s, mm).ok);                             // 3 darts: one target or three entries
    EncounterHooks h = spell_hooks();
    h.can_provide_components = [](const EncounterState&, const Combatant&, const SpellDef& d) {
        return (d.components & kComponentV) == 0;            // e.g. silenced
    };
    auto t = begin_encounter({wizard(1), tough_goblin(2)}, 3, nullptr, h);
    end_turns_until(t, 1);
    CHECK_FALSE(step(t, cast(1, kFireBolt, 2)).ok);
    CHECK(s.budget.action);
    CHECK(find(s, 1)->spell_slots == wizard(1).spell_slots);
}

TEST_CASE("Fire Bolt: spell attack with the spell attack bonus; cantrip scaling; no slot") {
    int hits = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = begin_encounter({wizard(1), tough_goblin(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kFireBolt, 2));
        REQUIRE(r.ok);
        CHECK_FALSE(s.budget.action);
        CHECK(find(s, 1)->spell_slots == wizard(1).spell_slots);
        const Event* a = first(r.events, EventKind::AttackResolved);
        REQUIRE(a);
        CHECK(a->b == a->a + 6);
        REQUIRE(first(r.events, EventKind::SpellResolved));
        if (const Event* d = first(r.events, EventKind::DamageDealt)) {
            ++hits;
            bool crit = (a->c & 2) != 0;
            CHECK(d->a >= (crit ? 4 : 2));           // caster level 5: 2d10 (4d10 on a crit)
            CHECK(d->a <= (crit ? 40 : 20));
        }
    }
    CHECK(hits > 10);
}

TEST_CASE("Magic Missile: auto-hit darts, upcast, split targets") {
    auto s = begin_encounter({wizard(1), tough_goblin(2), tough_goblin(3)}, 9, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kMagicMissile, 2, 1));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::DamageDealt) == 3);
    for (const auto& e : r.events) if (e.kind == EventKind::DamageDealt) { CHECK(e.a >= 2); CHECK(e.a <= 5); CHECK(e.target == 2); }
    CHECK(find(s, 1)->spell_slots[1] == 3);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto up = cast(1, kMagicMissile, 2, 3);
    up.targets = {2, 3, 3, 2, 3};                            // 5 darts at 3rd level
    auto r2 = step(s, up);
    REQUIRE(r2.ok);
    int at2 = 0, at3 = 0;
    for (const auto& e : r2.events) if (e.kind == EventKind::DamageDealt) (e.target == 2 ? at2 : at3)++;
    CHECK(at2 == 2); CHECK(at3 == 3);
    CHECK(find(s, 1)->spell_slots[3] == 1);
}

TEST_CASE("Shield: BeingHit reaction, +5 AC until the start of the caster's turn, spends a slot") {
    int used = 0, turned = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto s = begin_encounter({wizard(1), monster(2)}, seed, nullptr, spell_hooks());
        set_policy(mut(s, 1), ReactionTrigger::BeingHit, ReactionPolicy::Auto);
        end_turns_until(s, 2);
        if (current_actor(s) != 2) continue;
        auto r = step(s, {2, ActionKind::Attack, 1});
        REQUIRE(r.status == StepStatus::Done);
        if (!find(s, 1)->reaction_used) continue;
        ++used;
        CHECK(find(s, 1)->spell_slots[1] == 3);
        CHECK(find(s, 1)->reaction_ac_bonus == 5);
        const Event* cast_ev = first(r.events, EventKind::SpellCast);
        REQUIRE(cast_ev);
        CHECK(cast_ev->a == int32_t(kShield));
        CHECK(cast_ev->reaction_depth == 1);
        const Event* a = first(r.events, EventKind::AttackResolved);
        CHECK((a->c & 1) == 0);                    // Auto only casts when it turns the hit into a miss
        ++turned;
        end_turns_until(s, 1);
        CHECK(find(s, 1)->reaction_ac_bonus == 0);
    }
    CHECK(used > 0); CHECK(turned == used);
}

TEST_CASE("Shield blocks Magic Missile; the prompt names the spell") {
    auto s = begin_encounter({wizard(1), wizard(2, 1)}, 21, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kMagicMissile, 2, 1));
    REQUIRE(r.status == StepStatus::NeedsDecision);
    // First window: Counterspell (SpellCast) from the enemy wizard; decline it.
    CHECK(r.needs->trigger == ReactionTrigger::SpellCast);
    CHECK(r.needs->spell_id == kMagicMissile);
    CHECK(r.needs->options.size() == 1);          // only 3rd-level slots can counter (2 left, one option per level)
    CHECK(r.needs->options[0].slot_level == 3);
    r = step(s, decision_request(*r.needs, false));
    REQUIRE(r.status == StepStatus::NeedsDecision);
    CHECK(r.needs->trigger == ReactionTrigger::BeingHit);
    CHECK(r.needs->spell_id == kMagicMissile);
    CHECK(r.needs->attack_total == 0);
    REQUIRE(r.needs->options.size() == 1);
    CHECK(r.needs->options[0].option_id == kShield);
    CHECK(r.needs->options[0].slot_level == 1);
    r = step(s, decision_request(*r.needs, true));
    REQUIRE(r.status == StepStatus::Done);
    CHECK(count(r.events, EventKind::DamageDealt) == 0);
    CHECK(find(s, 2)->hp == 30);
    CHECK(find(s, 2)->spell_slots[1] == 3);
    CHECK(find(s, 1)->spell_slots[1] == 3);
    REQUIRE(r.logged);
    CHECK(r.logged->reactions.size() == 2);
}

TEST_CASE("Cure Wounds: 1d8 + spellcasting modifier, +1d8 per slot level above 1st") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = begin_encounter({cleric(1), pc(3), monster(2)}, seed, nullptr, spell_hooks());
        mut(s, 3).hp = 1;
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kCureWounds, 3, 1));
        REQUIRE(r.ok);
        const Event* h = first(r.events, EventKind::Healed);
        REQUIRE(h);
        CHECK(h->a >= 4); CHECK(h->a <= 11);
        CHECK(find(s, 1)->spell_slots[1] == 1);
    }
}

TEST_CASE("Hold Person: paralyzed on a failed WIS save, concentration, repeat save at end of turn") {
    int held = 0, freed = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto s = begin_encounter({wizard(1), tough_goblin(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kHoldPerson, 2, 2));
        REQUIRE(r.ok);
        const Event* sv = first(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv);
        CHECK(sv->target == CombatantId(Ability::Wis));
        CHECK(sv->c == int32_t(sv->b >= 14));
        if (sv->c) { CHECK_FALSE(find(s, 2)->has(Condition::Paralyzed)); CHECK(find(s, 1)->concentration_spell == 0); continue; }
        ++held;
        CHECK(find(s, 2)->has(Condition::Paralyzed));
        CHECK(find(s, 1)->concentration_spell == kHoldPerson);
        REQUIRE(s.effects.size() == 1);
        // Goblin's turn: incapacitated; its end-of-turn save may end the effect.
        end_turns_until(s, 2);
        auto e = step(s, {2, ActionKind::EndTurn});
        const Event* rs = first(e.events, EventKind::SavingThrowRolled);
        REQUIRE(rs);
        if (rs->c) {
            ++freed;
            CHECK_FALSE(find(s, 2)->has(Condition::Paralyzed));
            CHECK(s.effects.empty());
            CHECK(find(s, 1)->concentration_spell == 0);   // the spell has no target left
        } else {
            CHECK(find(s, 2)->has(Condition::Paralyzed));
        }
    }
    CHECK(held > 0); CHECK(freed > 0);
}

TEST_CASE("Concentration: CON save DC max(10, half damage); new concentration spell; incapacitation") {
    // Damage while concentrating.
    int broke = 0, kept = 0;
    for (uint64_t seed = 0; seed < 120; ++seed) {
        auto s = begin_encounter({wizard(1), tough_goblin(2), tough_goblin(3)}, seed, nullptr, spell_hooks());
        never_react(mut(s, 1));
        end_turns_until(s, 1);
        if (!step(s, cast(1, kHoldPerson, 2, 2)).ok || find(s, 1)->concentration_spell == 0) continue;
        end_turns_until(s, 3);
        if (current_actor(s) != 3 || find(s, 1)->concentration_spell == 0) continue;   // may have saved out
        auto r = step(s, {3, ActionKind::Attack, 1});
        const Event* d = first(r.events, EventKind::DamageDealt);
        if (!d) continue;
        const Event* cs = nullptr;
        for (const auto& e : r.events) if (e.kind == EventKind::SavingThrowRolled && (e.c & 2)) cs = &e;
        REQUIRE(cs);
        CHECK(cs->target == CombatantId(Ability::Con));
        bool success = cs->b >= std::max(10, d->a / 2);
        CHECK((cs->c & 1) == int32_t(success));
        if (success) { ++kept; CHECK(find(s, 2)->has(Condition::Paralyzed)); }
        else {
            ++broke;
            CHECK(first(r.events, EventKind::ConcentrationEnded));
            CHECK_FALSE(find(s, 2)->has(Condition::Paralyzed));
            CHECK(s.effects.empty());
        }
    }
    CHECK(broke > 0); CHECK(kept > 0);

    // A new concentration spell ends the first; incapacitation ends concentration.
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto s = begin_encounter({wizard(1), tough_goblin(2), tough_goblin(3)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        step(s, cast(1, kHoldPerson, 2, 2));
        if (find(s, 1)->concentration_spell == 0) continue;
        end_turns_until(s, 2); end_turns_until(s, 1);
        if (!find(s, 2)->has(Condition::Paralyzed)) continue;
        auto r = step(s, cast(1, kHoldPerson, 3, 2));
        REQUIRE(r.ok);
        CHECK(first(r.events, EventKind::ConcentrationEnded));
        CHECK_FALSE(find(s, 2)->has(Condition::Paralyzed));
        for (const auto& e : s.effects) CHECK(e.target == 3);
        // Incapacitate the caster: concentration ends after the next request.
        add_condition(mut(s, 1), Condition::Stunned);
        auto e = step(s, {1, ActionKind::EndTurn});
        REQUIRE(e.ok);
        CHECK(first(e.events, EventKind::ConcentrationEnded));
        CHECK(find(s, 1)->concentration_spell == 0);
        CHECK(s.effects.empty());
        CHECK_FALSE(find(s, 3)->has(Condition::Paralyzed));
        break;
    }
}

TEST_CASE("Counterspell: automatic at or above the spell's level, else check DC 10 + level") {
    // Same-level counter: always succeeds, spends the counterer's slot.
    {
        auto s = begin_encounter({wizard(1), wizard(2, 1)}, 4, nullptr, spell_hooks());
        set_policy(mut(s, 2), ReactionTrigger::SpellCast, ReactionPolicy::Auto);
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kMagicMissile, 2, 3));
        REQUIRE(r.status == StepStatus::Done);
        CHECK(first(r.events, EventKind::SpellCountered));
        CHECK_FALSE(first(r.events, EventKind::SpellCheckRolled));
        CHECK(count(r.events, EventKind::DamageDealt) == 0);
        CHECK(find(s, 1)->spell_slots[3] == 1);              // the caster's slot is still spent
        CHECK(find(s, 2)->spell_slots[3] == 1);
    }
    // Higher-level spell: ability check DC 10 + 4 = 14 with INT +3.
    int ok = 0, failed = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        auto w1 = wizard(1);
        w1.spell_slots[4] = w1.spell_slots_max[4] = 1;
        auto s = begin_encounter({w1, wizard(2, 1)}, seed, nullptr, spell_hooks());
        set_policy(mut(s, 2), ReactionTrigger::SpellCast, ReactionPolicy::Auto);
        never_react(mut(s, 1));
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kHoldPerson, 2, 4));
        REQUIRE(r.status == StepStatus::Done);
        const Event* chk = first(r.events, EventKind::SpellCheckRolled);
        REQUIRE(chk);
        CHECK(chk->b == chk->a + 3);
        CHECK(chk->c == int32_t(chk->b >= 14));
        bool countered = first(r.events, EventKind::SpellResolved) == nullptr;
        CHECK(countered == (chk->c == 1));
        (countered ? ok : failed)++;
    }
    CHECK(ok > 0); CHECK(failed > 0);
}

TEST_CASE("Counterspell offers one option per available slot level") {
    auto w2 = wizard(2, 1);
    w2.spell_slots[4] = w2.spell_slots_max[4] = 1;
    w2.spell_slots[5] = w2.spell_slots_max[5] = 1;
    auto s = begin_encounter({wizard(1), w2}, 4, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kHoldPerson, 2, 2));
    REQUIRE(r.status == StepStatus::NeedsDecision);
    REQUIRE(r.needs->options.size() == 3);
    CHECK(r.needs->options[0].slot_level == 3);
    CHECK(r.needs->options[2].slot_level == 5);
    CHECK(r.needs->spell_level == 2);
    CHECK(r.needs->target == 2);
    r = step(s, decision_request(*r.needs, true, 2));   // counter with the 5th-level slot
    REQUIRE(r.status == StepStatus::Done);
    CHECK(find(s, 2)->spell_slots[5] == 0);
    CHECK(find(s, 2)->spell_slots[3] == 2);
}

// ---- Golden: an encounter with spells (sync adapter, async, and log replay agree) ----
namespace {

uint64_t hash_spell_state(const EncounterState& s) {
    GoldenHash g;
    for (const auto& c : s.combatants) {
        g.u32(c.id); g.i32(c.hp); g.u32(c.conditions); g.byte(c.dead); g.byte(c.reaction_used);
        g.byte(uint8_t(c.reaction_ac_bonus)); g.u32(c.concentration_spell); g.u32(c.reaction_blocks_spell);
        for (uint8_t L = 1; L <= 9; ++L) g.byte(c.spell_slots[L]);
        g.byte(c.death.successes); g.byte(c.death.failures); g.byte(c.death.stable);
    }
    for (const auto& e : s.effects) { g.u32(e.spell_id); g.u32(e.caster); g.u32(e.target); }
    g.u32(s.round); g.u32(s.turn_index);
    auto st = s.rng.save(); g.u64(st.state); g.u64(st.inc);
    g.u32(s.next_window_seq); g.byte(s.ended);
    return g.h;
}

uint64_t hash_spell_log(const ActionLog& log) {
    GoldenHash g;
    for (const auto& e : log) {
        g.byte(uint8_t(e.request.kind)); g.u32(e.request.actor); g.u32(e.request.target);
        g.u32(e.request.spell_id); g.byte(e.request.spell_level);
        g.u32(uint32_t(e.reactions.size()));
        for (const auto& r : e.reactions) {
            g.u32(r.window_seq); g.byte(uint8_t(r.trigger)); g.u32(r.reactor);
            g.byte(r.decision.react); g.byte(r.decision.option);
        }
    }
    return g.h;
}

std::vector<Combatant> spell_party() {
    auto ench = wizard(3, 1);                  // enemy wizard
    ench.hp = ench.hp_max = 40;
    auto g = tough_goblin(4); g.hp = g.hp_max = 30;
    return {wizard(1), cleric(2), ench, g};
}

ReactionDecision spell_decide(const ReactionWindow& w, CombatantId reactor) {
    return ReactionDecision{0, ((w.seq * 5u + reactor) % 3u) != 0, 0};
}

ActionRequest spell_policy(const EncounterState& s, int i) {
    CombatantId a = current_actor(s);
    const Combatant* me = find(s, a);
    if (me->hp <= 0 || is_incapacitated(*me)) return {a, ActionKind::EndTurn};
    if (!s.budget.action) return {a, ActionKind::EndTurn};
    CombatantId foe = 0, hurt = 0;
    for (const auto& c : s.combatants) {
        if (c.dead) continue;
        if (c.team != me->team && c.hp > 0 && !foe) foe = c.id;
        if (c.team == me->team && c.hp < c.hp_max / 2 && !hurt) hurt = c.id;
    }
    if (!foe) return {a, ActionKind::EndTurn};
    if (hurt && knows_spell(*me, kCureWounds) && has_slot(*me, 1)) return cast(a, kCureWounds, hurt, 1);
    if (knows_spell(*me, kHoldPerson) && i % 5 == 1 && has_slot(*me, 2) && me->concentration_spell == 0)
        return cast(a, kHoldPerson, foe, 2);
    if (knows_spell(*me, kMagicMissile) && i % 4 == 2 && has_slot(*me, 1)) return cast(a, kMagicMissile, foe, 1);
    if (knows_spell(*me, kFireBolt)) return cast(a, kFireBolt, foe);
    ActionRequest r{a, ActionKind::Attack, foe}; r.target_within_5ft = true; return r;
}

constexpr uint64_t kSpellSeed = 0x5BE14ull;

} // namespace

TEST_CASE("golden: encounter with spells; sync adapter, async and log replay agree") {
    EncounterHooks hs = spell_hooks();
    hs.end_condition = end_when_no_pc_or_no_opponent;
    hs.decide_reaction = [](const EncounterState&, const ReactionWindow& w, const Combatant& r,
                            std::span<const ReactionOption>) { return spell_decide(w, r.id); };
    auto s = begin_encounter(spell_party(), kSpellSeed, nullptr, hs);
    ActionLog sync_log;
    int counters = 0, shields = 0, conc_saves = 0, heals = 0, holds = 0;
    for (int i = 0; i < 600 && !s.ended; ++i) {
        auto r = step_logged(s, spell_policy(s, i), sync_log);
        REQUIRE(r.status == StepStatus::Done);
        for (const auto& e : r.events) {
            if (e.kind == EventKind::SpellCast && e.a == int32_t(kCounterspell)) ++counters;
            if (e.kind == EventKind::SpellCast && e.a == int32_t(kShield)) ++shields;
            if (e.kind == EventKind::SavingThrowRolled && (e.c & 2)) ++conc_saves;
            if (e.kind == EventKind::Healed) ++heals;
            if (e.kind == EventKind::ConditionAdded && e.a == int32_t(Condition::Paralyzed)) ++holds;
        }
    }
    CHECK(s.ended);
    CHECK(counters > 0); CHECK(shields > 0); CHECK(conc_saves > 0); CHECK(heals > 0); CHECK(holds > 0);

    EncounterHooks ha = spell_hooks();
    ha.end_condition = end_when_no_pc_or_no_opponent;
    auto t = begin_encounter(spell_party(), kSpellSeed, nullptr, ha);
    ActionLog async_log;
    int pauses = 0;
    for (int i = 0; i < 600 && !t.ended; ++i) {
        auto r = step_logged(t, spell_policy(t, i), async_log);
        while (r.status == StepStatus::NeedsDecision) {
            ++pauses;
            r = step_logged(t, decision_request(*r.needs, spell_decide(r.needs->window, r.needs->reactor)), async_log);
        }
        REQUIRE(r.status == StepStatus::Done);
    }
    CHECK(pauses > 0);
    CHECK(hash_spell_state(t) == hash_spell_state(s));
    CHECK(hash_spell_log(async_log) == hash_spell_log(sync_log));

    auto u = begin_encounter(spell_party(), kSpellSeed, nullptr, ha);
    bool desync = false;
    for (const auto& e : sync_log) desync |= replay_step(u, e).desync;
    CHECK_FALSE(desync);
    CHECK(hash_spell_state(u) == hash_spell_state(s));

    CHECK(sync_log.size() == 115u);
    CHECK(hash_spell_state(s) == 241533432519136774ull);
    CHECK(hash_spell_log(sync_log) == 17334714446208789141ull);
}
