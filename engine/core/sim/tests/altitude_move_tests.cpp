// Altitude PR2: chunk@3 ground height, vertical movement (ActionRequest::alt_to, log_hash 0xD7), ledges and falls,
// airborne flyers over terrain / obstacles / creatures, swim descent, and pathing. chunk@1 / chunk@2 stay flat.
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

TEST_CASE("altitude PR2: ledge_ft / step_cost_ft rules (mirrored by tools/validate step_extra_ft)") {
    const MoveCaps walk{}, climb{false, false, true}, fly{true, false, false};
    static_assert(ledge_ft(0, 10) == 10 && ledge_ft(10, 0) == -10);
    CHECK(step_cost_ft(0, 0, walk) == 0);
    CHECK(step_cost_ft(0, 5, walk) == 5);
    CHECK(step_cost_ft(5, 0, walk) == 5);
    CHECK(step_cost_ft(0, 10, walk) == 20);    // a climb without a climb speed: double
    CHECK(step_cost_ft(0, 10, climb) == 10);
    CHECK(step_cost_ft(0, 10, fly) == 10);
    CHECK(step_cost_ft(20, 0, walk) == 0);     // a drop: a fall, no movement cost
    CHECK(body_height_ft(1) == 5);
    CHECK(body_height_ft(2) == 10);
    CHECK(spans_overlap(0, 1, 0, 1));
    CHECK_FALSE(spans_overlap(10, 1, 0, 1));
    CHECK(spans_overlap(5, 1, 0, 2));          // a Large creature is 10 ft tall
}

TEST_CASE("altitude PR2: chunk@1 / chunk@2 elevation is flat for the sim") {
    const std::vector<Cell> hill = {{{5, 4}, 0, 3}, {{6, 4}, 0, 1}};
    Fight flat("walker", hill, false), plain("walker", {}, false);
    CHECK(flat.c->abs_height_ft(flat.me) == 0);
    CHECK_FALSE(flat.c->grid().has_ground());
    REQUIRE(flat.step({5, 4}).ok);
    REQUIRE(plain.step({5, 4}).ok);
    REQUIRE(flat.step({6, 4}).ok);
    REQUIRE(plain.step({6, 4}).ok);
    CHECK(flat.left() == plain.left());
    CHECK(flat.left() == 20);
    CHECK(flat.alt() == 0);
    CHECK(falls_events(*flat.c) == 0);
    CHECK(flat.c->hash() == plain.c->hash());
    CHECK(flat.c->log_hash() == plain.c->log_hash());
}

TEST_CASE("altitude PR2: chunk@3 step cost up / down, climb over 5 ft, ground = max over the footprint") {
    Fight f("walker", {{{5, 4}, 0, 1}, {{6, 4}, 0, 3}, {{7, 4}, 0, 2}}, true);
    CHECK(f.c->grid().ground_ft({6, 4}) == 15);
    REQUIRE(f.step({5, 4}).ok);   // rise 5: 5 + 5
    CHECK(f.left() == 20);
    CHECK(f.c->abs_height_ft(f.me) == 5);
    CHECK(f.step({6, 4}).error == "not enough movement");   // rise 10 is a climb: 5 + 2 * 10 = 25 > 20
    REQUIRE(f.step({5, 5}).ok);   // drop 5: 5 + 5
    CHECK(f.left() == 10);
    f.check_replay();
}

TEST_CASE("altitude PR2: stepping off a drop of more than 5 ft is a fall (flyers become airborne)") {
    Fight f("walker", {{{4, 4}, 0, 4}}, true);
    CHECK(f.c->abs_height_ft(f.me) == 20);
    REQUIRE(f.step({5, 4}).ok);
    CHECK(f.left() == 25);
    CHECK(f.alt() == 0);
    CHECK(falls_events(*f.c) == 1);
    REQUIRE(f.c->falls().size() == 1);
    CHECK(f.c->falls()[0].fell_ft == 20);
    f.check_replay();
    Fight g("flyer", {{{4, 4}, 0, 4}}, true);
    REQUIRE(g.step({5, 4}).ok);
    CHECK(g.alt() == 20);
    CHECK(falls_events(*g.c) == 0);
    CHECK(g.c->airborne(g.me));
    CHECK(g.c->abs_height_ft(g.me) == 20);
}

TEST_CASE("altitude PR2: alt_to vertical move, 5 ft per 5 ft, folded at 0xD7 only when non-zero") {
    Fight f("flyer", {{{5, 4}, tf::kDifficult, 0}}, false);
    const uint64_t lh0 = f.c->log_hash();
    CHECK(f.vertical(5).error == "");
    CHECK(f.alt() == 5);
    REQUIRE(f.c->log().back().step.request.alt_to == 5);
    CHECK(f.c->log().back().step.request.move_mode == 1);
    CHECK(f.c->log_hash() != lh0);
    REQUIRE(f.vertical(5).ok);
    CHECK(f.alt() == 10);
    // airborne: the difficult tile costs 5, not 10
    const int before = f.left();
    REQUIRE(f.step({5, 4}).ok);
    CHECK(before - f.left() == 5);
    CHECK(f.alt() == 10);
    f.check_replay();
    // the fold carries the value: a log with a different alt_to hashes differently
    auto log = f.c->log();
    log[log.size() - 2].step.request.alt_to = 0;   // horizontal entries carry 0 and are unchanged
    ecs::Registry r2;
    std::string err;
    auto again = replay(r2, f.chunk, f.l, 1, log, &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->log_hash() != f.c->log_hash());
    // a walker cannot rise without climbable terrain; nobody goes below the ground without water
    Fight w("walker", {}, true);
    CHECK(w.vertical(5).error == "alt_to: needs flight or climbable terrain");
    CHECK(w.vertical(-5).error == "alt_to: cannot go below the ground");
    CHECK(w.c->log().empty());
}

TEST_CASE("altitude PR2: airborne flyers pass over low obstacles and creatures they do not overlap") {
    // A 5 ft boulder (blocks_move, elevation 1) at (5,4), a full-height wall (elevation 0) at (4,5), an ally at (3,4).
    const std::vector<Cell> cells = {{{5, 4}, tf::kBlocksMove, 1}, {{4, 5}, tf::kBlocksMove, 0}};
    Fight w("walker", cells, true, Vec2i{3, 4});
    CHECK(w.step({5, 4}).error == "tile not walkable");
    Fight f("flyer", cells, true, Vec2i{3, 4});
    CHECK(f.step({5, 4}).error == "tile not walkable");   // on the ground
    REQUIRE(f.vertical(5).ok);
    CHECK(f.step({5, 4}).error == "tile not walkable");   // at 5 ft: the boulder's top is not below it
    REQUIRE(f.vertical(5).ok);
    REQUIRE(f.step({5, 4}).ok);                           // at 10 ft: over it
    CHECK(f.step({4, 5}).error == "tile not walkable");   // full-height wall
    REQUIRE(f.step({4, 4}).ok);
    REQUIRE(f.step({3, 4}).ok);                           // over the ally (spans [10,15) vs [0,5))
    CHECK(f.c->pos(f.me) == Vec2i{3, 4});
    f.check_replay();
}

TEST_CASE("altitude PR2: swim descent is clamped to the water depth") {
    Fight s("swimmer", {{{4, 4}, tf::kWater, 2}}, true);
    CHECK(s.c->grid().water_depth_ft({4, 4}) == 10);
    CHECK(s.c->grid().ground_ft({4, 4}) == 0);
    REQUIRE(s.vertical(-20).ok);
    CHECK(s.alt() == -10);
    CHECK(s.c->log().back().step.request.alt_to == -10);
    CHECK(s.vertical(-5).error == "alt_to: no altitude change");
    s.check_replay();
}

TEST_CASE("altitude PR2: pathing never plans a fall for a walker and prices ground") {
    world::Chunk c = arena("walker", {}, true);
    // a 20 ft plateau x in [4, 10], y in [0, 31] except a ramp of 5 ft steps along y = 20
    for (int y = 0; y < 32; ++y)
        for (int x = 4; x <= 10; ++x) c.layers[0][std::size_t(y * 32 + x)].elevation = 4;
    for (int x = 11; x <= 14; ++x) c.layers[0][std::size_t(20 * 32 + x)].elevation = uint8_t(4 - (x - 10));
    Grid g(c);
    REQUIRE(g.has_ground());
    const auto walk = g.path({10, 4}, {{12, 4}}, {}, 1, MoveCaps{});
    REQUIRE_FALSE(walk.empty());
    bool via_ramp = false;
    for (Vec2i p : walk) via_ramp = via_ramp || p.y == 20;
    CHECK(via_ramp);
    const auto fly = g.path({10, 4}, {{12, 4}}, {}, 1, MoveCaps{true, false, false});
    CHECK(fly.size() == 2);   // a flyer may step off
}

TEST_CASE("altitude PR2: burrowers tunnel under difficult ground and creatures (feat/rules-burrow-speed)") {
    const std::vector<Cell> cells = {{{5, 4}, tf::kDifficult, 0}, {{5, 5}, 0, 3}, {{4, 6}, tf::kWater, 2}};
    Fight b("burrower", cells, true, Vec2i{3, 4});
    REQUIRE(b.c->state().combatants.size() >= 2);
    CHECK(rules5e::find(b.c->state(), b.me)->burrow_ft == 20);
    REQUIRE(b.vertical(-10).ok);
    CHECK(b.alt() == -10);
    CHECK(b.c->burrowed(b.me));
    CHECK(b.left() == 20);
    REQUIRE(b.step({5, 4}).ok);   // difficult tile: 5 ft underground
    CHECK(b.left() == 15);
    REQUIRE(b.step({5, 5}).ok);   // 15 ft rise of the ground above: no ledge cost, stays 10 ft below it
    CHECK(b.left() == 10);
    CHECK(b.alt() == -10);
    CHECK(falls_events(*b.c) == 0);
    CHECK(b.step({4, 6}).error == "tile not walkable");   // no tunnelling into water
    REQUIRE(b.step({4, 4}).ok);
    CHECK(b.step({3, 4}).error == "");   // under the ally: spans do not overlap
    b.check_replay();
    const auto path = b.c->path_for(b.me, b.c->pos(b.me), {{6, 4}}, 1);
    CHECK_FALSE(path.empty());
    Fight w("walker", cells, true);
    CHECK(w.vertical(-5).error == "alt_to: cannot go below the ground");
}
