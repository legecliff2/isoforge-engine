// R-G16: surprise, Hit Dice, short / long rest, Arcane / Natural Recovery.
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
EncounterHooks in_range_auto() {
    EncounterHooks h;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&,
                           std::span<const ReactionOption>) { return ReactionDecision{0, true, 0}; };
    return h;
}
// A seed where `first` acts first in initiative.
EncounterState start_with_first(std::vector<Combatant> cs, CombatantId first, const EncounterHooks& h = {}) {
    for (uint64_t seed = 1; seed < 500; ++seed) {
        auto s = begin_encounter(cs, seed, nullptr, h);
        if (current_actor(s) == first) return s;
    }
    return begin_encounter(cs, 1, nullptr, h);
}
Combatant fighter(CombatantId id, int hp_max, int con) {
    auto c = pc(id, hp_max);
    c.abilities.score[std::size_t(Ability::Con)] = int8_t(con);
    c.hit_die = 10; c.hit_dice = 5; c.hit_dice_max = 5;
    return c;
}
}  // namespace

TEST_CASE("defaults and passive Perception") {
    Combatant c{};
    CHECK_FALSE(c.surprised); CHECK(c.hit_die == 0); CHECK(c.hit_dice == 0); CHECK(c.hit_dice_max == 0);
    auto p = pc(1);
    p.abilities.score[std::size_t(Ability::Wis)] = int8_t(14);
    CHECK(passive_perception(p) == 12);
    p.skill_proficiencies |= bit(Skill::Perception);
    CHECK(passive_perception(p) == 14);
    CHECK(passive_perception(p, 5) == 19);
    CHECK(recovery_budget(1) == 1); CHECK(recovery_budget(5) == 3); CHECK(recovery_budget(6) == 3);
}

TEST_CASE("Tier-A §25: printed passive Perception override, Wis delta, caller bonus, surprise") {
    auto m = monster(2);
    m.abilities.score[std::size_t(Ability::Wis)] = int8_t(13);
    m.skill_proficiencies |= bit(Skill::Perception);
    m.proficiency_bonus = 2;
    CHECK(passive_perception(m) == 13);                  // derived 10 + 3
    m.passive_perception_override = 10;                  // SRD blink dog prints 10
    CHECK(passive_perception(m) == 10);
    CHECK(passive_perception(m, -5) == 5);               // the dim-light -5 still applies
    m.abilities.score[std::size_t(Ability::Wis)] = int8_t(9);   // Wis 13 reduced by 4: mod +1 -> -1
    m.ability_reduction[std::size_t(Ability::Wis)] = 4;
    CHECK(passive_perception(m) == 8);                   // the override shifts by the same delta (-2)
    m.passive_perception_override = -1;
    CHECK(passive_perception(m) == 11);                  // derived: 10 + (-1 + 2)
    auto a = pc(1);
    auto o = monster(3); o.passive_perception_override = 10;
    // a Stealth 11 hider: the override (10) is beaten, a derived 13 would not be
    CHECK(surprised_by_stealth({a, o}, {{1, 11}}) == std::vector<CombatantId>{3});
    CHECK(Combatant{}.passive_perception_override == -1);
}

TEST_CASE("surprised_by_stealth: surprised only when every living enemy is hidden from it") {
    auto a = pc(1); auto b = pc(3);
    b.abilities.score[std::size_t(Ability::Wis)] = int8_t(20);   // passive 15
    auto m1 = monster(2); auto m2 = monster(4);
    const std::vector<Combatant> cs{a, b, m1, m2};
    // monsters hide with 12 and 14: a (passive 10) is surprised, b (15) is not; the monsters (nobody on team 0 hides) are not
    CHECK(surprised_by_stealth(cs, {{2, 12}, {4, 14}}) == std::vector<CombatantId>{1});
    CHECK(surprised_by_stealth(cs, {{2, 15}, {4, 15}}) == std::vector<CombatantId>{1, 3});   // ties go to the hider
    CHECK(surprised_by_stealth(cs, {{2, 30}}).empty());          // monster 4 is not hiding: noticed
    auto dead = cs; dead[3].dead = true;
    CHECK(surprised_by_stealth(dead, {{2, 30}}) == std::vector<CombatantId>{1, 3});   // a dead enemy is no threat
    CHECK(surprised_by_stealth(cs, {}).empty());
    auto v = cs;
    apply_surprise(v, {1, 99});
    CHECK(v[0].surprised); CHECK_FALSE(v[1].surprised); CHECK_FALSE(v[2].surprised);
}

TEST_CASE("surprise: the first turn can only end, then the creature acts normally") {
    auto cs = std::vector<Combatant>{pc(1), monster(2)};
    apply_surprise(cs, {1});
    auto s = start_with_first(cs, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK(has_surprised(s));
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5;
    CHECK_FALSE(step(s, mv).ok);
    CHECK_FALSE(step(s, ActionRequest{1, ActionKind::Attack, 2, 0}).ok);
    CHECK_FALSE(step(s, ActionRequest{1, ActionKind::Dodge}).ok);
    ActionRequest dis{1, ActionKind::Disengage}; dis.as_bonus_action = true;
    CHECK_FALSE(step(s, dis).ok);
    const auto legal = legal_actions(s);
    REQUIRE(legal.size() == 1);
    CHECK(legal[0].kind == ActionKind::EndTurn);
    REQUIRE(step(s, ActionRequest{1, ActionKind::EndTurn}).ok);
    CHECK_FALSE(find(s, 1)->surprised);
    CHECK_FALSE(has_surprised(s));
    end_turns_until(s, 1);
    CHECK(step(s, ActionRequest{1, ActionKind::Dodge}).ok);
}

TEST_CASE("surprise: no reactions until the surprised creature's turn ends") {
    auto cs = std::vector<Combatant>{pc(1), monster(2)};
    cs[1].hp = cs[1].hp_max = 200;
    apply_surprise(cs, {1});
    auto s = start_with_first(cs, 2, in_range_auto());
    REQUIRE(current_actor(s) == 2);
    ActionRequest leave{2, ActionKind::Move}; leave.move_ft = 5; leave.leaves_reach = true;
    const auto r = step(s, leave);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);
    CHECK(count(r.events, EventKind::AttackResolved) == 0);
    CHECK_FALSE(find(s, 1)->reaction_used);
    end_turns_until(s, 1);
    REQUIRE(step(s, ActionRequest{1, ActionKind::EndTurn}).ok);   // surprise over
    end_turns_until(s, 2);
    const auto q = step(s, leave);
    REQUIRE(q.ok);
    CHECK(count(q.events, EventKind::ReactionTaken) == 1);
}

TEST_CASE("surprised_digest: unchanged seed without surprise, changes with it") {
    const auto s0 = begin_encounter({pc(1), monster(2)}, 3);
    CHECK_FALSE(has_surprised(s0));
    CHECK(surprised_digest(s0) == 0xCBF29CE484222325ull);
    auto cs = std::vector<Combatant>{pc(1), monster(2)};
    apply_surprise(cs, {2});
    const auto s1 = begin_encounter(cs, 3);
    CHECK(surprised_digest(s1) != surprised_digest(s0));
    CHECK(s1.order[0].id == s0.order[0].id);   // surprise draws no rng
}

TEST_CASE("spend_hit_dice: die + Con modifier each, stops at max HP, deterministic") {
    auto c = fighter(1, 60, 14);   // +2
    c.hp = 10;
    rng::Rng r1(7, 0);
    const auto h = spend_hit_dice(c, r1, 2);
    CHECK(h.spent == 2);
    REQUIRE(h.gains.size() == 2);
    for (int16_t g : h.gains) { CHECK(g >= 3); CHECK(g <= 12); }
    CHECK(h.healed == h.gains[0] + h.gains[1]);
    CHECK(c.hp == 10 + h.healed);
    CHECK(c.hit_dice == 3);
    auto d = fighter(1, 60, 14); d.hp = 10;
    rng::Rng r2(7, 0);
    CHECK(spend_hit_dice(d, r2, 2).healed == h.healed);
    // full HP: no die is spent
    auto f = fighter(1, 20, 14);
    rng::Rng r3(1, 0);
    CHECK(spend_hit_dice(f, r3, 3).spent == 0);
    CHECK(f.hit_dice == 5);
    // almost full: one die, capped
    f.hp = 19;
    const auto one = spend_hit_dice(f, r3, 3);
    CHECK(one.spent == 1); CHECK(one.healed == 1); CHECK(f.hp == 20);
    // no dice left / no hit die / dead
    auto e = fighter(1, 60, 14); e.hp = 1; e.hit_dice = 0;
    CHECK(spend_hit_dice(e, r3, 2).spent == 0);
    e.hit_dice = 2; e.hit_die = 0;
    CHECK(spend_hit_dice(e, r3, 2).spent == 0);
    e.hit_die = 10; e.dead = true;
    CHECK(spend_hit_dice(e, r3, 2).spent == 0);
}

TEST_CASE("spend_hit_dice: a negative Con modifier gives at least 0 per die; from 0 HP the creature wakes") {
    auto c = fighter(1, 60, 3);   // -4
    c.hit_die = 6; c.hp = 30;
    rng::Rng r(11, 0);
    const auto h = spend_hit_dice(c, r, 5);
    CHECK(h.spent == 5);
    for (int16_t g : h.gains) { CHECK(g >= 0); CHECK(g <= 2); }
    auto d = fighter(1, 60, 14);
    make_dying(d); d.death = {1, 2, false};
    const auto w = spend_hit_dice(d, r, 1);
    CHECK(w.spent == 1);
    CHECK(d.hp >= 3);
    CHECK_FALSE(d.has(Condition::Unconscious));
    CHECK(d.death.failures == 0);
}

TEST_CASE("short_rest: Hit Dice, Pact slots, ShortRest features and pools of that actor, recharge actions") {
    auto c = fighter(1, 60, 14); c.hp = 20;
    c.pact_slot_level = 1; c.pact_slots_max = 1; c.pact_slots = 0;
    c.spell_slots_max[1] = 2; c.spell_slots[1] = 0;
    std::vector<FeatureUses> feats{{1, 10, 1, 1, FeatureRecharge::ShortRest}, {1, 11, 1, 1, FeatureRecharge::LongRest},
                                   {2, 10, 1, 1, FeatureRecharge::ShortRest}};
    std::vector<ResourcePool> pools{{1, 20, 4, 0, FeatureRecharge::ShortRest}, {1, 21, 3, 0, FeatureRecharge::LongRest}};
    std::vector<ActionUse> uses{{1, 30, true}};
    rng::Rng r(5, 0);
    const auto res = short_rest(c, r, 1, RestTables{&feats, &pools, &uses});
    CHECK(res.ok);
    CHECK(res.hit_dice.spent == 1);
    CHECK(c.hp == 20 + res.hit_dice.healed);
    CHECK(c.pact_slots == 1);
    CHECK(c.spell_slots[1] == 0);
    CHECK(feats[0].used == 0); CHECK(feats[1].used == 1); CHECK(feats[2].used == 1);
    CHECK(pools[0].cur == 4); CHECK(pools[1].cur == 0);
    CHECK_FALSE(uses[0].expended);
    auto dead = fighter(2, 30, 10); dead.dead = true;
    CHECK_FALSE(short_rest(dead, r, 1).ok);
}

TEST_CASE("long_rest: full HP, half Hit Dice back (min 1), slots, innate uses, all features and pools") {
    auto c = fighter(1, 40, 14);
    c.hp = 5; c.temp_hp = 6; c.hit_dice = 0;
    c.spell_slots_max[1] = 4; c.spell_slots_max[2] = 2; c.pact_slots_max = 2;
    c.innate = {InnateSpell{77, 3, 0}};
    std::vector<FeatureUses> feats{{1, 11, 1, 1, FeatureRecharge::LongRest}, {1, 12, 2, 2, FeatureRecharge::None}};
    std::vector<ResourcePool> pools{{1, 21, 3, 0, FeatureRecharge::LongRest}};
    const auto res = long_rest(c, RestTables{&feats, &pools, nullptr});
    CHECK(res.ok);
    CHECK(res.hp_regained == 35); CHECK(c.hp == 40); CHECK(c.temp_hp == 0);
    CHECK(res.hit_dice_regained == 2); CHECK(c.hit_dice == 2);   // 5 / 2
    CHECK(c.spell_slots[1] == 4); CHECK(c.spell_slots[2] == 2); CHECK(c.pact_slots == 2);
    CHECK(c.innate[0].uses_left == 3);
    CHECK(feats[0].used == 0); CHECK(feats[1].used == 2);
    CHECK(pools[0].cur == 3);
    CHECK_FALSE(res.exhaustion_reduced);
    // capped at the maximum; a 1-die creature regains 1
    c.hit_dice = 4;
    CHECK(long_rest(c).hit_dice_regained == 1); CHECK(c.hit_dice == 5);
    auto low = fighter(2, 10, 10); low.hit_dice_max = 1; low.hit_dice = 0;
    CHECK(long_rest(low).hit_dice_regained == 1);
    // needs at least 1 HP; dead creatures don't rest
    auto down = fighter(3, 10, 10); make_dying(down);
    CHECK_FALSE(long_rest(down).ok);
    CHECK(down.hp == 0);
}

TEST_CASE("long_rest: exhaustion -1 with food and drink, before HP is restored") {
    auto c = fighter(1, 40, 14);
    c.exhaustion_level = 4; c.hp = 10;   // max halved to 20
    const auto res = long_rest(c);
    CHECK(res.exhaustion_reduced);
    CHECK(c.exhaustion_level == 3);
    CHECK(c.hp == 40);
    auto d = fighter(2, 40, 14);
    d.exhaustion_level = 4; d.hp = 10;
    const auto no_food = long_rest(d, {}, false);
    CHECK_FALSE(no_food.exhaustion_reduced);
    CHECK(d.exhaustion_level == 4);
    CHECK(d.hp == 20);
}

TEST_CASE("recover_spell_slots (Arcane / Natural Recovery): budget, level limit, maximum") {
    auto w = pc(1);
    w.spell_slots_max = {0, 4, 3, 2, 0, 0, 0, 0, 0, 0};
    w.spell_slots = {0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    const int budget = recovery_budget(5);   // 3
    std::array<uint8_t, 10> two_seconds{}; two_seconds[2] = 2;   // 4 levels
    CHECK_FALSE(recover_spell_slots(w, two_seconds, budget));
    CHECK(w.spell_slots[2] == 1);
    std::array<uint8_t, 10> too_many{}; too_many[1] = 4;          // over the maximum of 4
    CHECK_FALSE(recover_spell_slots(w, too_many, 10));
    std::array<uint8_t, 10> sixth{}; sixth[6] = 1;
    w.spell_slots_max[6] = 1;
    CHECK_FALSE(recover_spell_slots(w, sixth, 10));
    std::array<uint8_t, 10> ok{}; ok[1] = 1; ok[2] = 1;            // 3 levels
    CHECK(recover_spell_slots(w, ok, budget));
    CHECK(w.spell_slots[1] == 2); CHECK(w.spell_slots[2] == 2);
    std::array<uint8_t, 10> third{}; third[3] = 1;
    CHECK(recover_spell_slots(w, third, budget));
    CHECK(w.spell_slots[3] == 1);
}

TEST_CASE("slot recovery feature (Arcane / Natural Recovery): budget rounding, level cap, once per day, long-rest reset") {
    CHECK(slot_recovery_budget(SlotRecoverySpec{1, 1}) == 1);
    CHECK(slot_recovery_budget(SlotRecoverySpec{1, 2}) == 1);
    CHECK(slot_recovery_budget(SlotRecoverySpec{1, 3}) == 2);
    CHECK(slot_recovery_budget(SlotRecoverySpec{1, 5}) == 3);
    CHECK(slot_recovery_budget(SlotRecoverySpec{1, 20}) == 10);

    auto w = pc(1);
    w.spell_slots_max = {0, 4, 3, 3, 3, 2, 1, 0, 0, 0};
    w.spell_slots = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const SlotRecoverySpec arcane{77, 12};   // budget 6, max level 5, 1 use
    std::vector<FeatureUses> rows{slot_recovery_uses(1, arcane)};
    CHECK(rows[0].recharge == FeatureRecharge::LongRest);
    CHECK(rows[0].max == 1);
    const RestTables tables{&rows, nullptr, nullptr};

    std::array<uint8_t, 10> none{};
    CHECK(apply_slot_recovery(w, arcane, rows, none) == SlotRecoveryResult::Empty);
    std::array<uint8_t, 10> sixth{}; sixth[6] = 1;
    CHECK(apply_slot_recovery(w, arcane, rows, sixth) == SlotRecoveryResult::LevelTooHigh);
    std::array<uint8_t, 10> over{}; over[5] = 1; over[2] = 1;   // 7 > 6
    CHECK(apply_slot_recovery(w, arcane, rows, over) == SlotRecoveryResult::OverBudget);
    std::array<uint8_t, 10> above{}; above[1] = 5;              // max 4
    CHECK(apply_slot_recovery(w, arcane, rows, above) == SlotRecoveryResult::AboveMax);
    const SlotRecoverySpec other{78, 12};
    CHECK(apply_slot_recovery(w, other, rows, none) == SlotRecoveryResult::NoFeature);
    CHECK(rows[0].used == 0);
    CHECK(w.spell_slots == std::array<uint8_t, 10>{});

    std::array<uint8_t, 10> ok{}; ok[5] = 1; ok[1] = 1;         // exactly 6
    CHECK(apply_slot_recovery(w, arcane, rows, ok) == SlotRecoveryResult::Ok);
    CHECK(w.spell_slots[5] == 1); CHECK(w.spell_slots[1] == 1); CHECK(rows[0].used == 1);
    std::array<uint8_t, 10> one{}; one[1] = 1;
    CHECK(apply_slot_recovery(w, arcane, rows, one) == SlotRecoveryResult::NoUsesLeft);   // once per day
    CHECK(w.spell_slots[1] == 1);

    rng::Rng rr(7, 2);
    REQUIRE(short_rest(w, rr, 0, tables).ok);
    CHECK(rows[0].used == 1);                                   // a short rest does not reset it
    CHECK(apply_slot_recovery(w, arcane, rows, one) == SlotRecoveryResult::NoUsesLeft);
    REQUIRE(long_rest(w, tables).ok);
    CHECK(rows[0].used == 0);                                   // long rest resets
    w.spell_slots[1] = 0;
    CHECK(apply_slot_recovery(w, arcane, rows, one) == SlotRecoveryResult::Ok);

    // Natural Recovery: druid 3 -> budget 2
    auto d = pc(2);
    d.spell_slots_max = {0, 4, 2, 0, 0, 0, 0, 0, 0, 0};
    d.spell_slots = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const SlotRecoverySpec natural{79, 3};
    rows.push_back(slot_recovery_uses(2, natural));
    std::array<uint8_t, 10> three{}; three[1] = 1; three[2] = 1;
    CHECK(apply_slot_recovery(d, natural, rows, three) == SlotRecoveryResult::OverBudget);
    std::array<uint8_t, 10> two{}; two[2] = 1;
    CHECK(apply_slot_recovery(d, natural, rows, two) == SlotRecoveryResult::Ok);
    CHECK(rows[0].used == 1);   // the wizard's row is untouched
}
