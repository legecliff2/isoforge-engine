// SRD 5.1 Green Hag Invisible Passage. An action. Invisible until the user attacks or casts a spell,
// or until concentration ends. No duration. Physical evidence of passage is none while it is up.
// Magical tracking is unanswered. Not innate_invisibility. Does not load green_hag.json.
#include <doctest/doctest.h>

#include <memory>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kPassage = spell_id_of("test.passage");
const uint32_t kConc = spell_id_of("test.conc.self");
const uint32_t kDispel = spell_id_of("test.spell#dispel");

SpellDef passage_def() {
    SpellDef d;
    d.id = kPassage; d.content_id = "test.passage"; d.level = 0; d.is_spell = false; d.magical = false;
    d.kind = SpellEffectKind::ApplyCondition; d.condition = Condition::Invisible; d.concentration = true;
    d.ends_on = uint8_t(kEndsOnAttack | kEndsOnCast); d.invisible_passage = true;
    d.casting_time = CastingTime::Action;
    return d;
}
SpellDef conc_spell() {
    SpellDef d;
    d.id = kConc; d.content_id = "test.conc.self"; d.level = 1; d.range_ft = 5; d.is_spell = true;
    d.kind = SpellEffectKind::ApplyCondition; d.applies_condition = true;
    d.condition = Condition::Invisible; d.concentration = true;
    d.duration = SpellDuration::Hours; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 600;
    return d;
}
SpellDef dispel_def() {
    SpellDef d;
    d.id = kDispel; d.content_id = "test.spell#dispel"; d.level = 3; d.range_ft = 120; d.is_spell = true;
    d.kind = SpellEffectKind::Dispel;
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(passage_def());
    book->add(conc_spell());
    book->add(dispel_def());
    h.spells = book;
    return h;
}
Combatant hag(CombatantId id) {
    Combatant c = wizard(id, 0);
    c.spells_known.push_back(kPassage);
    c.spells_known.push_back(kConc);
    never_react(c);
    return c;
}
EncounterState ready() {
    Combatant foe = monster(2);
    foe.team = 1;
    never_react(foe);
    auto s = begin_encounter({hag(1), foe}, 1, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
bool concentrating_on(const EncounterState& s, CombatantId id, uint32_t spell) {
    return find(s, id)->concentration_spell == spell;
}
}  // namespace

TEST_CASE("invisible passage: an action, invisible, concentrating, no duration") {
    auto s = ready();
    const auto size = find(s, 1)->size;
    const auto type = find(s, 1)->creature_type;
    const auto attacks = find(s, 1)->attacks.size();
    CHECK(s.budget.reaction);
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK_FALSE(find(s, 1)->innate_invisible);
    CHECK(concentrating_on(s, 1, kPassage));
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.bonus_action);
    CHECK(s.budget.reaction);
    CHECK(find(s, 1)->size == size);
    CHECK(find(s, 1)->creature_type == type);
    CHECK(find(s, 1)->attacks.size() == attacks);
    CHECK(s.figures.empty());
    CHECK(track_evidence(s, 1, TrackEvidence::Physical) == TrackAnswer::None);
    CHECK(track_evidence(s, 1, TrackEvidence::Magical) == TrackAnswer::Unanswered);
    CHECK(track_evidence(s, 2, TrackEvidence::Physical) == TrackAnswer::Unanswered);
    for (int i = 0; i < 4; ++i) REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK(concentrating_on(s, 1, kPassage));
    CHECK(track_evidence(s, 1, TrackEvidence::Physical) == TrackAnswer::None);
}

TEST_CASE("invisible passage: a target or a bonus action is refused and spends nothing") {
    auto s = ready();
    auto bad = cast(1, kPassage, 2);
    auto r = step(s, bad);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "invisible passage: no target");
    CHECK(s.budget.action);
    CHECK_FALSE(find(s, 1)->has(Condition::Invisible));
    auto bonus = cast(1, kPassage, 1);
    bonus.as_bonus_action = true;
    r = step(s, bonus);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "invisible passage: an action");
    CHECK(s.budget.action);
    CHECK(s.budget.bonus_action);
}

TEST_CASE("invisible passage: EndConcentration ends it, and innate invisibility stays") {
    auto s = ready();
    mut(s, 1).innate_invisible = true;
    add_condition(mut(s, 1), Condition::Invisible);
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    CHECK(concentrating_on(s, 1, kPassage));
    const bool reaction = s.budget.reaction;
    REQUIRE(step(s, {1, ActionKind::EndConcentration}).ok);
    CHECK_FALSE(concentrating_on(s, 1, kPassage));
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK(find(s, 1)->innate_invisible);
    CHECK(s.budget.reaction == reaction);
    CHECK(track_evidence(s, 1, TrackEvidence::Physical) == TrackAnswer::Unanswered);
    CHECK(track_evidence(s, 1, TrackEvidence::Magical) == TrackAnswer::Unanswered);
}

TEST_CASE("invisible passage: an attack ends it") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, {1, ActionKind::Attack, 2, 0}).ok);
    CHECK_FALSE(find(s, 1)->has(Condition::Invisible));
    CHECK_FALSE(concentrating_on(s, 1, kPassage));
    CHECK(track_evidence(s, 1, TrackEvidence::Physical) == TrackAnswer::Unanswered);
}

TEST_CASE("invisible passage: casting a spell ends it") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kFireBolt, 2));
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(find(s, 1)->has(Condition::Invisible));
    CHECK_FALSE(concentrating_on(s, 1, kPassage));
}

TEST_CASE("invisible passage: it occupies the one concentration channel") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kConc, 1, 1)).ok);
    CHECK(concentrating_on(s, 1, kConc));
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    CHECK(concentrating_on(s, 1, kPassage));
    CHECK(find(s, 1)->has(Condition::Invisible));
    int passage = 0;
    for (const auto& e : s.effects) passage += e.spell_id == kPassage;
    CHECK(passage == 1);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    CHECK_FALSE(s.budget.action);
    CHECK(concentrating_on(s, 1, kPassage));
    passage = 0;
    for (const auto& e : s.effects) passage += e.spell_id == kPassage;
    CHECK(passage == 1);
}

TEST_CASE("invisible passage: dispel magic does not end it") {
    Combatant foe = monster(2);
    foe.team = 1;
    never_react(foe);
    Combatant mage = wizard(3, 0);
    mage.spells_known.push_back(kDispel);
    never_react(mage);
    auto s = begin_encounter({hag(1), foe, mage}, 3, nullptr, hooks());
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kPassage, 1)).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 3);
    auto r = step(s, cast(3, kDispel, 1, 3));
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK(concentrating_on(s, 1, kPassage));
}

TEST_CASE("invisible passage: replay matches") {
    auto run = [] {
        Combatant a = hag(1);
        Combatant foe = monster(2);
        foe.team = 1;
        never_react(foe);
        auto s = begin_encounter({a, foe}, 9, nullptr, hooks());
        end_turns_until(s, 1);
        ActionLog log;
        REQUIRE(step_logged(s, cast(1, kPassage, 1), log).status == StepStatus::Done);
        REQUIRE(step_logged(s, {1, ActionKind::EndConcentration}, log).status == StepStatus::Done);
        return std::make_pair(std::move(s), log);
    };
    const auto first = run();
    Combatant a = hag(1);
    Combatant foe = monster(2);
    foe.team = 1;
    never_react(foe);
    auto again = begin_encounter({a, foe}, 9, nullptr, hooks());
    end_turns_until(again, 1);
    for (const auto& e : first.second) REQUIRE(replay_step(again, e).status == StepStatus::Done);
    CHECK_FALSE(find(again, 1)->has(Condition::Invisible));
    CHECK_FALSE(concentrating_on(again, 1, kPassage));
    CHECK(again.effects.size() == first.first.effects.size());
    CHECK(track_evidence(again, 1, TrackEvidence::Physical) == TrackAnswer::Unanswered);
}
