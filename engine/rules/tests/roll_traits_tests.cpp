// Conditional advantage / disadvantage traits (T&C-G7): Pack Tactics, Blood Frenzy, Reckless,
// Magic Resistance, Gnome Cunning, Fey Ancestry, Dwarven Resilience.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
RollTrait atk_trait(RollMode m, uint16_t when, TraitRoll r = TraitRoll::Attack) { RollTrait t; t.roll = r; t.mode = m; t.when = when; return t; }
RollTrait save_trait(uint16_t when, uint8_t abilities = 0, uint32_t conds = 0, uint32_t dmg = 0) {
    RollTrait t; t.roll = TraitRoll::Save; t.when = when; t.save_abilities = abilities; t.save_vs_conditions = conds; t.save_vs_damage_types = dmg; return t;
}
RollMode mode_of(const Combatant& a, const Combatant& t, bool ally = false, bool ranged = false) {
    AttackContext c; c.ally_near_target = ally; c.ranged = ranged;
    return attack_roll_mode(a, t, c);
}
RollMode save_mode(const Combatant& c, Ability a, const SaveSource& s, RollMode base = RollMode::Normal) {
    const auto m = save_trait_modes(c, a, s);
    return combine_modes(base == RollMode::Advantage || m.advantage, base == RollMode::Disadvantage || m.disadvantage);
}
} // namespace

TEST_CASE("roll traits: Pack Tactics needs the ally fact") {
    Combatant wolf = monster(1), pc_ = pc(2);
    wolf.roll_traits = {atk_trait(RollMode::Advantage, kWhenAllyNearTarget)};
    CHECK(mode_of(wolf, pc_, false) == RollMode::Normal);
    CHECK(mode_of(wolf, pc_, true) == RollMode::Advantage);
    CHECK(mode_of(pc_, wolf, true) == RollMode::Normal);   // the trait belongs to the wolf
}

TEST_CASE("roll traits: Blood Frenzy reads the target's hit points") {
    Combatant shark = monster(1), pc_ = pc(2, 28);
    shark.roll_traits = {atk_trait(RollMode::Advantage, kWhenTargetDamaged)};
    CHECK(mode_of(shark, pc_) == RollMode::Normal);
    pc_.hp = 27;
    CHECK(mode_of(shark, pc_) == RollMode::Advantage);
}

TEST_CASE("roll traits: Reckless (melee only, both directions) and cancellation") {
    Combatant mino = monster(1), pc_ = pc(2);
    mino.roll_traits = {atk_trait(RollMode::Advantage, kWhenMeleeAttack),
                        atk_trait(RollMode::Advantage, kWhenMeleeAttack, TraitRoll::AttackedBy)};
    CHECK(mode_of(mino, pc_, false, false) == RollMode::Advantage);
    CHECK(mode_of(mino, pc_, false, true) == RollMode::Normal);       // ranged attack: not covered
    CHECK(mode_of(pc_, mino, false, false) == RollMode::Advantage);   // attacks against it
    CHECK(mode_of(pc_, mino, false, true) == RollMode::Normal);
    add_condition(pc_, Condition::Poisoned);                          // advantage + disadvantage cancel
    CHECK(mode_of(pc_, mino, false, false) == RollMode::Normal);
    pc_.conditions = 0;
    mino.roll_traits = {atk_trait(RollMode::Disadvantage, kWhenRangedAttack)};   // a disadvantage trait
    CHECK(mode_of(mino, pc_, false, true) == RollMode::Disadvantage);
}

TEST_CASE("roll traits: target filters") {
    Combatant a = monster(1), t = pc(2);
    RollTrait r = atk_trait(RollMode::Advantage, 0); r.target_conditions = bit(Condition::Charmed) | bit(Condition::Deafened);
    a.roll_traits = {r};
    CHECK(mode_of(a, t) == RollMode::Normal);
    add_condition(t, Condition::Deafened);   // neither condition changes attack rolls on its own
    CHECK(mode_of(a, t) == RollMode::Advantage);
    RollTrait u = atk_trait(RollMode::Disadvantage, 0); u.target_types = bit(CreatureType::Undead);
    Combatant b = monster(3), z = pc(4);
    b.roll_traits = {u};
    CHECK(mode_of(b, z) == RollMode::Normal);
    z.creature_type = CreatureType::Undead;
    CHECK(mode_of(b, z) == RollMode::Disadvantage);
}

TEST_CASE("roll traits: Magic Resistance, Gnome Cunning, Fey Ancestry, Dwarven Resilience") {
    const SaveSource spell{true, 0, 0}, plain{false, 0, 0};
    Combatant imp = monster(1);
    imp.roll_traits = {save_trait(kWhenSourceMagical)};
    CHECK(save_mode(imp, Ability::Dex, spell) == RollMode::Advantage);
    CHECK(save_mode(imp, Ability::Dex, plain) == RollMode::Normal);

    Combatant gnome = pc(2);
    gnome.roll_traits = {save_trait(kWhenSourceMagical, uint8_t(bit(Ability::Int) | bit(Ability::Wis) | bit(Ability::Cha)))};
    CHECK(save_mode(gnome, Ability::Wis, spell) == RollMode::Advantage);
    CHECK(save_mode(gnome, Ability::Dex, spell) == RollMode::Normal);
    CHECK(save_mode(gnome, Ability::Wis, plain) == RollMode::Normal);

    Combatant elf = pc(3);
    elf.roll_traits = {save_trait(0, 0, bit(Condition::Charmed))};
    CHECK(save_mode(elf, Ability::Wis, SaveSource{true, bit(Condition::Charmed), 0}) == RollMode::Advantage);
    CHECK(save_mode(elf, Ability::Wis, SaveSource{true, bit(Condition::Paralyzed), 0}) == RollMode::Normal);
    CHECK(save_mode(elf, Ability::Wis, spell) == RollMode::Normal);

    Combatant dwarf = pc(4);
    dwarf.roll_traits = {save_trait(0, 0, bit(Condition::Poisoned), bit(DamageType::Poison))};
    CHECK(save_mode(dwarf, Ability::Con, SaveSource{false, bit(Condition::Poisoned), 0}) == RollMode::Advantage);
    CHECK(save_mode(dwarf, Ability::Con, SaveSource{false, 0, bit(DamageType::Poison)}) == RollMode::Advantage);
    CHECK(save_mode(dwarf, Ability::Con, SaveSource{false, 0, bit(DamageType::Fire)}) == RollMode::Normal);
    CHECK(save_mode(dwarf, Ability::Con, plain, RollMode::Disadvantage) == RollMode::Disadvantage);
    CHECK(save_mode(dwarf, Ability::Con, SaveSource{false, bit(Condition::Poisoned), 0}, RollMode::Disadvantage) == RollMode::Normal);
}

TEST_CASE("roll traits: saving_throw with a source draws like the plain mode it resolves to") {
    Combatant c = monster(1);
    c.roll_traits = {save_trait(kWhenSourceMagical)};
    rng::Rng a(7, 0), b(7, 0);
    auto adv = saving_throw(a, c, Ability::Wis, 15, RollMode::Normal, SaveSource{true, 0, 0});
    auto ref = saving_throw(b, c, Ability::Wis, 15, RollMode::Advantage);
    CHECK(adv.roll.natural == ref.roll.natural); CHECK(adv.roll.total == ref.roll.total); CHECK(adv.success == ref.success);
    rng::Rng d(7, 0), e(7, 0);
    auto none = saving_throw(d, c, Ability::Wis, 15, RollMode::Normal, SaveSource{});
    auto ref2 = saving_throw(e, c, Ability::Wis, 15);
    CHECK(none.roll.total == ref2.roll.total);
}

TEST_CASE("roll traits: ActionRequest::ally_near_target reaches the attack roll") {
    auto hits = [](bool trait, bool fact) {   // to-hit 0 vs AC 21: only a natural 20 hits (5%, ~9.75% with advantage)
        int n = 0;
        for (uint64_t seed = 1; seed <= 600; ++seed) {
            Combatant w = monster(1); w.attacks[0].to_hit = 0;
            if (trait) w.roll_traits = {atk_trait(RollMode::Advantage, kWhenAllyNearTarget)};
            Combatant p = pc(2, 5000); p.ac = 21;
            auto s = begin_encounter({w, p}, seed);
            end_turns_until(s, 1);
            ActionRequest r{1, ActionKind::Attack, 2, 0};
            r.ally_near_target = fact;
            auto res = step(s, r);
            REQUIRE(res.ok);
            n += count(res.events, EventKind::AttackResolved) == 1 && p.hp != mut(s, 2).hp;
        }
        return n;
    };
    const int base = hits(false, true), none = hits(true, false), both = hits(true, true);
    CHECK(none == base);          // fact absent: identical to no trait (same seeds)
    CHECK(both > base + 12);      // advantage roughly doubles the hit rate
}
