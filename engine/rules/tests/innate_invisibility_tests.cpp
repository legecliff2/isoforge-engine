// B14 Innate Invisibility (CONTENT_CONVENTIONS §44): the trait holds Condition::Invisible.
// No duration, no concentration. An attack or a cast does not clear the bit.
// A spell with ends_on still ends; a creature without the trait still loses the condition.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kInvis = spell_id_of("test.spell.invisibility");

SpellDef invisibility() {
    SpellDef d;
    d.id = kInvis;
    d.content_id = "test.spell.invisibility";
    d.level = 2;
    d.range_ft = 5;
    d.kind = SpellEffectKind::ApplyCondition;
    d.condition = Condition::Invisible;
    d.applies_condition = true;
    d.concentration = true;
    d.targets = 2;
    d.duration = SpellDuration::Hours;
    d.duration_amount = 1;
    d.expiry = 1;
    d.duration_rounds = 600;
    d.ends_on = kEndsOnAttack | kEndsOnCast;
    return d;
}

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(invisibility());
    h.spells = book;
    return h;
}

Combatant stalker(CombatantId id) {
    auto c = monster(id);
    c.hp = c.hp_max = 104;
    c.ac = 14;
    c.innate_invisible = true;
    add_condition(c, Condition::Invisible);
    c.attacks = {Attack{"slam", 40, {1, 6, 3}, DamageType::Bludgeoning, 5, false, false}};
    c.spells_known = {kFireBolt};
    c.spellcasting_ability = Ability::Int;
    return c;
}

Combatant foe(CombatantId id) {
    auto c = pc(id, 40);
    c.team = 0;
    c.ac = 10;
    c.reaction_policy.fill(ReactionPolicy::Never);
    return c;
}

EncounterState start(std::vector<Combatant> cs, uint64_t seed = 7) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    return s;
}

bool removed_invisible(const std::vector<Event>& ev, CombatantId id) {
    for (const auto& e : ev)
        if (e.kind == EventKind::ConditionRemoved && e.actor == id && e.a == int32_t(Condition::Invisible)) return true;
    return false;
}

int invisible_effects(const EncounterState& s, CombatantId id) {
    int n = 0;
    for (const auto& e : s.effects)
        if (e.target == id && !e.no_condition && e.condition == Condition::Invisible) ++n;
    return n;
}

}  // namespace

TEST_CASE("innate invisibility: no duration, no concentration, attack and cast do not end it") {
    auto s = start({stalker(1), foe(2)});
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 1).innate_invisible);
    CHECK(mut(s, 1).concentration_spell == 0);
    CHECK(s.effects.empty());
    CHECK(attack_roll_mode(mut(s, 1), mut(s, 2), {}) == RollMode::Advantage);
    CHECK(attack_roll_mode(mut(s, 2), mut(s, 1), {}) == RollMode::Disadvantage);

    end_turns_until(s, 1);
    auto hit = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    REQUIRE_MESSAGE(hit.ok, hit.error);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 1).concentration_spell == 0);
    CHECK_FALSE(removed_invisible(hit.events, 1));
    CHECK(attack_roll_mode(mut(s, 1), mut(s, 2), {}) == RollMode::Advantage);
    CHECK(attack_roll_mode(mut(s, 2), mut(s, 1), {}) == RollMode::Disadvantage);

    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    ActionRequest cast{1, ActionKind::CastSpell, 2};
    cast.spell_id = kFireBolt;
    cast.spell_level = 0;
    auto fired = step(s, cast);
    REQUIRE_MESSAGE(fired.ok, fired.error);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 1).concentration_spell == 0);
    CHECK_FALSE(removed_invisible(fired.events, 1));
    CHECK(s.effects.empty());

    for (int i = 0; i < 12; ++i) REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 1).concentration_spell == 0);
    CHECK(invisible_effects(s, 1) == 0);
}

TEST_CASE("innate invisibility: spell ends_on still ends the spell, and only a creature without the trait loses Invisible") {
    auto plain = pc(3, 30);
    plain.team = 0;
    plain.reaction_policy.fill(ReactionPolicy::Never);
    auto wiz = wizard(2);
    wiz.spells_known.push_back(kInvis);
    wiz.reaction_policy.fill(ReactionPolicy::Never);
    auto s = start({stalker(1), wiz, plain});
    end_turns_until(s, 2);
    ActionRequest rq{2, ActionKind::CastSpell, 1};
    rq.spell_id = kInvis;
    rq.spell_level = 2;
    rq.targets = {1, 3};
    auto cast = step(s, rq);
    REQUIRE_MESSAGE(cast.ok, cast.error);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 3).has(Condition::Invisible));
    CHECK(mut(s, 2).concentration_spell == kInvis);
    CHECK(invisible_effects(s, 1) == 1);
    CHECK(invisible_effects(s, 3) == 1);

    end_turns_until(s, 3);
    auto plain_hit = step(s, ActionRequest{3, ActionKind::Attack, 1, 0, 0, false, true});
    REQUIRE_MESSAGE(plain_hit.ok, plain_hit.error);
    CHECK_FALSE(mut(s, 3).has(Condition::Invisible));
    CHECK(removed_invisible(plain_hit.events, 3));
    CHECK(invisible_effects(s, 3) == 0);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 2).concentration_spell == kInvis);   // the stalker's spell effect is still up

    end_turns_until(s, 1);
    auto stalker_hit = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    REQUIRE_MESSAGE(stalker_hit.ok, stalker_hit.error);
    CHECK(mut(s, 1).has(Condition::Invisible));
    CHECK(mut(s, 1).innate_invisible);
    CHECK_FALSE(removed_invisible(stalker_hit.events, 1));
    CHECK(invisible_effects(s, 1) == 0);
    CHECK(mut(s, 2).concentration_spell == 0);
    bool ended_held = false;
    for (const auto& e : stalker_hit.events)
        if (e.kind == EventKind::SpellEffectEnded && e.actor == 1 && e.a == int32_t(kInvis) && e.b == -1) ended_held = true;
    CHECK(ended_held);
    CHECK(attack_roll_mode(mut(s, 1), mut(s, 2), {}) == RollMode::Advantage);
}

TEST_CASE("innate invisibility: a gazer with the condition is not seen with sight") {
    auto book = std::make_shared<SpellBook>(*spell_hooks().spells);
    const uint32_t kGaze = spell_id_of("test.gaze#innate");
    SpellDef b;
    b.id = kGaze;
    b.content_id = "test.gaze#innate";
    b.is_spell = false;
    b.range_ft = 30;
    b.kind = SpellEffectKind::Save;
    b.save_ability = Ability::Con;
    b.fixed_dc = 30;
    b.applies_condition = true;
    b.condition = Condition::Restrained;
    REQUIRE(book->add(b));
    auto h = spell_hooks();
    h.spells = book;
    auto facts = [](const EncounterState&, const Combatant&, const Combatant& subject) {
        SightFacts f;
        f.distance_ft = 10;
        f.line_of_sight = true;
        f.subject_invisible = subject.has(Condition::Invisible);
        return f;
    };
    auto run = [&](bool innate) {
        auto v = pc(1, 200);
        auto g = monster(2);
        g.hp = g.hp_max = 200;
        g.gazes = {Combatant::Gaze{kGaze, 30}};
        if (innate) {
            g.innate_invisible = true;
            add_condition(g, Condition::Invisible);
        }
        auto s = begin_encounter({v, g}, 5, nullptr, h);
        s.hooks.sight_facts = facts;
        end_turns_until(s, 1);
        if (!s.budget.gaze_pending) {
            end_turns_until(s, 2);
            end_turns_until(s, 1);
        }
        REQUIRE(s.budget.gaze_pending);
        CHECK(sight(s, 1, 2).can_see != innate);
        REQUIRE(step(s, ActionRequest{1, ActionKind::GazeResponse}).ok);
        return find(s, 1)->has(Condition::Restrained);
    };
    CHECK_FALSE(run(true));
    CHECK(run(false));
}
