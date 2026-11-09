// B8: Bulette Deadly Leap. Its own action. A completed jump of at least 15 feet, then each named creature
// (the ones in the landing space; rules has no tiles) makes one Str-or-Dex save. Fail: both damages and prone.
// Success: half of each, and a 5 ft push; no space means prone in the space. No new Combat::hash tag.
// jump_ft is log_hash 0xE4 (sim). The choice and the push distance are not request fields.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kLeap = spell_id_of("test.bulette#deadly_leap");

SpellDef leap_def(int dc = 16) {
    SpellDef d;
    d.id = kLeap; d.content_id = "test.bulette#deadly_leap"; d.level = 0; d.targets = 8;
    d.kind = SpellEffectKind::Save; d.is_spell = false; d.magical = false; d.components = 0;
    d.save_ability = Ability::Str; d.has_save_choice = true; d.save_alt = Ability::Dex; d.fixed_dc = int8_t(dc);
    d.dice = {0, 0, 14}; d.damage_type = DamageType::Bludgeoning; d.on_save = OnSave::Half;
    d.has_also_damage = true; d.also_dice = {0, 0, 14}; d.also_damage_type = DamageType::Piercing;
    d.applies_condition = true; d.condition = Condition::Prone; d.condition_untracked = true;
    d.requires_jumped_ft = 15; d.leap_push_ft = 5;
    return d;
}

EncounterHooks hooks_for(const SpellDef& def, int feet = 5) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    REQUIRE(b->add(def));
    h.spells = b;
    h.forced_move = [feet](const EncounterState&, const ForcedMoveQuery& q) {
        ForcedMoveResult r;
        r.feet_moved = uint16_t(std::min(feet, int(q.feet)));
        r.stopped_short = r.feet_moved < q.feet;
        return r;
    };
    return h;
}

Combatant leaper() {
    auto c = monster(1);
    c.hp = c.hp_max = 120; c.speed_ft = 40; c.spells_known = {kLeap};
    return c;
}

Combatant victim(CombatantId id, int str, int dex, uint32_t save_prof = 0, int hp = 80) {
    auto c = pc(id, hp);
    c.abilities.score[std::size_t(Ability::Str)] = int8_t(str);
    c.abilities.score[std::size_t(Ability::Dex)] = int8_t(dex);
    c.save_proficiencies = save_prof;
    return c;
}

ActionRequest hop(int tiles, int jump) {
    ActionRequest r{1, ActionKind::Move};
    r.move_ft = int16_t(5 * tiles);
    r.move_tiles = uint8_t(tiles);
    r.jump_ft = uint16_t(jump);
    return r;
}

ActionRequest leap_at(std::vector<CombatantId> ids) {
    ActionRequest r{1, ActionKind::CastSpell, ids.empty() ? CombatantId(0) : ids[0]};
    r.spell_id = kLeap;
    r.targets = std::move(ids);
    return r;
}

EncounterState begin(std::vector<Combatant> cs, const SpellDef& def, int feet = 5) {
    auto s = begin_encounter(std::move(cs), 1, nullptr, hooks_for(def, feet));
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

Ability saved_with(const std::vector<Event>& ev, CombatantId id) {
    for (const auto& e : ev)
        if (e.kind == EventKind::SavingThrowRolled && e.actor == id) return Ability(e.target);
    return Ability::Cha;
}

bool listed(const EncounterState& s) {
    for (const auto& q : legal_actions(s))
        if (q.kind == ActionKind::CastSpell && q.spell_id == kLeap) return true;
    return false;
}

}  // namespace

TEST_CASE("Deadly Leap: the jump gate is jumped_ft, not any movement") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    CHECK(s.budget.jumped_ft == 0);
    CHECK_FALSE(listed(s));
    auto early = step(s, leap_at({2}));
    CHECK_FALSE(early.ok);
    CHECK(early.error == "has not jumped far enough");
    CHECK(s.budget.action);

    const int before_walk = s.budget.movement_ft;
    auto walked = step(s, hop(3, 0));
    REQUIRE(walked.ok);
    CHECK(s.budget.moved_ft == 15);
    CHECK(s.budget.jumped_ft == 0);
    CHECK(s.budget.movement_ft == int16_t(before_walk - 15));   // the jump is not a second spend
    auto still = step(s, leap_at({2}));
    CHECK_FALSE(still.ok);
    CHECK(still.error == "has not jumped far enough");
    CHECK(s.budget.action);

    auto s2 = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    REQUIRE(step(s2, hop(2, 10)).ok);
    CHECK(s2.budget.jumped_ft == 10);
    CHECK_FALSE(step(s2, leap_at({2})).ok);
    CHECK(s2.budget.action);

    auto s3 = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    const int move_before = s3.budget.movement_ft;
    REQUIRE(step(s3, hop(3, 15)).ok);
    CHECK(s3.budget.jumped_ft == 15);
    CHECK(s3.budget.moved_ft == 15);
    CHECK(s3.budget.movement_ft == int16_t(move_before - 15));
    CHECK(listed(s3));
    auto land = step(s3, leap_at({2}));
    REQUIRE(land.ok);
    CHECK_FALSE(s3.budget.action);
}

TEST_CASE("Deadly Leap: a failed save is both damages and prone, and is not pushed") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    REQUIRE(step(s, hop(3, 15)).ok);
    const int hp = find(s, 2)->hp;
    auto land = step(s, leap_at({2}));
    REQUIRE(land.ok);
    CHECK(saved_with(land.events, 2) == Ability::Str);   // tie: the first listed ability
    CHECK(damage_n(land.events, 2) == 2);
    CHECK(damage_to(land.events, 2) == 28);
    CHECK(find(s, 2)->hp == hp - 28);
    CHECK(find(s, 2)->has(Condition::Prone));
    CHECK(prone_added(land.events, 2));
    CHECK(count(land.events, EventKind::Moved) == 0);
}

TEST_CASE("Deadly Leap: a successful save is half of each damage and a 5 ft push") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def(1), 5);
    REQUIRE(step(s, hop(3, 15)).ok);
    auto land = step(s, leap_at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 14);
    CHECK_FALSE(find(s, 2)->has(Condition::Prone));
    bool pushed = false;
    for (const auto& e : land.events)
        if (e.kind == EventKind::Moved && e.actor == 2 && e.target == 1 && e.a == 5 && e.c == kMovedForced) pushed = true;
    CHECK(pushed);
}

TEST_CASE("Deadly Leap: a successful save with nowhere to go falls prone in the space") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def(1), 0);
    REQUIRE(step(s, hop(3, 15)).ok);
    auto land = step(s, leap_at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 14);
    CHECK(find(s, 2)->has(Condition::Prone));
    bool stuck = false;
    for (const auto& e : land.events)
        if (e.kind == EventKind::Moved && e.actor == 2 && e.a == 0) stuck = true;
    CHECK(stuck);
}

TEST_CASE("Deadly Leap: the target's choice is the higher save bonus, Strength on a tie") {
    auto higher_str = begin({leaper(), victim(2, 16, 10)}, leap_def(30));
    REQUIRE(step(higher_str, hop(3, 15)).ok);
    auto a = step(higher_str, leap_at({2}));
    REQUIRE(a.ok);
    CHECK(saved_with(a.events, 2) == Ability::Str);

    auto higher_dex = begin({leaper(), victim(2, 10, 16)}, leap_def(30));
    REQUIRE(step(higher_dex, hop(3, 15)).ok);
    auto b = step(higher_dex, leap_at({2}));
    REQUIRE(b.ok);
    CHECK(saved_with(b.events, 2) == Ability::Dex);

    auto tie = begin({leaper(), victim(2, 14, 14)}, leap_def(30));
    REQUIRE(step(tie, hop(3, 15)).ok);
    auto c = step(tie, leap_at({2}));
    REQUIRE(c.ok);
    CHECK(saved_with(c.events, 2) == Ability::Str);

    // Proficiency counts. Str 10 proficient (+2) beats Dex 12 (+1).
    auto prof = begin({leaper(), victim(2, 10, 12, bit(Ability::Str), 80)}, leap_def(30));
    mut(prof, 2).proficiency_bonus = 2;
    REQUIRE(step(prof, hop(3, 15)).ok);
    auto d = step(prof, leap_at({2}));
    REQUIRE(d.ok);
    CHECK(saved_with(d.events, 2) == Ability::Str);
}

TEST_CASE("Deadly Leap: every creature in the space saves, from one roll of each damage") {
    auto s = begin({leaper(), victim(2, 10, 10), victim(3, 10, 18)}, leap_def(30));
    REQUIRE(step(s, hop(3, 15)).ok);
    auto land = step(s, leap_at({2, 3}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 28);
    CHECK(damage_to(land.events, 3) == 28);
    CHECK(find(s, 2)->has(Condition::Prone));
    CHECK(find(s, 3)->has(Condition::Prone));
    CHECK(saved_with(land.events, 2) == Ability::Str);
    CHECK(saved_with(land.events, 3) == Ability::Dex);
    CHECK(count(land.events, EventKind::Moved) == 0);
}

TEST_CASE("Deadly Leap: immunity to prone skips the fall; immunity to a damage type skips that packet") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    mut(s, 2).condition_immunities |= bit(Condition::Prone);
    REQUIRE(step(s, hop(3, 15)).ok);
    auto land = step(s, leap_at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 28);
    CHECK_FALSE(find(s, 2)->has(Condition::Prone));
    CHECK_FALSE(prone_added(land.events, 2));

    auto blunt = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    mut(blunt, 2).immunities |= bit(DamageType::Bludgeoning);
    REQUIRE(step(blunt, hop(3, 15)).ok);
    auto only_pierce = step(blunt, leap_at({2}));
    REQUIRE(only_pierce.ok);
    CHECK(damage_to(only_pierce.events, 2) == 14);   // the piercing packet; bludgeoning is immune
    CHECK(find(blunt, 2)->has(Condition::Prone));

    auto both = begin({leaper(), victim(2, 10, 10)}, leap_def(30));
    mut(both, 2).immunities |= bit(DamageType::Bludgeoning) | bit(DamageType::Piercing);
    REQUIRE(step(both, hop(3, 15)).ok);
    auto none = step(both, leap_at({2}));
    REQUIRE(none.ok);
    CHECK(damage_to(none.events, 2) == 0);
    CHECK(find(both, 2)->has(Condition::Prone));

    auto blocked = begin({leaper(), victim(2, 10, 10)}, leap_def(1), 0);
    mut(blocked, 2).condition_immunities |= bit(Condition::Prone);
    REQUIRE(step(blocked, hop(3, 15)).ok);
    auto stay = step(blocked, leap_at({2}));
    REQUIRE(stay.ok);
    CHECK(damage_to(stay.events, 2) == 14);
    CHECK_FALSE(find(blocked, 2)->has(Condition::Prone));
}

TEST_CASE("Deadly Leap: Evasion applies only when Dexterity is the chosen ability") {
    auto dex = begin({leaper(), victim(2, 10, 16)}, leap_def(1));
    mut(dex, 2).evasion = true;
    REQUIRE(step(dex, hop(3, 15)).ok);
    auto saved = step(dex, leap_at({2}));
    REQUIRE(saved.ok);
    CHECK(saved_with(saved.events, 2) == Ability::Dex);
    CHECK(damage_to(saved.events, 2) == 0);
    CHECK_FALSE(find(dex, 2)->has(Condition::Prone));

    auto dex_fail = begin({leaper(), victim(2, 10, 16)}, leap_def(30));
    mut(dex_fail, 2).evasion = true;
    REQUIRE(step(dex_fail, hop(3, 15)).ok);
    auto failed = step(dex_fail, leap_at({2}));
    REQUIRE(failed.ok);
    CHECK(damage_to(failed.events, 2) == 14);   // half of each, not the full 28
    CHECK(find(dex_fail, 2)->has(Condition::Prone));

    auto str = begin({leaper(), victim(2, 16, 10)}, leap_def(1));
    mut(str, 2).evasion = true;
    REQUIRE(step(str, hop(3, 15)).ok);
    auto str_save = step(str, leap_at({2}));
    REQUIRE(str_save.ok);
    CHECK(saved_with(str_save.events, 2) == Ability::Str);
    CHECK(damage_to(str_save.events, 2) == 14);
}

TEST_CASE("Deadly Leap: a bad jump or the leaper itself does not spend the action") {
    auto s = begin({leaper(), victim(2, 10, 10)}, leap_def());
    const int full = s.budget.movement_ft;
    auto bad = step(s, hop(1, 7));
    CHECK_FALSE(bad.ok);
    CHECK(bad.error == "jump_ft: a multiple of 5");
    CHECK(s.budget.movement_ft == full);

    auto over = step(s, hop(1, 10));
    CHECK_FALSE(over.ok);
    CHECK(over.error == "jump_ft exceeds this step");

    ActionRequest atk{1, ActionKind::Attack, 2, 0};
    atk.jump_ft = 5;
    auto flagged = step(s, atk);
    CHECK_FALSE(flagged.ok);
    CHECK(flagged.error == "jump_ft: Move only");

    mut(s, 1).conditions |= bit(Condition::Prone);
    auto crawling = step(s, hop(1, 5));
    CHECK_FALSE(crawling.ok);
    CHECK(crawling.error == "cannot jump while prone");
    mut(s, 1).conditions &= ~bit(Condition::Prone);

    REQUIRE(step(s, hop(3, 15)).ok);
    auto self = step(s, leap_at({1}));
    CHECK_FALSE(self.ok);
    CHECK(self.error == "bad target");
    CHECK(s.budget.action);
}
