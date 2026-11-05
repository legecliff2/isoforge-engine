// Visible figure (CONTENT_CONVENTIONS §52). Not a form. No green_hag.json.
// PRODUCT CHOICES are marked in the cases that lock them: a second start does not spend the action,
// and discernment is observer+subject across later activations.
#include <doctest/doctest.h>

#include <memory>
#include <vector>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kFigure = spell_id_of("test.figure#illusory_appearance");
const uint32_t kHard = spell_id_of("test.figure#hard");

SpellDef figure(uint32_t id, int dc) {
    SpellDef d;
    d.id = id; d.content_id = "test.figure"; d.is_spell = false; d.magical = false;
    d.kind = SpellEffectKind::ApplyCondition; d.illusory_appearance = true; d.level = 0;
    d.fixed_dc = int8_t(dc); d.range_ft = 0;
    return d;
}
EncounterHooks hooks(LoadoutView* gear) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(figure(kFigure, 1));
    book->add(figure(kHard, 30));
    h.spells = book;
    if (gear) h.loadout_of = [gear](const EncounterState&, CombatantId id) {
        return id == 1 ? gear : nullptr;
    };
    return h;
}
Combatant hag(CombatantId id) {
    Combatant c = monster(id);
    c.team = 0;
    c.creature_type = CreatureType::Fey;
    c.size = Size::Medium;
    c.spells_known = {kFigure, kHard};
    c.abilities.score[std::size_t(Ability::Int)] = 10;
    never_react(c);
    return c;
}
Combatant watcher(CombatantId id) {
    Combatant c = pc(id, 20);
    c.team = 1;
    c.abilities.score[std::size_t(Ability::Int)] = 10;
    never_react(c);
    return c;
}
EncounterState start(std::vector<Combatant> cs, LoadoutView* gear = nullptr) {
    auto s = begin_encounter(std::move(cs), 1, nullptr, hooks(gear));
    end_turns_until(s, 1);
    return s;
}
ActionRequest start_figure(uint32_t spell = kFigure) {
    ActionRequest r = cast(1, spell, 0);
    return r;
}
ActionRequest end_figure() {
    ActionRequest r = cast(1, kFigure, 0);
    r.as_bonus_action = true;
    return r;
}
}  // namespace

TEST_CASE("visible figure: an action stores humanoid shape at her size and does not change the body") {
    LoadoutView gear;
    gear.main = HeldItem{7, 1, 1, 0};
    gear.off = HeldItem{7, 1, 1, 0};   // the same instance in both hands is stored once
    gear.carried = {HeldItem{8, 2, 1, 0}, HeldItem{0, 0, 1, 0}};
    gear.armor_def = 3;
    auto s = start({hag(1), watcher(2)}, &gear);
    const auto attacks = mut(s, 1).attacks.size();
    REQUIRE(step(s, start_figure()).ok);
    CHECK(mut(s, 1).form == FormKind::None);
    CHECK_FALSE(mut(s, 1).shapechanger);
    CHECK(mut(s, 1).size == Size::Medium);
    CHECK(mut(s, 1).creature_type == CreatureType::Fey);
    CHECK(mut(s, 1).attacks.size() == attacks);
    const auto view = visible_figure(s, 2, 1);
    CHECK(view.active);
    CHECK(view.shape == FigureShape::Humanoid);
    CHECK(view.size == Size::Medium);
    CHECK(view.armor_def == 3);
    CHECK(view.covered_items == std::vector<uint32_t>{7, 8});
    CHECK_FALSE(view.discerned);
    const auto touch = physical_mismatch(s, 1);
    CHECK(touch.active);
    CHECK(touch.presented_shape == FigureShape::Humanoid);
    CHECK(touch.presented_size == Size::Medium);
    CHECK(touch.true_size == Size::Medium);
    CHECK(touch.true_type == CreatureType::Fey);
    CHECK(touch.true_attack_count == attacks);
    CHECK_FALSE(view.discerned);
    gear.main.instance = 99;
    gear.armor_def = 0;
    const auto later = visible_figure(s, 2, 1);
    CHECK(later.covered_items == std::vector<uint32_t>{7, 8});
    CHECK(later.armor_def == 3);
}

TEST_CASE("visible figure: a second start does not spend the action") {
    auto s = start({hag(1), watcher(2)});
    REQUIRE(step(s, start_figure()).ok);
    CHECK_FALSE(s.budget.action);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK(s.budget.action);
    auto again = step(s, start_figure());
    CHECK_FALSE(again.ok);
    CHECK(again.error == "illusory appearance: already disguised");
    CHECK(s.budget.action);
    CHECK(visible_figure(s, 2, 1).instance == 1);
}

TEST_CASE("visible figure: a bonus action ends it; death ends it; 0 hit points does not") {
    auto s = start({hag(1), watcher(2)});
    REQUIRE(step(s, start_figure()).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, end_figure()).ok);
    CHECK(s.budget.action);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK_FALSE(visible_figure(s, 2, 1).active);
    REQUIRE(step(s, start_figure()).ok);
    mut(s, 1).hp = 0;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK(visible_figure(s, 2, 1).active);
}

TEST_CASE("visible figure: dying clears the figure and keeps discernment") {
    auto s = start({hag(1), watcher(2)});
    mut(s, 2).attacks[0].to_hit = 30;
    mut(s, 2).attacks[0].damage = {8, 8, 20};
    REQUIRE(step(s, start_figure()).ok);
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::InspectFigure, 1}).ok);
    CHECK(visible_figure(s, 2, 1).discerned);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto killed = step(s, {2, ActionKind::Attack, 1, 0});
    REQUIRE_MESSAGE(killed.ok, killed.error);
    CHECK(mut(s, 1).dead);
    CHECK_FALSE(visible_figure(s, 2, 1).active);
    CHECK(visible_figure(s, 2, 1).discerned);
}

TEST_CASE("visible figure: a hit is not discernment; a successful inspect persists across a later activation") {
    auto s = start({hag(1), watcher(2), watcher(3)});
    mut(s, 2).attacks[0].to_hit = 30;
    mut(s, 2).attacks[0].damage = {1, 4, 0};
    REQUIRE(step(s, start_figure()).ok);
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::Attack, 1, 0}).ok);
    CHECK(mut(s, 1).hp < mut(s, 1).hp_max);
    CHECK_FALSE(visible_figure(s, 2, 1).discerned);
    CHECK(visible_figure(s, 2, 1).active);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto saw = step(s, {2, ActionKind::InspectFigure, 1});
    REQUIRE_MESSAGE(saw.ok, saw.error);
    CHECK(visible_figure(s, 2, 1).discerned);
    CHECK(visible_figure(s, 3, 1).active);
    CHECK_FALSE(visible_figure(s, 3, 1).discerned);
    CHECK(visible_figure(s, 2, 1).shape == FigureShape::Humanoid);
    end_turns_until(s, 1);
    REQUIRE(step(s, end_figure()).ok);
    CHECK_FALSE(visible_figure(s, 2, 1).active);
    CHECK(visible_figure(s, 2, 1).discerned);
    REQUIRE(step(s, start_figure(kHard)).ok);
    CHECK(visible_figure(s, 2, 1).active);
    CHECK(visible_figure(s, 2, 1).discerned);
    CHECK(visible_figure(s, 2, 1).instance != 1);
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::InspectFigure, 1}).ok);
    CHECK(visible_figure(s, 2, 1).discerned);
}

TEST_CASE("visible figure: inspect without sight or without a figure does not spend the action") {
    auto s = start({hag(1), watcher(2)});
    s.hooks.sight_facts = [](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f;
        f.line_of_sight = false;
        return f;
    };
    end_turns_until(s, 2);
    CHECK(s.budget.action);
    auto blind = step(s, {2, ActionKind::InspectFigure, 1});
    CHECK_FALSE(blind.ok);
    CHECK(blind.error == "illusory appearance: no illusion");
    CHECK(s.budget.action);
    end_turns_until(s, 1);
    REQUIRE(step(s, start_figure()).ok);
    end_turns_until(s, 2);
    auto hidden = step(s, {2, ActionKind::InspectFigure, 1});
    CHECK_FALSE(hidden.ok);
    CHECK(hidden.error == "illusory appearance: cannot see the illusion");
    CHECK(s.budget.action);
    CHECK_FALSE(visible_figure(s, 2, 1).discerned);
}

TEST_CASE("visible figure: legal actions offer the start, then the bonus end, and an inspect") {
    auto s = start({hag(1), watcher(2)});
    bool start_listed = false, end_listed = false, inspect = false;
    for (const auto& q : legal_actions(s)) {
        start_listed = start_listed || (q.kind == ActionKind::CastSpell && q.spell_id == kFigure && !q.as_bonus_action);
        end_listed = end_listed || (q.kind == ActionKind::CastSpell && q.as_bonus_action);
        inspect = inspect || q.kind == ActionKind::InspectFigure;
    }
    CHECK(start_listed);
    CHECK_FALSE(end_listed);
    CHECK_FALSE(inspect);
    REQUIRE(step(s, start_figure()).ok);
    end_turns_until(s, 2);
    inspect = false;
    for (const auto& q : legal_actions(s)) inspect = inspect || (q.kind == ActionKind::InspectFigure && q.target == 1);
    CHECK(inspect);
    end_turns_until(s, 1);
    start_listed = false;
    end_listed = false;
    for (const auto& q : legal_actions(s)) {
        start_listed = start_listed || (q.kind == ActionKind::CastSpell && q.spell_id == kFigure && !q.as_bonus_action);
        end_listed = end_listed || (q.kind == ActionKind::CastSpell && q.spell_id == kFigure && q.as_bonus_action);
    }
    CHECK_FALSE(start_listed);
    CHECK(end_listed);
}
