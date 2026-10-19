// Row 9b: reduce_to_zero (Rampage bonus_attack), drop_to_zero (Undead Fortitude set_hp + save predicate),
// death (Death Burst area_save), turn_start / turn_end, damage_dealt; heal_self and temp_hp riders.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBurst = spell_id_of("test.bomber#death_burst");

Predicate pred(PredicateKind k, int a = 0, int b = 0, uint32_t mask = 0) {
    Predicate p; p.kind = k; p.a = int16_t(a); p.b = int16_t(b); p.mask = mask; return p;
}
RiderEffect eff(RiderKind k, int value = 0, DiceExpr dice = {0, 0, 0}, uint32_t spell = 0) {
    RiderEffect e; e.kind = k; e.value = int16_t(value); e.dice = dice; e.spell_id = spell; return e;
}
Trigger trig(uint32_t id, TriggerOn on, std::vector<Predicate> when, std::vector<RiderEffect> effects,
             TriggerLimit lim = TriggerLimit::None) {
    Trigger t; t.id = id; t.on = on; t.limit = lim; t.when = std::move(when); t.effects = std::move(effects); return t;
}

Combatant striker(CombatantId id, std::vector<Trigger> triggers = {}) {
    auto c = pc(id, 50);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true},
                 Attack{"bite", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    c.triggers = std::move(triggers);
    return c;
}
Combatant target(CombatantId id, int hp = 500) {
    auto m = monster(id);
    m.hp = m.hp_max = int16_t(hp);
    m.ac = 10;
    m.reaction_policy.fill(ReactionPolicy::Never);
    return m;
}
EncounterHooks hooks_with(std::vector<SpellDef> extra = {}) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    for (auto& d : extra) book->add(d);
    h.spells = book;
    h.area_targets = [](const EncounterState& s, const Combatant& src, const SpellDef&) {
        std::vector<CombatantId> ids;
        for (const auto& c : s.combatants) if (c.id != src.id) ids.push_back(c.id);
        return ids;
    };
    return h;
}
SpellDef burst(DiceExpr dice = {2, 6, 0}) {
    SpellDef d; d.id = kBurst; d.content_id = "test.bomber#death_burst"; d.level = 0; d.range_ft = 10;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 30; d.on_save = OnSave::Half;
    d.dice = dice; d.damage_type = DamageType::Fire; d.is_spell = false; d.magical = false;
    return d;
}
ActionRequest stab(CombatantId a, CombatantId t, uint8_t idx = 0) {
    ActionRequest r{a, ActionKind::Attack, t, idx, 0, false, true};
    return r;
}
ActionRequest bonus_stab(CombatantId a, CombatantId t, uint8_t idx) {
    ActionRequest r{a, ActionKind::Attack, t, idx, 0, true, true};
    return r;
}
ActionRequest end_turn(CombatantId a) { return ActionRequest{a, ActionKind::EndTurn}; }
int idx_of(const std::vector<Event>& ev, EventKind k, int from = 0) {
    for (int i = from; i < int(ev.size()); ++i) if (ev[std::size_t(i)].kind == k) return i;
    return -1;
}
bool landed(const StepResult& r) {
    const int i = idx_of(r.events, EventKind::AttackResolved);
    return i >= 0 && (r.events[std::size_t(i)].c & 1);
}
bool crit_of(const StepResult& r) {
    const int i = idx_of(r.events, EventKind::AttackResolved);
    return i >= 0 && (r.events[std::size_t(i)].c & 2);
}
int fts(const std::vector<Event>& ev, uint32_t id) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::FeatureTriggered && e.a == int32_t(id)) ++n;
    return n;
}
}  // namespace

TEST_CASE("Rampage: reduce_to_zero grants a bonus-action attack, once, on the owner's own turn") {
    const uint32_t id = 11;
    auto mk = [&] {
        return trig(id, TriggerOn::ReduceToZero, {pred(PredicateKind::Weapon, int(WeaponIs::Melee))},
                    {eff(RiderKind::BonusAttack, 1)});
    };
    int granted = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto s = begin_encounter({striker(1, {mk()}), target(2), target(3)}, seed, nullptr, hooks_with());
        end_turns_until(s, 1);
        s.combatants[1].hp = 1;
        // No grant yet: a bonus-action attack is refused.
        CHECK_FALSE(step(s, bonus_stab(1, 3, 1)).ok);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        if (!landed(r)) continue;
        CHECK(fts(r.events, id) == 1);
        const Event& ft = r.events[std::size_t(idx_of(r.events, EventKind::FeatureTriggered))];
        CHECK(ft.actor == 1); CHECK(ft.target == 2); CHECK(ft.b == int32_t(TriggerOn::ReduceToZero)); CHECK(ft.c == 1);
        CHECK(s.budget.bonus_attack_grant == 2);
        CHECK(s.combatants[1].dead);
        CHECK_FALSE(step(s, bonus_stab(1, 3, 0)).ok);   // wrong attack
        auto b = step(s, bonus_stab(1, 3, 1));
        REQUIRE(b.ok);
        CHECK(idx_of(b.events, EventKind::AttackResolved) >= 0);
        CHECK_FALSE(s.budget.bonus_action);
        CHECK(s.budget.bonus_attack_grant == 0);
        CHECK_FALSE(step(s, bonus_stab(1, 3, 1)).ok);   // spent
        ++granted;
    }
    CHECK(granted > 0);
}

TEST_CASE("Rampage: needs a kill, the owner's turn and a free bonus action") {
    auto mk = [&] { return trig(12, TriggerOn::ReduceToZero, {}, {eff(RiderKind::BonusAttack, 1)}); };
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto s = begin_encounter({striker(1, {mk()}), target(2), target(3)}, seed, nullptr, hooks_with());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));   // 500 hp: not a kill
        CHECK(fts(r.events, 12) == 0);
        CHECK(s.budget.bonus_attack_grant == 0);
        auto s2 = begin_encounter({striker(1, {mk()}), target(2), target(3)}, seed, nullptr, hooks_with());
        end_turns_until(s2, 1);
        s2.combatants[1].hp = 1;
        s2.budget.bonus_action = false;
        CHECK(fts(step(s2, stab(1, 2)).events, 12) == 0);
        auto s3 = begin_encounter({striker(1, {trig(12, TriggerOn::ReduceToZero, {}, {eff(RiderKind::BonusAttack, 7)})}), target(2), target(3)},
                                  seed, nullptr, hooks_with());
        end_turns_until(s3, 1);
        s3.combatants[1].hp = 1;
        CHECK(fts(step(s3, stab(1, 2)).events, 12) == 0);   // bad attack index
    }
}

TEST_CASE("Undead Fortitude: drop_to_zero + Con save (DC 5 + damage) sets hp to 1") {
    const uint32_t id = 21;
    auto zombie = [&](int con, int base, uint32_t type_mask, bool not_crit) {
        auto z = target(2, 5);
        z.creature_type = CreatureType::Undead;
        z.abilities.score[std::size_t(Ability::Con)] = uint8_t(con);
        std::vector<Predicate> when{pred(PredicateKind::DamageTypeNot, 0, 0, type_mask)};
        if (not_crit) when.push_back(pred(PredicateKind::NotCrit));
        when.push_back(pred(PredicateKind::Save, int(Ability::Con), base, 1u));
        z.triggers = {trig(id, TriggerOn::DropToZero, when, {eff(RiderKind::SetHp, 1)})};
        z.hp = 1;
        return z;
    };
    int saved = 0, failed = 0, crits = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        // Always passes: +10 against a DC of 1..4 (a non-crit d4).
        auto s = begin_encounter({striker(1), zombie(30, 0, bit(DamageType::Radiant), true)}, seed, nullptr, hooks_with());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        if (!landed(r)) continue;
        if (crit_of(r)) {
            ++crits;
            CHECK(fts(r.events, id) == 0);          // not_crit: no trigger, the zombie falls
            CHECK(s.combatants[1].dead);
            continue;
        }
        ++saved;
        CHECK(fts(r.events, id) == 1);
        CHECK_FALSE(s.combatants[1].dead);
        CHECK(s.combatants[1].hp == 1);
        const int sv = idx_of(r.events, EventKind::SavingThrowRolled);
        const int ft = idx_of(r.events, EventKind::FeatureTriggered);
        const int dd = idx_of(r.events, EventKind::DamageDealt);
        CHECK(sv >= 0); CHECK(sv < ft); CHECK(ft < dd);
        CHECK(r.events[std::size_t(sv)].actor == 2);
        CHECK(r.events[std::size_t(ft)].actor == 2); CHECK(r.events[std::size_t(ft)].c == 1);
        CHECK(r.events[std::size_t(dd)].b == 1);    // DamageDealt reports hp_after = 1
        CHECK(idx_of(r.events, EventKind::CombatantDied) < 0);
        // Always fails: Con 1 (-5) against DC 30 + damage.
        auto s2 = begin_encounter({striker(1), zombie(1, 30, bit(DamageType::Radiant), true)}, seed, nullptr, hooks_with());
        end_turns_until(s2, 1);
        auto r2 = step(s2, stab(1, 2));
        CHECK(fts(r2.events, id) == 1);
        const Event& fe = r2.events[std::size_t(idx_of(r2.events, EventKind::FeatureTriggered))];
        CHECK(fe.c == 3);
        CHECK(s2.combatants[1].dead);
        ++failed;
    }
    CHECK(saved > 0); CHECK(failed > 0); CHECK(crits > 0);
}

TEST_CASE("Undead Fortitude: radiant damage and a drop that does not reach zero are not saved") {
    auto zombie = [&](int hp) {
        auto z = target(2, 5);
        z.abilities.score[std::size_t(Ability::Con)] = 30;
        z.triggers = {trig(22, TriggerOn::DropToZero, {pred(PredicateKind::DamageTypeNot, 0, 0, bit(DamageType::Radiant)),
                                                       pred(PredicateKind::Save, int(Ability::Con), 0, 1u)},
                           {eff(RiderKind::SetHp, 1)})};
        z.hp = int16_t(hp);
        return z;
    };
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto atk = striker(1);
        atk.attacks[0].type = DamageType::Radiant;
        auto s = begin_encounter({atk, zombie(1)}, seed, nullptr, hooks_with());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (landed(r)) { CHECK(fts(r.events, 22) == 0); CHECK(s.combatants[1].dead); CHECK(idx_of(r.events, EventKind::SavingThrowRolled) < 0); }
        auto s2 = begin_encounter({striker(1), zombie(5)}, seed, nullptr, hooks_with());
        end_turns_until(s2, 1);
        s2.combatants[1].hp = 500; s2.combatants[1].hp_max = 500;
        auto r2 = step(s2, stab(1, 2));
        CHECK(fts(r2.events, 22) == 0);
        CHECK(idx_of(r2.events, EventKind::SavingThrowRolled) < 0);
    }
}

TEST_CASE("Death Burst: death trigger runs an area_save through hooks.area_targets, once") {
    auto bomber = [&](CombatantId id) {
        auto b = target(id, 1);
        b.triggers = {trig(31, TriggerOn::Death, {}, {eff(RiderKind::AreaSave, 0, {0, 0, 0}, kBurst)})};
        return b;
    };
    int done = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto s = begin_encounter({striker(1), bomber(2), target(3)}, seed, nullptr, hooks_with({burst()}));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        if (!landed(r)) continue;
        ++done;
        CHECK(s.combatants[1].dead);
        const int died = idx_of(r.events, EventKind::CombatantDied);
        const int ft = idx_of(r.events, EventKind::FeatureTriggered);
        REQUIRE(died >= 0); REQUIRE(ft > died);
        CHECK(r.events[std::size_t(ft)].actor == 2); CHECK(r.events[std::size_t(ft)].b == int32_t(TriggerOn::Death));
        // Both survivors save (and fail against DC 30): two save rolls after the trigger.
        int saves = 0;
        for (int i = ft; i < int(r.events.size()); ++i)
            if (r.events[std::size_t(i)].kind == EventKind::SavingThrowRolled) ++saves;
        CHECK(saves == 2);
        CHECK(s.combatants[0].hp < 50);
        CHECK(s.combatants[2].hp < 500);
        // The death is processed once.
        CHECK(fts(step(s, end_turn(1)).events, 31) == 0);
        CHECK(s.combatants[1].death_triggers_done);
    }
    CHECK(done > 0);
    // Without hooks.area_targets (or nobody inside) it does not fire.
    auto h = hooks_with({burst()});
    h.area_targets = nullptr;
    for (uint64_t seed = 0; seed < 10; ++seed) {
        auto s = begin_encounter({striker(1), bomber(2), target(3)}, seed, nullptr, h);
        end_turns_until(s, 1);
        CHECK(fts(step(s, stab(1, 2)).events, 31) == 0);
    }
}

TEST_CASE("Death Burst chain: one burst killing another bomber triggers its burst, each exactly once") {
    auto bomber = [&](CombatantId id, uint32_t tid) {
        auto b = target(id, 1);
        b.triggers = {trig(tid, TriggerOn::Death, {}, {eff(RiderKind::AreaSave, 0, {0, 0, 0}, kBurst)})};
        return b;
    };
    int done = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto s = begin_encounter({striker(1), bomber(2, 41), bomber(3, 42)}, seed, nullptr, hooks_with({burst({4, 6, 0})}));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (!landed(r)) continue;
        ++done;
        CHECK(fts(r.events, 41) == 1);
        CHECK(fts(r.events, 42) == 1);   // the 4d6 burst (>= 4) kills the 1 hp bomber
        CHECK(s.combatants[2].dead);
    }
    CHECK(done > 0);
}

TEST_CASE("turn_start / turn_end: heal_self and temp_hp riders, limits, no stacking") {
    auto regen = [&] {
        auto t = target(2, 40);
        t.hp = 10;
        t.triggers = {trig(51, TriggerOn::TurnStart, {}, {eff(RiderKind::HealSelf, 3)}),
                      trig(52, TriggerOn::TurnEnd, {}, {eff(RiderKind::TempHp, 0, {1, 1, 4})}, TriggerLimit::OncePerTurn)};
        return t;
    };
    auto s = begin_encounter({striker(1), regen()}, 6, nullptr, hooks_with());
    end_turns_until(s, 1);
    auto r = step(s, end_turn(1));   // the regenerator's turn starts
    REQUIRE(r.ok);
    CHECK(fts(r.events, 51) == 1);
    const int started = idx_of(r.events, EventKind::TurnStarted);
    const int ft = idx_of(r.events, EventKind::FeatureTriggered);
    CHECK(started >= 0); CHECK(ft > started);
    CHECK(s.combatants[1].hp == 13);
    const int healed = idx_of(r.events, EventKind::Healed);
    CHECK(healed > ft);
    CHECK(r.events[std::size_t(healed)].actor == 2);
    CHECK(r.events[std::size_t(healed)].a == 3);
    // turn_end: temp hp 1d1+4 = 5, then a larger pool is kept.
    auto e = step(s, end_turn(2));
    REQUIRE(e.ok);
    CHECK(fts(e.events, 52) == 1);
    CHECK(idx_of(e.events, EventKind::FeatureTriggered) < idx_of(e.events, EventKind::TurnEnded));
    CHECK(s.combatants[1].temp_hp == 5);
    end_turns_until(s, 2);
    s.combatants[1].temp_hp = 9;
    CHECK(fts(step(s, end_turn(2)).events, 52) == 1);
    CHECK(s.combatants[1].temp_hp == 9);
}

TEST_CASE("turn triggers do not fire for the dead and heal_self respects hp max") {
    auto t = target(2, 40);
    t.hp = 39;
    t.triggers = {trig(53, TriggerOn::TurnStart, {}, {eff(RiderKind::HealSelf, 10)})};
    auto s = begin_encounter({striker(1), t}, 6, nullptr, hooks_with());
    end_turns_until(s, 1);
    (void)step(s, end_turn(1));
    CHECK(s.combatants[1].hp == 40);
    auto d = target(2, 40);
    d.triggers = {trig(53, TriggerOn::TurnStart, {}, {eff(RiderKind::HealSelf, 10)})};
    d.dead = true; d.hp = 0;
    auto s2 = begin_encounter({striker(1), d, target(3)}, 6, nullptr, hooks_with());
    for (int i = 0; i < 6; ++i) CHECK(fts(step(s2, end_turn(s2.order[s2.turn_index].id)).events, 53) == 0);
}

TEST_CASE("damage_dealt: fires after the damage with the type predicate; heal_self on the owner") {
    auto mk = [&](uint32_t mask) {
        return trig(61, TriggerOn::DamageDealt, {pred(PredicateKind::DamageTypeIs, 0, 0, mask)}, {eff(RiderKind::HealSelf, 0, {1, 1, 1})});
    };
    int fired = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto a = striker(1, {mk(bit(DamageType::Piercing))});
        a.hp = 10;
        auto s = begin_encounter({a, target(2)}, seed, nullptr, hooks_with());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (!landed(r)) { CHECK(fts(r.events, 61) == 0); continue; }
        ++fired;
        CHECK(fts(r.events, 61) == 1);
        CHECK(idx_of(r.events, EventKind::DamageDealt) < idx_of(r.events, EventKind::FeatureTriggered));
        CHECK(s.combatants[0].hp == 12);   // 1d1 + 1
        // wrong type: no trigger
        auto b = striker(1, {mk(bit(DamageType::Fire))});
        b.hp = 10;
        auto s2 = begin_encounter({b, target(2)}, seed, nullptr, hooks_with());
        end_turns_until(s2, 1);
        CHECK(fts(step(s2, stab(1, 2)).events, 61) == 0);
        // an immune target takes no damage: nothing is dealt
        auto c = striker(1, {mk(bit(DamageType::Piercing))});
        auto im = target(2); im.immunities = bit(DamageType::Piercing);
        auto s3 = begin_encounter({c, im}, seed, nullptr, hooks_with());
        end_turns_until(s3, 1);
        CHECK(fts(step(s3, stab(1, 2)).events, 61) == 0);
    }
    CHECK(fired > 0);
}

TEST_CASE("spell damage counts for damage_dealt, weapon predicates do not") {
    auto w = wizard(1);
    w.triggers = {trig(62, TriggerOn::DamageDealt, {pred(PredicateKind::Weapon, int(WeaponIs::Melee))}, {eff(RiderKind::TempHp, 4)}),
                  trig(63, TriggerOn::DamageDealt, {}, {eff(RiderKind::TempHp, 6)})};
    auto s = begin_encounter({w, target(2)}, 3, nullptr, hooks_with());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kFireBolt, 2));
    REQUIRE(r.ok);
    if (idx_of(r.events, EventKind::DamageDealt) >= 0) {
        CHECK(fts(r.events, 62) == 0);
        CHECK(fts(r.events, 63) == 1);
        CHECK(s.combatants[0].temp_hp == 6);
    }
}

TEST_CASE("reserved combinations never fire: hit-only kinds on other events, bad data") {
    auto fire = [&](Trigger t, bool kill) {
        auto a = striker(1, {t});
        auto s = begin_encounter({a, target(2, kill ? 1 : 500)}, 3, nullptr, hooks_with());
        end_turns_until(s, 1);
        return fts(step(s, stab(1, 2)).events, t.id);
    };
    CHECK(fire(trig(71, TriggerOn::ReduceToZero, {}, {eff(RiderKind::ExtraDamage, 0, {1, 6, 0})}), true) == 0);
    CHECK(fire(trig(72, TriggerOn::DamageDealt, {}, {eff(RiderKind::SetHp, 1)}), false) == 0);
    CHECK(fire(trig(73, TriggerOn::DamageDealt, {}, {eff(RiderKind::AreaSave, 0, {0, 0, 0}, kBurst)}), false) == 0);   // no hook, no spell
    CHECK(fire(trig(74, TriggerOn::DamageDealt, {}, {eff(RiderKind::SaveCondition), eff(RiderKind::Spend)}), false) == 0);
    Trigger ask = trig(75, TriggerOn::DamageDealt, {}, {eff(RiderKind::HealSelf, 1)});
    ask.choice = 1;
    CHECK(fire(ask, false) == 0);
}

TEST_CASE("depth guard: a damage_dealt area_save that damages its own owner's targets stops after 4 levels") {
    auto a = striker(1, {trig(81, TriggerOn::DamageDealt, {}, {eff(RiderKind::AreaSave, 0, {0, 0, 0}, kBurst)})});
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto s = begin_encounter({a, target(2)}, seed, nullptr, hooks_with({burst({1, 4, 0})}));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        if (landed(r)) { CHECK(fts(r.events, 81) == 4); break; }
    }
}

TEST_CASE("no triggers: 9b hooks add no events and no rng draws") {
    auto run = [&](bool with) {
        auto a = striker(1, with ? std::vector<Trigger>{trig(91, TriggerOn::TurnStart, {pred(PredicateKind::SelfHpBelow, 1, 100)}, {eff(RiderKind::HealSelf, 1)})}
                                 : std::vector<Trigger>{});
        auto s = begin_encounter({a, target(2, 1)}, 7, nullptr, hooks_with());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        (void)step(s, end_turn(1));
        return std::make_pair(s.rng, r.events.size());
    };
    auto x = run(false), y = run(true);
    CHECK(x.second == y.second);
    CHECK(x.first.d(1000) == y.first.d(1000));
}
