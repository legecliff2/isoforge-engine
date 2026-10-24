// Altitude PR4 (sim side): ledge falls deal falling damage (1d6 per 10 ft, rules RNG) and land prone; one kAltFall event;
// rules-side falls (prone flyer, revoke_fly) surface as AltitudeChanged kAltFall from complete(); replay determinism.
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <string>

#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
namespace tf = world::tile_flags;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json goblin(const std::string& id, const Json& speed) {
    std::ifstream f(kRoot + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE(f.good());
    Json m = Json::parse(f);
    m["$schema"] = "monster@3";
    m["id"] = "test.monster." + id;
    m["speed"] = speed;
    m["hit_points"]["average"] = 60;
    return m;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE_FALSE(l.add(goblin("walker", Json::parse(R"({"walk":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("flyer", Json::parse(R"({"walk":30,"fly":60})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("swimmer", Json::parse(R"({"walk":30,"swim":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("burrower", Json::parse(R"({"walk":30,"burrow":20})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("foe", Json::parse(R"({"walk":30})")), "monster"));
    return l;
}
struct Cell { Vec2i p; uint8_t flags = 0; uint8_t elevation = 0; };
world::Chunk arena(const std::string& who, const std::vector<Cell>& cells, bool ground, std::optional<Vec2i> ally = std::nullopt) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    c.elevation_is_ground = ground;
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (const Cell& k : cells) {
        auto& t = c.layers[0][std::size_t(k.p.y * 32 + k.p.x)];
        t.flags = k.flags;
        t.elevation = k.elevation;
    }
    c.spawns.push_back({"test.monster." + who, Vec2i{4, 4}, world::spawn_stable_id(c.coord, 0), R"({"team":"a"})"});
    c.spawns.push_back({"test.monster.foe", Vec2i{28, 28}, world::spawn_stable_id(c.coord, 1), R"({"team":"b"})"});
    if (ally) c.spawns.push_back({"test.monster.walker", *ally, world::spawn_stable_id(c.coord, 2), R"({"team":"a"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    ContentLibrary l = lib();
    std::optional<Combat> c;
    CombatantId me = 0;
    Fight(const std::string& who, const std::vector<Cell>& cells, bool ground, std::optional<Vec2i> ally = std::nullopt)
        : chunk(arena(who, cells, ground, ally)) {
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        me = chunk.spawns[0].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != me; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == me);
    }
    SubmitResult step(Vec2i to) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = to;
        return c->submit(s);
    }
    SubmitResult vertical(int16_t d) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = c->pos(me);
        s.request.alt_to = d;
        return c->submit(s);
    }
    void next_turn() {   // end turns until it is `me` again
        for (int i = 0; i < 7; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
            if (rules5e::current_actor(c->state()) == me) return;
        }
        FAIL("me never got a turn");
    }
    uint8_t last_mode() const { return c->log().back().step.request.move_mode; }
    int alt() const { return rules5e::find(c->state(), me)->alt_ft; }
    int left() const { return c->state().budget.movement_ft; }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, l, 1, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
int falls_events(const Combat& c) {
    int n = 0;
    for (const auto& e : c.events()) n += (e.kind == SimEvent::Kind::AltitudeChanged && e.rules.b == kAltFall) ? 1 : 0;
    return n;
}
}  // namespace

TEST_CASE("altitude PR4: a ledge fall deals 1d6 per 10 ft and lands prone (one kAltFall event, replays)") {
    Fight f("walker", {{{4, 4}, 0, 10}}, true);   // 50 ft drop: 5d6
    const auto* me = rules5e::find(f.c->state(), f.me);
    const int hp0 = me->hp;
    rng::Rng r = f.c->state().rng;
    int want = 0;
    for (int i = 0; i < 5; ++i) want += r.d(6);
    REQUIRE(f.step({5, 4}).ok);
    me = rules5e::find(f.c->state(), f.me);
    CHECK(f.alt() == 0);
    CHECK(me->hp == hp0 - want);
    CHECK(me->has(rules5e::Condition::Prone));
    CHECK(falls_events(*f.c) == 1);
    REQUIRE(f.c->falls().size() == 1);
    CHECK(f.c->falls()[0].fell_ft == 50);
    f.check_replay();
    f.next_turn();
    f.check_replay();
}

TEST_CASE("altitude PR4: a flyer that steps off a ledge does not fall; a 5 ft drop is a step") {
    Fight g("flyer", {{{4, 4}, 0, 10}}, true);
    const int hp0 = rules5e::find(g.c->state(), g.me)->hp;
    REQUIRE(g.step({5, 4}).ok);
    CHECK(g.alt() == 50);
    CHECK(rules5e::find(g.c->state(), g.me)->hp == hp0);
    CHECK(falls_events(*g.c) == 0);
    Fight w("walker", {{{4, 4}, 0, 1}}, true);
    const int hw = rules5e::find(w.c->state(), w.me)->hp;
    REQUIRE(w.step({5, 4}).ok);
    CHECK(rules5e::find(w.c->state(), w.me)->hp == hw);
    CHECK_FALSE(rules5e::find(w.c->state(), w.me)->has(rules5e::Condition::Prone));
}

TEST_CASE("altitude PR4: identical ledge falls hash identically (deterministic)") {
    Fight a("walker", {{{4, 4}, 0, 8}}, true), b("walker", {{{4, 4}, 0, 8}}, true);
    REQUIRE(a.step({5, 4}).ok);
    REQUIRE(b.step({5, 4}).ok);
    CHECK(a.c->hash() == b.c->hash());
    CHECK(a.c->log_hash() == b.c->log_hash());
    a.check_replay();
}
