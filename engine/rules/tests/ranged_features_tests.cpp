// Rules-level tests: ranged disadvantage request facts, attack ranges, readied-response refresh,
// feature uses in the encounter state, and the EventKind count.
#include <doctest/doctest.h>

#include "encounter_util.hpp"
#include "test_util.hpp"

using namespace rules5e;
using namespace enc;

namespace {

const Attack kLongbow{"longbow", 5, {1, 8, 3}, DamageType::Piercing, 150, true, false, 150, 600};

Combatant archer(CombatantId id) {
    auto c = pc(id);
    c.attacks = {kLongbow, kLongsword};
    return c;
}

struct Shot { int natural; int other; };

// One attack by 1 on 2 with the given facts; returns the d20s from AttackResolved/the roll.
Shot shoot(uint64_t seed, uint8_t attack_index, bool hostile_5ft, bool beyond, bool helped = false,
           bool target_restrained = false) {
    auto s = begin_encounter({archer(1), monster(2)}, seed);
    mut(s, 2).hp = mut(s, 2).hp_max = 500;
    if (target_restrained) mut(s, 2).conditions |= bit(Condition::Restrained);
    end_turns_until(s, 1);
    if (helped) { mut(s, 1).helped_vs = 2; mut(s, 1).helped_by = 9; }
    ActionRequest q{1, ActionKind::Attack, 2, attack_index};
    q.ranged_hostile_within_5ft = hostile_5ft;
    q.beyond_normal_range = beyond;
    auto r = step(s, q);
    REQUIRE(r.ok);
    for (const auto& e : r.events)
        if (e.kind == EventKind::AttackResolved) return {e.a, e.b - kLongbow.to_hit};
    FAIL("no AttackResolved");
    return {};
}

}  // namespace

TEST_CASE("EventKind::Count_ / kEventKindCount follows the last kind") {
    CHECK(kEventKindCount == std::size_t(EventKind::ZoneEnded) + 1);
}

TEST_CASE("attack ranges: normal/long range widened past one byte; helpers") {
    CHECK(normal_range_ft(kLongbow) == 150);
    CHECK(long_range_ft(kLongbow) == 600);
    CHECK_FALSE(beyond_normal_range(kLongbow, 150));
    CHECK(beyond_normal_range(kLongbow, 155));
    CHECK_FALSE(out_of_range(kLongbow, 600));
    CHECK(out_of_range(kLongbow, 605));
    // Legacy attacks (no range_ft): reach_or_range_ft is the normal range and there is no long range.
    const Attack sling_like{"x", 3, {1, 4, 1}, DamageType::Bludgeoning, 30, true, false};
    CHECK(normal_range_ft(sling_like) == 30);
    CHECK(long_range_ft(sling_like) == 30);
    CHECK_FALSE(beyond_normal_range(kLongsword, 100));   // melee: never "beyond normal range"
}

TEST_CASE("ranged_hostile_within_5ft and beyond_normal_range each give disadvantage on ranged attacks") {
    int lower_hostile = 0, lower_beyond = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        const Shot plain = shoot(seed, 0, false, false);
        const Shot hostile = shoot(seed, 0, true, false);
        const Shot beyond = shoot(seed, 0, false, true);
        const Shot both = shoot(seed, 0, true, true);
        // Same stream: the first d20 matches; disadvantage keeps the lower of two.
        CHECK(hostile.natural <= plain.natural);
        CHECK(beyond.natural <= plain.natural);
        CHECK(both.natural == beyond.natural);   // two sources are still one disadvantage
        CHECK(hostile.natural == beyond.natural);
        lower_hostile += hostile.natural < plain.natural;
        lower_beyond += beyond.natural < plain.natural;
        // Melee attacks ignore both facts.
        CHECK(shoot(seed, 1, true, true).natural == shoot(seed, 1, false, false).natural);
    }
    CHECK(lower_hostile > 0);
    CHECK(lower_beyond > 0);
}

TEST_CASE("ranged disadvantage combines with every other source at once (any adv + any dis = normal)") {
    AttackContext ctx;
    ctx.mode = RollMode::Advantage;   // e.g. Help
    ctx.extra_disadvantage = true;    // e.g. long range
    auto a = make_combatant(1), t = make_combatant(2);
    t.conditions |= bit(Condition::Restrained);   // another advantage source
    CHECK(attack_roll_mode(a, t, ctx) == RollMode::Normal);
    a.conditions |= bit(Condition::Poisoned);
    t.conditions = 0;
    ctx.mode = RollMode::Normal;
    CHECK(attack_roll_mode(a, t, ctx) == RollMode::Disadvantage);
    // End to end: helped + long range + restrained target rolls one d20, like a plain attack.
    for (uint64_t seed = 1; seed <= 20; ++seed)
        CHECK(shoot(seed, 0, false, true, true, true).natural == shoot(seed, 0, false, false).natural);
}

TEST_CASE("Ready: refresh_ready re-evaluates the response's facts when it fires") {
    auto run = [](uint64_t seed, bool refresh, int* calls) {
        EncounterHooks h;
        h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
            return e.kind == EventKind::Moved && e.actor == 2;
        };
        if (refresh)
            h.refresh_ready = [calls](const EncounterState&, const ReadiedAction& ra, const Event& e)
                -> std::optional<ActionRequest> {
                ++*calls;
                CHECK(e.actor == 2);
                ActionRequest q = ra.response;
                q.beyond_normal_range = true;   // the target moved out past normal range
                q.target = 99;                  // ignored: the readied target stays
                return q;
            };
        auto s = begin_encounter({archer(1), monster(2)}, seed, nullptr, h);
        mut(s, 2).hp = mut(s, 2).hp_max = 500;
        mut(s, 1).reaction_policy[std::size_t(ReactionTrigger::ReadiedTrigger)] = ReactionPolicy::Auto;
        end_turns_until(s, 1);
        ActionRequest ready{1, ActionKind::Ready, 2, 0};
        ready.ready_kind = ActionKind::Attack;
        ready.trigger_id = 1;
        REQUIRE(step(s, ready).ok);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 2);
        auto r = step(s, {2, ActionKind::Move, 0, 0, 5});
        REQUIRE(r.ok);
        for (const auto& e : r.events)
            if (e.kind == EventKind::AttackResolved) { CHECK(e.target == 2); return e.a; }
        FAIL("readied attack did not fire");
        return 0;
    };
    int calls = 0, lower = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const int frozen = run(seed, false, &calls);
        const int fresh = run(seed, true, &calls);
        CHECK(fresh <= frozen);
        lower += fresh < frozen;
    }
    CHECK(calls == 40);
    CHECK(lower > 0);
}

TEST_CASE("feature uses live in the encounter state: enforced, recharged, digested, replayed") {
    auto s = begin_encounter({pc(1), monster(2)}, 11);
    register_feature(s, 1, 7, 1, FeatureRecharge::ShortRest);
    CHECK(feature_uses_left(s, 1, 7) == 1);
    CHECK(feature_uses_left(s, 1, 8) == 255);   // unregistered: unlimited
    const uint64_t d0 = feature_uses_digest(s);
    ActionLog log;
    end_turns_until(s, 1);
    const EncounterState start = s;
    ActionRequest u{1, ActionKind::UseFeature};
    u.feature_id = 7; u.as_bonus_action = true; u.heal_dice = {1, 10, 1};
    REQUIRE(step_logged(s, u, log).ok);
    const uint64_t after_use = feature_uses_digest(s);
    CHECK(feature_uses_left(s, 1, 7) == 0);
    CHECK(feature_uses_digest(s) != d0);
    u.as_bonus_action = false;
    auto r = step(s, u);
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.error) == "feature has no uses left");
    CHECK(s.budget.action);   // rejected: nothing spent
    // Unregistered features stay unlimited.
    ActionRequest other = u; other.feature_id = 8;
    CHECK(step(s, other).ok);
    // Replay rebuilds the uses from the state + log alone.
    EncounterState rep = start;
    for (const auto& ls : log) REQUIRE(replay_step(rep, ls).ok);
    CHECK(feature_uses_left(rep, 1, 7) == 0);
    CHECK(feature_uses_digest(rep) == after_use);
    // Same digest as the live state had right after the logged use.
    // Rests.
    register_feature(s, 1, 9, 1, FeatureRecharge::LongRest, 1);
    recover_features(s, false);
    CHECK(feature_uses_left(s, 1, 7) == 1);
    CHECK(feature_uses_left(s, 1, 9) == 0);
    recover_features(s, true);
    CHECK(feature_uses_left(s, 1, 9) == 1);
}
