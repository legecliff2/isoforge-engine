// B6: SRD 5.1 otyugh / death dog bite disease. A failed Constitution save poisons until cured and schedules
// the same save every 24 hours (14400 rounds). A failed repeat reduces hit point maximum by the dice. A
// success cures that disease. The reduction is not Life Drain and a long rest does not clear it.
#include <doctest/doctest.h>

#include "rules5e/conditions.hpp"
#include "rules5e/rest.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBite = spell_id_of("test.otyugh#bite_disease");
const uint32_t kOther = spell_id_of("test.death_dog#bite_disease");

SpellDef disease_save(uint32_t id, const char* name, int dc) {
    SpellDef d;
    d.id = id; d.content_id = name; d.level = 0; d.range_ft = 5;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Con; d.fixed_dc = int8_t(dc);
    d.on_save = OnSave::None; d.applies_condition = true; d.condition = Condition::Poisoned;
    d.condition_untracked = true; d.targets = 1; d.is_spell = false; d.magical = false;
    d.disease = true; d.disease_hours = 24; d.disease_dice = {1, 10, 0};
    return d;
}

Combatant biter(CombatantId id, std::vector<uint32_t> known) {
    auto c = monster(id);
    c.hp = c.hp_max = 80;
    c.spells_known = std::move(known);
    never_react(c);
    return c;
}

Combatant patient(CombatantId id) {
    auto c = pc(id, 40);
    c.abilities.score[std::size_t(Ability::Con)] = 10;
    c.hp = c.hp_max = 40;
    never_react(c);
    return c;
}

EncounterHooks hooks(std::initializer_list<SpellDef> defs) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>();
    for (const auto& d : defs) REQUIRE(b->add(d));
    h.spells = b;
    return h;
}

StepResult advance_to(EncounterState& s, CombatantId id) {
    StepResult last{};
    for (int i = 0; i < 20 && current_actor(s) != id; ++i)
        last = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(current_actor(s) == id);
    return last;
}

StepResult next_own_turn(EncounterState& s, CombatantId id) {
    REQUIRE(current_actor(s) == id);
    (void)step(s, {id, ActionKind::EndTurn});
    return advance_to(s, id);
}

const Disease* disease_on(const EncounterState& s, CombatantId id) {
    for (const auto& d : s.diseases) if (d.target == id) return &d;
    return nullptr;
}

}  // namespace

TEST_CASE("bite disease: a failed save poisons and schedules 24 hours; a success contracts nothing") {
    auto s = begin_encounter({biter(1, {kBite}), patient(2)}, 1, nullptr,
                             hooks({disease_save(kBite, "test.otyugh#bite_disease", 30)}));
    advance_to(s, 1);
    auto r = step(s, cast(1, kBite, 2));
    REQUIRE(r.ok);
    CHECK(find(s, 2)->has(Condition::Poisoned));
    REQUIRE(s.diseases.size() == 1);
    CHECK(s.diseases[0].target == 2);
    CHECK(s.diseases[0].spell_id == kBite);
    CHECK(s.diseases[0].dc == 30);
    CHECK(s.diseases[0].dice == DiceExpr{1, 10, 0});
    CHECK(s.diseases[0].interval_rounds == 24u * 600u);
    CHECK(s.diseases[0].next_round == uint32_t(s.round) + 24u * 600u);
    CHECK(s.diseases[0].reduction == 0);
    CHECK(find(s, 2)->disease_hp_max_reduction == 0);
    const uint32_t due = s.diseases[0].next_round;
    (void)step(s, {1, ActionKind::EndTurn});   // one action a turn; the clock is not due yet
    advance_to(s, 1);
    auto again = step(s, cast(1, kBite, 2));   // the same disease does not stack or reset
    REQUIRE(again.ok);
    CHECK(s.diseases.size() == 1);
    CHECK(s.diseases[0].next_round == due);

    auto clean = begin_encounter({biter(1, {kBite}), patient(2)}, 1, nullptr,
                                 hooks({disease_save(kBite, "test.otyugh#bite_disease", 1)}));
    advance_to(clean, 1);
    auto ok = step(clean, cast(1, kBite, 2));
    REQUIRE(ok.ok);
    CHECK_FALSE(find(clean, 2)->has(Condition::Poisoned));
    CHECK(clean.diseases.empty());
}

TEST_CASE("bite disease: a failed repeat reduces hit point maximum; a success cures it; a rest does not") {
    auto s = begin_encounter({biter(1, {kBite}), patient(2)}, 3, nullptr,
                             hooks({disease_save(kBite, "test.otyugh#bite_disease", 30)}));
    advance_to(s, 1);
    REQUIRE(step(s, cast(1, kBite, 2)).ok);
    REQUIRE(disease_on(s, 2));
    s.diseases[0].next_round = uint32_t(s.round);
    auto tick = advance_to(s, 2);
    const Disease* d = disease_on(s, 2);
    REQUIRE(d);
    CHECK(d->reduction >= 1);
    CHECK(d->reduction <= 10);
    CHECK(find(s, 2)->disease_hp_max_reduction == d->reduction);
    CHECK(effective_hp_max(*find(s, 2)) == 40 - d->reduction);
    CHECK(find(s, 2)->hp == effective_hp_max(*find(s, 2)));
    CHECK(find(s, 2)->has(Condition::Poisoned));
    CHECK(d->next_round == uint32_t(s.round) + 24u * 600u);
    bool saw_drop = false;
    for (const auto& e : tick.events)
        if (e.kind == EventKind::ModifierApplied && e.b == int32_t(ModStat::MaxHp) && e.c == -d->reduction) saw_drop = true;
    CHECK(saw_drop);

    const int16_t held = find(s, 2)->hp;
    const int16_t red = d->reduction;
    mut(s, 2).hp_max_reduction = 5;   // Life Drain, cleared by the rest; the disease is not
    mut(s, 2).hp = 1;
    RestResult rest = long_rest(mut(s, 2), {});
    CHECK(rest.ok);
    CHECK(mut(s, 2).hp_max_reduction == 0);
    CHECK(mut(s, 2).disease_hp_max_reduction == red);
    CHECK(mut(s, 2).hp == effective_hp_max(mut(s, 2)));
    CHECK(mut(s, 2).hp == int16_t(40 - red));
    CHECK(disease_on(s, 2));

    s.diseases[0].dc = 1;
    s.diseases[0].next_round = uint32_t(s.round);
    auto cured = next_own_turn(s, 2);
    CHECK(s.diseases.empty());
    CHECK(find(s, 2)->disease_hp_max_reduction == 0);
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    CHECK(find(s, 2)->hp == held);   // restoring the maximum does not heal
    CHECK(effective_hp_max(*find(s, 2)) == 40);
    bool saw_back = false;
    for (const auto& e : cured.events)
        if (e.kind == EventKind::ModifierApplied && e.b == int32_t(ModStat::MaxHp) && e.c == red) saw_back = true;
    CHECK(saw_back);
}

TEST_CASE("bite disease: hit point maximum 0 kills; poisoned immunity still contracts the schedule") {
    auto s = begin_encounter({biter(1, {kBite}), patient(2)}, 4, nullptr,
                             hooks({disease_save(kBite, "test.otyugh#bite_disease", 30)}));
    advance_to(s, 1);
    REQUIRE(step(s, cast(1, kBite, 2)).ok);
    mut(s, 2).hp = mut(s, 2).hp_max = 1;
    s.diseases[0].next_round = uint32_t(s.round);
    advance_to(s, 2);
    CHECK(find(s, 2)->dead);
    CHECK(effective_hp_max(*find(s, 2)) == 0);

    auto immune = begin_encounter({biter(1, {kBite}), patient(2)}, 5, nullptr,
                                  hooks({disease_save(kBite, "test.otyugh#bite_disease", 30)}));
    mut(immune, 2).condition_immunities |= bit(Condition::Poisoned);
    advance_to(immune, 1);
    REQUIRE(step(immune, cast(1, kBite, 2)).ok);
    CHECK_FALSE(find(immune, 2)->has(Condition::Poisoned));
    REQUIRE(disease_on(immune, 2));
    immune.diseases[0].dc = 1;
    immune.diseases[0].next_round = uint32_t(immune.round);
    advance_to(immune, 2);
    CHECK(immune.diseases.empty());
    CHECK_FALSE(find(immune, 2)->has(Condition::Poisoned));
}

TEST_CASE("bite disease: a second disease is its own schedule; curing one leaves the other") {
    auto s = begin_encounter({biter(1, {kBite, kOther}), patient(2)}, 6, nullptr,
                             hooks({disease_save(kBite, "test.otyugh#bite_disease", 30),
                                    disease_save(kOther, "test.death_dog#bite_disease", 30)}));
    advance_to(s, 1);
    REQUIRE(step(s, cast(1, kBite, 2)).ok);
    (void)step(s, {1, ActionKind::EndTurn});
    advance_to(s, 1);
    REQUIRE(step(s, cast(1, kOther, 2)).ok);
    CHECK(s.diseases.size() == 2);
    s.diseases[0].next_round = uint32_t(s.round);
    s.diseases[1].next_round = uint32_t(s.round);
    advance_to(s, 2);
    REQUIRE(s.diseases.size() == 2);
    CHECK(s.diseases[0].reduction >= 1);
    CHECK(s.diseases[1].reduction >= 1);
    const int16_t kept = s.diseases[1].reduction;
    CHECK(find(s, 2)->disease_hp_max_reduction == int16_t(s.diseases[0].reduction + kept));
    s.diseases[0].dc = 1;
    s.diseases[0].next_round = uint32_t(s.round);
    next_own_turn(s, 2);
    REQUIRE(s.diseases.size() == 1);
    CHECK(s.diseases[0].spell_id == kOther);
    CHECK(s.diseases[0].reduction == kept);
    CHECK(find(s, 2)->has(Condition::Poisoned));   // the other disease still applies it
    CHECK(find(s, 2)->disease_hp_max_reduction == kept);
}
