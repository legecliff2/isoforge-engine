// Combat::hash folds the v7 state tables (action_uses tag 0xA1, pools tag 0xA3) only when non-empty.
// CombatTestAccess is a test-only friend of Combat: it registers rows (nothing in sim does yet, so
// no public API can); it is not part of the sim contract.
#include <doctest/doctest.h>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;

namespace {
world::Chunk arena() {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {"srd51.character.fighter_1", "srd51.monster.goblin"};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + 3 * i), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
}  // namespace

TEST_CASE("Combat::hash: v7 sections are folded only when non-empty, each under its own tag") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena();
    auto make = [&](ecs::Registry& reg) { auto c = Combat::create(reg, ch, l, 9); REQUIRE(c); return std::move(*c); };
    ecs::Registry r0, r1, r2, r3, r4;
    Combat base = make(r0), au = make(r1), pl = make(r2), both = make(r3), again = make(r4);
    const CombatantId a = ch.spawns[0].stable_id;
    const uint64_t h0 = base.hash();
    CHECK(again.hash() == h0);   // deterministic, and empty tables fold nothing

    rules5e::register_action_use(CombatTestAccess::state(au), a, 77);
    rules5e::register_pool(CombatTestAccess::state(pl), a, 5, 3, 3);
    rules5e::register_action_use(CombatTestAccess::state(both), a, 77);
    rules5e::register_pool(CombatTestAccess::state(both), a, 5, 3, 3);

    const uint64_t hau = au.hash(), hpl = pl.hash(), hboth = both.hash();
    CHECK(hau != h0);
    CHECK(hpl != h0);
    CHECK(hau != hpl);               // the tag bytes keep "action uses only" and "pools only" apart
    CHECK(hboth != hau);
    CHECK(hboth != hpl);
    // Every field of a row is covered.
    CHECK_FALSE(hau == [&] { rules5e::register_action_use(CombatTestAccess::state(au), a, 77, true); return au.hash(); }());
    rules5e::register_action_use(CombatTestAccess::state(au), a, 77, false);
    CHECK(au.hash() == hau);          // restoring the row restores the hash
    rules5e::register_pool(CombatTestAccess::state(pl), a, 5, 3, 2);
    CHECK(pl.hash() != hpl);
    // Emptying a table returns to the baseline: nothing is folded for an empty table.
    CombatTestAccess::state(both).action_uses.clear();
    CombatTestAccess::state(both).pools.clear();
    CHECK(both.hash() == h0);
}
