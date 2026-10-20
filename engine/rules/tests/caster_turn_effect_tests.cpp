// EC loader durations (#212, flagged rules5e edit): a condition effect with Expiry::EndOfCasterNextTurn, self-targeted
// (caster == target): handled once per turn end, lasts exactly until the end of the caster's next turn, removed once.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kSelf = spell_id_of("test.spell.self_blur");
EncounterHooks hooks() {
    auto book = srd_spellbook();
    SpellDef d; d.content_id = "test.spell.self_blur"; d.id = kSelf; d.level = 0; d.range_ft = 5;
    d.kind = SpellEffectKind::ApplyCondition; d.condition = Condition::Invisible; d.applies_condition = true;
    d.is_spell = false; d.magical = false;
    d.expiry = uint8_t(Expiry::EndOfCasterNextTurn);
    book->add(d);
    EncounterHooks h;
    h.spells = book;
    return h;
}
Combatant me(CombatantId id) { auto c = wizard(id); c.spells_known.push_back(kSelf); return c; }
Combatant foe(CombatantId id) { auto m = monster(id); m.hp = m.hp_max = 500; m.reaction_policy.fill(ReactionPolicy::Never); return m; }
int n_of(const std::vector<Event>& ev, EventKind k) { int n = 0; for (const auto& e : ev) n += e.kind == k; return n; }
const ActiveSpellEffect* fx(const EncounterState& s) {
    for (const auto& e : s.effects) if (e.spell_id == kSelf) return &e;
    return nullptr;
}

std::vector<Event> run(uint64_t seed) {
    std::vector<Event> all;
    auto s = begin_encounter({me(1), foe(2)}, seed, nullptr, hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kSelf, 1));
    REQUIRE_MESSAGE(r.ok, r.error);
    all.insert(all.end(), r.events.begin(), r.events.end());
    REQUIRE(find(s, 1)->has(Condition::Invisible));
    REQUIRE(fx(s));
    CHECK(fx(s)->caster == fx(s)->target);
    CHECK(fx(s)->timed.rounds_left == 2);                       // applied on the caster's own turn: that turn does not count
    r = step(s, ActionRequest{1, ActionKind::EndTurn});         // end of the casting turn: one tick, not two
    all.insert(all.end(), r.events.begin(), r.events.end());
    REQUIRE(fx(s));
    CHECK(fx(s)->timed.rounds_left == 1);
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK(n_of(r.events, EventKind::SpellEffectEnded) == 0);
    while (current_actor(s) != 1) {                             // the foe's turn: no tick
        r = step(s, ActionRequest{current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
        CHECK(find(s, 1)->has(Condition::Invisible));
    }
    CHECK(find(s, 1)->has(Condition::Invisible));               // still on through the caster's next turn
    r = step(s, ActionRequest{1, ActionKind::EndTurn});         // end of the caster's next turn: gone, exactly once
    all.insert(all.end(), r.events.begin(), r.events.end());
    CHECK_FALSE(find(s, 1)->has(Condition::Invisible));
    CHECK(fx(s) == nullptr);
    CHECK(n_of(r.events, EventKind::SpellEffectEnded) == 1);
    CHECK(n_of(r.events, EventKind::ConditionRemoved) == 1);
    return all;
}
} // namespace

TEST_CASE("EndOfCasterNextTurn self-targeted condition: one tick per turn end, ends at the caster's next turn end, removed once; replay") {
    const auto a = run(7);
    const auto b = run(7);   // replay: same seed and requests give the same event stream
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK((a[i].kind == b[i].kind && a[i].actor == b[i].actor && a[i].target == b[i].target && a[i].a == b[i].a && a[i].b == b[i].b && a[i].c == b[i].c));
}
