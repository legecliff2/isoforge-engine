// VERSION_BUMP_BATCH 7.2ze: spell batch 3 (T&C agreed shapes). Bless / Bane (ModifierSpec::also_rolls, die_mult, the per-target
// SpellDef::has_modifier_save gate; 3 targets + 1 per slot through the existing targets / upcast_targets) and Blindness/Deafness
// (a Save with SpellDef::condition_choices picked by ActionRequest::condition_choice, repeat save at the end of each turn).
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBless = spell_id_of("test.spell.bless");
const uint32_t kBane = spell_id_of("test.spell.bane");
const uint32_t kBlind = spell_id_of("test.spell.blindness_deafness");
const uint32_t kProbe = spell_id_of("test.spell.probe");

SpellDef die_spell(uint32_t id, const char* cid, int8_t mult) {
    SpellDef d; d.id = id; d.content_id = cid; d.level = 1; d.range_ft = 30; d.targets = 3; d.upcast_targets = 1;
    d.concentration = true; d.duration = SpellDuration::Rounds; d.duration_amount = 10; d.expiry = 1; d.duration_rounds = 10;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier.id = spell_id_of(std::string(cid) + "#modifier"); d.modifier.stat = ModStat::AttackRoll;
    d.modifier.die = DiceExpr{1, 1, 0}; d.modifier.die_mult = mult; d.modifier.also_rolls = kRollSave;
    d.modifier.concentration = true;
    return d;
}
EncounterHooks hooks(int8_t bane_dc) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(die_spell(kBless, "test.spell.bless", 1));
    SpellDef bane = die_spell(kBane, "test.spell.bane", -1);
    bane.has_modifier_save = true; bane.modifier_save = Ability::Cha; bane.fixed_dc = bane_dc;
    book->add(bane);
    SpellDef b; b.id = kBlind; b.content_id = "test.spell.blindness_deafness"; b.level = 2; b.range_ft = 30;
    b.targets = 1; b.upcast_targets = 1; b.duration = SpellDuration::Rounds; b.duration_amount = 10;
    b.kind = SpellEffectKind::Save; b.save_ability = Ability::Con; b.fixed_dc = 100; b.applies_condition = true;
    b.condition_choices = {Condition::Blinded, Condition::Deafened}; b.condition = Condition::Blinded;
    b.repeat_save = RepeatSave::EndOfTurn;
    book->add(b);
    SpellDef p; p.id = kProbe; p.content_id = "test.spell.probe"; p.level = 1; p.range_ft = 60;   // a plain Wis save, no effect
    p.kind = SpellEffectKind::Save; p.save_ability = Ability::Wis; p.fixed_dc = 10; p.dice = DiceExpr{1, 1, 0};
    p.damage_type = DamageType::Force; p.on_save = OnSave::Half;
    book->add(p);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id, uint8_t team = 0) {
    auto c = wizard(id, team);
    c.hp = c.hp_max = 40;
    c.spells_known.insert(c.spells_known.end(), {kBless, kBane, kBlind, kProbe});
    never_react(c);
    return c;
}
EncounterState start(std::vector<Combatant> cs, int8_t bane_dc = 100, uint64_t seed = 7) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(bane_dc));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(CombatantId a, uint32_t spell, std::vector<CombatantId> ts, uint8_t lvl) {
    ActionRequest r{a, ActionKind::CastSpell, ts.empty() ? 0 : ts[0]}; r.spell_id = spell; r.spell_level = lvl;
    if (ts.size() > 1) r.targets = ts;
    return r;
}
int n_events(const std::vector<Event>& ev, EventKind k) { int n = 0; for (const auto& e : ev) n += e.kind == k; return n; }
const Event* save_of(const std::vector<Event>& ev, CombatantId t) {
    for (const auto& e : ev) if (e.kind == EventKind::SavingThrowRolled && e.actor == t) return &e;
    return nullptr;
}
void to_turn(EncounterState& s, CombatantId id) { (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn}); end_turns_until(s, id); }
// bane_dc -100: Bane is cast and resisted (the same rng draws as an affecting cast, no modifier).
int probe_total(bool bless, bool bane, uint64_t seed, int8_t bane_dc = 100) {
    auto s = start({caster(1), caster(2), caster(3, 1)}, bane_dc, seed);
    if (bless) REQUIRE(step(s, cast_at(1, kBless, {2}, 1)).ok);
    if (bane) { to_turn(s, 3); REQUIRE(step(s, cast_at(3, kBane, {2}, 1)).ok); }
    to_turn(s, 3);
    auto r = step(s, cast_at(3, kProbe, {2}, 1));
    REQUIRE(r.ok);
    REQUIRE(save_of(r.events, 2));
    return int(save_of(r.events, 2)->b);
}

}  // namespace

TEST_CASE("7.2ze Bless: 3 targets + 1 per slot above 1st") {
    auto s = start({caster(1), caster(2), caster(3), caster(4), caster(5), caster(6, 1)});
    CHECK(std::string(step(s, cast_at(1, kBless, {1, 2, 3, 4}, 1)).error) == "too many targets");
    auto r = step(s, cast_at(1, kBless, {1, 2, 3, 4}, 2));
    REQUIRE(r.ok);
    CHECK(n_events(r.events, EventKind::ModifierApplied) == 4);
}

TEST_CASE("7.2ze Bless / Bane: the die adds to (or subtracts from) saving throws as well as attack rolls") {
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const int plain = probe_total(false, false, seed);
        CHECK(probe_total(true, false, seed) == plain + 1);
        CHECK(probe_total(false, true, seed) == probe_total(false, true, seed, -100) - 1);
    }
}

TEST_CASE("7.2ze Bane: a Charisma save against the caster's DC resists it") {
    auto s = start({caster(1), caster(2, 1)}, 100);
    auto r = step(s, cast_at(1, kBane, {2}, 1));
    REQUIRE(r.ok);
    REQUIRE(save_of(r.events, 2));
    CHECK((save_of(r.events, 2)->c & 8) != 0);
    CHECK(n_events(r.events, EventKind::ModifierApplied) == 1);
    auto s2 = start({caster(1), caster(2, 1)}, -100);
    r = step(s2, cast_at(1, kBane, {2}, 1));
    REQUIRE(r.ok);
    CHECK((save_of(r.events, 2)->c & 1) != 0);
    CHECK(n_events(r.events, EventKind::ModifierApplied) == 0);
}

TEST_CASE("7.2ze Blindness/Deafness: the caster picks the condition; 1 target + 1 per slot above 2nd") {
    auto s = start({caster(1), caster(2, 1), caster(3, 1)});
    CHECK(std::string(step(s, cast_at(1, kBlind, {2}, 2)).error) == "choose a condition");
    auto two = cast_at(1, kBlind, {2, 3}, 2); two.condition_choice = 2;
    CHECK(std::string(step(s, two).error) == "too many targets");
    auto q = cast_at(1, kBlind, {2}, 2); q.condition_choice = 2;
    REQUIRE(step(s, q).ok);
    CHECK(mut(s, 2).has(Condition::Deafened));
    CHECK(!mut(s, 2).has(Condition::Blinded));
    to_turn(s, 1);
    auto up = cast_at(1, kBlind, {2, 3}, 3); up.condition_choice = 1;
    REQUIRE(step(s, up).ok);
    CHECK(mut(s, 3).has(Condition::Blinded));
}

TEST_CASE("7.2ze replay: Bless / Bane / Blindness casts are deterministic") {
    auto run = [] {
        auto s = start({caster(1), caster(2), caster(3, 1)}, 100, 23);
        std::vector<Event> all;
        auto put = [&](const StepResult& r) { REQUIRE(r.ok); all.insert(all.end(), r.events.begin(), r.events.end()); };
        put(step(s, cast_at(1, kBless, {1, 2}, 1)));
        to_turn(s, 3);
        put(step(s, cast_at(3, kBane, {1, 2}, 1)));
        to_turn(s, 3);
        auto q = cast_at(3, kBlind, {2}, 2); q.condition_choice = 1;
        put(step(s, q));
        return std::make_pair(all, modifiers_digest(s));
    };
    const auto a = run(), b = run();
    CHECK(a.second == b.second);
    REQUIRE(a.first.size() == b.first.size());
    for (std::size_t i = 0; i < a.first.size(); ++i) { CHECK(a.first[i].kind == b.first[i].kind); CHECK(a.first[i].b == b.first[i].b); }
}
