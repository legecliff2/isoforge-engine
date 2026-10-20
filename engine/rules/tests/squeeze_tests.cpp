// SRD 5.1 Squeezing (feat/size-space): Combatant::squeezing, set / cleared by a Move's ActionRequest::squeeze.
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
EncounterState start(uint64_t seed) {
    auto a = pc(1), b = monster(2);
    a.hp = a.hp_max = int16_t(200); b.hp = b.hp_max = int16_t(200);
    auto s = begin_encounter({a, b}, seed);
    end_turns_until(s, 1);
    return s;
}
int natural(EncounterState& s) {
    ActionRequest atk{1, ActionKind::Attack, 2, 0};
    atk.target_within_5ft = true;
    auto r = step(s, atk);
    REQUIRE(r.ok);
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) return e.a;
    FAIL("no attack");
    return 0;
}
}  // namespace

TEST_CASE("squeeze: a Move sets and clears the flag") {
    auto s = start(3);
    ActionRequest mv{1, ActionKind::Move};
    mv.move_ft = 10; mv.squeeze = 1;
    REQUIRE(step(s, mv).ok);
    CHECK(find(s, 1)->squeezing);
    mv.move_ft = 5; mv.squeeze = 0;
    REQUIRE(step(s, mv).ok);
    CHECK(find(s, 1)->squeezing);
    mv.squeeze = 2;
    REQUIRE(step(s, mv).ok);
    CHECK_FALSE(find(s, 1)->squeezing);
}

TEST_CASE("squeeze: disadvantage on its attacks, advantage on attacks against it") {
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        auto plain = start(seed), sq = start(seed), tg = start(seed);
        mut(sq, 1).squeezing = true;
        mut(tg, 2).squeezing = true;
        const int n0 = natural(plain), n1 = natural(sq), n2 = natural(tg);
        CHECK(n1 <= n0);   // the lower of two; the first die is the twin's single die
        CHECK(n2 >= n0);
    }
}
