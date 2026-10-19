// R-G9: forced movement (pushes) and self-teleport, per docs/proposals/FORCED_MOVEMENT.md.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kThunderwave = spell_id_of("test.spell#thunderwave");
const uint32_t kEasyWave = spell_id_of("test.spell#easy_wave");     // DC 1: every save succeeds
const uint32_t kBlast = spell_id_of("test.spell#repelling_blast");  // spell attack + "up to 10 ft" push
const uint32_t kMisty = spell_id_of("test.spell#misty_step");
const uint32_t kSmallWave = spell_id_of("test.spell#small_wave");   // push limited to Medium

struct Seen {
    std::vector<ForcedMoveQuery> queries;
    uint16_t feet = 0xFFFF;                     // answer: feet moved (0xFFFF = as requested)
    std::vector<CombatantId> out_of_reach{};    // answer for every query
    std::vector<std::size_t> order{};           // forced_move_order answer (empty = no hook)
    int order_calls = 0;
};

SpellEffect push_rider(uint16_t feet, bool up_to = false, int max_size = -1) {
    SpellEffect e; e.kind = SpellEffectKind::ForcedMove; e.affected_only = true;
    e.forced.mode = 1; e.forced.feet = feet; e.forced.up_to = up_to; e.forced.max_size = int8_t(max_size);
    return e;
}

EncounterHooks hooks(const std::shared_ptr<Seen>& seen, bool with_hook) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef tw; tw.id = kThunderwave; tw.content_id = "test.spell#thunderwave"; tw.level = 1; tw.range_ft = 0;
    tw.kind = SpellEffectKind::Save; tw.save_ability = Ability::Con; tw.on_save = OnSave::Half; tw.dice = DiceExpr{2, 8, 0};
    tw.damage_type = DamageType::Thunder; tw.area = AreaShape::Cube; tw.area_size_ft = 15; tw.fixed_dc = 30;
    tw.extra_effects = {push_rider(10)};
    book->add(tw);
    SpellDef ew = tw; ew.id = kEasyWave; ew.content_id = "test.spell#easy_wave"; ew.fixed_dc = 1;
    book->add(ew);
    SpellDef sw = tw; sw.id = kSmallWave; sw.content_id = "test.spell#small_wave";
    sw.extra_effects = {push_rider(10, false, int(Size::Medium))};
    book->add(sw);
    SpellDef rb; rb.id = kBlast; rb.content_id = "test.spell#repelling_blast"; rb.level = 0; rb.range_ft = 120;
    rb.kind = SpellEffectKind::SpellAttack; rb.dice = DiceExpr{1, 10, 0}; rb.damage_type = DamageType::Force; rb.fixed_attack = 30;
    rb.extra_effects = {push_rider(10, true)};
    book->add(rb);
    SpellDef ms; ms.id = kMisty; ms.content_id = "test.spell#misty_step"; ms.level = 2; ms.range_ft = 0;
    ms.casting_time = CastingTime::BonusAction; ms.kind = SpellEffectKind::Teleport; ms.teleport.max_feet = 30;
    book->add(ms);
    h.spells = book;
    if (with_hook) {
        h.forced_move = [seen](const EncounterState&, const ForcedMoveQuery& q) {
            seen->queries.push_back(q);
            ForcedMoveResult r;
            r.feet_moved = seen->feet == 0xFFFF ? q.feet : seen->feet;
            r.stopped_short = r.feet_moved < q.feet;
            r.out_of_reach_of = seen->out_of_reach;
            return r;
        };
        h.forced_move_order = [seen](const EncounterState&, std::span<const ForcedMoveQuery>) {
            ++seen->order_calls;
            return seen->order;
        };
    }
    return h;
}
Combatant caster(CombatantId id) {
    auto w = wizard(id);
    w.spells_known = {kThunderwave, kEasyWave, kSmallWave, kBlast, kMisty};
    never_react(w);
    return w;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 300;
    never_react(m);
    return m;
}
EncounterState start(std::vector<Combatant> cs, const std::shared_ptr<Seen>& seen, bool with_hook, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(seen, with_hook));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(CombatantId a, uint32_t spell, std::vector<CombatantId> targets, uint8_t level = 1) {
    ActionRequest r{a, ActionKind::CastSpell, targets.front()};
    r.spell_id = spell; r.spell_level = level; r.targets = std::move(targets);
    return r;
}
std::vector<Event> of_kind(const std::vector<Event>& ev, EventKind k) {
    std::vector<Event> out;
    for (const auto& e : ev) if (e.kind == k) out.push_back(e);
    return out;
}
int index_of(const std::vector<Event>& ev, EventKind k, bool last = false) {
    int at = -1;
    for (int i = 0; i < int(ev.size()); ++i)
        if (ev[std::size_t(i)].kind == k) { at = i; if (!last) break; }
    return at;
}

}  // namespace

TEST_CASE("defaults: no forced movement, no hook") {
    ForcedMoveSpec f; CHECK(f.mode == 1); CHECK(f.feet == 0); CHECK_FALSE(f.up_to); CHECK(f.max_size == -1); CHECK_FALSE(f.provokes_oa);
    TeleportSpec t; CHECK(t.max_feet == 0); CHECK(t.needs_sight);
    ActionRequest r{1, ActionKind::CastSpell};
    CHECK(r.forced_feet == 0); CHECK(r.teleport_feet == 0); CHECK_FALSE(r.has_teleport_dest);
    EncounterHooks h;
    CHECK_FALSE(h.forced_move); CHECK_FALSE(h.forced_move_order); CHECK_FALSE(h.shove_forced_flag);
    SpellEffect e; SpellDef d;
    CHECK(e.forced.feet == 0); CHECK(d.teleport.max_feet == 0);
    const SpellDef v = effect_view(d, push_rider(10, true, 2));
    CHECK(v.forced.feet == 10); CHECK(v.forced.up_to); CHECK(v.forced.max_size == 2);
}

TEST_CASE("Thunderwave: a failed save pushes 10 ft after every save and damage, before SpellResolved (no hook)") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2), dummy(3)}, seen, false);
    const int16_t move_before = s.budget.movement_ft;
    auto r = step(s, cast_at(1, kThunderwave, {2, 3}));
    REQUIRE(r.ok);
    const auto moved = of_kind(r.events, EventKind::Moved);
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].actor == 2); CHECK(moved[1].actor == 3);   // the given (target) order without an order hook
    for (const auto& m : moved) { CHECK(m.target == 1); CHECK(m.a == 10); CHECK(m.b == 0); CHECK(m.c == 1); }
    CHECK(index_of(r.events, EventKind::Moved) > index_of(r.events, EventKind::DamageDealt, true));
    CHECK(index_of(r.events, EventKind::Moved) > index_of(r.events, EventKind::SavingThrowRolled, true));
    CHECK(index_of(r.events, EventKind::Moved, true) < index_of(r.events, EventKind::SpellResolved));
    CHECK(s.budget.movement_ft == move_before);   // nobody's movement is spent
    CHECK(seen->queries.empty());
}

TEST_CASE("Thunderwave: a successful save is not pushed") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2)}, seen, true);
    auto r = step(s, cast_at(1, kEasyWave, {2}));
    REQUIRE(r.ok);
    CHECK(of_kind(r.events, EventKind::Moved).empty());
    CHECK(seen->queries.empty());
    CHECK_FALSE(of_kind(r.events, EventKind::DamageDealt).empty());
}

TEST_CASE("the hook: query facts, truncation, and the order hook's permutation") {
    auto seen = std::make_shared<Seen>();
    seen->feet = 5;
    seen->order = {1, 0};
    auto s = start({caster(1), dummy(2), dummy(3)}, seen, true);
    auto r = step(s, cast_at(1, kThunderwave, {2, 3}));
    REQUIRE(r.ok);
    CHECK(seen->order_calls == 1);
    REQUIRE(seen->queries.size() == 2);
    CHECK(seen->queries[0].mover == 3); CHECK(seen->queries[1].mover == 2);   // sim's order
    for (const auto& q : seen->queries) {
        CHECK(q.source == 1); CHECK(q.mode == 1); CHECK(q.feet == 10); CHECK_FALSE(q.teleport); CHECK_FALSE(q.has_dest);
    }
    CHECK(seen->queries[0].step_seq == 1); CHECK(seen->queries[1].step_seq == 0);
    const auto moved = of_kind(r.events, EventKind::Moved);
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].actor == 3); CHECK(moved[0].a == 5); CHECK(moved[0].c == 1);
    CHECK(moved[1].actor == 2);
    // not a permutation: the given order
    auto seen2 = std::make_shared<Seen>();
    seen2->order = {0, 0};
    auto s2 = start({caster(1), dummy(2), dummy(3)}, seen2, true);
    REQUIRE(step(s2, cast_at(1, kThunderwave, {2, 3})).ok);
    REQUIRE(seen2->queries.size() == 2);
    CHECK(seen2->queries[0].mover == 2);
}

TEST_CASE("blocked on the first step: Moved{a = 0} all the same") {
    auto seen = std::make_shared<Seen>();
    seen->feet = 0;
    auto s = start({caster(1), dummy(2)}, seen, true);
    auto r = step(s, cast_at(1, kThunderwave, {2}));
    REQUIRE(r.ok);
    const auto moved = of_kind(r.events, EventKind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].a == 0); CHECK(moved[0].c == 1);
}

TEST_CASE("a grapple ends when the hook reports the pair out of reach; not without a hook") {
    for (bool with_hook : {true, false}) {
        auto seen = std::make_shared<Seen>();
        seen->out_of_reach = {3};
        auto s = start({caster(1), dummy(2), dummy(3)}, seen, with_hook);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 3, 2, 0, sink));
        REQUIRE(mut(s, 2).has(Condition::Grappled));
        auto r = step(s, cast_at(1, kThunderwave, {2}));
        REQUIRE(r.ok);
        if (with_hook) {
            CHECK_FALSE(mut(s, 2).has(Condition::Grappled));
            CHECK(grappled_by(s, 3).empty());
            const int mv = index_of(r.events, EventKind::Moved);
            const int rm = index_of(r.events, EventKind::ConditionRemoved);
            REQUIRE(rm >= 0);
            CHECK(r.events[std::size_t(rm)].a == int32_t(Condition::Grappled));
            CHECK(mv < rm);
        } else {
            CHECK(mut(s, 2).has(Condition::Grappled));   // speed 0 does not stop the push either
            CHECK(of_kind(r.events, EventKind::Moved).size() == 1);
        }
    }
}

TEST_CASE("the grappler pushed out of reach frees the creature it held") {
    auto seen = std::make_shared<Seen>();
    seen->out_of_reach = {3};
    auto s = start({caster(1), dummy(2), dummy(3)}, seen, true);
    std::vector<Event> sink;
    REQUIRE(grapple(s, 2, 3, 0, sink));   // 2 holds 3; 2 is pushed
    REQUIRE(step(s, cast_at(1, kThunderwave, {2})).ok);
    CHECK_FALSE(mut(s, 3).has(Condition::Grappled));
}

TEST_CASE("forced moves never provoke: no LeavingReach window, no reaction spent") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2), dummy(3)}, seen, true);
    auto& guard = mut(s, 3);
    guard.team = 0;   // an enemy of 2, next to it
    set_policy(guard, ReactionTrigger::LeavingReach, ReactionPolicy::Auto);
    auto r = step(s, cast_at(1, kThunderwave, {2}));
    REQUIRE(r.ok);
    CHECK(r.status == StepStatus::Done);
    CHECK(of_kind(r.events, EventKind::AttackResolved).empty());
    CHECK_FALSE(mut(s, 3).reaction_used);
}

TEST_CASE("max_size: a creature over the limit is not pushed but still takes the damage") {
    auto seen = std::make_shared<Seen>();
    auto big = dummy(2); big.size = Size::Large;
    auto s = start({caster(1), big, dummy(3)}, seen, true);
    auto r = step(s, cast_at(1, kSmallWave, {2, 3}));
    REQUIRE(r.ok);
    const auto moved = of_kind(r.events, EventKind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].actor == 3);
    bool hurt = false;
    for (const auto& e : r.events) hurt = hurt || (e.kind == EventKind::DamageDealt && e.target == 2);
    CHECK(hurt);
}

TEST_CASE("Repelling Blast: an 'up to 10 ft' push on a hit; forced_feet picks the distance") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2)}, seen, true);
    ActionRequest r = cast_at(1, kBlast, {2}, 0);
    r.forced_feet = 5;
    auto out = step(s, r);
    REQUIRE(out.ok);
    const auto moved = of_kind(out.events, EventKind::Moved);
    const bool hit = std::any_of(out.events.begin(), out.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && (e.c & 1); });
    if (hit) {
        REQUIRE(moved.size() == 1);
        CHECK(moved[0].a == 5); CHECK(moved[0].c == 1);
        REQUIRE(seen->queries.size() == 1); CHECK(seen->queries[0].feet == 5);
    } else {
        CHECK(moved.empty());
    }
    for (uint16_t bad : {uint16_t(7), uint16_t(15)}) {
        auto t = start({caster(1), dummy(2)}, seen, true);
        ActionRequest q = cast_at(1, kBlast, {2}, 0); q.forced_feet = bad;
        auto f = step(t, q);
        CHECK_FALSE(f.ok);
        CHECK(std::string(f.error).find("forced_feet") != std::string::npos);
        CHECK(t.budget.action);
    }
    auto w = start({caster(1), dummy(2)}, seen, true);
    ActionRequest fixed = cast_at(1, kThunderwave, {2}); fixed.forced_feet = 5;   // Thunderwave's push is exactly 10 ft
    CHECK_FALSE(step(w, fixed).ok);
    ActionRequest atk{1, ActionKind::Attack, 2}; atk.forced_feet = 5;
    auto a = step(w, atk);
    CHECK_FALSE(a.ok);
    CHECK(std::string(a.error) == "forced_feet / teleport: CastSpell only");
}

TEST_CASE("Misty Step: bonus action self-teleport; the destination goes to the hook; distance budget checked") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2)}, seen, true);
    const int16_t move_before = s.budget.movement_ft;
    const auto legal = legal_actions(s);
    CHECK(std::any_of(legal.begin(), legal.end(), [](const ActionRequest& q) {
        return q.kind == ActionKind::CastSpell && q.spell_id == kMisty && q.target == 1; }));
    ActionRequest r{1, ActionKind::CastSpell};
    r.spell_id = kMisty; r.spell_level = 2; r.teleport_feet = 30; r.has_teleport_dest = true; r.teleport_x = 7; r.teleport_y = -3;
    auto out = step(s, r);
    REQUIRE(out.ok);
    const auto moved = of_kind(out.events, EventKind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].actor == 1); CHECK(moved[0].target == 1); CHECK(moved[0].a == 30); CHECK(moved[0].b == 0); CHECK(moved[0].c == 2);
    REQUIRE(seen->queries.size() == 1);
    const ForcedMoveQuery& q = seen->queries[0];
    CHECK(q.teleport); CHECK(q.mode == 0); CHECK(q.feet == 30);
    CHECK(q.has_dest); CHECK(q.dest_x == 7); CHECK(q.dest_y == -3);
    CHECK(s.budget.movement_ft == move_before);
    CHECK(s.budget.action);
    CHECK_FALSE(s.budget.bonus_action);
}

TEST_CASE("Misty Step: refusals change nothing; no positions = no distance check") {
    auto seen = std::make_shared<Seen>();
    auto s = start({caster(1), dummy(2)}, seen, false);
    ActionRequest far{1, ActionKind::CastSpell, 1}; far.spell_id = kMisty; far.spell_level = 2; far.teleport_feet = 35;
    auto f = step(s, far);
    CHECK_FALSE(f.ok); CHECK(std::string(f.error) == "teleport: beyond the spell's range");
    ActionRequest other{1, ActionKind::CastSpell, 2}; other.spell_id = kMisty; other.spell_level = 2;
    auto o = step(s, other);
    CHECK_FALSE(o.ok); CHECK(std::string(o.error) == "teleport: self only");
    ActionRequest dest_on_wave = cast_at(1, kThunderwave, {2}); dest_on_wave.has_teleport_dest = true;
    CHECK_FALSE(step(s, dest_on_wave).ok);
    CHECK(s.budget.bonus_action);
    CHECK(mut(s, 1).spell_slots[2] == 3);
    ActionRequest plain{1, ActionKind::CastSpell}; plain.spell_id = kMisty; plain.spell_level = 2;   // sim without positions
    auto p = step(s, plain);
    REQUIRE(p.ok);
    const auto moved = of_kind(p.events, EventKind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].a == 0); CHECK(moved[0].c == 2);
}

TEST_CASE("Misty Step out of a grapple: ends when the hook reports out of reach") {
    auto seen = std::make_shared<Seen>();
    seen->out_of_reach = {2};
    auto s = start({caster(1), dummy(2)}, seen, true);
    std::vector<Event> sink;
    REQUIRE(grapple(s, 2, 1, 0, sink));
    ActionRequest r{1, ActionKind::CastSpell}; r.spell_id = kMisty; r.spell_level = 2; r.teleport_feet = 20;
    auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK_FALSE(mut(s, 1).has(Condition::Grappled));
}

TEST_CASE("shove 5 ft away: c = 0 by default (no locked digest moves), c = 1 with shove_forced_flag, hook distance") {
    for (int mode = 0; mode < 3; ++mode) {
        int pushes = 0;
        for (uint64_t seed = 0; seed < 12; ++seed) {
            auto seen = std::make_shared<Seen>();
            seen->feet = 0;
            auto h = hooks(seen, mode == 2);
            h.shove_forced_flag = mode >= 1;
            auto s = begin_encounter({pc(1), dummy(2)}, seed, nullptr, h);
            end_turns_until(s, 1);
            ActionRequest r{1, ActionKind::Attack, 2}; r.special = 3; r.target_within_5ft = true;
            auto out = step(s, r);
            REQUIRE(out.ok);
            const auto moved = of_kind(out.events, EventKind::Moved);
            if (moved.empty()) continue;
            ++pushes;
            REQUIRE(moved.size() == 1);
            CHECK(moved[0].actor == 2); CHECK(moved[0].target == 1); CHECK(moved[0].b == 0);
            CHECK(moved[0].c == (mode >= 1 ? 1 : 0));
            CHECK(moved[0].a == (mode == 2 ? 0 : 5));
            if (mode == 2) { REQUIRE(seen->queries.size() == 1); CHECK(seen->queries[0].feet == 5); }
        }
        CHECK(pushes > 0);
    }
}
