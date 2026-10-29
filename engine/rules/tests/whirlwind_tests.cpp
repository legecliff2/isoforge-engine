// B10: Air Elemental Whirlwind. Each named creature (the ones in the elemental's space; rules has no
// tiles) makes one Strength save. Fail: the bludgeoning packet, prone, and a fling of up to fling_ft
// in a random direction (one d8, face 1 is +x, clockwise). Success: none of those, and not half.
// A reported object strike deals the impact packet once per impact_per_ft of feet moved. A reported
// creature makes the Dexterity save: fail takes that same packet and falls prone; success takes neither.
// No new hash tag. air_elemental.json stays deferred.
#include <doctest/doctest.h>

#include <cstdlib>
#include <vector>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kWhirl = spell_id_of("test.air#whirlwind");

struct Seen {
    std::vector<ForcedMoveQuery> qs;
    int feet = 20;
    bool object = false;
    CombatantId struck = 0;
};

SpellDef whirl_def(int dc = 30, int impact_dc = 30) {
    SpellDef d;
    d.id = kWhirl; d.content_id = "test.air#whirlwind"; d.level = 0; d.targets = 8;
    d.kind = SpellEffectKind::Save; d.is_spell = false; d.magical = false; d.components = 0;
    d.save_ability = Ability::Str; d.fixed_dc = int8_t(dc); d.on_save = OnSave::None;
    d.dice = {0, 0, 15}; d.damage_type = DamageType::Bludgeoning;
    d.applies_condition = true; d.condition = Condition::Prone; d.condition_untracked = true;
    d.whirl_ft = 20; d.impact_per_ft = 10; d.impact_die = {0, 0, 3}; d.impact_type = DamageType::Bludgeoning;
    d.impact_save = Ability::Dex; d.impact_dc = int8_t(impact_dc);
    d.usage_kind = 1; d.recharge_min = 4;
    return d;
}

EncounterHooks hooks_for(const SpellDef& def, Seen* seen) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    REQUIRE(b->add(def));
    h.spells = b;
    h.forced_move = [seen](const EncounterState&, const ForcedMoveQuery& q) {
        if (seen) seen->qs.push_back(q);
        ForcedMoveResult r;
        const int feet = seen ? seen->feet : int(q.feet);
        r.feet_moved = uint16_t(std::min(feet, int(q.feet)));
        r.stopped_short = r.feet_moved < q.feet;
        if (seen && seen->object) r.struck_object = true;
        if (seen) r.struck = seen->struck;
        return r;
    };
    return h;
}

Combatant elemental() {
    auto c = monster(1);
    c.hp = c.hp_max = 120; c.spells_known = {kWhirl};
    return c;
}

Combatant body(CombatantId id, int str, int dex, int hp = 80) {
    auto c = pc(id, hp);
    c.abilities.score[std::size_t(Ability::Str)] = int8_t(str);
    c.abilities.score[std::size_t(Ability::Dex)] = int8_t(dex);
    return c;
}

ActionRequest at(std::vector<CombatantId> ids) {
    ActionRequest r{1, ActionKind::CastSpell, ids.empty() ? CombatantId(0) : ids[0]};
    r.spell_id = kWhirl;
    r.targets = std::move(ids);
    return r;
}

EncounterState begin(std::vector<Combatant> cs, const SpellDef& def, Seen* seen, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks_for(def, seen));
    register_action_use(s, 1, kWhirl);
    end_turns_until(s, 1);
    return s;
}

int damage_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a;
    return n;
}

int damage_n(const std::vector<Event>& ev, CombatantId id) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) ++n;
    return n;
}

bool prone_added(const std::vector<Event>& ev, CombatantId id) {
    for (const auto& e : ev)
        if (e.kind == EventKind::ConditionAdded && e.actor == id && e.a == int32_t(Condition::Prone)) return true;
    return false;
}

bool dir_ok(int x, int y) { return std::abs(x) <= 1 && std::abs(y) <= 1 && (x != 0 || y != 0); }

}  // namespace

TEST_CASE("Whirlwind: a failed Strength save deals the packet, knocks prone, and flings; a success does none of it") {
    Seen fail_seen;
    auto failed = begin({elemental(), body(2, 10, 10)}, whirl_def(30), &fail_seen, 4);
    auto land = step(failed, at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 15);
    CHECK(damage_n(land.events, 2) == 1);   // open path: the hook reported no strike
    CHECK(prone_added(land.events, 2));
    CHECK(find(failed, 2)->has(Condition::Prone));
    REQUIRE(fail_seen.qs.size() == 1);
    CHECK(fail_seen.qs[0].feet == 20);
    CHECK(fail_seen.qs[0].mode == 1);
    CHECK(fail_seen.qs[0].mover == 2);
    CHECK(fail_seen.qs[0].source == 1);
    CHECK(fail_seen.qs[0].impact_per_ft == 10);
    CHECK(dir_ok(fail_seen.qs[0].dir_x, fail_seen.qs[0].dir_y));

    Seen again;
    auto twin = begin({elemental(), body(2, 10, 10)}, whirl_def(30), &again, 4);
    REQUIRE(step(twin, at({2})).ok);
    REQUIRE(again.qs.size() == 1);
    CHECK(again.qs[0].dir_x == fail_seen.qs[0].dir_x);
    CHECK(again.qs[0].dir_y == fail_seen.qs[0].dir_y);

    Seen saved_seen;
    auto saved = begin({elemental(), body(2, 18, 10)}, whirl_def(1), &saved_seen, 4);
    auto ok = step(saved, at({2}));
    REQUIRE(ok.ok);
    CHECK(damage_to(ok.events, 2) == 0);   // not half of 15
    CHECK_FALSE(prone_added(ok.events, 2));
    CHECK_FALSE(find(saved, 2)->has(Condition::Prone));
    CHECK(saved_seen.qs.empty());   // no d8, no fling
}

TEST_CASE("Whirlwind: an object strike is 1 per 10 feet moved; under 10 feet deals none") {
    Seen wall;
    wall.object = true;
    wall.feet = 20;
    auto far = begin({elemental(), body(2, 10, 10)}, whirl_def(), &wall);
    auto hit = step(far, at({2}));
    REQUIRE(hit.ok);
    CHECK(damage_to(hit.events, 2) == 15 + 6);   // flat 3, twice
    CHECK(damage_n(hit.events, 2) == 2);

    Seen close;
    close.object = true;
    close.feet = 5;
    auto near = begin({elemental(), body(2, 10, 10)}, whirl_def(), &close);
    auto tap = step(near, at({2}));
    REQUIRE(tap.ok);
    CHECK(damage_to(tap.events, 2) == 15);
    CHECK(damage_n(tap.events, 2) == 1);
    CHECK(find(near, 2)->has(Condition::Prone));
}

TEST_CASE("Whirlwind: a creature in the way saves Dexterity for the same impact, not the whirlwind packet") {
    Seen into;
    into.struck = 3;
    into.feet = 20;
    auto fail = begin({elemental(), body(2, 10, 10), body(3, 10, 10)}, whirl_def(30, 30), &into);
    auto land = step(fail, at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 15);   // the thrown creature does not also take the impact
    CHECK(find(fail, 2)->has(Condition::Prone));
    CHECK(damage_to(land.events, 3) == 6);
    CHECK(find(fail, 3)->has(Condition::Prone));
    bool dex = false;
    for (const auto& e : land.events)
        if (e.kind == EventKind::SavingThrowRolled && e.actor == 3 && e.target == CombatantId(Ability::Dex)) dex = true;
    CHECK(dex);

    Seen missed;
    missed.struck = 3;
    missed.feet = 20;
    auto save = begin({elemental(), body(2, 10, 10), body(3, 10, 18)}, whirl_def(30, 1), &missed);
    auto ok = step(save, at({2}));
    REQUIRE(ok.ok);
    CHECK(damage_to(ok.events, 3) == 0);   // not half
    CHECK_FALSE(find(save, 3)->has(Condition::Prone));
    CHECK(find(save, 2)->has(Condition::Prone));

    Seen bump;
    bump.struck = 3;
    bump.feet = 0;
    auto zip = begin({elemental(), body(2, 10, 10), body(3, 10, 10)}, whirl_def(30, 30), &bump);
    auto none = step(zip, at({3}));
    // id 3 is the target of the whirlwind here, and the hook says it struck itself. The strike is ignored.
    REQUIRE(none.ok);
    bump.qs.clear();
    auto other = begin({elemental(), body(2, 10, 10), body(3, 10, 10)}, whirl_def(30, 30), &bump);
    auto zero = step(other, at({2}));
    REQUIRE(zero.ok);
    CHECK(damage_to(zero.events, 3) == 0);
    CHECK(find(other, 3)->has(Condition::Prone));   // the save still fails; prone does not need dice
}

TEST_CASE("Whirlwind: immunities, two creatures, an ally, an empty space, and recharge") {
    Seen plain;
    auto immune = begin({elemental(), body(2, 10, 10)}, whirl_def(), &plain);
    mut(immune, 2).immunities = bit(DamageType::Bludgeoning);
    mut(immune, 2).condition_immunities = bit(Condition::Prone);
    plain.object = true;
    auto no = step(immune, at({2}));
    REQUIRE(no.ok);
    CHECK(damage_to(no.events, 2) == 0);
    CHECK_FALSE(find(immune, 2)->has(Condition::Prone));
    CHECK(plain.qs.size() == 1);   // the fling still happens

    Seen both;
    auto two = begin({elemental(), body(2, 10, 10), body(3, 10, 10)}, whirl_def(), &both);
    auto land = step(two, at({2, 3}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 15);
    CHECK(damage_to(land.events, 3) == 15);
    CHECK(find(two, 2)->has(Condition::Prone));
    CHECK(find(two, 3)->has(Condition::Prone));
    CHECK(both.qs.size() == 2);

    auto ally_c = body(3, 10, 10);
    ally_c.team = 1;   // same team as the elemental. A hostile decoy keeps the encounter from ending.
    Seen ally_seen;
    auto ally = begin({elemental(), body(2, 10, 10), ally_c}, whirl_def(), &ally_seen);
    bool listed = false;
    for (const auto& q : legal_actions(ally))
        if (q.kind == ActionKind::CastSpell && q.spell_id == kWhirl && q.target == 3) listed = true;
    CHECK_FALSE(listed);   // legal_actions lists hostiles only
    auto hit = step(ally, at({3}));
    REQUIRE(hit.ok);
    CHECK(damage_to(hit.events, 3) == 15);

    Seen empty_seen;
    auto empty = begin({elemental(), body(2, 10, 10)}, whirl_def(), &empty_seen);
    empty.hooks.spell_target_ok = [](const EncounterState&, const Combatant&, const SpellDef&, const Combatant&) { return false; };
    int offered = 0;
    for (const auto& q : legal_actions(empty))
        if (q.kind == ActionKind::CastSpell && q.spell_id == kWhirl) { CHECK(q.target == 0); ++offered; }
    CHECK(offered == 1);
    auto air = step(empty, at({}));
    REQUIRE(air.ok);
    CHECK(count(air.events, EventKind::ActionUsed) == 1);
    CHECK(count(air.events, EventKind::SavingThrowRolled) == 0);
    CHECK(count(air.events, EventKind::DamageDealt) == 0);
    CHECK(empty_seen.qs.empty());
    const ActionUse* use = find_action_use(empty, 1, kWhirl);
    REQUIRE(use);
    CHECK(use->expended);

    Seen self_seen;
    auto self_s = begin({elemental(), body(2, 10, 10)}, whirl_def(), &self_seen);
    auto self = step(self_s, at({1}));
    CHECK_FALSE(self.ok);
    CHECK(self.error == std::string("bad target"));
    CHECK(self_s.budget.action);
    CHECK(self_seen.qs.empty());
}

TEST_CASE("Whirlwind: the recharge row is spent, and the action is not spent on a bad target") {
    Seen seen;
    auto s = begin({elemental(), body(2, 10, 10)}, whirl_def(), &seen);
    CHECK(s.budget.action);
    auto bad = step(s, at({1}));
    CHECK_FALSE(bad.ok);
    CHECK(s.budget.action);
    CHECK_FALSE(find_action_use(s, 1, kWhirl)->expended);
    REQUIRE(step(s, at({2})).ok);
    CHECK_FALSE(s.budget.action);
    CHECK(find_action_use(s, 1, kWhirl)->expended);
}
