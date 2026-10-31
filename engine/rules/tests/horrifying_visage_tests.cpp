// B17 Ghost Horrifying Visage (SRD 5.1, CONTENT_CONVENTIONS §49). Area Wisdom save, non-undead, must see the
// user, frightened for a minute with an end-of-turn repeat, fail-by-5 ages 1d4 x 10 years, and a success or the
// effect ending grants 24-hour immunity to this user's action. No ghost.json.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kFail = spell_id_of("test.b17#fail");     // DC 30, margin 5: fail and age
const uint32_t kWide = spell_id_of("test.b17#wide");     // DC 30, margin 30: fail, do not age
const uint32_t kEasy = spell_id_of("test.b17#easy");     // DC 1: success
const uint32_t kShort = spell_id_of("test.b17#short");   // DC 30, 1 round
const uint32_t kSingle = spell_id_of("test.b17#single"); // no area: naming an undead is refused

SpellDef visage(uint32_t id, int dc, int margin, int rounds, bool area) {
    SpellDef g;
    g.id = id; g.level = 0; g.is_spell = false; g.kind = SpellEffectKind::Save;
    g.save_ability = Ability::Wis; g.fixed_dc = int8_t(dc); g.magical = false;
    if (area) { g.area = AreaShape::Sphere; g.area_size_ft = 60; g.range_ft = 0; }
    else g.range_ft = 60;
    g.applies_condition = true; g.condition = Condition::Frightened;
    g.repeat_save = RepeatSave::EndOfTurn;
    g.expiry = uint8_t(Expiry::Rounds); g.duration_rounds = uint16_t(rounds);
    g.excluded_types = bit(CreatureType::Undead);
    g.target_sees_source = true;
    g.success_immunity_hours = 24; g.success_immunity_scope = 1;
    g.immune_on_effect_end = true;
    g.age_margin = uint8_t(margin); g.age_dice = {1, 4, 0}; g.age_years_per = 10;
    return g;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(visage(kFail, 30, 5, 10, true));
    book->add(visage(kWide, 30, 30, 10, true));
    book->add(visage(kEasy, 1, 5, 10, true));
    book->add(visage(kShort, 30, 5, 1, true));
    book->add(visage(kSingle, 30, 5, 10, false));
    h.spells = book;
    return h;
}
Combatant ghost(CombatantId id) {
    auto w = wizard(id);
    w.creature_type = CreatureType::Undead;
    w.spells_known = {kFail, kWide, kEasy, kShort, kSingle};
    return w;
}
Combatant person(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = int16_t(300);
    m.creature_type = CreatureType::Humanoid;
    return m;
}
EncounterState start(std::vector<Combatant> cs) {
    auto s = begin_encounter(std::move(cs), 3, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
const Event* find_ev(const std::vector<Event>& ev, EventKind k, CombatantId actor = 0) {
    for (const auto& e : ev)
        if (e.kind == k && (actor == 0 || e.actor == actor)) return &e;
    return nullptr;
}
int saves_by(const std::vector<Event>& ev, CombatantId who) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::SavingThrowRolled && e.actor == who) ++n;
    return n;
}
bool aged_decade(uint16_t years) {
    return years == 10 || years == 20 || years == 30 || years == 40;
}
}  // namespace

TEST_CASE("B17: defaults add no years") {
    SpellDef d;
    CHECK(d.age_margin == 0);
    CHECK_FALSE(d.immune_on_effect_end);
    Combatant c = person(1);
    CHECK(c.aged_years == 0);
}

TEST_CASE("B17: a failure frightens for the minute and ages 1d4 x 10; the effect records immunity on end") {
    auto s = start({ghost(1), person(2)});
    REQUIRE(step(s, cast(1, kFail, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK(aged_decade(mut(s, 2).aged_years));
    CHECK(s.action_immunities.empty());
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].condition == Condition::Frightened);
    CHECK(s.effects[0].caster == 1);
    CHECK(s.effects[0].repeat_save == RepeatSave::EndOfTurn);
    CHECK(s.effects[0].timed.kind == Expiry::Rounds);
    CHECK(s.effects[0].timed.rounds_left == 10);
    CHECK(s.effects[0].action_immunity_on_end_hours == 24);
    CHECK(s.effects[0].action_immunity_on_end_scope == 1);
    CHECK_FALSE(s.effects[0].repeat_save_magical);
}

TEST_CASE("B17: failing by less than the margin frightens and does not age") {
    auto s = start({ghost(1), person(2)});
    REQUIRE(step(s, cast(1, kWide, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK(mut(s, 2).aged_years == 0);
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].action_immunity_on_end_hours == 24);
}

TEST_CASE("B17: a success grants same-source immunity and does not age") {
    auto s = start({ghost(1), person(2), ghost(3)});
    REQUIRE(step(s, cast(1, kEasy, 2)).ok);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(mut(s, 2).aged_years == 0);
    CHECK(s.effects.empty());
    REQUIRE(s.action_immunities.size() == 1);
    CHECK(s.action_immunities[0].creature == 2);
    CHECK(s.action_immunities[0].source == 1);
    CHECK(s.action_immunities[0].spell_id == kEasy);
    CHECK(s.action_immunities[0].until_round == uint32_t(s.round) + 24u * 600u);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    auto again = step(s, cast(1, kEasy, 2));
    REQUIRE(again.ok);
    CHECK(saves_by(again.events, 2) == 0);   // still immune to this ghost
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 3);
    auto other = step(s, cast(3, kEasy, 2));
    REQUIRE(other.ok);
    CHECK(saves_by(other.events, 2) == 1);   // this ghost only
}

TEST_CASE("B17: a creature that cannot see the ghost makes no save and does not age") {
    auto t = person(2);
    t.conditions |= bit(Condition::Blinded);
    auto s = start({ghost(1), t});
    auto r = step(s, cast(1, kEasy, 2));
    REQUIRE(r.ok);
    CHECK(saves_by(r.events, 2) == 0);
    const Event* f = find_ev(r.events, EventKind::FeatureTriggered, 1);
    REQUIRE(f);
    CHECK(f->target == 2);
    CHECK(f->c == 3);
    CHECK(s.action_immunities.empty());
    CHECK(mut(s, 2).aged_years == 0);
    CHECK(s.effects.empty());
}

TEST_CASE("B17: undead in the area are skipped; a single-target naming of one is refused") {
    auto dead = person(3);
    dead.creature_type = CreatureType::Undead;
    auto s = start({ghost(1), person(2), dead});
    ActionRequest both = cast(1, kFail, 2);
    both.targets = {2, 3};
    auto r = step(s, both);
    REQUIRE(r.ok);
    CHECK(saves_by(r.events, 2) == 1);
    CHECK(saves_by(r.events, 3) == 0);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK_FALSE(mut(s, 3).has(Condition::Frightened));
    CHECK(mut(s, 3).aged_years == 0);
    CHECK(mut(s, 2).aged_years > 0);
    auto s2 = start({ghost(1), dead});
    auto refused = step(s2, cast(1, kSingle, 3));
    CHECK_FALSE(refused.ok);
    CHECK(std::string(refused.error).find("invalid creature type") != std::string::npos);
    CHECK(s2.budget.action);
    CHECK(mut(s2, 3).aged_years == 0);
}

TEST_CASE("B17: immune to frightened still ages on a fail by 5, and gains no immunity") {
    auto t = person(2);
    t.condition_immunities = bit(Condition::Frightened);
    auto s = start({ghost(1), t});
    REQUIRE(step(s, cast(1, kFail, 2)).ok);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(s.effects.empty());
    CHECK(s.action_immunities.empty());
    CHECK(aged_decade(mut(s, 2).aged_years));
    auto s2 = start({ghost(1), t});
    REQUIRE(step(s2, cast(1, kWide, 2)).ok);
    CHECK(mut(s2, 2).aged_years == 0);
    CHECK(s2.effects.empty());
}

TEST_CASE("B17: a successful repeat save ends the fright and grants the immunity; the years stay") {
    auto s = start({ghost(1), person(2)});
    REQUIRE(step(s, cast(1, kFail, 2)).ok);
    const uint16_t years = mut(s, 2).aged_years;
    REQUIRE(years > 0);
    REQUIRE(s.effects.size() == 1);
    s.effects[0].dc = 1;
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(s.effects.empty());
    CHECK(mut(s, 2).aged_years == years);
    REQUIRE(s.action_immunities.size() == 1);
    CHECK(s.action_immunities[0].creature == 2);
    CHECK(s.action_immunities[0].source == 1);
    CHECK(s.action_immunities[0].spell_id == kFail);
}

TEST_CASE("B17: the minute ending grants the same immunity") {
    auto s = start({ghost(1), person(2)});
    REQUIRE(step(s, cast(1, kShort, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK(s.action_immunities.empty());
    const uint16_t round = s.round;
    for (int i = 0; i < 8 && s.round == round; ++i)
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(s.round == uint16_t(round + 1));
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(s.effects.empty());
    REQUIRE(s.action_immunities.size() == 1);
    CHECK(s.action_immunities[0].source == 1);
    CHECK(s.action_immunities[0].spell_id == kShort);
    CHECK(mut(s, 2).aged_years > 0);
}
