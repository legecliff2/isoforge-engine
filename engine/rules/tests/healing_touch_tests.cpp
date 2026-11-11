// Healing Touch (CONTENT_CONVENTIONS §56). Another creature, touch, 2d8+2, 3/day long rest.
// Then every stored Disease row, then Poisoned and tracked effects that apply it.
// No poison resistance and no save advantage. Specimen does not load unicorn.json.
#include <doctest/doctest.h>

#include <algorithm>

#include "rules5e/conditions.hpp"
#include "rules5e/dice.hpp"
#include "rules5e/rest.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kTouch = spell_id_of("test.touch#healing_touch");
const uint32_t kPoisonFx = spell_id_of("test.touch#poison_effect");
const uint32_t kFear = spell_id_of("test.touch#fear");
const uint32_t kBite = spell_id_of("test.touch#bite_a");
const uint32_t kBiteB = spell_id_of("test.touch#bite_b");
const uint32_t kOtherBite = spell_id_of("test.touch#bite_other");

SpellDef touch_def() {
    SpellDef d;
    d.id = kTouch;
    d.content_id = "test.touch#healing_touch";
    d.level = 0;
    d.kind = SpellEffectKind::Heal;
    d.is_spell = false;
    d.magical = true;
    d.targets = 1;
    d.range_ft = 5;
    d.dice = {2, 8, 2};
    d.healing_touch = true;
    d.usage_kind = 2;
    d.casting_time = CastingTime::Action;
    return d;
}

Combatant healer(CombatantId id) {
    auto c = pc(id, 40);
    c.team = 0;
    c.hp = c.hp_max = 40;
    c.spells_known = {kTouch};
    never_react(c);
    return c;
}

Combatant patient(CombatantId id, uint8_t team = 1) {
    auto c = pc(id, 40);
    c.team = team;
    c.hp = c.hp_max = 40;
    c.uses_death_saves = true;
    never_react(c);
    return c;
}

EncounterHooks hooks() {
    EncounterHooks h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    REQUIRE(book->add(touch_def()));
    h.spells = book;
    return h;
}

EncounterState open(std::vector<Combatant> cs) {
    auto s = begin_encounter(std::move(cs), 1, nullptr, hooks());
    register_feature(s, 1, kTouch, 3, FeatureRecharge::LongRest, 0);
    return s;
}

void advance_to(EncounterState& s, CombatantId id) {
    for (int i = 0; i < 20 && current_actor(s) != id; ++i)
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(s) == id);
}

ActionRequest touch(CombatantId who, CombatantId target, bool within = true) {
    ActionRequest r = cast(who, kTouch, target);
    r.target_within_5ft = within;
    return r;
}

const Event* healed(const std::vector<Event>& ev, CombatantId target) {
    const Event* found = nullptr;
    for (const auto& e : ev)
        if (e.kind == EventKind::Healed && e.target == target) found = &e;
    return found;
}

Disease row(CombatantId target, uint32_t spell, int16_t reduction) {
    Disease d;
    d.target = target;
    d.spell_id = spell;
    d.source = 9;
    d.save_ability = Ability::Con;
    d.dc = 15;
    d.dice = {1, 10, 0};
    d.condition = Condition::Poisoned;
    d.interval_rounds = 14400;
    d.next_round = 14400;
    d.reduction = reduction;
    return d;
}

}  // namespace

TEST_CASE("healing touch: self is refused and the action is not spent") {
    auto s = open({healer(1), patient(2)});
    advance_to(s, 1);
    auto r = step(s, touch(1, 1));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "healing touch: another creature");
    CHECK(s.budget.action);
    CHECK(feature_uses_left(s, 1, kTouch) == 3);
    CHECK(find(s, 1)->hp == 40);
}

TEST_CASE("healing touch: past 5 feet is refused and the action is not spent") {
    auto s = open({healer(1), patient(2)});
    advance_to(s, 1);
    mut(s, 2).hp = 10;
    auto r = step(s, touch(1, 2, false));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "healing touch: touch range");
    CHECK(s.budget.action);
    CHECK(feature_uses_left(s, 1, kTouch) == 3);
    CHECK(find(s, 2)->hp == 10);
}

TEST_CASE("healing touch: 2d8+2 is capped at the effective maximum and temporary hit points stay") {
    auto s = open({healer(1), patient(2)});
    advance_to(s, 1);
    mut(s, 2).hp = 29;
    mut(s, 2).hp_max = 30;
    mut(s, 2).temp_hp = 8;
    auto rng = s.rng;
    const int roll = roll_dice(rng, DiceExpr{2, 8, 2});
    CHECK(roll >= 4);
    CHECK(roll <= 18);
    auto r = step(s, touch(1, 2));
    REQUIRE(r.ok);
    const Event* h = healed(r.events, 2);
    REQUIRE(h);
    CHECK(h->a == 1);
    CHECK(find(s, 2)->hp == 30);
    CHECK(find(s, 2)->temp_hp == 8);
    CHECK(feature_uses_left(s, 1, kTouch) == 2);
}

TEST_CASE("healing touch: the same seed rolls the same 2d8+2") {
    auto once = [&] {
        auto s = open({healer(1), patient(2)});
        advance_to(s, 1);
        mut(s, 2).hp = 1;
        mut(s, 2).hp_max = 80;
        auto rng = s.rng;
        const int roll = roll_dice(rng, DiceExpr{2, 8, 2});
        auto r = step(s, touch(1, 2));
        REQUIRE(r.ok);
        const Event* h = healed(r.events, 2);
        REQUIRE(h);
        CHECK(h->a == roll);
        CHECK(find(s, 2)->hp == int16_t(1 + roll));
        return roll;
    };
    CHECK(once() == once());
}

TEST_CASE("healing touch: NoHealing heals 0 and the stored cure still runs") {
    auto s = open({healer(1), patient(2)});
    advance_to(s, 1);
    mut(s, 2).hp = 10;
    mut(s, 2).hp_max = 40;
    mut(s, 2).disease_hp_max_reduction = 6;
    s.diseases.push_back(row(2, kBite, 6));
    add_condition(mut(s, 2), Condition::Poisoned);
    ModifierSpec mark;
    mark.id = 9500;
    mark.stat = ModStat::NoHealing;
    std::vector<Event> ev;
    REQUIRE(apply_modifier(s, 1, 2, mark, ev));
    auto r = step(s, touch(1, 2));
    REQUIRE(r.ok);
    const Event* h = healed(r.events, 2);
    REQUIRE(h);
    CHECK(h->a == 0);
    CHECK(find(s, 2)->hp == 10);
    CHECK(s.diseases.empty());
    CHECK(find(s, 2)->disease_hp_max_reduction == 0);
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    CHECK(effective_hp_max(*find(s, 2)) == 40);
}

TEST_CASE("healing touch: every stored disease row is cured and Poisoned effects end") {
    auto s = open({healer(1), patient(2), patient(3)});
    advance_to(s, 1);
    mut(s, 2).hp = 20;
    mut(s, 2).hp_max = 40;
    mut(s, 2).disease_hp_max_reduction = 10;
    mut(s, 3).hp = 12;
    mut(s, 3).disease_hp_max_reduction = 2;
    s.diseases.push_back(row(2, kBite, 6));
    s.diseases.push_back(row(2, kBiteB, 4));
    s.diseases.push_back(row(3, kOtherBite, 2));
    add_condition(mut(s, 2), Condition::Poisoned);
    add_condition(mut(s, 2), Condition::Paralyzed);
    add_condition(mut(s, 2), Condition::Frightened);
    s.effects.push_back(ActiveSpellEffect{kPoisonFx, 9, 2, Condition::Poisoned, Ability::Con, 0, RepeatSave::None, true});
    s.effects.back().has_also = true;
    s.effects.back().also = Condition::Paralyzed;
    s.effects.push_back(ActiveSpellEffect{kFear, 9, 2, Condition::Frightened, Ability::Wis, 0, RepeatSave::None, false});
    s.effects.push_back(ActiveSpellEffect{kPoisonFx, 9, 3, Condition::Poisoned, Ability::Con, 0, RepeatSave::None, false});
    add_condition(mut(s, 3), Condition::Poisoned);
    ModifierSpec res;
    res.id = 77;
    res.stat = ModStat::Resistance;
    res.damage_type = DamageType::Poison;
    res.has_damage_type = true;
    ModifierSpec adv;
    adv.id = 78;
    adv.stat = ModStat::Save;
    adv.mode = RollMode::Advantage;
    std::vector<Event> ev;
    REQUIRE(apply_modifier(s, 1, 2, res, ev));
    REQUIRE(apply_modifier(s, 1, 2, adv, ev));
    const int mods = int(s.modifiers.size());
    auto rng = s.rng;
    const int roll = roll_dice(rng, DiceExpr{2, 8, 2});
    auto r = step(s, touch(1, 2));
    REQUIRE(r.ok);
    const Event* h = healed(r.events, 2);
    REQUIRE(h);
    const int capped = std::min(roll, 30 - 20);   // effective max is 40 - 10 before the cure
    CHECK(h->a == capped);
    CHECK(find(s, 2)->hp == int16_t(20 + capped));
    CHECK(effective_hp_max(*find(s, 2)) == 40);
    CHECK(find(s, 2)->disease_hp_max_reduction == 0);
    CHECK(s.diseases.size() == 1);
    CHECK(s.diseases[0].target == 3);
    CHECK(find(s, 3)->disease_hp_max_reduction == 2);
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    CHECK_FALSE(find(s, 2)->has(Condition::Paralyzed));
    CHECK(find(s, 2)->has(Condition::Frightened));
    CHECK(find(s, 3)->has(Condition::Poisoned));
    int poison_fx = 0, fear_fx = 0;
    for (const auto& e : s.effects) {
        if (e.target == 2 && e.condition == Condition::Poisoned) ++poison_fx;
        if (e.target == 2 && e.condition == Condition::Frightened) ++fear_fx;
        if (e.target == 3 && e.condition == Condition::Poisoned) ++poison_fx;
    }
    CHECK(poison_fx == 1);
    CHECK(fear_fx == 1);
    CHECK(int(s.modifiers.size()) == mods);
    bool resist = false, advantage = false;
    for (const auto& m : s.modifiers) {
        resist = resist || (m.target == 2 && m.spec.id == 77 && m.spec.stat == ModStat::Resistance &&
                            m.spec.damage_type == DamageType::Poison);
        advantage = advantage || (m.target == 2 && m.spec.id == 78 && m.spec.stat == ModStat::Save &&
                                  m.spec.mode == RollMode::Advantage);
    }
    CHECK(resist);
    CHECK(advantage);
}

TEST_CASE("healing touch: three uses, a short rest leaves them spent, a long rest refills them") {
    auto s = open({healer(1), patient(2)});
    for (int n = 0; n < 3; ++n) {
        advance_to(s, 1);
        mut(s, 2).hp = 10;
        auto r = step(s, touch(1, 2));
        REQUIRE(r.ok);
        CHECK(feature_uses_left(s, 1, kTouch) == uint8_t(2 - n));
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    }
    advance_to(s, 1);
    auto denied = step(s, touch(1, 2));
    CHECK_FALSE(denied.ok);
    CHECK(denied.error == "no uses left");
    CHECK(s.budget.action);
    CHECK(feature_uses_left(s, 1, kTouch) == 0);
    RestTables tables{&s.features, nullptr, nullptr};
    rng::Rng rest_rng{1, 1};
    REQUIRE(short_rest(mut(s, 1), rest_rng, 0, tables).ok);
    CHECK(feature_uses_left(s, 1, kTouch) == 0);
    auto still = step(s, touch(1, 2));
    CHECK_FALSE(still.ok);
    CHECK(still.error == "no uses left");
    REQUIRE(long_rest(mut(s, 1), tables).ok);
    CHECK(feature_uses_left(s, 1, kTouch) == 3);
    mut(s, 2).hp = 10;
    auto again = step(s, touch(1, 2));
    REQUIRE(again.ok);
    CHECK(feature_uses_left(s, 1, kTouch) == 2);
    CHECK(find(s, 2)->hp > 10);
}

TEST_CASE("healing touch: legal actions offer another creature and not the user") {
    auto s = open({healer(1), patient(2)});
    advance_to(s, 1);
    bool self = false, other = false;
    for (const auto& q : legal_actions(s)) {
        if (q.kind != ActionKind::CastSpell || q.spell_id != kTouch) continue;
        self = self || q.target == 1;
        other = other || q.target == 2;
    }
    CHECK_FALSE(self);
    CHECK(other);
}
