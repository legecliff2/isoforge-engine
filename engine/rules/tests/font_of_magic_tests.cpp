// Font of Magic: sorcery points and Flexible Casting (VERSION_BUMP_BATCH 7.2o).
#include <doctest/doctest.h>

#include "rules5e/font_of_magic.hpp"
#include "rules5e/rest.hpp"
#include "spell_util.hpp"

using namespace enc;

namespace {
constexpr uint32_t kSp = 901;
Combatant sorc(CombatantId id) {
    auto c = wizard(id);
    c.sorcery_pool = kSp;
    c.spell_slots_max = {0, 4, 3, 2, 0, 0, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    return c;
}
std::vector<ResourcePool> pool(CombatantId id, uint16_t max, uint16_t cur) {
    ResourcePool p; p.actor = id; p.pool_id = kSp; p.max = max; p.cur = cur; p.recharge = FeatureRecharge::LongRest;
    return {p};
}
ActionRequest flex(uint8_t dir, uint8_t lvl) {
    ActionRequest r{1, ActionKind::FlexibleCasting};
    r.as_bonus_action = true; r.flexible = dir; r.flexible_level = lvl;
    return r;
}
EncounterState start(uint16_t cur, uint64_t seed = 3) {
    auto s = begin_encounter({sorc(1), monster(2)}, seed, nullptr, spell_hooks());
    register_pool(s, 1, kSp, 5, cur, FeatureRecharge::LongRest);
    end_turns_until(s, 1);
    return s;
}
}  // namespace

TEST_CASE("font of magic: point maximum and the cost table") {
    CHECK(sorcery_points_max(1) == 0); CHECK(sorcery_points_max(2) == 2); CHECK(sorcery_points_max(5) == 5);
    CHECK(sorcery_points_max(20) == 20);
    CHECK(flexible_casting_cost(0) == 0);
    CHECK(flexible_casting_cost(1) == 2); CHECK(flexible_casting_cost(2) == 3); CHECK(flexible_casting_cost(3) == 5);
    CHECK(flexible_casting_cost(4) == 6); CHECK(flexible_casting_cost(5) == 7);
    CHECK(flexible_casting_cost(6) == 0); CHECK(flexible_casting_cost(9) == 0);
}

TEST_CASE("font of magic: out-of-encounter conversion both ways, cap, refusals, long rest") {
    auto c = sorc(1);
    auto ps = pool(1, 5, 0);
    auto o = flexible_casting(c, ps, 1, 2);   // a 2nd-level slot -> 2 points
    CHECK(o.result == FlexibleResult::Ok); CHECK(o.points_delta == 2); CHECK(ps[0].cur == 2); CHECK(c.spell_slots[2] == 2);
    o = flexible_casting(c, ps, 1, 3);        // 2 + 3 = 5: exactly the maximum
    CHECK(o.result == FlexibleResult::Ok); CHECK(ps[0].cur == 5); CHECK(c.spell_slots[3] == 1);
    o = flexible_casting(c, ps, 1, 1);        // 6 > 5: refused, not capped
    CHECK(o.result == FlexibleResult::OverMax); CHECK(ps[0].cur == 5); CHECK(c.spell_slots[1] == 4);
    o = flexible_casting(c, ps, 2, 3);        // 5 points -> a 3rd-level slot
    CHECK(o.result == FlexibleResult::Ok); CHECK(o.points_delta == -5); CHECK(ps[0].cur == 0); CHECK(c.spell_slots[3] == 2);
    CHECK(flexible_casting(c, ps, 2, 1).result == FlexibleResult::NotEnoughPoints);
    ps[0].cur = 5;
    CHECK(flexible_casting(c, ps, 2, 1).result == FlexibleResult::Ok);   // above spell_slots_max: a created slot
    CHECK(c.spell_slots[1] == 5);
    CHECK(flexible_casting(c, ps, 2, 6).result == FlexibleResult::BadLevel);   // only 1st..5th
    CHECK(flexible_casting(c, ps, 2, 0).result == FlexibleResult::BadLevel);
    CHECK(flexible_casting(c, ps, 1, 10).result == FlexibleResult::BadLevel);
    CHECK(flexible_casting(c, ps, 1, 4).result == FlexibleResult::NoSlot);
    CHECK(flexible_casting(c, ps, 3, 1).result == FlexibleResult::BadDirection);
    CHECK(flexible_casting(c, ps, 0, 1).result == FlexibleResult::BadDirection);
    auto none = c; none.sorcery_pool = 0;
    CHECK(flexible_casting(none, ps, 1, 1).result == FlexibleResult::NoPool);
    auto other = sorc(2);
    CHECK(flexible_casting(other, ps, 1, 1).result == FlexibleResult::NoPool);   // the row is actor 1's
    CHECK(ps[0].cur == 3); CHECK(c.spell_slots[1] == 5);

    // Long rest: created slots vanish (slots back to their maximum), points refill.
    std::vector<FeatureUses> feats;
    const RestTables t{&feats, &ps, nullptr};
    REQUIRE(long_rest(c, t).ok);
    CHECK(c.spell_slots == c.spell_slots_max);
    CHECK(ps[0].cur == 5);
}

TEST_CASE("font of magic: FlexibleCasting is a bonus action in an encounter") {
    auto s = start(0);
    auto r = step(s, flex(1, 2));
    REQUIRE(r.ok);
    CHECK(find_pool(s, 1, kSp)->cur == 2); CHECK(find(s, 1)->spell_slots[2] == 2);
    CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.action);
    REQUIRE(count(r.events, EventKind::ResourceSpent) == 1);
    CHECK(r.events[0].a == int32_t(kSp)); CHECK(r.events[0].b == -2); CHECK(r.events[0].c == 2);
    auto again = step(s, flex(2, 1));
    CHECK_FALSE(again.ok); CHECK(again.error == "no bonus action available");
    CHECK(find_pool(s, 1, kSp)->cur == 2);

    auto s2 = start(5);
    CHECK(step(s2, flex(1, 1)).error == "sorcery points would exceed the maximum");
    CHECK(step(s2, flex(2, 4)).error == "not enough sorcery points");   // 6 > 5
    CHECK(step(s2, flex(2, 6)).error == "bad flexible_level");
    CHECK(step(s2, flex(1, 5)).error == "no spell slot of that level");
    CHECK(step(s2, flex(7, 1)).error == "flexible must be 1 (slot to points) or 2 (points to slot)");
    CHECK(s2.budget.bonus_action);   // refusals change nothing
    mut(s2, 1).conditions |= bit(Condition::Stunned);
    CHECK(step(s2, flex(2, 1)).error == "incapacitated");
    mut(s2, 1).conditions = 0;
    auto r2 = step(s2, flex(2, 2));   // 3 points -> a 2nd-level slot
    REQUIRE(r2.ok);
    CHECK(r2.events[0].b == 3); CHECK(r2.events[0].c == 2); CHECK(find(s2, 1)->spell_slots[2] == 4);
    s2.budget.bonus_action = false;
    CHECK(step(s2, flex(2, 1)).error == "no bonus action available");
}

TEST_CASE("font of magic: legal_actions lists every valid conversion") {
    auto s = start(3);
    auto has = [&](uint8_t dir, uint8_t lvl) {
        for (const auto& r : legal_actions(s))
            if (r.kind == ActionKind::FlexibleCasting && r.flexible == dir && r.flexible_level == lvl && r.as_bonus_action) return true;
        return false;
    };
    CHECK(has(1, 1)); CHECK(has(1, 2)); CHECK_FALSE(has(1, 3));   // 3 + 3 > 5
    CHECK(has(2, 1)); CHECK(has(2, 2)); CHECK_FALSE(has(2, 3));   // 5 > 3 points
    CHECK_FALSE(has(1, 4));                                        // no 4th-level slot
    s.budget.bonus_action = false;
    CHECK_FALSE(has(2, 1));
    auto plain = begin_encounter({wizard(1), monster(2)}, 3, nullptr, spell_hooks());
    end_turns_until(plain, 1);
    for (const auto& r : legal_actions(plain)) CHECK(r.kind != ActionKind::FlexibleCasting);
}

TEST_CASE("font of magic: replay determinism") {
    auto a = start(1, 11);
    ActionLog log;
    REQUIRE(step_logged(a, flex(1, 3), log).ok);
    REQUIRE(step_logged(a, ActionRequest{1, ActionKind::EndTurn}, log).ok);
    end_turns_until(a, 1);
    REQUIRE(step_logged(a, flex(2, 1), log).ok);
    auto b = start(1, 11);
    std::vector<Event> ea, eb;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (i == 2) end_turns_until(b, 1);
        auto r = replay_step(b, log[i]);
        REQUIRE(r.ok);
        CHECK_FALSE(r.desync);
    }
    CHECK(find_pool(a, 1, kSp)->cur == find_pool(b, 1, kSp)->cur);
    CHECK(find_pool(b, 1, kSp)->cur == 2);
    CHECK(find(a, 1)->spell_slots == find(b, 1)->spell_slots);
    CHECK(pools_digest(a) == pools_digest(b));
}
