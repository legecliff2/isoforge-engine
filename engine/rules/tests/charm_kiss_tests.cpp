// B16: Charm (command control) and Draining Kiss (save drain). No succubus.json.
#include <algorithm>
#include <doctest/doctest.h>

#include "rules5e/rest.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kCharm = spell_id_of("test.fiend#charm");
const uint32_t kKiss = spell_id_of("test.fiend#draining_kiss");

SpellDef charm(int dc) {
    SpellDef d;
    d.id = kCharm; d.content_id = "test.fiend#charm"; d.is_spell = false; d.magical = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Wis; d.fixed_dc = int8_t(dc);
    d.range_ft = 30; d.targets = 1; d.caster_sees_target = true;
    d.target_types = bit(CreatureType::Humanoid);
    d.applies_condition = true; d.condition = Condition::Charmed;
    d.repeat_save = RepeatSave::OnDamage;
    d.expiry = uint8_t(Expiry::Rounds); d.duration_rounds = 14400;
    d.control = ControlKind::Command; d.charm_command = true;
    d.success_immunity_hours = 24; d.success_immunity_scope = 1;
    return d;
}

SpellDef kiss(int dc, int bonus) {
    SpellDef d;
    d.id = kKiss; d.content_id = "test.fiend#draining_kiss"; d.is_spell = false; d.magical = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Con; d.fixed_dc = int8_t(dc);
    d.range_ft = 5; d.targets = 1; d.on_save = OnSave::Half;
    d.dice = {0, 0, int16_t(bonus)}; d.damage_type = DamageType::Psychic;
    d.save_drain_max_hp = true;
    d.target_requires_any = uint8_t(kTargetGateCharmedBySelf | kTargetGateWilling);
    return d;
}

EncounterHooks hooks(int charm_dc, int kiss_dc, int kiss_bonus, bool blind = false) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(charm(charm_dc));
    book->add(kiss(kiss_dc, kiss_bonus));
    h.spells = book;
    if (blind) h.sight_facts = [](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f; f.line_of_sight = false; return f;
    };
    return h;
}

Combatant fiend() {
    auto c = monster(1);
    c.abilities.score[1] = 30;
    c.hp = c.hp_max = 80;
    c.spells_known = {kCharm, kKiss};
    c.creature_type = CreatureType::Fiend;
    return c;
}

Combatant human(CombatantId id, int hp = 40, uint8_t team = 0) {
    auto c = monster(id);
    c.team = team;
    c.creature_type = CreatureType::Humanoid;
    c.hp = c.hp_max = int16_t(hp);
    c.abilities.score[1] = 10;
    return c;
}

void next_turn(EncounterState& s, CombatantId id) {
    if (current_actor(s) == id) REQUIRE(step(s, {id, ActionKind::EndTurn}).ok);
    end_turns_until(s, id);
    REQUIRE(current_actor(s) == id);
}

EncounterState start(std::vector<Combatant> cs, int charm_dc = 30, int kiss_dc = 30, int kiss_bonus = 10, bool blind = false) {
    auto s = begin_encounter(std::move(cs), 4, nullptr, hooks(charm_dc, kiss_dc, kiss_bonus, blind));
    end_turns_until(s, 1);
    return s;
}

ActiveSpellEffect* charm_on(EncounterState& s, CombatantId t) {
    for (auto& e : s.effects)
        if (e.spell_id == kCharm && e.target == t && e.control == uint8_t(ControlKind::Command)) return &e;
    return nullptr;
}

ActionRequest order(CommandWord w, CombatantId t) {
    auto r = cast(1, kCharm, t, 0);
    r.command_word = uint8_t(w);
    return r;
}

}  // namespace

TEST_CASE("charm: a humanoid who fails is charmed and commanded; a success is 24-hour immunity") {
    auto s = start({fiend(), human(2)});
    REQUIRE(s.budget.action);
    auto failed = step(s, cast(1, kCharm, 2, 0));
    REQUIRE(failed.ok);
    CHECK_FALSE(s.budget.action);
    CHECK(find(s, 2)->has(Condition::Charmed));
    CHECK(charmed_by(s, 2, 1));
    const auto* e = charm_on(s, 2);
    REQUIRE(e);
    CHECK(e->command_word == 0);
    CHECK(e->repeat_save == RepeatSave::OnDamage);
    CHECK(e->action_immunity_on_end_hours == 24);
    CHECK(e->action_immunity_on_end_scope == 1);
    CHECK(e->timed.rounds_left == 14400);

    auto saved = start({fiend(), human(2)}, 1);
    auto ok = step(saved, cast(1, kCharm, 2, 0));
    REQUIRE(ok.ok);
    CHECK_FALSE(find(saved, 2)->has(Condition::Charmed));
    REQUIRE(saved.action_immunities.size() == 1);
    CHECK(saved.action_immunities[0].creature == 2);
    CHECK(saved.action_immunities[0].source == 1);
    CHECK(saved.action_immunities[0].spell_id == kCharm);
    CHECK(saved.action_immunities[0].until_round == uint32_t(saved.round) + 24u * 600u);
    next_turn(saved, 1);
    auto again = step(saved, cast(1, kCharm, 2, 0));
    REQUIRE(again.ok);
    CHECK(std::none_of(again.events.begin(), again.events.end(),
                       [](const Event& ev) { return ev.kind == EventKind::SavingThrowRolled; }));
    CHECK_FALSE(find(saved, 2)->has(Condition::Charmed));
}

TEST_CASE("charm: not a humanoid, unseen, or a word on the cast, is refused before the action is spent") {
    auto beast_c = monster(2);
    beast_c.team = 0;
    beast_c.creature_type = CreatureType::Beast;
    auto beast = start({fiend(), beast_c});
    CHECK(beast.budget.action);
    auto type = step(beast, cast(1, kCharm, 2, 0));
    CHECK_FALSE(type.ok);
    CHECK(type.error == "invalid creature type for this spell");
    CHECK(beast.budget.action);

    auto dark = start({fiend(), human(2)}, 30, 30, 10, true);
    auto seen = step(dark, cast(1, kCharm, 2, 0));
    CHECK_FALSE(seen.ok);
    CHECK(seen.error == "caster cannot see the target");
    CHECK(dark.budget.action);

    auto worded = start({fiend(), human(2)});
    auto word = step(worded, order(CommandWord::Halt, 2));
    CHECK_FALSE(word.ok);
    CHECK(word.error == "command_word: a Charm cast takes no word");
    CHECK(worded.budget.action);
}

TEST_CASE("charm: one target; charming another ends the first and that ending grants immunity") {
    auto s = start({fiend(), human(2), human(3)});
    REQUIRE(step(s, cast(1, kCharm, 2, 0)).ok);
    REQUIRE(charm_on(s, 2));
    next_turn(s, 1);
    REQUIRE(step(s, cast(1, kCharm, 3, 0)).ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Charmed));
    CHECK(charm_on(s, 2) == nullptr);
    CHECK(find(s, 3)->has(Condition::Charmed));
    REQUIRE(charm_on(s, 3));
    bool immune = false;
    for (const auto& im : s.action_immunities)
        if (im.creature == 2 && im.source == 1 && im.spell_id == kCharm) immune = true;
    CHECK(immune);
}

TEST_CASE("charm: an order does not spend the action; Approach restricts; suicidal offers the save") {
    auto s = start({fiend(), human(2), human(4)});
    REQUIRE(step(s, cast(1, kCharm, 2, 0)).ok);
    next_turn(s, 1);
    REQUIRE(s.budget.action);
    auto told = step(s, order(CommandWord::Approach, 2));
    REQUIRE(told.ok);
    CHECK(s.budget.action);
    REQUIRE(charm_on(s, 2));
    CHECK(charm_on(s, 2)->command_word == uint8_t(CommandWord::Approach));
    end_turns_until(s, 2);
    CHECK(step(s, {2, ActionKind::Attack, 4}).error == "command: move only");

    auto loose = start({fiend(), human(2), human(4)});
    REQUIRE(step(loose, cast(1, kCharm, 2, 0)).ok);
    end_turns_until(loose, 2);
    CHECK(step(loose, {2, ActionKind::Attack, 1}).error == "a charmed creature cannot attack its charmer");

    auto harm = start({fiend(), human(2)});
    REQUIRE(step(harm, cast(1, kCharm, 2, 0)).ok);
    REQUIRE(charm_on(harm, 2));
    charm_on(harm, 2)->dc = 1;
    next_turn(harm, 1);
    mut(harm, 1).attacks[0].to_hit = 40;
    mut(harm, 1).attacks[0].damage = {0, 0, 1};
    auto hit = step(harm, {1, ActionKind::Attack, 2});
    REQUIRE(hit.ok);
    CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) { return e.kind == EventKind::SavingThrowRolled; }));
    CHECK_FALSE(find(harm, 2)->has(Condition::Charmed));
    CHECK(std::any_of(harm.action_immunities.begin(), harm.action_immunities.end(),
                      [](const ActionImmunity& im) { return im.creature == 2 && im.source == 1; }));

    auto suicide = start({fiend(), human(2)});
    REQUIRE(step(suicide, cast(1, kCharm, 2, 0)).ok);
    next_turn(suicide, 1);
    auto lived = step(suicide, order(CommandWord::Suicidal, 2));
    REQUIRE(lived.ok);
    CHECK(find(suicide, 2)->has(Condition::Charmed));
    CHECK(charm_on(suicide, 2)->command_word == 0);
    charm_on(suicide, 2)->dc = 1;
    auto ended = step(suicide, order(CommandWord::Suicidal, 2));
    REQUIRE(ended.ok);
    CHECK_FALSE(find(suicide, 2)->has(Condition::Charmed));
    CHECK(std::any_of(suicide.action_immunities.begin(), suicide.action_immunities.end(),
                      [](const ActionImmunity& im) { return im.creature == 2; }));
}

TEST_CASE("draining kiss: charmed by the user, half on a success, maximum 0 kills, long rest clears") {
    auto blocked = start({fiend(), human(2)});
    auto no = step(blocked, cast(1, kKiss, 2, 0));
    CHECK_FALSE(no.ok);
    CHECK(no.error == "target must be charmed by the user or willing");
    CHECK(blocked.budget.action);

    auto s = start({fiend(), human(2)});
    REQUIRE(step(s, cast(1, kCharm, 2, 0)).ok);
    next_turn(s, 1);
    const int hp = find(s, 2)->hp;
    auto full = step(s, cast(1, kKiss, 2, 0));
    REQUIRE(full.ok);
    CHECK(find(s, 2)->hp == int16_t(hp - 10));
    CHECK(find(s, 2)->hp_max_reduction == 10);
    CHECK(find(s, 2)->has(Condition::Charmed));   // DC 30 repeat save fails

    auto half = start({fiend(), human(2)}, 30, 1);
    REQUIRE(step(half, cast(1, kCharm, 2, 0)).ok);
    next_turn(half, 1);
    const int hp2 = find(half, 2)->hp;
    REQUIRE(step(half, cast(1, kKiss, 2, 0)).ok);
    CHECK(find(half, 2)->hp == int16_t(hp2 - 5));
    CHECK(find(half, 2)->hp_max_reduction == 5);
    CHECK(long_rest(mut(half, 2)).ok);
    CHECK(find(half, 2)->hp_max_reduction == 0);

    auto ally = start({fiend(), human(2, 40, 1), human(3)}, 30, 30);
    auto willing = cast(1, kKiss, 2, 0);
    willing.target_willing = true;
    REQUIRE(step(ally, willing).ok);
    CHECK(find(ally, 2)->hp_max_reduction == 10);

    auto foe = start({fiend(), human(2)});
    auto flagged = cast(1, kKiss, 2, 0);
    flagged.target_willing = true;
    CHECK_FALSE(step(foe, flagged).ok);
    CHECK(foe.budget.action);

    auto other = start({fiend(), human(2)});
    add_condition(mut(other, 2), Condition::Charmed);
    other.sourced.push_back(SourcedCondition{Condition::Charmed, 2, 9, 0});
    CHECK_FALSE(step(other, cast(1, kKiss, 2, 0)).ok);

    auto immune = start({fiend(), human(2)});
    REQUIRE(step(immune, cast(1, kCharm, 2, 0)).ok);
    next_turn(immune, 1);
    mut(immune, 2).immunities |= bit(DamageType::Psychic);
    const int before = find(immune, 2)->hp;
    REQUIRE(step(immune, cast(1, kKiss, 2, 0)).ok);
    CHECK(find(immune, 2)->hp == before);
    CHECK(find(immune, 2)->hp_max_reduction == 0);

    auto dead = start({fiend(), human(2, 12)}, 30, 30, 20);
    mut(dead, 2).uses_death_saves = true;
    REQUIRE(step(dead, cast(1, kCharm, 2, 0)).ok);
    next_turn(dead, 1);
    REQUIRE(step(dead, cast(1, kKiss, 2, 0)).ok);
    CHECK(find(dead, 2)->dead);
    CHECK(find(dead, 2)->hp == 0);
    CHECK(find(dead, 2)->hp_max_reduction == 12);
}
