// SRD 5.1 movement modes (feat/move-modes): fly / swim / climb speeds, water / climbable / open_air terrain, switching speeds.
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
    REQUIRE_FALSE(l.add(goblin("climber", Json::parse(R"({"walk":30,"climb":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("burrower", Json::parse(R"({"walk":30,"burrow":20})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("burrow_hi", Json::parse(R"({"walk":30,"burrow":300})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("burrow_neg", Json::parse(R"({"walk":30,"burrow":-10})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("foe", Json::parse(R"({"walk":30})")), "monster"));
    Json big = goblin("large_swimmer", Json::parse(R"({"walk":30,"swim":30})"));
    big["size"] = "large";
    REQUIRE_FALSE(l.add(big, "monster"));
    return l;
}
struct Cell { Vec2i p; uint8_t flags; bool ground = true; };
// 32x32 floor; `cells` set flags (and remove the ground for open air). The mover (spawn 0) faces a foe far away.
world::Chunk arena(const std::string& who, const std::vector<Cell>& cells, std::optional<Vec2i> ally = std::nullopt) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (const Cell& k : cells) {
        auto& t = c.layers[0][std::size_t(k.p.y * 32 + k.p.x)];
        t.flags = k.flags;
        if (!k.ground) t.tile = 0;
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
    Fight(const std::string& who, const std::vector<Cell>& cells, std::optional<Vec2i> ally = std::nullopt)
        : chunk(arena(who, cells, ally)) {
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
    int left() const { return c->state().budget.movement_ft; }
    uint8_t last_mode() const { return c->log().back().step.request.move_mode; }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, l, 1, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
}  // namespace

TEST_CASE("move modes: content speeds reach the rules") {
    Fight f("flyer", {});
    const auto* me = rules5e::find(f.c->state(), f.me);
    CHECK(me->fly_ft == 60); CHECK(me->swim_ft == 0); CHECK(me->speed_ft == 30);
    CHECK(f.c->caps_of(f.me).fly);
    Fight w("walker", {});
    CHECK_FALSE(w.c->caps_of(w.me).fly);
    CHECK(rules5e::find(w.c->state(), w.me)->burrow_ft == 0);
}

TEST_CASE("burrow speed: the loader parses speed.burrow; walking replay and hash match a walker") {
    ContentLibrary l = lib();
    const Archetype* bur = l.find("test.monster.burrower");
    const Archetype* hi = l.find("test.monster.burrow_hi");
    const Archetype* neg = l.find("test.monster.burrow_neg");
    REQUIRE(bur); REQUIRE(hi); REQUIRE(neg);
    CHECK(bur->base.burrow_ft == 20);
    CHECK(bur->base.fly_ft == 0);
    CHECK(bur->base.swim_ft == 0);
    CHECK(bur->base.climb_ft == 0);
    CHECK(hi->base.burrow_ft == 255);    // same clamp as climb
    CHECK(neg->base.burrow_ft == 0);
    Fight b("burrower", {});
    const auto* me = rules5e::find(b.c->state(), b.me);
    REQUIRE(me);
    CHECK(me->burrow_ft == 20);
    CHECK(me->speed_ft == 30);
    CHECK_FALSE(b.c->caps_of(b.me).fly);
    CHECK_FALSE(b.c->caps_of(b.me).swim);
    CHECK_FALSE(b.c->caps_of(b.me).climb);
    REQUIRE(b.step({5, 4}).ok);
    CHECK(b.last_mode() == 0);   // still walking: burrow is not a move mode
    CHECK(b.left() == 25);
    CHECK(rules5e::find(b.c->state(), b.me)->burrow_ft == 20);
    b.check_replay();
    Fight w("walker", {});
    REQUIRE(w.step({5, 4}).ok);
    CHECK(b.c->hash() == w.c->hash());
    CHECK(b.c->log_hash() == w.c->log_hash());
}

TEST_CASE("move modes: water costs a walker 1 extra foot per foot; a swimmer or flyer pays the normal cost") {
    const std::vector<Cell> pool = {{{5, 4}, tf::kWater}, {{6, 4}, tf::kWater}};
    Fight w("walker", pool);
    REQUIRE(w.step({5, 4}).ok);
    CHECK(w.left() == 20);
    CHECK(w.last_mode() == 0);
    Fight s("swimmer", pool);
    REQUIRE(s.step({5, 4}).ok);
    CHECK(s.left() == 25);
    CHECK(s.last_mode() == 2);
    REQUIRE(s.step({6, 4}).ok);
    REQUIRE(s.step({7, 4}).ok);   // back on land: walking
    CHECK(s.last_mode() == 0);
    s.check_replay();
    Fight fl("flyer", pool);
    REQUIRE(fl.step({5, 4}).ok);
    CHECK(fl.left() == 25);
    CHECK(fl.last_mode() == 1);
}

TEST_CASE("move modes: climb terrain - a walker climbs at double cost, a climber at the normal cost") {
    const std::vector<Cell> wall = {{{5, 4}, uint8_t(tf::kBlocksMove | tf::kClimbable)}};
    Fight w("walker", wall);
    REQUIRE(w.step({5, 4}).ok);
    CHECK(w.left() == 20);
    Fight k("climber", wall);
    REQUIRE(k.step({5, 4}).ok);
    CHECK(k.left() == 25);
    CHECK(k.last_mode() == 3);
    k.check_replay();
    // A plain wall (no climbable flag) still blocks everyone.
    Fight b("climber", {{{5, 4}, tf::kBlocksMove}});
    CHECK(b.step({5, 4}).error == "tile not walkable");
}

TEST_CASE("move modes: open air - only a flyer enters; a hole without the flag stays a hole") {
    const std::vector<Cell> chasm = {{{5, 4}, tf::kOpenAir, false}};
    Fight w("walker", chasm);
    CHECK(w.step({5, 4}).error == "tile not walkable");
    Fight f("flyer", chasm);
    REQUIRE(f.step({5, 4}).ok);
    CHECK(f.last_mode() == 1);
    Fight h("flyer", {{{5, 4}, 0, false}});   // no ground, no flag: not open air (old maps unchanged)
    CHECK(h.step({5, 4}).error == "tile not walkable");
}

TEST_CASE("move modes: switching speeds subtracts the distance already moved") {
    Fight f("flyer", {});   // walk 30, fly 60
    for (int x = 5; x <= 10; ++x) REQUIRE(f.step({x, 4}).ok);   // 30 ft walking
    CHECK(f.last_mode() == 0);
    CHECK(f.left() == 0);
    for (int x = 11; x <= 16; ++x) REQUIRE(f.step({x, 4}).ok);   // 30 more by flight (60 - 30 moved)
    CHECK(f.last_mode() == 1);
    CHECK(f.step({17, 4}).error == "not enough movement");
    f.check_replay();
    Fight w("walker", {});
    for (int x = 5; x <= 10; ++x) REQUIRE(w.step({x, 4}).ok);
    CHECK(w.step({11, 4}).error == "not enough movement");
}

TEST_CASE("move modes: pathing - a flyer crosses a chasm, a walker goes around; deterministic; no flags = old search") {
    std::vector<Cell> chasm;
    for (int y = 0; y < 30; ++y) chasm.push_back({{8, y}, tf::kOpenAir, false});   // a gap open only at y 30, 31
    Fight f("flyer", chasm);
    const Grid& g = f.c->grid();
    const auto fly = g.path({4, 4}, {{12, 4}}, {}, 1, f.c->caps_of(f.me));
    REQUIRE_FALSE(fly.empty());
    CHECK(fly.size() == 8);
    CHECK(fly == g.path({4, 4}, {{12, 4}}, {}, 1, f.c->caps_of(f.me)));
    const auto walk = g.path({4, 4}, {{12, 4}}, {}, 1, MoveCaps{});
    REQUIRE_FALSE(walk.empty());
    CHECK(walk.size() > 20);
    CHECK(walk == g.path({4, 4}, {{12, 4}}, {}, 1));   // no water on the way: the caps search equals the old one
}

TEST_CASE("move modes x size space: a Large swimmer swims through an ally's space (difficult) and can't end there") {
    std::vector<Cell> pool;
    for (int x = 6; x <= 9; ++x) for (int y = 4; y <= 5; ++y) pool.push_back({{x, y}, tf::kWater});
    Fight f("large_swimmer", pool, Vec2i{7, 4});   // a Medium ally in the water
    REQUIRE(f.step({5, 4}).ok);   // enters x 6 (water): swimming, 5 ft
    CHECK(f.last_mode() == 2);
    CHECK(f.left() == 25);
    REQUIRE(f.step({6, 4}).ok);   // enters x 7: water + the ally's space: difficult, 10 ft, still swimming
    CHECK(f.last_mode() == 2);
    CHECK(f.left() == 15);
    Command e;
    e.request = {f.me, rules5e::ActionKind::EndTurn};
    CHECK(f.c->submit(e).error == "cannot end movement in another creature's space");
    REQUIRE(f.step({7, 4}).ok);   // x 7..8: still over the ally, 10 ft, 5 left for the way out
    CHECK(f.left() == 5);
    REQUIRE(f.step({8, 4}).ok);   // x 8..9: out of the ally, water: 5 ft
    CHECK(f.last_mode() == 2);
    CHECK(f.left() == 0);
    f.check_replay();
}

TEST_CASE("move modes x size space: a walker wading through an ally's space in water pays 15 ft (swim + difficult stack)") {
    std::vector<Cell> pool;
    for (int x = 5; x <= 7; ++x) pool.push_back({{x, 4}, tf::kWater});
    Fight f("walker", pool, Vec2i{6, 4});   // a Medium ally in the water
    REQUIRE(f.step({5, 4}).ok);   // water, no swim speed: 10 ft
    CHECK(f.left() == 20);
    REQUIRE(f.step({6, 4}).ok);   // water + the ally's space: 5 + 5 (swimming) + 5 (difficult) = 15 ft
    CHECK(f.left() == 5);
    CHECK(f.step({7, 4}).error.size() > 0);   // 10 ft of water left to pay with 5 ft
    f.check_replay();
}
