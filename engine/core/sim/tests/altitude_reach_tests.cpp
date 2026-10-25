// Altitude PR3 (design §4): 3D distance (max(dx, dy, dz) over absolute heights and vertical spans), melee reach, ranges
// and opportunity attacks in 3D (rising out of reach provokes), and the SRD Flyby trait (no OA when it flies out of reach).
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <string>

#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json goblin(const std::string& id, const Json& speed, bool flyby = false) {
    std::ifstream f(kRoot + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE(f.good());
    Json m = Json::parse(f);
    m["$schema"] = "monster@3";
    m["id"] = "test.monster." + id;
    m["speed"] = speed;
    m["hit_points"]["average"] = 60;
    if (flyby) m["traits"].push_back(Json::parse(R"({"id":"flyby","name":"Flyby","description":"No opportunity attacks when it flies out of reach."})"));
    return m;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE_FALSE(l.add(goblin("flyer", Json::parse(R"({"walk":30,"fly":60})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("owlish", Json::parse(R"({"walk":30,"fly":60})"), true), "monster"));
    REQUIRE_FALSE(l.add(goblin("foe", Json::parse(R"({"walk":30})")), "monster"));
    return l;
}
world::Chunk arena(const std::string& who, Vec2i foe, bool ground, uint8_t foe_elev = 0) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    c.elevation_is_ground = ground;
    for (auto& cell : c.layers[0]) cell.tile = 1;
    c.layers[0][std::size_t(foe.y * 32 + foe.x)].elevation = foe_elev;
    c.spawns.push_back({"test.monster." + who, Vec2i{4, 4}, world::spawn_stable_id(c.coord, 0), R"({"team":"a"})"});
    c.spawns.push_back({"test.monster.foe", foe, world::spawn_stable_id(c.coord, 1), R"({"team":"b"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    ContentLibrary l = lib();
    std::optional<Combat> c;
    CombatantId me = 0, foe = 0;
    Fight(const std::string& who, Vec2i foe_at = {5, 4}, bool ground = false, uint8_t foe_elev = 0)
        : chunk(arena(who, foe_at, ground, foe_elev)) {
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        me = chunk.spawns[0].stable_id;
        foe = chunk.spawns[1].stable_id;
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
    SubmitResult melee(CombatantId t) {
        Command a;
        a.request = {me, rules5e::ActionKind::Attack, t, 0};   // goblin attack 0: Scimitar (5 ft reach)
        return c->submit(a);
    }
    int alt() const { return rules5e::find(c->state(), me)->alt_ft; }
};
bool oa_window(const SubmitResult& r) { return r.status == StepStatus::NeedsDecision && r.needs && r.needs->trigger == ReactionTrigger::LeavingReach; }
}  // namespace

TEST_CASE("altitude PR3: flat equivalence - with every height 0 the 3D distance is the 2D one") {
    for (int na = 1; na <= 4; ++na)
        for (int nb = 1; nb <= 4; ++nb)
            for (int y = -6; y <= 6; ++y)
                for (int x = -6; x <= 6; ++x) {
                    const Vec2i a{10, 10}, b{10 + x, 10 + y};
                    REQUIRE(rect_distance(a, na, 0, b, nb, 0) == rect_distance(a, na, b, nb));
                    REQUIRE(rect_distance(a, na, 15, b, nb, 15) == rect_distance(a, na, b, nb));   // same height anywhere
                }
    static_assert(rect_distance(Vec2i{0, 0}, 1, 0, Vec2i{3, 1}, 1, 0) == chebyshev(Vec2i{0, 0}, Vec2i{3, 1}));
    // in the sim: a flat board measures exactly as before
    Fight f("flyer", {7, 6});
    CHECK(f.c->distance_tiles(f.me, f.foe) == rect_distance(Vec2i{4, 4}, 1, Vec2i{7, 6}, 1));
    CHECK(f.c->distance_tiles(f.me, f.foe) == 3);
}

TEST_CASE("altitude PR3: vertical spans - dz counts like dx (0 overlap, 1 touching), Medium 1 tile, N x N N tiles tall") {
    const Vec2i o{0, 0};
    CHECK(rect_distance(o, 1, 5, o, 1, 0) == 1);     // directly above a Medium creature, 5 ft up: adjacent
    CHECK(rect_distance(o, 1, 10, o, 1, 0) == 2);    // 10 ft up: out of 5 ft reach
    CHECK(rect_distance(o, 1, -10, o, 1, 0) == 2);   // burrowed 10 ft down: symmetric
    CHECK(rect_distance(o, 1, 0, o, 1, 10) == rect_distance(o, 1, 10, o, 1, 0));
    // Large (2x2) is 10 ft tall: a flyer 10 ft up touches its top, 15 ft is one tile clear
    CHECK(rect_distance(o, 2, 0, o, 1, 5) == 0);
    CHECK(rect_distance(o, 2, 0, o, 1, 10) == 1);
    CHECK(rect_distance(o, 2, 0, o, 1, 15) == 2);
    // Huge (3x3) 15 ft, Gargantuan (4x4) 20 ft
    CHECK(rect_distance(o, 3, 0, Vec2i{5, 0}, 1, 25) == 3);   // dx 3, dz 25/5 - 2 = 3
    CHECK(rect_distance(o, 4, 0, Vec2i{4, 0}, 1, 30) == 3);   // dx 1, dz 6 - 3 = 3
    CHECK(rect_distance(o, 4, 0, Vec2i{4, 0}, 1, 15) == 1);   // overlapping spans: horizontal distance only
    // the larger of horizontal and vertical (Chebyshev)
    CHECK(rect_distance(o, 1, 0, Vec2i{4, 0}, 1, 10) == 4);
}

TEST_CASE("altitude PR3: melee reach vs a flyer, ranges and uneven ground use absolute heights") {
    Fight f("flyer");
    CHECK(f.c->distance_tiles(f.me, f.foe) == 1);
    REQUIRE(f.vertical(5).ok);
    CHECK(f.c->distance_tiles(f.me, f.foe) == 1);   // 5 ft up, still in reach (OA-free: still within reach)
    CHECK(f.c->log().back().step.request.leaves_reach == false);
    // (alt 10 is tested below with the OA)
    Fight g("flyer");
    g.c->set_altitude(g.me, 10, kAltForced);
    CHECK(g.c->distance_tiles(g.me, g.foe) == 2);
    CHECK(g.melee(g.foe).error == "target out of reach");
    g.c->set_altitude(g.me, 5, kAltForced);
    CHECK(g.melee(g.foe).error != "target out of reach");
    // chunk@3: the foe stands on a 10 ft rise; the walker-height flyer at 0 is 2 tiles from it
    Fight h("flyer", {5, 4}, true, 2);
    CHECK(h.c->abs_height_ft(h.foe) == 10);
    CHECK(h.c->distance_tiles(h.me, h.foe) == 2);
    CHECK(h.melee(h.foe).error == "target out of reach");
}

TEST_CASE("altitude PR3: rising out of reach provokes an opportunity attack") {
    Fight f("flyer");
    REQUIRE(f.vertical(5).ok);
    CHECK_FALSE(f.c->would_leave_reach(f.me, {4, 4}, {4, 4}));
    auto r = f.vertical(5);   // 5 -> 10 ft: dz 2 > reach 1
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(oa_window(r));
    CHECK(r.needs->reactor == f.foe);
    CHECK(r.needs->trigger_actor == f.me);
    CHECK(f.alt() == 5);   // paused before the move applies
    auto r2 = f.c->decide({r.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(r2.ok, r2.error);
    CHECK(r2.reactions.size() == 1);
    CHECK(rules5e::find(f.c->state(), f.foe)->reaction_used);
    CHECK(f.alt() == 10);
    CHECK(f.c->log().back().step.request.leaves_reach);
    // replay reproduces it
    ecs::Registry r3;
    std::string err;
    auto again = replay(r3, f.chunk, f.l, 1, f.c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == f.c->hash());
    // once out of reach, moving on (sideways, still high) provokes nothing more
    Fight g("flyer");
    g.c->set_altitude(g.me, 10, kAltForced);
    CHECK_FALSE(g.c->would_leave_reach(g.me, {4, 4}, {3, 4}));
}

TEST_CASE("altitude PR3: Flyby skips an OA only when flying out of reach; a ground walk still provokes") {
    Fight f("owlish");
    CHECK(rules5e::find(f.c->state(), f.me)->flyby);
    CHECK_FALSE(rules5e::find(f.c->state(), f.foe)->flyby);
    // vertical fly (move_mode 1, alt_to): out of reach, no window; the fact stays set
    REQUIRE(f.vertical(5).ok);
    auto r = f.vertical(5);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.status == StepStatus::Done);
    CHECK_FALSE(r.needs);
    CHECK(f.c->log().back().step.request.leaves_reach);
    CHECK(f.c->log().back().step.request.move_mode == 1);
    CHECK(f.c->log().back().step.request.alt_to == 5);
    CHECK_FALSE(rules5e::find(f.c->state(), f.foe)->reaction_used);
    CHECK(f.alt() == 10);
    // horizontal fly while airborne: also a fly leave, so no window
    Fight air("owlish");
    REQUIRE(air.vertical(5).ok);
    auto side = air.step({3, 4});
    REQUIRE_MESSAGE(side.ok, side.error);
    CHECK(side.status == StepStatus::Done);
    CHECK_FALSE(side.needs);
    CHECK(air.c->log().back().step.request.leaves_reach);
    CHECK(air.c->log().back().step.request.move_mode == 1);
    CHECK(air.c->log().back().step.request.alt_to == 0);
    CHECK_FALSE(rules5e::find(air.c->state(), air.foe)->reaction_used);
    // ground walk-out still provokes; sim still flags leaves_reach
    Fight g("owlish");
    auto walk = g.step({3, 4});
    REQUIRE_MESSAGE(walk.ok, walk.error);
    REQUIRE(oa_window(walk));
    CHECK(walk.needs->reactor == g.foe);
    auto taken = g.c->decide({walk.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(taken.ok, taken.error);
    CHECK(taken.reactions.size() == 1);
    CHECK(rules5e::find(g.c->state(), g.foe)->reaction_used);
    CHECK(g.c->log().back().step.request.leaves_reach);
    CHECK(g.c->log().back().step.request.move_mode == 0);
    CHECK(g.c->log().back().step.request.alt_to == 0);
    // the plain flyer does provoke on the same ground step
    Fight h("flyer");
    CHECK(oa_window(h.step({3, 4})));
}
