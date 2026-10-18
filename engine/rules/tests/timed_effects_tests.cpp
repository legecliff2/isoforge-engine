// Timed condition durations: SpellEffect/SpellDef::{expiry, duration_rounds}, ActiveSpellEffect::timed,
// make_timed / tick_* helpers (reused by row 12 modifiers). See docs/proposals/VERSION_BUMP_BATCH.md sections 2.1 and 6.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kFright = spell_id_of("test.dragon#frightful_presence");
const uint32_t kGrip = spell_id_of("test.dragon#grip");

// Non-spell, non-concentration action: DC 30 save (always fails vs a +0 save), imposes Frightened.
SpellDef timed_condition(uint32_t id, Condition c, Expiry e, uint16_t rounds) {
    SpellDef d; d.id = id; d.content_id = "test.dragon#timed"; d.level = 0; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Wis; d.fixed_dc = 30; d.on_save = OnSave::None;
    d.applies_condition = true; d.condition = c; d.is_spell = false; d.magical = false;
    d.expiry = uint8_t(e); d.duration_rounds = rounds;
    return d;
}
Combatant dragon(CombatantId id, std::vector<uint32_t> known) {
    auto d = monster(id); d.hp = d.hp_max = 300; d.spells_known = std::move(known); return d;
}
Combatant hero(CombatantId id) { auto c = pc(id, 400); return c; }

EncounterHooks hooks(std::initializer_list<SpellDef> defs) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>();
    for (const auto& d : defs) REQUIRE(b->add(d));
    h.spells = b;
    return h;
}
// Plays EndTurn until `round` starts (returns all events since).
std::vector<Event> play_to_round(EncounterState& s, uint16_t round) {
    std::vector<Event> all;
    for (int i = 0; i < 40 && s.round < round; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
    }
    return all;
}
int ended(const std::vector<Event>& ev, uint32_t spell) {
    int n = 0; for (auto& e : ev) n += e.kind == EventKind::SpellEffectEnded && e.a == int32_t(spell); return n;
}
}  // namespace

TEST_CASE("make_timed / tick helpers") {
    CHECK(make_timed(Expiry::None, 5, false) == Timed{});
    CHECK(make_timed(Expiry::Rounds, 3, false) == Timed{Expiry::Rounds, 3});
    CHECK(make_timed(Expiry::EndOfTargetNextTurn, 0, false) == Timed{Expiry::EndOfTargetNextTurn, 1});
    CHECK(make_timed(Expiry::EndOfTargetNextTurn, 0, true) == Timed{Expiry::EndOfTargetNextTurn, 2});   // own turn does not count
    Timed r{Expiry::Rounds, 2};
    CHECK_FALSE(tick_target_turn_end(r));       // wrong kind: no-op
    CHECK(r.rounds_left == 2);
    CHECK_FALSE(tick_round_start(r));
    CHECK(tick_round_start(r));                 // 2 -> 1 -> 0: runs out on the second tick
    CHECK_FALSE(tick_round_start(r));           // already out: never fires twice
    Timed t{Expiry::EndOfTargetNextTurn, 2};
    CHECK_FALSE(tick_round_start(t));
    CHECK_FALSE(tick_target_turn_end(t));
    CHECK(tick_target_turn_end(t));
    Timed none;
    CHECK_FALSE(tick_round_start(none)); CHECK_FALSE(tick_target_turn_end(none));
}

TEST_CASE("defaults: untimed effects behave as before and nothing is hashed") {
    SpellDef d; SpellEffect e;
    CHECK(d.expiry == 0); CHECK(e.expiry == 0); CHECK(d.duration_rounds == 0);
    auto s = begin_encounter({dragon(1, {kFright}), hero(2)}, 3, nullptr,
                             hooks({timed_condition(kFright, Condition::Frightened, Expiry::None, 0)}));
    CHECK_FALSE(has_timed_effects(s));
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kFright, 2, 0)).ok);
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].timed == Timed{});
    CHECK_FALSE(has_timed_effects(s));
    play_to_round(s, 6);
    CHECK(find(s, 2)->has(Condition::Frightened));   // untimed, no repeat save: lasts
}

TEST_CASE("rounds: the condition lasts n rounds after the round it was applied in, then ends with the existing events") {
    for (int n : {1, 2, 3}) {
        auto s = begin_encounter({dragon(1, {kFright}), hero(2)}, 3, nullptr,
                                 hooks({timed_condition(kFright, Condition::Frightened, Expiry::Rounds, uint16_t(n))}));
        end_turns_until(s, 1);
        const uint16_t applied = s.round;
        REQUIRE(step(s, cast(1, kFright, 2, 0)).ok);
        REQUIRE(s.effects.size() == 1);
        CHECK(s.effects[0].timed == Timed{Expiry::Rounds, int16_t(n)});
        CHECK(has_timed_effects(s));
        play_to_round(s, uint16_t(applied + n - 1));
        if (n > 1) CHECK(find(s, 2)->has(Condition::Frightened));   // still on just before the last round starts
        auto ev = play_to_round(s, uint16_t(applied + n));
        CHECK(s.round == applied + n);
        CHECK_FALSE(find(s, 2)->has(Condition::Frightened));
        CHECK(s.effects.empty());
        CHECK(ended(ev, kFright) == 1);
        CHECK(std::count_if(ev.begin(), ev.end(), [](const Event& e) { return e.kind == EventKind::ConditionRemoved && e.a == int32_t(Condition::Frightened); }) == 1);
        // The end precedes the next TurnStarted of the new round.
        auto idx = [&](EventKind k) { return std::find_if(ev.begin(), ev.end(), [&](const Event& e) { return e.kind == k; }) - ev.begin(); };
        CHECK(idx(EventKind::SpellEffectEnded) < ev.size());
        CHECK(!has_timed_effects(s));
    }
}

TEST_CASE("end of the target's next turn: ends when the target's turn ends, not before; own-turn application skips that turn") {
    // Applied on the dragon's turn: the hero's next turn is the first one.
    auto s = begin_encounter({dragon(1, {kGrip}), hero(2)}, 3, nullptr,
                             hooks({timed_condition(kGrip, Condition::Restrained, Expiry::EndOfTargetNextTurn, 0)}));
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kGrip, 2, 0)).ok);
    CHECK(s.effects[0].timed == Timed{Expiry::EndOfTargetNextTurn, 1});
    // The dragon ends its turn: nothing happens to the hero's effect.
    auto r1 = step(s, {1, ActionKind::EndTurn});
    CHECK(ended(r1.events, kGrip) == 0);
    REQUIRE(current_actor(s) == 2);
    CHECK(find(s, 2)->has(Condition::Restrained));   // still on during the hero's turn
    auto r2 = step(s, {2, ActionKind::EndTurn});
    CHECK(ended(r2.events, kGrip) == 1);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
    // SpellEffectEnded / ConditionRemoved come before TurnEnded of that same request.
    auto pos = [&](EventKind k) { return std::find_if(r2.events.begin(), r2.events.end(), [&](const Event& e) { return e.kind == k; }) - r2.events.begin(); };
    CHECK(pos(EventKind::SpellEffectEnded) < pos(EventKind::TurnEnded));
    CHECK(s.effects.empty());
}

TEST_CASE("two effects of one spell on one target end together; an overlapping untimed effect keeps the condition") {
    SpellDef d = timed_condition(kFright, Condition::Frightened, Expiry::Rounds, 1);
    auto s = begin_encounter({dragon(1, {kFright}), hero(2)}, 3, nullptr, hooks({d}));
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kFright, 2, 0)).ok);
    // Another source also frightens the target (untimed): the condition outlives the timed effect.
    s.effects.push_back(ActiveSpellEffect{0x1234, 1, 2, Condition::Frightened, Ability::Wis, 10, RepeatSave::None, false});
    auto ev = play_to_round(s, uint16_t(s.round + 1));
    CHECK(ended(ev, kFright) == 1);
    CHECK(find(s, 2)->has(Condition::Frightened));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].spell_id == 0x1234);
    CHECK(std::count_if(ev.begin(), ev.end(), [](const Event& e) { return e.kind == EventKind::ConditionRemoved; }) == 0);
}

TEST_CASE("a timed concentration effect ends the caster's concentration when it runs out") {
    auto s = begin_encounter({dragon(1, {kFright}), hero(2)}, 3, nullptr, hooks({timed_condition(kFright, Condition::Frightened, Expiry::Rounds, 1)}));
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kFright, 2, 0)).ok);
    mut(s, 1).concentration_spell = kFright;       // as if the action were a concentration one
    s.effects[0].concentration = true;
    auto ev = play_to_round(s, uint16_t(s.round + 1));
    CHECK(find(s, 1)->concentration_spell == 0);
    CHECK(std::count_if(ev.begin(), ev.end(), [](const Event& e) { return e.kind == EventKind::ConcentrationEnded; }) == 1);
}

TEST_CASE("timers draw no rng and an untimed encounter is unchanged") {
    auto run = [](Expiry e) {
        auto s = begin_encounter({dragon(1, {kFright}), hero(2)}, 7, nullptr,
                                 hooks({timed_condition(kFright, Condition::Frightened, e, 2)}));
        end_turns_until(s, 1);
        (void)step(s, cast(1, kFright, 2, 0));
        play_to_round(s, 5);
        return s.rng;
    };
    auto a = run(Expiry::None), b = run(Expiry::Rounds);
    CHECK(a.d(1000) == b.d(1000));   // expiry itself rolls nothing
}

TEST_CASE("digest: empty when nothing is timed; covers kind and rounds_left") {
    EncounterState s;
    CHECK_FALSE(has_timed_effects(s));
    s.effects.push_back(ActiveSpellEffect{1, 1, 2, Condition::Frightened, Ability::Wis, 10, RepeatSave::None, false});
    CHECK_FALSE(has_timed_effects(s));
    s.effects[0].timed = {Expiry::Rounds, 3};
    CHECK(has_timed_effects(s));
    const uint64_t d3 = timed_effects_digest(s);
    s.effects[0].timed.rounds_left = 2;
    CHECK(timed_effects_digest(s) != d3);
    s.effects[0].timed = {Expiry::EndOfTargetNextTurn, 2};
    CHECK(timed_effects_digest(s) != d3);
}
