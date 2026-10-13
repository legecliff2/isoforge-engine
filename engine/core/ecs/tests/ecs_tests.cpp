#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>
#include "ecs/registry.hpp"

using namespace eng::ecs;

namespace {
struct Pos { int32_t x, y; };
struct Hp { int16_t hp; };
} // namespace

TEST_CASE("create/destroy: generation-checked handles, FIFO slot reuse") {
    Registry r;
    Entity a = r.create(10), b = r.create(20), c = r.create(30);
    CHECK(a.index == 0); CHECK(b.index == 1); CHECK(c.index == 2);
    CHECK(r.alive(a));
    CHECK(r.destroy(b));
    CHECK(r.destroy(a));
    CHECK_FALSE(r.alive(a));
    CHECK_FALSE(r.destroy(a));                 // double destroy is a no-op
    Entity d = r.create(40);                   // oldest freed slot first (b's)
    CHECK(d.index == 1);
    CHECK(d.generation == 1);
    Entity e = r.create(50);
    CHECK(e.index == 0);
    CHECK_FALSE(r.alive(b));                   // stale handle to reused slot
    CHECK(r.get<Pos>(b) == nullptr);
    CHECK(r.size() == 3);
    CHECK_FALSE(r.alive(kNullEntity));
}

TEST_CASE("StableId lookup and uniqueness") {
    Registry r;
    Entity a = r.create(7);
    CHECK(r.find(7) == a);
    CHECK_FALSE(r.find(8).has_value());
    CHECK_THROWS_AS(r.create(7), std::invalid_argument);
    r.destroy(a);
    CHECK_FALSE(r.find(7).has_value());
    Entity b = r.create(7);                    // id free again after destroy
    CHECK(r.stable_id(b) == 7u);
}

TEST_CASE("typed components: emplace/get/has/remove, destroy clears all") {
    Registry r;
    Entity a = r.create(1);
    r.emplace(a, Pos{3, 4});
    r.emplace(a, Hp{7});
    CHECK(r.get<Pos>(a)->x == 3);
    CHECK(r.has<Hp>(a));
    r.emplace(a, Pos{5, 6});                   // assign replaces
    CHECK(r.get<Pos>(a)->y == 6);
    CHECK(r.count<Pos>() == 1);
    CHECK(r.remove<Hp>(a));
    CHECK_FALSE(r.has<Hp>(a));
    CHECK_FALSE(r.remove<Hp>(a));
    r.destroy(a);
    CHECK(r.count<Pos>() == 0);
    CHECK_THROWS_AS(r.emplace(a, Pos{0, 0}), std::invalid_argument);
}

TEST_CASE("iteration is in StableId order regardless of creation/insertion order") {
    auto run = [](const std::vector<StableId>& create_order, bool reverse_components) {
        Registry r;
        std::vector<Entity> es;
        for (StableId id : create_order) es.push_back(r.create(id));
        std::vector<std::size_t> idx(es.size());
        for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = reverse_components ? idx.size() - 1 - i : i;
        for (std::size_t i : idx) {
            r.emplace(es[i], Pos{int32_t(create_order[i]), 0});
            if (create_order[i] % 2 == 0) r.emplace(es[i], Hp{int16_t(create_order[i])});
        }
        std::vector<StableId> seen, seen2;
        r.each<Pos>([&](Entity e, Pos& p) { seen.push_back(*r.stable_id(e)); CHECK(p.x == int32_t(*r.stable_id(e))); });
        r.each<Pos, Hp>([&](Entity e, Pos&, Hp& h) { seen2.push_back(*r.stable_id(e)); CHECK(h.hp % 2 == 0); });
        std::vector<StableId> all;
        for (Entity e : r.entities()) all.push_back(*r.stable_id(e));
        CHECK(all == seen);
        return std::make_pair(seen, seen2);
    };
    auto a = run({50, 3, 900, 12, 7, 8}, false);
    auto b = run({8, 7, 12, 900, 3, 50}, true);
    CHECK(a == b);
    CHECK(a.first == std::vector<StableId>{3, 7, 8, 12, 50, 900});
    CHECK(a.second == std::vector<StableId>{8, 12, 50, 900});
}

TEST_CASE("each skips entities missing secondary components; mutation through refs") {
    Registry r;
    Entity a = r.create(2), b = r.create(1);
    r.emplace(a, Pos{0, 0}); r.emplace(b, Pos{0, 0}); r.emplace(a, Hp{1});
    int n = 0;
    r.each<Pos, Hp>([&](Entity, Pos& p, Hp&) { p.x = 99; ++n; });
    CHECK(n == 1);
    CHECK(r.get<Pos>(a)->x == 99);
    CHECK(r.get<Pos>(b)->x == 0);
}
