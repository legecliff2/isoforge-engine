#include <doctest/doctest.h>

#include <algorithm>
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
    REQUIRE(r.needs->options.size() == 3);       // Ask: one Shield option per available slot level
    for (uint8_t i = 0; i < 3; ++i) {
        CHECK(r.needs->options[i].option_id == kShield);
        CHECK(r.needs->options[i].slot_level == i + 1);
        CHECK(r.needs->options[i].ac_bonus == 5);
    }
    r = step(s, decision_request(*r.needs, true));   // option 0 = lowest slot
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

TEST_CASE("Shield under Ask: casting with a higher slot spends that slot") {
    for (uint64_t seed = 0; seed < 100; ++seed) {
        auto s = begin_encounter({wizard(1), monster(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 2);
        if (current_actor(s) != 2) continue;
        auto r = step(s, {2, ActionKind::Attack, 1});
        if (r.status != StepStatus::NeedsDecision) continue;
        REQUIRE(r.needs->options.size() == 3);
        r = step(s, decision_request(*r.needs, true, 1));
        REQUIRE(r.status == StepStatus::Done);
        CHECK(find(s, 1)->spell_slots[1] == 4);
        CHECK(find(s, 1)->spell_slots[2] == 2);
        CHECK(find(s, 1)->reaction_ac_bonus == 5);
        return;
    }
    FAIL("no hit found");
}

TEST_CASE("legal_actions suggests spell casts") {
    auto s = begin_encounter({wizard(1), cleric(3), tough_goblin(2)}, 3, nullptr, spell_hooks());
    mut(s, 1).spell_slots[3] = 0;
    end_turns_until(s, 1);
    auto acts = legal_actions(s);
    auto has = [&](uint32_t sp, CombatantId t, uint8_t L) {
        return std::any_of(acts.begin(), acts.end(), [&](const ActionRequest& r) {
            return r.kind == ActionKind::CastSpell && r.spell_id == sp && r.target == t && r.spell_level == L; });
    };
    CHECK(has(kFireBolt, 2, 0));
    CHECK_FALSE(has(kFireBolt, 3, 0));             // not at allies
    CHECK(has(kMagicMissile, 2, 1)); CHECK(has(kMagicMissile, 2, 2));
    CHECK_FALSE(has(kMagicMissile, 2, 3));         // no 3rd-level slot left
    CHECK(has(kHoldPerson, 2, 2)); CHECK_FALSE(has(kHoldPerson, 2, 1));
    for (const auto& r : acts) {
        CHECK(r.spell_id != kShield); CHECK(r.spell_id != kCounterspell);   // reactions
        if (r.kind == ActionKind::CastSpell) { auto c = s; CHECK(step(c, r).ok); }   // every suggestion is legal
    }
    end_turns_until(s, 3);
    acts = legal_actions(s);
    CHECK(has(kCureWounds, 1, 1)); CHECK(has(kCureWounds, 3, 1));   // allies incl. self
    CHECK_FALSE(has(kCureWounds, 2, 1));
    // Spent action: no spell suggestions.
    REQUIRE(step(s, cast(3, kCureWounds, 1, 1)).ok);
    for (const auto& r : legal_actions(s)) CHECK(r.kind != ActionKind::CastSpell);
}

// ---- spell@1 review (T&C): targeting, distribution, beams, repeat timing, sight ----

TEST_CASE("creature types: Hold Person humanoids only; Cure Wounds no effect on undead") {
    auto s = begin_encounter({wizard(1), cleric(3), tough_goblin(2)}, 3, nullptr, spell_hooks());
    mut(s, 2).creature_type = CreatureType::Undead;
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kHoldPerson, 2, 2));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "invalid creature type for this spell");
    for (const auto& a : legal_actions(s)) CHECK_FALSE((a.spell_id == kHoldPerson && a.target == 2));
    mut(s, 3).creature_type = CreatureType::Construct;   // e.g. a warforged-like ally
    mut(s, 3).hp = 5;
    end_turns_until(s, 3);
    auto h = step(s, cast(3, kCureWounds, 3, 1));
    REQUIRE(h.ok);                                         // cast (slot spent) but no effect
    CHECK(count(h.events, EventKind::Healed) == 0);
    CHECK(find(s, 3)->hp == 5);
    CHECK(find(s, 3)->spell_slots[1] == 1);
}

TEST_CASE("Hold Person upcast: targets within 30 ft of each other (sim hook)") {
    EncounterHooks h = spell_hooks();
    int asked_ft = 0;
    h.targets_within = [&](const EncounterState&, const std::vector<CombatantId>& ids, int ft) {
        asked_ft = ft; return ids.size() <= 1;   // pretend they're far apart
    };
    auto s = begin_encounter({wizard(1), tough_goblin(2), tough_goblin(3)}, 3, nullptr, h);
    never_react(mut(s, 2)); never_react(mut(s, 3));
    end_turns_until(s, 1);
    auto r = cast(1, kHoldPerson, 2, 3); r.targets = {2, 3};
    auto res = step(s, r);
    CHECK_FALSE(res.ok);
    CHECK(res.error == "targets too far apart");
    CHECK(asked_ft == 30);
    s.hooks.targets_within = nullptr;
    CHECK(step(s, r).ok);
    CHECK(count(step(s, {1, ActionKind::EndTurn}).events, EventKind::TurnEnded) == 1);
}

namespace {
std::shared_ptr<SpellBook> book_with(std::initializer_list<SpellDef> extra) {
    auto b = srd_spellbook();
    for (const auto& d : extra) b->add(d);
    return b;
}
SpellDef test_def(const char* cid, SpellEffectKind k) {
    SpellDef d; d.content_id = cid; d.id = spell_id_of(cid); d.components = kComponentV; d.range_ft = 60; d.kind = k;
    return d;
}
}

TEST_CASE("auto_hit distribution: single and distinct") {
    auto single = test_def("test.spell.single", SpellEffectKind::AutoHit);
    single.level = 1; single.darts = 2; single.dice = {1, 4, 0}; single.distribution = DartDistribution::Single;
    auto distinct = test_def("test.spell.distinct", SpellEffectKind::AutoHit);
    distinct.level = 1; distinct.darts = 2; distinct.dice = {1, 4, 0}; distinct.distribution = DartDistribution::Distinct;
    EncounterHooks h = spell_hooks();
    h.spells = book_with({single, distinct});
    auto w = wizard(1); w.spells_known = {single.id, distinct.id};
    auto s = begin_encounter({w, tough_goblin(2), tough_goblin(3)}, 3, nullptr, h);
    end_turns_until(s, 1);
    auto a = cast(1, single.id, 2, 1); a.targets = {2, 3};
    CHECK_FALSE(step(s, a).ok);
    auto b = cast(1, distinct.id, 2, 1);
    CHECK_FALSE(step(s, b).ok);                       // needs one creature per dart
    b.targets = {2, 2};
    CHECK_FALSE(step(s, b).ok);                       // ... all different
    b.targets = {2, 3};
    auto r = step(s, b);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::DamageDealt) == 2);
}

TEST_CASE("Magic Missile is simultaneous: every target's Shield prompt comes before any damage") {
    auto s = begin_encounter({wizard(1), wizard(2, 1), wizard(3, 1)}, 5, nullptr, spell_hooks());
    for (CombatantId id : {2u, 3u}) {
        set_policy(mut(s, id), ReactionTrigger::BeingHit, ReactionPolicy::Never);
        set_policy(mut(s, id), ReactionTrigger::SpellCast, ReactionPolicy::Never);
    }
    set_policy(mut(s, 3), ReactionTrigger::BeingHit, ReactionPolicy::Auto);
    set_policy(mut(s, 2), ReactionTrigger::BeingHit, ReactionPolicy::Auto);
    end_turns_until(s, 1);
    auto r = cast(1, kMagicMissile, 2, 1); r.targets = {2, 3, 2};
    auto res = step(s, r);
    REQUIRE(res.status == StepStatus::Done);
    std::size_t last_shield = 0, first_damage = res.events.size();
    for (std::size_t i = 0; i < res.events.size(); ++i) {
        if (res.events[i].kind == EventKind::SpellCast && res.events[i].a == int32_t(kShield)) last_shield = i;
        if (res.events[i].kind == EventKind::DamageDealt && first_damage == res.events.size()) first_damage = i;
    }
    CHECK(count(res.events, EventKind::DamageDealt) == 0);   // both shielded
    CHECK(last_shield > 0);
    CHECK(first_damage == res.events.size());
}

TEST_CASE("Shield-like spells are offered against Magic Missile only via or_targeted_by") {
    auto plain = test_def("test.spell.plain_shield", SpellEffectKind::AcBonus);
    plain.level = 1; plain.casting_time = CastingTime::Reaction; plain.reaction_trigger = ReactionTrigger::BeingHit;
    plain.ac_bonus = 5; plain.duration = SpellDuration::UntilStartOfCasterTurn;   // no or_targeted_by
    EncounterHooks h = spell_hooks();
    h.spells = book_with({plain});
    auto target = wizard(2, 1); target.spells_known = {plain.id};
    auto s = begin_encounter({wizard(1), target}, 5, nullptr, h);
    end_turns_until(s, 1);
    auto res = step(s, cast(1, kMagicMissile, 2, 1));
    REQUIRE(res.status == StepStatus::Done);           // no prompt: not triggered by being targeted
    CHECK(count(res.events, EventKind::ReactionWindowOpened) == 0);
    CHECK(count(res.events, EventKind::DamageDealt) == 3);
}

TEST_CASE("multi-beam spell attack: cantrip growth adds beams; one target or one per beam") {
    auto beam = test_def("test.spell.beam", SpellEffectKind::SpellAttack);
    beam.level = 0; beam.dice = {1, 10, 0}; beam.damage_type = DamageType::Force; beam.beams = 1;
    beam.cantrip_scales = true; beam.cantrip_adds_beams = true;
    CHECK(scaled_beams(beam, 0, 4) == 1);
    CHECK(scaled_beams(beam, 0, 11) == 3);
    CHECK(scaled_dice(beam, 0, 11).count == 1);        // beams instead of dice
    EncounterHooks h = spell_hooks();
    h.spells = book_with({beam});
    auto w = wizard(1); w.spells_known = {beam.id}; w.caster_level = 5;   // 2 beams
    auto s = begin_encounter({w, tough_goblin(2), tough_goblin(3)}, 5, nullptr, h);
    end_turns_until(s, 1);
    auto bad = cast(1, beam.id, 2); bad.targets = {2, 3, 2};
    CHECK_FALSE(step(s, bad).ok);
    auto ok = cast(1, beam.id, 2); ok.targets = {2, 3};
    auto r = step(s, ok);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::AttackResolved) == 2);
}

TEST_CASE("repeat save timing: start_of_turn and on_damage") {
    for (RepeatSave timing : {RepeatSave::StartOfTurn, RepeatSave::OnDamage}) {
        auto hold = test_def("test.spell.hold", SpellEffectKind::Save);
        hold.level = 1; hold.save_ability = Ability::Wis; hold.applies_condition = true;
        hold.condition = Condition::Restrained; hold.repeat_save = timing; hold.concentration = true;
        hold.duration = SpellDuration::Minutes; hold.duration_amount = 1;
        EncounterHooks h = spell_hooks();
        h.spells = book_with({hold});
        int checked = 0;
        for (uint64_t seed = 0; seed < 60 && checked < 3; ++seed) {
            auto w = wizard(1); w.spells_known = {hold.id, kFireBolt};
            auto s = begin_encounter({w, tough_goblin(2)}, seed, nullptr, h);
            end_turns_until(s, 1);
            step(s, cast(1, hold.id, 2, 1));
            if (s.effects.empty()) continue;
            std::vector<Event> ev;
            if (timing == RepeatSave::StartOfTurn) {
                ev = step(s, {1, ActionKind::EndTurn}).events;   // goblin's turn starts
            } else {
                end_turns_until(s, 2); end_turns_until(s, 1);
                if (s.effects.empty()) continue;
                ev = step(s, cast(1, kFireBolt, 2)).events;      // damage (if it hits) triggers the save
                if (count(ev, EventKind::DamageDealt) == 0) continue;
            }
            int saves = 0;
            for (const auto& e : ev) saves += e.kind == EventKind::SavingThrowRolled && e.actor == 2 && e.target == CombatantId(Ability::Wis);
            CHECK(saves == 1);
            ++checked;
        }
        CHECK(checked > 0);
    }
}

TEST_CASE("Counterspell requires sight (hooks.can_see); long casting times are rejected") {
    EncounterHooks h = spell_hooks();
    h.can_see = [](const EncounterState&, const Combatant&, const Combatant&) { return false; };
    auto s = begin_encounter({wizard(1), wizard(2, 1)}, 4, nullptr, h);
    end_turns_until(s, 1);
    set_policy(mut(s, 2), ReactionTrigger::BeingHit, ReactionPolicy::Never);
    auto r = step(s, cast(1, kMagicMissile, 2, 1));
    REQUIRE(r.status == StepStatus::Done);
    CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);   // can't see the caster: no Counterspell

    auto ritual = test_def("test.spell.slow", SpellEffectKind::Heal);
    ritual.level = 1; ritual.casting_time = CastingTime::Minute; ritual.casting_amount = 1; ritual.dice = {1, 4, 0};
    EncounterHooks h2 = spell_hooks();
    h2.spells = book_with({ritual});
    auto c = cleric(1); c.spells_known = {ritual.id};
    auto t = begin_encounter({c, monster(2)}, 4, nullptr, h2);
    end_turns_until(t, 1);
    CHECK(step(t, cast(1, ritual.id, 1, 1)).error == "casting time too long for an encounter");
    for (const auto& a : legal_actions(t)) CHECK(a.spell_id != ritual.id);
}
