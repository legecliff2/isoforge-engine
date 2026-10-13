#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;

TEST_CASE("initiative is sorted by total, then dex, with deterministic ties") {
    std::vector<Combatant> cs;
    for (uint32_t i = 1; i <= 12; ++i) cs.push_back(make_combatant(i, 8 + int(i % 5) * 2));
    rng::Rng r(555, 0);
    auto order = roll_initiative(r, cs);
    REQUIRE(order.size() == cs.size());
    for (std::size_t i = 1; i < order.size(); ++i) {
        CHECK(order[i - 1].total >= order[i].total);
        if (order[i - 1].total == order[i].total) CHECK(order[i - 1].dex_mod >= order[i].dex_mod);
    }
    for (const auto& e : order) {
        int dex = e.dex_mod;
        CHECK(e.total >= 1 + dex);
        CHECK(e.total <= 20 + dex);
    }
    rng::Rng r2(555, 0);
    auto again = roll_initiative(r2, cs);
    for (std::size_t i = 0; i < order.size(); ++i) CHECK(again[i].id == order[i].id);
}

TEST_CASE("initiative dex mod comes from the Dex score") {
    std::vector<Combatant> cs{make_combatant(1, 14), make_combatant(2, 7)};
    rng::Rng r(1, 0);
    auto order = roll_initiative(r, cs);
    for (const auto& e : order) CHECK(e.dex_mod == (e.id == 1 ? 2 : -2));
}

TEST_CASE("initiative on an empty list") {
    rng::Rng r(1, 0);
    CHECK(roll_initiative(r, {}).empty());
}
