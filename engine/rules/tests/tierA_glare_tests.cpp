// Tier-A §26 (mummy Dreadful Glare, Lich rulings): also_on_fail_by on one effect entry, paralyzed despite a frightened
// immunity, immunity after a success (EncounterState::action_immunities, 0xE1), and no roll for a target that cannot see.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kGlare = spell_id_of("test.tierA#glare");
const uint32_t kGlareMiss = spell_id_of("test.tierA#glare_wide");   // margin 30: never met
const uint32_t kGlareEasy = spell_id_of("test.tierA#glare_easy");   // DC 1: always a success

SpellDef glare(uint32_t id, const char* cid, int dc, int margin) {
    SpellDef g; g.id = id; g.content_id = cid; g.level = 0; g.range_ft = 60;
    g.kind = SpellEffectKind::Save; g.save_ability = Ability::Wis; g.fixed_dc = int8_t(dc); g.magical = true;
    g.applies_condition = true; g.condition = Condition::Frightened;
    g.expiry = uint8_t(Expiry::EndOfCasterNextTurn);
    g.also_margin = uint8_t(margin); g.also_condition = Condition::Paralyzed;
    g.success_immunity_hours = 24; g.target_sees_source = true;
    return g;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(glare(kGlare, "test.tierA#glare", 30, 1));
    book->add(glare(kGlareMiss, "test.tierA#glare_wide", 30, 30));
    book->add(glare(kGlareEasy, "test.tierA#glare_easy", 1, 5));
    h.spells = book;
    return h;
}
Combatant mummy(CombatantId id) {
    auto w = wizard(id);
    w.spells_known = {kGlare, kGlareMiss, kGlareEasy};
    return w;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = int16_t(300);
    return m;
}
EncounterState start(std::vector<Combatant> cs) {
    auto s = begin_encounter(std::move(cs), 3, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
const Event* find_ev(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
}  // namespace

TEST_CASE("Tier-A §26: defaults add nothing") {
    SpellDef d;
    CHECK(d.also_margin == 0); CHECK(d.success_immunity_hours == 0); CHECK_FALSE(d.target_sees_source);
    EncounterState s;
    CHECK(s.action_immunities.empty());
}

TEST_CASE("Tier-A §26: failing by the margin adds paralyzed on the SAME entry; its expiry ends both") {
    auto s = start({mummy(1), dummy(2)});
    REQUIRE(step(s, cast(1, kGlare, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK(mut(s, 2).has(Condition::Paralyzed));
    REQUIRE(s.effects.size() == 1);   // one entry
    CHECK(s.effects[0].condition == Condition::Frightened);
    CHECK(s.effects[0].has_also);
    CHECK(s.effects[0].also == Condition::Paralyzed);
    CHECK(s.action_immunities.empty());   // a failure grants nothing
    // until the end of the mummy's next turn: through the target's turn, gone after the mummy's next turn
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    CHECK(mut(s, 2).has(Condition::Paralyzed));
    (void)step(s, {1, ActionKind::EndTurn});
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK_FALSE(mut(s, 2).has(Condition::Paralyzed));
    CHECK(s.effects.empty());
}

TEST_CASE("Tier-A §26: a plain failure (margin not met) only frightens") {
    auto s = start({mummy(1), dummy(2)});
    REQUIRE(step(s, cast(1, kGlareMiss, 2)).ok);
    CHECK(mut(s, 2).has(Condition::Frightened));
    CHECK_FALSE(mut(s, 2).has(Condition::Paralyzed));
    REQUIRE(s.effects.size() == 1);
    CHECK_FALSE(s.effects[0].has_also);
}

TEST_CASE("Tier-A §26 (pinned ruling): immune to frightened, the margin still paralyzes") {
    auto t = dummy(2); t.condition_immunities = bit(Condition::Frightened);
    auto s = start({mummy(1), t});
    REQUIRE(step(s, cast(1, kGlare, 2)).ok);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(mut(s, 2).has(Condition::Paralyzed));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].condition == Condition::Paralyzed);
    CHECK_FALSE(s.effects[0].has_also);
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    (void)step(s, {1, ActionKind::EndTurn});
    CHECK_FALSE(mut(s, 2).has(Condition::Paralyzed));   // same duration
    // immune to frightened and the margin not met: nothing
    auto s2 = start({mummy(1), t});
    REQUIRE(step(s2, cast(1, kGlareMiss, 2)).ok);
    CHECK_FALSE(mut(s2, 2).has(Condition::Paralyzed));
    CHECK(s2.effects.empty());
}

TEST_CASE("Tier-A §26: a success grants immunity to the action (every user), and the immune target makes no roll") {
    auto s = start({mummy(1), dummy(2), mummy(3)});
    REQUIRE(step(s, cast(1, kGlareEasy, 2)).ok);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    REQUIRE(s.action_immunities.size() == 1);
    CHECK(s.action_immunities[0].creature == 2);
    CHECK(s.action_immunities[0].source == 0);   // same_action: "the glare of all mummies"
    CHECK(s.action_immunities[0].spell_id == kGlareEasy);
    CHECK(s.action_immunities[0].until_round == uint32_t(s.round) + 24u * 600u);
    end_turns_until(s, 3);
    auto r = step(s, cast(3, kGlareEasy, 2));
    REQUIRE(r.ok);
    CHECK_FALSE(find_ev(r.events, EventKind::SavingThrowRolled));
    CHECK(s.action_immunities.size() == 1);
    s.action_immunities[0].until_round = uint32_t(s.round);   // expired: saves again
    end_turns_until(s, 1);
    auto r2 = step(s, cast(1, kGlareEasy, 2));
    CHECK(find_ev(r2.events, EventKind::SavingThrowRolled));
    // scope same_source: only from that user
    SpellDef d = glare(1, "x", 1, 5); d.success_immunity_scope = 1;
    CHECK(d.success_immunity_scope == 1);
}

TEST_CASE("Tier-A §26: a target that cannot see the user makes no roll and gains no immunity (FeatureTriggered c = 3)") {
    auto t = dummy(2); t.conditions |= bit(Condition::Blinded);
    auto s = start({mummy(1), t});
    auto r = step(s, cast(1, kGlareEasy, 2));
    REQUIRE(r.ok);
    CHECK_FALSE(find_ev(r.events, EventKind::SavingThrowRolled));
    const Event* f = find_ev(r.events, EventKind::FeatureTriggered);
    REQUIRE(f);
    CHECK(f->target == 2);
    CHECK(f->a == int32_t(kGlareEasy));
    CHECK(f->c == 3);
    CHECK(s.action_immunities.empty());
    CHECK(s.effects.empty());
}
