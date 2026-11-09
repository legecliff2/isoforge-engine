// B15: Horrific Appearance (trait) and Death Glare (save_effect). No sea_hag.json.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kAppear = spell_id_of("test.hag#horrific_appearance");
const uint32_t kGlare = spell_id_of("test.hag#death_glare");

struct See { bool los = true; bool invisible = false; };

SpellDef appearance(int dc) {
    SpellDef d;
    d.id = kAppear; d.content_id = "test.hag#horrific_appearance"; d.is_spell = false; d.magical = false;
    d.kind = SpellEffectKind::Zone; d.fixed_dc = int8_t(dc); d.level = 0;
    d.zone.shape = AreaShape::Sphere; d.zone.anchor = ZoneAnchor::Creature; d.zone.size_ft = 30;
    ZoneTriggerRow row;
    row.on = ZoneTrigger::StartTurn; row.group = 1;
    row.effect.kind = SpellEffectKind::Save; row.effect.save_ability = Ability::Wis;
    row.effect.applies_condition = true; row.effect.condition = Condition::Frightened;
    row.effect.repeat_save = RepeatSave::EndOfTurn;
    row.condition_rounds = 10; row.success_immunity_hours = 24;
    row.creature_types = bit(CreatureType::Humanoid);
    row.must_see_source = true; row.repeat_los_disadvantage = true;
    row.immunity_on_end = true; row.avert_eyes = true;
    d.zone.triggers = {row};
    return d;
}

SpellDef glare(int dc) {
    SpellDef d;
    d.id = kGlare; d.content_id = "test.hag#death_glare"; d.is_spell = false; d.magical = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Wis; d.fixed_dc = int8_t(dc);
    d.range_ft = 30; d.targets = 1; d.drop_to_zero = true;
    d.has_target_condition = true; d.target_condition = Condition::Frightened;
    d.caster_sees_target = true; d.target_sees_source = true;
    return d;
}

EncounterHooks hooks(const std::shared_ptr<See>& see, int dc) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(appearance(dc));
    book->add(glare(dc));
    h.spells = book;
    h.zone_contains = [](const EncounterState&, uint32_t, CombatantId c, bool) { return c == 1; };
    h.sight_facts = [see](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f;
        f.line_of_sight = see->los;
        f.subject_invisible = see->invisible;
        f.distance_ft = 10;
        return f;
    };
    return h;
}

EncounterState board(int dc, const std::shared_ptr<See>& see, uint64_t seed, bool glare_too = false) {
    auto hag = monster(2);
    hag.hp = hag.hp_max = 40;
    if (glare_too) hag.spells_known = {kGlare};
    auto s = begin_encounter({pc(1, 30), hag}, seed, nullptr, hooks(see, dc));
    REQUIRE(add_feature_aura(s, 2, kAppear) != 0);
    return s;
}

bool to_pc(EncounterState& s, int depth = 0) {
    if (depth >= 4) return false;
    for (int i = 0; i < 6 && current_actor(s) != 1; ++i) {
        if (s.budget.gaze_pending && !step(s, {current_actor(s), ActionKind::GazeResponse}).ok) return false;
        if (s.budget.appearance_pending && !step(s, {current_actor(s), ActionKind::AppearanceResponse}).ok) return false;
        if (!step(s, {current_actor(s), ActionKind::EndTurn}).ok) return false;
    }
    if (current_actor(s) != 1) return false;
    if (!s.budget.appearance_pending) {
        if (s.budget.gaze_pending && !step(s, {1, ActionKind::GazeResponse}).ok) return false;
        if (!step(s, {1, ActionKind::EndTurn}).ok) return false;
        return to_pc(s, depth + 1);
    }
    return true;
}

ActionRequest look() { return {1, ActionKind::AppearanceResponse}; }
ActionRequest avert() { ActionRequest r{1, ActionKind::AppearanceResponse}; r.avert_gaze = true; return r; }

const ActiveModifier* avert_mod(const EncounterState& s) {
    for (const auto& m : s.modifiers)
        if (m.target == 1 && m.spec.stat == ModStat::AttackRoll && m.spec.mode == RollMode::Disadvantage) return &m;
    return nullptr;
}

}  // namespace

TEST_CASE("horrific appearance: look frightens; avert skips the save and is not gaze_averted") {
    auto see = std::make_shared<See>();
    auto s = board(30, see, 4);
    REQUIRE(to_pc(s));
    REQUIRE(s.budget.appearance_pending);
    CHECK(step(s, {1, ActionKind::Dodge}).error == "appearance pending: answer with AppearanceResponse first");
    auto la = legal_actions(s);
    REQUIRE(la.size() == 2);
    CHECK(la[0].kind == ActionKind::AppearanceResponse); CHECK_FALSE(la[0].avert_gaze);
    CHECK(la[1].avert_gaze);
    auto looked = step(s, look());
    REQUIRE(looked.ok);
    CHECK(count(looked.events, EventKind::SavingThrowRolled) == 1);
    CHECK(find(s, 1)->has(Condition::Frightened));
    CHECK_FALSE(s.budget.appearance_pending);
    CHECK(s.aura_immunities.empty());
    bool los_flag = false;
    for (const auto& e : s.effects)
        if (e.target == 1 && e.condition == Condition::Frightened) {
            los_flag = e.repeat_los_disadvantage;
            CHECK(e.immunity_on_end_hours == 24);
            CHECK(e.repeat_save == RepeatSave::EndOfTurn);
        }
    CHECK(los_flag);

    auto again = board(30, see, 4);
    REQUIRE(to_pc(again));
    auto av = step(again, avert());
    REQUIRE(av.ok);
    CHECK(count(av.events, EventKind::SavingThrowRolled) == 0);
    CHECK_FALSE(find(again, 1)->has(Condition::Frightened));
    CHECK_FALSE(find(again, 1)->gaze_averted);
    const auto* mod = avert_mod(again);
    REQUIRE(mod);
    CHECK(mod->mark == 2);
    CHECK(mod->spec.mark_scope == 1);
    CHECK(mod->timed.kind == Expiry::StartOfTargetNextTurn);
}

TEST_CASE("horrific appearance: a success grants 24-hour immunity; a beast is not asked; surprised cannot avert") {
    auto see = std::make_shared<See>();
    auto s = board(1, see, 3);
    REQUIRE(to_pc(s));
    auto ok = step(s, look());
    REQUIRE(ok.ok);
    CHECK_FALSE(find(s, 1)->has(Condition::Frightened));
    REQUIRE(s.aura_immunities.size() == 1);
    CHECK(s.aura_immunities[0].creature == 1);
    CHECK(s.aura_immunities[0].source == 2);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    for (int i = 0; i < 4 && current_actor(s) != 1; ++i)
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(s) == 1);
    CHECK_FALSE(s.budget.appearance_pending);

    auto beast = board(30, see, 3);
    mut(beast, 1).creature_type = CreatureType::Beast;
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(beast.budget.appearance_pending);
        REQUIRE(step(beast, {current_actor(beast), ActionKind::EndTurn}).ok);
    }

    auto surprised = board(30, see, 9);
    if (current_actor(surprised) == 1 && !surprised.budget.appearance_pending)
        REQUIRE(step(surprised, {1, ActionKind::EndTurn}).ok);
    mut(surprised, 1).surprised = true;
    for (int i = 0; i < 6 && current_actor(surprised) != 1; ++i)
        REQUIRE(step(surprised, {current_actor(surprised), ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(surprised) == 1);
    REQUIRE(surprised.budget.appearance_pending);
    auto only = legal_actions(surprised);
    REQUIRE(only.size() == 1);
    CHECK_FALSE(only[0].avert_gaze);
    CHECK(step(surprised, avert()).error == "surprised: cannot avert the eyes");
    REQUIRE(step(surprised, look()).ok);
    CHECK(find(surprised, 1)->has(Condition::Frightened));
}

TEST_CASE("horrific appearance: the end of the turn repeats the save, harder in line of sight") {
    // The initial save needs the creature to see the hag. Line of sight changes only the repeat:
    // disadvantage while the hag is in line of sight, a straight roll when she is not. Same seed,
    // so the straight d20 is the first die of the disadvantaged roll.
    int compared = 0, stricter = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        auto los_see = std::make_shared<See>();
        auto plain_see = std::make_shared<See>();
        auto los = board(30, los_see, seed);
        auto plain = board(30, plain_see, seed);
        if (!to_pc(los) || !to_pc(plain)) continue;
        REQUIRE(step(los, look()).ok);
        REQUIRE(step(plain, look()).ok);
        REQUIRE(find(los, 1)->has(Condition::Frightened));
        REQUIRE(find(plain, 1)->has(Condition::Frightened));
        for (auto& e : los.effects) if (e.target == 1) e.dc = 11;
        for (auto& e : plain.effects) if (e.target == 1) e.dc = 11;
        plain_see->los = false;
        auto e1 = step(los, {1, ActionKind::EndTurn});
        auto e2 = step(plain, {1, ActionKind::EndTurn});
        REQUIRE(e1.ok);
        REQUIRE(e2.ok);
        const Event* a = nullptr;
        const Event* b = nullptr;
        for (const auto& e : e1.events) if (e.kind == EventKind::SavingThrowRolled) a = &e;
        for (const auto& e : e2.events) if (e.kind == EventKind::SavingThrowRolled) b = &e;
        REQUIRE(a);
        REQUIRE(b);
        CHECK(a->a <= b->a);   // kept natural under disadvantage is the lower die
        ++compared;
        if (a->a < b->a) ++stricter;
    }
    CHECK(compared >= 1);
    CHECK(stricter >= 1);
}

void reach_hag(EncounterState& s, bool avert_pc) {
    bool answered = false;
    for (int i = 0; i < 10 && !(answered && current_actor(s) == 2); ++i) {
        if (s.budget.appearance_pending) {
            ActionRequest r{current_actor(s), ActionKind::AppearanceResponse};
            r.avert_gaze = avert_pc;
            REQUIRE(step(s, r).ok);
            answered = true;
            continue;
        }
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    }
    REQUIRE(answered);
    REQUIRE(current_actor(s) == 2);
}

TEST_CASE("death glare: a frightened creature drops to 0; a calm one is refused; a blind one makes no save") {
    auto see = std::make_shared<See>();
    auto s = board(30, see, 2, true);
    reach_hag(s, false);   // the pc looked and failed the DC 30 save: frightened
    CHECK(find(s, 1)->has(Condition::Frightened));
    const int before = find(s, 1)->hp;
    auto r = step(s, cast(2, kGlare, 1, 0));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::DamageDealt) == 0);
    CHECK(find(s, 1)->hp == 0);
    CHECK_FALSE(find(s, 1)->dead);   // the pc uses death saves
    CHECK(find(s, 1)->has(Condition::Unconscious));
    CHECK(before > 0);

    auto calm = board(30, see, 2, true);
    reach_hag(calm, true);   // averted: not frightened
    CHECK_FALSE(find(calm, 1)->has(Condition::Frightened));
    CHECK(calm.budget.action);
    auto refused = step(calm, cast(2, kGlare, 1, 0));
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "target is not frightened");
    CHECK(calm.budget.action);
    CHECK(find(calm, 1)->hp > 0);

    auto blind = board(30, see, 2, true);
    reach_hag(blind, false);
    add_condition(mut(blind, 1), Condition::Blinded);
    auto skipped = step(blind, cast(2, kGlare, 1, 0));
    REQUIRE(skipped.ok);
    CHECK(find(blind, 1)->hp > 0);
    bool flagged = false;
    for (const auto& e : skipped.events)
        if (e.kind == EventKind::FeatureTriggered && e.c == 3) flagged = true;
    CHECK(flagged);

    Combatant victim = monster(1);
    victim.hp = victim.hp_max = 12;
    victim.team = 0;
    auto hag = monster(2);
    hag.hp = hag.hp_max = 40;
    hag.spells_known = {kGlare};
    auto dead = begin_encounter({victim, hag}, 2, nullptr, hooks(see, 30));
    end_turns_until(dead, 2);
    add_condition(mut(dead, 1), Condition::Frightened);
    auto killed = step(dead, cast(2, kGlare, 1, 0));
    REQUIRE(killed.ok);
    CHECK(count(killed.events, EventKind::DamageDealt) == 0);
    CHECK(find(dead, 1)->dead);
    CHECK(find(dead, 1)->hp == 0);
}
