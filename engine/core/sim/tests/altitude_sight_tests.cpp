// Altitude PR5: elevation-aware line of sight and cover (absolute heights at both ends, integer interpolation), with
// sight_facts_of routed to the 3D LoS. chunk@1 / chunk@2 (and all-zero heights) give exactly the old 2D results.
#include <doctest/doctest.h>

#include <cstdint>
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
    return l;
}
struct Cell { Vec2i p; uint8_t flags = 0; uint8_t elevation = 0; };
world::Chunk arena(const std::vector<Cell>& cells, bool ground, bool bystander) {
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
    c.spawns.push_back({"test.monster.flyer", Vec2i{4, 4}, world::spawn_stable_id(c.coord, 0), R"({"team":"a"})"});
    c.spawns.push_back({"test.monster.walker", Vec2i{10, 4}, world::spawn_stable_id(c.coord, 1), R"({"team":"b"})"});
    if (bystander)
        c.spawns.push_back({"test.monster.walker", Vec2i{7, 4}, world::spawn_stable_id(c.coord, 2), R"({"team":"b"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    ContentLibrary l = lib();
    std::optional<Combat> c;
    CombatantId me = 0, foe = 0;
    Fight(const std::vector<Cell>& cells, bool ground, bool bystander = false) : chunk(arena(cells, ground, bystander)) {
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        me = chunk.spawns[0].stable_id;
        foe = chunk.spawns[1].stable_id;
    }
    void fly(int ft) { REQUIRE(c->set_altitude(me, int16_t(ft), kAltForced)); }
    rules5e::Cover cover() const {
        rules5e::CoverQuery q;
        q.source = me;
        q.target = foe;
        return c->cover_of(q);
    }
};

// The pre-PR5 Grid::line_of_sight, verbatim over a blocks_sight predicate (flat-equivalence reference).
template <class F> bool old_los(Vec2i a, Vec2i b, F&& blocks) {
    const int dx = a.x < b.x ? b.x - a.x : a.x - b.x, dy = a.y < b.y ? b.y - a.y : a.y - b.y;
    const int sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
    int x = a.x, y = a.y, ix = 0, iy = 0;
    auto blocked_mid = [&](Vec2i p) { return !(p == b) && blocks(p); };
    while (ix < dx || iy < dy) {
        const int64_t l = int64_t(1 + 2 * ix) * dy, r = int64_t(1 + 2 * iy) * dx;
        if (l == r) {
            if (blocked_mid({x + sx, y}) && blocked_mid({x, y + sy})) return false;
            x += sx; y += sy; ++ix; ++iy;
        } else if (l < r) { x += sx; ++ix; }
        else { y += sy; ++iy; }
        if (blocked_mid({x, y})) return false;
    }
    return true;
}
}  // namespace

TEST_CASE("altitude PR5: a flyer is seen over a low wall; a full-height (or chunk@1) wall still hides it") {
    const std::vector<Cell> low = {{{7, 4}, tf::kBlocksSight, 2}};   // 10 ft wall on chunk@3
    Fight g(low, true);
    CHECK_FALSE(g.c->line_of_sight(g.me, g.foe));                     // line at 0 ft < 10 ft top
    CHECK_FALSE(g.c->sight_facts_of(g.foe, g.me).line_of_sight);
    g.fly(30);                                                        // line height over the wall: 15 ft >= 10 ft
    CHECK(g.c->line_of_sight(g.me, g.foe));
    CHECK(g.c->line_of_sight(g.foe, g.me));                           // symmetric
    CHECK(g.c->sight_facts_of(g.foe, g.me).line_of_sight);            // sight_facts_of routes to the 3D LoS
    CHECK(g.c->sight_facts_of(g.me, g.foe).line_of_sight);
    g.fly(15);                                                        // 7.5 ft at the wall: still below the top
    CHECK_FALSE(g.c->line_of_sight(g.me, g.foe));
    g.fly(20);                                                        // exactly 10 ft at the wall: not below, seen
    CHECK(g.c->line_of_sight(g.me, g.foe));

    Fight full({{{7, 4}, tf::kBlocksSight, 0}}, true);               // elevation 0 = full height
    full.fly(200);
    CHECK_FALSE(full.c->line_of_sight(full.me, full.foe));
    Fight flat(low, false);                                           // chunk@1: elevation is render-only, wall is full
    flat.fly(200);
    CHECK_FALSE(flat.c->line_of_sight(flat.me, flat.foe));
}

TEST_CASE("altitude PR5: higher ground blocks line of sight below its top") {
    const std::vector<Cell> ridge = {{{6, 4}, 0, 4}, {{7, 4}, 0, 4}, {{8, 4}, 0, 4}};   // 20 ft ridge, no sight flag
    Fight g(ridge, true);
    CHECK_FALSE(g.c->line_of_sight(g.me, g.foe));
    CHECK_FALSE(g.c->sight_facts_of(g.me, g.foe).line_of_sight);
    CHECK(g.c->grid().sight_top_ft({7, 4}) == 20);
    CHECK(g.c->grid().line_of_sight({4, 4}, {10, 4}, 20, 20));        // level with the ridge top: seen
    CHECK_FALSE(g.c->grid().line_of_sight({4, 4}, {10, 4}, 20, 15));
    g.fly(40);                                                        // 40 -> 0 ft: 13 ft at (8,4) < 20 ft, hidden
    CHECK_FALSE(g.c->line_of_sight(g.me, g.foe));
    g.fly(60);                                                        // exactly 20 ft at (8,4): seen over the ridge
    CHECK(g.c->line_of_sight(g.me, g.foe));
    Fight flat(ridge, false);                                         // chunk@1: no ground, nothing blocks
    CHECK(flat.c->line_of_sight(flat.me, flat.foe));
    CHECK(flat.c->grid().sight_top_ft({7, 4}) == 0);
}

TEST_CASE("altitude PR5: cover across heights (walls and creatures by vertical span)") {
    Fight crowd({}, true, true);                                      // a creature at (7,4) between, on the ground
    CHECK(crowd.cover() == rules5e::Cover::Half);
    crowd.fly(30);                                                    // line passes 15 ft over a 5 ft tall creature
    CHECK(crowd.cover() == rules5e::Cover::None);
    crowd.fly(5);                                                     // 2-3 ft at the bystander: inside [0, 5)
    CHECK(crowd.cover() == rules5e::Cover::Half);

    Fight wall({{{7, 3}, tf::kBlocksSight, 2}, {{7, 4}, tf::kBlocksSight, 2}, {{7, 5}, tf::kBlocksSight, 2}}, true);
    CHECK(wall.cover() == rules5e::Cover::Total);
    wall.fly(40);                                                     // >= 10 ft over the 10 ft wall on every line
    CHECK(wall.cover() == rules5e::Cover::None);
    // Explicit-height form agrees, and the 6-arg form is the ground-height one.
    CHECK(wall.c->cover_between({4, 4}, 1, 40, {10, 4}, 1, 0, wall.me, wall.foe) == rules5e::Cover::None);
    CHECK(wall.c->cover_between({4, 4}, 1, {10, 4}, 1, wall.me, wall.foe) == rules5e::Cover::Total);

    Fight flat({{{7, 3}, tf::kBlocksSight, 2}, {{7, 4}, tf::kBlocksSight, 2}, {{7, 5}, tf::kBlocksSight, 2}}, false);
    flat.fly(40);                                                     // chunk@1: full-height wall regardless of altitude
    CHECK(flat.cover() == rules5e::Cover::Total);
}

TEST_CASE("altitude PR5: flat equivalence - all heights 0 (or chunk@1 / chunk@2) give the 2D results") {
    uint32_t seed = 0x5EEDu;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int ground = 0; ground < 2; ++ground)
        for (int map = 0; map < 6; ++map) {
            std::vector<Cell> cells;
            for (int i = 0; i < 120; ++i) {
                const Vec2i p{int(next() % 32), int(next() % 32)};
                // chunk@1: random render-only elevations; chunk@3: every elevation 0.
                cells.push_back({p, uint8_t(next() % 3 == 0 ? tf::kBlocksSight : 0), uint8_t(ground ? 0 : next() % 8)});
            }
            const world::Chunk ch = arena(cells, ground != 0, false);
            const Grid gr(ch);
            auto blocks = [&](Vec2i p) { return gr.blocks_sight(p); };
            for (int i = 0; i < 400; ++i) {
                const Vec2i a{int(next() % 32), int(next() % 32)}, b{int(next() % 32), int(next() % 32)};
                const bool want = old_los(a, b, blocks);
                CHECK(gr.line_of_sight(a, b) == want);
                CHECK(gr.line_of_sight(a, b, 0, 0) == want);
                if (!ground) CHECK(gr.line_of_sight(a, b, int(next() % 100), int(next() % 100)) == want);   // full walls
            }
        }
}
