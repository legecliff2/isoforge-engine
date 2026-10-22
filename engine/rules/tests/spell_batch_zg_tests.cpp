// VERSION_BUMP_BATCH 7.2zg spell batch (rules side): Slow's spell-failure roll and save gate; the others follow per group.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kSlowZ = spell_id_of("test.zg.slow");
ModifierSpec row(uint32_t id, ModStat st) { ModifierSpec m; m.id = id; m.source = kSlowZ; m.stat = st; m.applies_to = ModAppliesTo::Target; return m; }
EncounterHooks zg_hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef sl; sl.id = kSlowZ; sl.content_id = "test.zg.slow"; sl.level = 3; sl.range_ft = 120; sl.concentration = true;
    sl.kind = SpellEffectKind::ApplyModifier; sl.modifier = row(spell_id_of("test.zg.slow#slow_marker"), ModStat::Slow);
    sl.modifier.repeat_save = 1; sl.modifier.repeat_ability = Ability::Wis;
    sl.has_modifier_save = true; sl.modifier_save = Ability::Wis; sl.fixed_dc = 30;
    SpellEffect ac; ac.kind = SpellEffectKind::ApplyModifier; ac.affected_only = true;
    ac.modifier = row(spell_id_of("test.zg.slow#slow_ac"), ModStat::Ac); ac.modifier.bonus = -2;
    sl.extra_effects = {ac};
    book->add(sl);
    h.spells = book;
    return h;
}
int count_kind(const std::vector<Event>& ev, EventKind k) { return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.kind == k; })); }
}  // namespace

TEST_CASE("7.2zg slow: one save gates every row; a slowed caster of a 1-action spell rolls the d20 (11+: lost)") {
    auto w = wizard(1); w.spells_known.push_back(kSlowZ); w.spell_slots_max = {0, 4, 3, 3, 0, 0, 0, 0, 0, 0}; w.spell_slots = w.spell_slots_max;
    never_react(w);
    auto m = monster(2); m.hp = m.hp_max = 300; never_react(m);
    auto s = begin_encounter({w, m}, 7, nullptr, zg_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kSlowZ, 2, 3));
    REQUIRE(r.ok);
    CHECK(count_kind(r.events, EventKind::SavingThrowRolled) == 1);   // the extra row does not save again
    int rows = 0; for (const auto& am : s.modifiers) rows += am.target == 2;
    CHECK(rows == 2);
    // slow the wizard itself and cast Fire Bolt (1 action) until both outcomes show
    int lost = 0, cast_ok = 0;
    for (int i = 0; i < 12; ++i) {
        (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn});
        end_turns_until(s, 1);
        std::vector<Event> sink;
        ModifierSpec mk = row(spell_id_of("test.zg.slow#slow_marker"), ModStat::Slow);
        if (!std::any_of(s.modifiers.begin(), s.modifiers.end(), [](const auto& x) { return x.target == 1 && x.spec.stat == ModStat::Slow; }))
            REQUIRE(apply_modifier(s, 2, 1, mk, sink));
        auto c = step(s, cast(1, kFireBolt, 2));
        REQUIRE(c.ok);
        const Event* ft = nullptr;
        for (const auto& e : c.events) if (e.kind == EventKind::FeatureTriggered && e.actor == 1) ft = &e;
        REQUIRE(ft);
        CHECK(ft->a == int32_t(kSlowZ));
        CHECK(ft->b >= 1); CHECK(ft->b <= 20);
        CHECK(ft->c == int32_t(ft->b >= 11));
        if (ft->c) { ++lost; CHECK(count_kind(c.events, EventKind::SpellCountered) == 1); CHECK(count_kind(c.events, EventKind::AttackResolved) == 0); }
        else ++cast_ok;
    }
    CHECK(lost > 0); CHECK(cast_ok > 0);
}
