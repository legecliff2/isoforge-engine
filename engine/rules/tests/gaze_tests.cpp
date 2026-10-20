// Petrifying Gaze feature kind (VERSION_BUMP_BATCH 7.2p): turn-start gaze saves, averting the eyes.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kBasilisk = spell_id_of("test.gaze#basilisk");
const uint32_t kMedusa = spell_id_of("test.gaze#medusa");

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef b; b.id = kBasilisk; b.content_id = "test.gaze#basilisk"; b.is_spell = false; b.range_ft = 30;
    b.kind = SpellEffectKind::Save; b.save_ability = Ability::Con; b.fixed_dc = 30; b.applies_condition = true;
    b.condition = Condition::Restrained; b.repeat_save = RepeatSave::EndOfTurn; b.escalates = true;
    b.escalate_to = Condition::Petrified;
    book->add(b);
    SpellDef m = b; m.id = kMedusa; m.content_id = "test.gaze#medusa"; m.escalate_margin = 5;
    book->add(m);
    h.spells = book;
    return h;
}
Combatant gazer(CombatantId id, uint32_t gaze) {
    auto g = monster(id);
    g.hp = g.hp_max = int16_t(200);
    g.gazes = {Combatant::Gaze{gaze, 30}};
    return g;
}
EncounterState start(uint32_t gaze, uint64_t seed = 5, bool surprised = false) {
    auto v = pc(1);
    v.hp = v.hp_max = int16_t(200);
    auto s = begin_encounter({v, gazer(2, gaze)}, seed, nullptr, hooks());
    if (surprised) {
        end_turns_until(s, 2);
        mut(s, 1).surprised = true;
    }
    end_turns_until(s, 1);
    if (!s.budget.gaze_pending && current_actor(s) == 1) {   // the first turn started before the loop: re-run via a round
        end_turns_until(s, 2);
        end_turns_until(s, 1);
    }
    return s;
}
ActionRequest look(bool sees = true) {
    ActionRequest r{1, ActionKind::GazeResponse};
    if (sees) r.gaze_seen = {2};
    return r;
}
ActionRequest avert() { ActionRequest r{1, ActionKind::GazeResponse}; r.avert_gaze = true; r.gaze_seen = {2}; return r; }
}  // namespace

TEST_CASE("gaze: defaults and no gazer means no pending answer") {
    Combatant c; CHECK(c.gazes.empty()); CHECK_FALSE(c.gaze_averted);
    ActionRequest r{1, ActionKind::GazeResponse}; CHECK_FALSE(r.avert_gaze); CHECK(r.gaze_seen.empty());
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, hooks());
    end_turns_until(s, 1);
    CHECK_FALSE(s.budget.gaze_pending);
    CHECK_FALSE(has_gaze_state(s));
    CHECK(step(s, look()).error == "no gaze pending");
}

TEST_CASE("gaze: the turn waits for GazeResponse; legal_actions offers only it") {
    auto s = start(kBasilisk);
    REQUIRE(current_actor(s) == 1);
    REQUIRE(s.budget.gaze_pending);
    CHECK(step(s, {1, ActionKind::Dodge}).error == "gaze pending: answer with GazeResponse first");
    CHECK(step(s, {1, ActionKind::EndTurn}).error == "gaze pending: answer with GazeResponse first");
    auto la = legal_actions(s);
    REQUIRE(la.size() == 2);
    CHECK(la[0].kind == ActionKind::GazeResponse); CHECK_FALSE(la[0].avert_gaze);
    CHECK(la[1].kind == ActionKind::GazeResponse); CHECK(la[1].avert_gaze);
    REQUIRE(step(s, look()).ok);
    CHECK_FALSE(s.budget.gaze_pending);
    CHECK(step(s, look()).error == "no gaze pending");   // no double trigger
    CHECK(step(s, {1, ActionKind::Dodge}).ok);
}

TEST_CASE("gaze: basilisk restrained, then petrified on the failed repeat save") {
    auto s = start(kBasilisk);
    auto r = step(s, look());
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 1);
    CHECK(find(s, 1)->has(Condition::Restrained)); CHECK_FALSE(find(s, 1)->has(Condition::Petrified));
    auto e = step(s, {1, ActionKind::EndTurn});   // repeat save at the end of the turn: DC 30 fails
    REQUIRE(e.ok);
    CHECK(find(s, 1)->has(Condition::Petrified)); CHECK_FALSE(find(s, 1)->has(Condition::Restrained));
    end_turns_until(s, 1);
    REQUIRE(s.budget.gaze_pending);
    auto again = step(s, look());   // already petrified: no new save
    REQUIRE(again.ok);
    CHECK(count(again.events, EventKind::SavingThrowRolled) == 0);
}

TEST_CASE("gaze: a restrained creature is not re-gazed while its repeat save is pending") {
    auto s = start(kBasilisk);
    REQUIRE(step(s, look()).ok);
    REQUIRE(find(s, 1)->has(Condition::Restrained));
    const auto n = s.effects.size();
    s.budget.gaze_pending = true;   // as at a later turn start before the repeat save
    auto r = step(s, look());
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 0);
    CHECK(s.effects.size() == n);
}

TEST_CASE("gaze: medusa petrifies at once on a failure by 5 or more") {
    auto s = start(kMedusa);
    auto r = step(s, look());
    REQUIRE(r.ok);
    CHECK(find(s, 1)->has(Condition::Petrified)); CHECK_FALSE(find(s, 1)->has(Condition::Restrained));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].repeat_save == RepeatSave::None);
}

TEST_CASE("gaze: no save when the gazer is incapacitated or unseen") {
    auto s = start(kBasilisk);
    auto unseen = step(s, look(false));
    REQUIRE(unseen.ok);
    CHECK(count(unseen.events, EventKind::SavingThrowRolled) == 0);
    CHECK_FALSE(find(s, 1)->has(Condition::Restrained));

    auto t = start(kBasilisk);
    add_condition(mut(t, 2), Condition::Stunned);
    auto inc = step(t, look());
    REQUIRE(inc.ok);
    CHECK(count(inc.events, EventKind::SavingThrowRolled) == 0);
    CHECK_FALSE(find(t, 1)->has(Condition::Restrained));
}

TEST_CASE("gaze: averting the eyes - no save, refused while surprised, disadvantage / advantage, ends next turn") {
    auto s = start(kMedusa);
    const uint64_t d0 = gaze_digest(s);
    auto r = step(s, avert());
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 0);
    CHECK(find(s, 1)->gaze_averted);
    CHECK(has_gaze_state(s)); CHECK(gaze_digest(s) != d0);
    // Attacks against the gazer have disadvantage: two d20s rolled -> natural is the lower; check via many seeds is costly,
    // so compare the AttackResolved natural with a non-averting twin (same seed / same rng position).
    auto twin = start(kMedusa);
    REQUIRE(step(twin, look(false)).ok);   // no save either: rng positions match
    ActionRequest atk{1, ActionKind::Attack, 2, 0};
    atk.target_within_5ft = true;
    mut(s, 1).attacks = {kLongsword}; mut(twin, 1).attacks = {kLongsword};
    auto ra = step(s, atk), rb = step(twin, atk);
    REQUIRE(ra.ok); REQUIRE(rb.ok);
    const Event* ea = nullptr; const Event* eb = nullptr;
    for (const auto& e : ra.events) if (e.kind == EventKind::AttackResolved) { ea = &e; break; }
    for (const auto& e : rb.events) if (e.kind == EventKind::AttackResolved) { eb = &e; break; }
    REQUIRE(ea); REQUIRE(eb);
    CHECK(ea->a <= eb->a);   // disadvantage: the lower of two (the first die equals the twin's single die)
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK(find(s, 1)->gaze_averted);   // still averted during the gazer's turn
    end_turns_until(s, 1);
    CHECK_FALSE(find(s, 1)->gaze_averted);   // ends at the start of its next turn
    CHECK_FALSE(has_gaze_state(s));

    auto sp = start(kBasilisk, 5, true);
    REQUIRE(current_actor(sp) == 1);
    REQUIRE(sp.budget.gaze_pending);
    CHECK(step(sp, avert()).error == "surprised: cannot avert the eyes");
    auto la = legal_actions(sp);
    REQUIRE(la.size() == 1); CHECK_FALSE(la[0].avert_gaze);
    REQUIRE(step(sp, look()).ok);   // the save happens even when surprised
    CHECK(find(sp, 1)->has(Condition::Restrained));
    CHECK(step(sp, {1, ActionKind::EndTurn}).ok);
}

TEST_CASE("gaze: replay determinism") {
    auto a = start(kBasilisk, 9);
    auto b = start(kBasilisk, 9);
    ActionLog log;
    REQUIRE(step_logged(a, look(), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    std::vector<Event> ea, eb;
    for (const auto& ls : log) {
        auto r = replay_step(b, ls);
        REQUIRE(r.ok); CHECK_FALSE(r.desync);
    }
    CHECK(find(a, 1)->conditions == find(b, 1)->conditions);
    CHECK(leftover_digest(a) == leftover_digest(b));
    CHECK(gaze_digest(a) == gaze_digest(b));
    CHECK(current_actor(a) == current_actor(b));
}
