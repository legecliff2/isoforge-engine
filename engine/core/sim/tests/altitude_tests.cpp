// Altitude PR1: Combatant::alt_ft storage, the Combat::hash 0xD6 fold (only when some alt_ft != 0) and the
// SimEvent::AltitudeChanged event (v12, only on change).
#include <doctest/doctest.h>

#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
world::Chunk alt_arena() {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* ids[2] = {"srd51.character.fighter_1", "srd51.monster.goblin"};
    const Vec2i at[2] = {{4, 4}, {7, 4}};
    for (uint32_t i = 0; i < 2; ++i) c.spawns.push_back({ids[i], at[i], world::spawn_stable_id(c.coord, i), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
int alt_events(const Combat& c) {
    int n = 0;
    for (const auto& e : c.events()) n += e.kind == SimEvent::Kind::AltitudeChanged ? 1 : 0;
    return n;
}
}  // namespace

TEST_CASE("altitude: 0xD6 fold only off the ground; AltitudeChanged only on change; v12 gating") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    auto ch = alt_arena();
    ecs::Registry ra, rb;
    auto a = Combat::create(ra, ch, lib, 7);
    auto b = Combat::create(rb, ch, lib, 7);
    REQUIRE(a);
    REQUIRE(b);
    const CombatantId f = ch.spawns[0].stable_id;
    for (const auto& c : a->state().combatants) CHECK(c.alt_ft == 0);
    const uint64_t h0 = a->hash();
    const uint32_t v0 = event_stream_version(a->events());
    CHECK(v0 < 12);
    // setting the same value (0) is no change: no event, hash unchanged
    CHECK_FALSE(a->set_altitude(f, 0, kAltMove));
    CHECK(alt_events(*a) == 0);
    CHECK(a->hash() == h0);
    CHECK(a->hash() == b->hash());
    // off the ground: hash changes, one event, stream is v12
    CHECK(a->set_altitude(f, 15, kAltMove));
    CHECK(rules5e::find(a->state(), f)->alt_ft == 15);
    CHECK(a->hash() != h0);
    CHECK(alt_events(*a) == 1);
    CHECK(event_stream_version(a->events()) == 12);
    const auto& e = a->events().back();
    CHECK(e.kind == SimEvent::Kind::AltitudeChanged);
    CHECK(e.actor == f);
    CHECK(e.tile == a->pos(f));
    CHECK(e.rules.a == 15);
    CHECK(e.rules.b == kAltMove);
    // repeating the value: no event
    CHECK_FALSE(a->set_altitude(f, 15, kAltMove));
    CHECK(alt_events(*a) == 1);
    // altitude value is part of the fold
    const uint64_t h15 = a->hash();
    CHECK(a->set_altitude(f, -10, kAltForced));
    CHECK(a->hash() != h15);
    CHECK(a->events().back().rules.a == int32_t(uint16_t(int16_t(-10))));
    // back to the ground (the hash also covers the stream, so it need not return to h0)
    CHECK(a->set_altitude(f, 0, kAltFall));
    CHECK(rules5e::find(a->state(), f)->alt_ft == 0);
    CHECK(alt_events(*a) == 3);
    CHECK_FALSE(a->set_altitude(0xFFFFFFFFu, 5, kAltMove));   // unknown id
}
