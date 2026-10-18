// Multi-tile footprints for Large+ creatures (docs/proposals/LARGE_CREATURE_FOOTPRINT_AUDIT.md):
// size -> footprint loading (monster@3 / character@3 only), anchor convention, occupancy and spawns.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

// A goblin stat block relabelled as `<size>` creature `test.monster.<size>`.
Json sized_doc(const std::string& size, const std::string& schema = "monster@3") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = "test.monster." + size;
    m["size"] = size;
    m["hit_points"]["average"] = 60;
    return m;
}

ContentLibrary make_lib(const std::vector<std::string>& sizes) {
    ContentLibrary lib;
    for (const auto& s : sizes) REQUIRE_FALSE(lib.add(sized_doc(s), "monster"));
    return lib;
}

struct Spawn { std::string archetype; int x, y; };
// 32x32 floor arena; medium creatures are team "small", everything else team "big" (hostile to each other).
//  `walls` block movement (layer 2), `holes` have no ground.
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> walls = {}, std::vector<Vec2i> holes = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksMove;
    for (Vec2i h : holes) c.layers[0][std::size_t(h.y * 32 + h.x)].tile = 0;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)),
                            spawns[i].archetype == "test.monster.medium" ? R"({"team":"small"})" : R"({"team":"big"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    std::optional<Combat> c;
    explicit Fight(const ContentLibrary& lib, std::vector<Spawn> spawns, std::vector<Vec2i> walls = {}, uint64_t seed = 1) {
        chunk = arena(spawns, walls);
        c = Combat::create(reg, chunk, lib, seed);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {
        while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != who; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
            REQUIRE(settle(c->submit(e)).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    SubmitResult step(Vec2i to) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = to;
        return settle(c->submit(s));
    }
};
} // namespace

TEST_CASE("footprint: size category -> footprint side") {
    CHECK(footprint_of(CreatureSize::Tiny) == 1);
    CHECK(footprint_of(CreatureSize::Small) == 1);
    CHECK(footprint_of(CreatureSize::Medium) == 1);
    CHECK(footprint_of(CreatureSize::Large) == 2);
    CHECK(footprint_of(CreatureSize::Huge) == 3);
    CHECK(footprint_of(CreatureSize::Gargantuan) == 4);
}

TEST_CASE("footprint: rect_distance is nearest-tile distance (== chebyshev for 1x1)") {
    CHECK(rect_distance({0, 0}, 1, {3, -2}, 1) == chebyshev({0, 0}, {3, -2}));
    CHECK(rect_distance({4, 4}, 1, {4, 4}, 1) == 0);
    CHECK(rect_distance({0, 0}, 2, {2, 0}, 1) == 1);    // adjacent to the 2x2's right edge
    CHECK(rect_distance({0, 0}, 2, {2, 2}, 1) == 1);    // diagonal neighbour of its corner
    CHECK(rect_distance({0, 0}, 2, {1, 1}, 1) == 0);    // inside it
    CHECK(rect_distance({0, 0}, 2, {5, 0}, 1) == 4);
    CHECK(rect_distance({0, 0}, 2, {4, 0}, 2) == 3);    // two Larges, two tiles of gap
    CHECK(rect_distance({3, 3}, 3, {0, 0}, 2) == 2);    // symmetric
    CHECK(rect_distance({0, 0}, 2, {3, 3}, 3) == 2);
}

TEST_CASE("footprint content: monster@3 size gives the footprint, tiny to medium stay 1x1") {
    auto lib = make_lib({"tiny", "small", "medium", "large", "huge", "gargantuan"});
    const int want[] = {1, 1, 1, 2, 3, 4};
    const char* names[] = {"tiny", "small", "medium", "large", "huge", "gargantuan"};
    for (int i = 0; i < 6; ++i) {
        const Archetype* a = lib.find(std::string("test.monster.") + names[i]);
        REQUIRE(a);
        CHECK(int(a->size) == i);
        CHECK(int(a->footprint) == want[i]);
        CHECK(a->inert_mechanics.empty());
    }
    // character@3 too.
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["id"] = "test.character.big";
    c["size"] = "large";
    ContentLibrary lib2;
    REQUIRE_FALSE(lib2.add(c, "character"));
    CHECK(lib2.find("test.character.big")->footprint == 2);
}

TEST_CASE("footprint content: pre-@3 Large+ is inert (1x1), recorded in inert_mechanics") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    for (const char* size : {"large", "huge", "gargantuan"}) {
        auto r = reg.load(sized_doc(size, "monster@2"));
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(r.data["inert_mechanics"] == Json::array({"size"}));
        ContentLibrary lib;
        REQUIRE_FALSE(lib.add(r.data, "monster"));
        const Archetype* a = lib.find(std::string("test.monster.") + size);
        CHECK(a->footprint == 1);
        CHECK(a->size != CreatureSize::Medium);   // the category itself is still stored
    }
    // Small / medium pre-@3 files get no marker: byte-identical migration output.
    auto r = reg.load(sized_doc("small", "monster@2"));
    REQUIRE(r.ok);
    CHECK_FALSE(r.data.contains("inert_mechanics"));
    // A @3 file is never marked.
    auto r3 = reg.load(sized_doc("large"));
    REQUIRE(r3.ok);
    CHECK_FALSE(r3.data.contains("inert_mechanics"));
}

TEST_CASE("footprint spawn: anchor is the min-x/min-y tile; occupancy is every footprint tile") {
    auto lib = make_lib({"large", "huge", "gargantuan", "medium"});
    ecs::Registry reg;
    auto chunk = arena({{"test.monster.large", 4, 6}, {"test.monster.huge", 10, 10}, {"test.monster.gargantuan", 20, 2},
                        {"test.monster.medium", 1, 1}});
    auto c = Combat::create(reg, chunk, lib, 7);
    REQUIRE(c);
    const CombatantId L = chunk.spawns[0].stable_id, H = chunk.spawns[1].stable_id, G = chunk.spawns[2].stable_id,
                      M = chunk.spawns[3].stable_id;
    CHECK(c->pos(L) == Vec2i{4, 6});
    CHECK(c->footprint_size(L) == 2);
    CHECK(c->footprint_size(H) == 3);
    CHECK(c->footprint_size(G) == 4);
    CHECK(c->footprint_size(M) == 1);
    CHECK(c->footprint_tiles(L) == std::vector<Vec2i>{{4, 6}, {5, 6}, {4, 7}, {5, 7}});
    CHECK(c->footprint_tiles(M) == std::vector<Vec2i>{{1, 1}});
    CHECK(c->footprint_tiles_at(L, {9, 9}) == std::vector<Vec2i>{{9, 9}, {10, 9}, {9, 10}, {10, 10}});
    // ECS: Footprint only on multi-tile creatures; TilePos stays the anchor.
    CHECK(reg.get<Footprint>(c->entity(L))->size == 2);
    CHECK(reg.get<Footprint>(c->entity(G))->size == 4);
    CHECK(reg.get<Footprint>(c->entity(M)) == nullptr);
    CHECK(reg.get<TilePos>(c->entity(L))->x == 4);
    // occupied_except(M): 4 + 9 + 16 tiles of the others.
    const auto occ = c->occupied_except(M);
    CHECK(occ.size() == 4 + 9 + 16);
    for (Vec2i t : c->footprint_tiles(L)) CHECK(std::count(occ.begin(), occ.end(), t) == 1);
    CHECK(std::count(occ.begin(), occ.end(), Vec2i{12, 12}) == 1);   // Huge far corner
    CHECK(std::count(occ.begin(), occ.end(), Vec2i{13, 13}) == 0);
    CHECK(c->occupied_except(L).size() == 9 + 16 + 1);
}

TEST_CASE("footprint spawn: overlapping or unwalkable footprints are rejected; 1x1 stacking is unchanged") {
    auto lib = make_lib({"large", "medium"});
    ecs::Registry reg;
    std::string err;
    // Large vs Large overlap (diagonal overlap of one tile).
    CHECK_FALSE(Combat::create(reg, arena({{"test.monster.large", 4, 4}, {"test.monster.large", 5, 5}}), lib, 1, &err));
    CHECK(err == "spawn footprint overlaps another spawn");
    // A 1x1 spawned inside an earlier Large, and a Large over an earlier 1x1.
    ecs::Registry reg2;
    CHECK_FALSE(Combat::create(reg2, arena({{"test.monster.large", 4, 4}, {"test.monster.medium", 5, 5}}), lib, 1, &err));
    ecs::Registry reg3;
    CHECK_FALSE(Combat::create(reg3, arena({{"test.monster.medium", 5, 5}, {"test.monster.large", 4, 4}}), lib, 1, &err));
    // Adjacent is fine.
    ecs::Registry reg4;
    CHECK(Combat::create(reg4, arena({{"test.monster.large", 4, 4}, {"test.monster.large", 6, 4}, {"test.monster.medium", 4, 6}}), lib, 1));
    // A wall or a hole under any footprint tile.
    ecs::Registry reg5;
    CHECK_FALSE(Combat::create(reg5, arena({{"test.monster.large", 4, 4}}, {{5, 5}}), lib, 1, &err));
    CHECK(err == "spawn footprint is not on walkable ground");
    ecs::Registry reg6;
    CHECK_FALSE(Combat::create(reg6, arena({{"test.monster.large", 4, 4}}, {}, {{4, 5}}), lib, 1, &err));
    // 1x1 stacking is still accepted (v0 behaviour, no occupancy check).
    ecs::Registry reg7;
    CHECK(Combat::create(reg7, arena({{"test.monster.medium", 3, 3}, {"test.monster.medium", 3, 3}}), lib, 1));
}

TEST_CASE("footprint spawn: a pre-@3 Large is one tile, exactly as before") {
    assets::SchemaRegistry sreg;
    assets::register_content_schemas(sreg);
    auto r = sreg.load(sized_doc("large", "monster@2"));
    REQUIRE(r.ok);
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(r.data, "monster"));
    ecs::Registry reg;
    auto chunk = arena({{"test.monster.large", 4, 4}, {"test.monster.large", 5, 5}});   // would overlap if 2x2
    auto c = Combat::create(reg, chunk, lib, 1);
    REQUIRE(c);
    CHECK(c->footprint_size(chunk.spawns[0].stable_id) == 1);
    CHECK(c->occupied_except(chunk.spawns[0].stable_id).size() == 1);
    CHECK(reg.get<Footprint>(c->entity(chunk.spawns[0].stable_id)) == nullptr);
}

// ---- movement, pathing, line of sight (PR B) -----------------------------------------------------------

TEST_CASE("footprint grid: path is footprint-aware and has no squeezing") {
    auto lib = make_lib({"medium"});
    // A wall column at x = 10 with a one-tile gap at y = 8.
    std::vector<Vec2i> walls;
    for (int y = 0; y < 32; ++y) if (y != 8) walls.push_back({10, y});
    auto chunk = arena({{"test.monster.medium", 2, 8}}, walls);
    Grid g(chunk);
    const std::vector<Vec2i> none;
    auto p1 = g.path({2, 8}, {{20, 8}}, none, 1);
    REQUIRE_FALSE(p1.empty());                       // a 1x1 goes through the gap
    CHECK(p1.back() == Vec2i{20, 8});
    CHECK(g.path({2, 8}, {{20, 8}}, none).size() == p1.size());   // default footprint == 1
    CHECK(g.path({2, 8}, {{20, 8}}, none, 2).empty());            // a 2x2 does not fit the gap
    CHECK(g.path({2, 8}, {{20, 8}}, none, 4).empty());
    // Open a second tile of gap: the 2x2 passes with its anchor at y = 8 (tiles y 8..9).
    walls.erase(std::remove(walls.begin(), walls.end(), Vec2i{10, 9}), walls.end());
    Grid g2(arena({{"test.monster.medium", 2, 8}}, walls));
    auto p2 = g2.path({2, 8}, {{20, 8}}, none, 2);
    REQUIRE_FALSE(p2.empty());
    CHECK(p2.back() == Vec2i{20, 8});
    for (Vec2i q : p2) CHECK(g2.footprint_walkable(q, 2));
    // Occupied tiles are tested over the whole footprint.
    CHECK(g2.path({2, 8}, {{20, 8}}, {{10, 8}, {10, 9}}, 2).empty());
}

TEST_CASE("footprint grid: move cost is the dearest newly entered tile") {
    world::Chunk c = arena({});
    c.layers[0][std::size_t(5 * 32 + 6)].flags = world::tile_flags::kDifficult;   // (6,5)
    Grid g(c);
    CHECK(g.move_cost_ft({4, 4}, {5, 4}, 1) == 5);
    CHECK(g.move_cost_ft({4, 4}, {5, 4}, 2) == 10);   // a 2x2 at (4,4) stepping east enters (6,4) and (6,5)
    CHECK(g.move_cost_ft({4, 4}, {4, 3}, 2) == 5);    // north: (4,3),(5,3) are ordinary
    CHECK(g.move_cost_ft({4, 4}, {5, 5}, 2) == 10);   // diagonal enters (6,5),(6,6),(5,6),(4,6)? no: (6,5),(6,6),(5,6)
    CHECK(g.move_cost_ft({4, 4}, {3, 3}, 3) == 5);
    // Anything the footprint already covered is not "entered".
    CHECK(g.move_cost_ft({5, 4}, {4, 4}, 2) == 5);
}

TEST_CASE("footprint combat: a Large creature steps a whole footprint") {
    auto lib = make_lib({"large", "medium"});
    Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 20, 20}});
    const CombatantId L = f.id(0);
    f.advance_to(L);
    CHECK(f.step({5, 4}).ok);   // east: enters x = 6, y = 4..5
    CHECK(f.c->pos(L) == Vec2i{5, 4});
    CHECK(f.c->footprint_tiles(L) == std::vector<Vec2i>{{5, 4}, {6, 4}, {5, 5}, {6, 5}});
    CHECK(f.c->state().budget.movement_ft == 25);   // 30 - 5
    CHECK(f.reg.get<TilePos>(f.c->entity(L))->x == 5);
    CHECK(f.step({6, 5}).ok);   // diagonal
    CHECK(f.c->pos(L) == Vec2i{6, 5});
    CHECK_FALSE(f.step({9, 9}).ok);   // not adjacent
    // The last TileMoved event carries the anchor.
    const auto& ev = f.c->events();
    CHECK(ev.back().kind == SimEvent::Kind::TileMoved);
    CHECK(ev.back().tile == Vec2i{6, 5});
}

TEST_CASE("footprint combat: steps respect walls, edges and other creatures over the new tiles") {
    auto lib = make_lib({"large", "medium"});
    // A wall at (6,5) (the lower of the two tiles a 2x2 at (4,4) enters going east); a goblin at (4,7).
    Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 4, 7}}, {{6, 5}});
    const CombatantId L = f.id(0);
    f.advance_to(L);
    auto r = f.step({5, 4});
    CHECK_FALSE(r.ok);
    CHECK(r.error == "tile not walkable");
    CHECK(f.c->pos(L) == Vec2i{4, 4});
    CHECK(f.step({4, 5}).ok);   // south: enters (4,6),(5,6): free; now tiles y 5..6, goblin at y 7 adjacent
    r = f.step({4, 6});          // south again would enter (4,7),(5,7): goblin on (4,7)
    CHECK_FALSE(r.ok);
    CHECK(r.error == "tile occupied");
    CHECK(f.step({3, 5}).ok);   // west is free
    CHECK(f.c->state().budget.movement_ft == 20);   // two 5 ft steps; the rejected ones cost nothing
}

TEST_CASE("footprint combat: a Large's step over difficult ground costs 10 ft once") {
    auto lib = make_lib({"large", "medium"});
    ecs::Registry reg2;
    auto chunk = arena({{"test.monster.large", 4, 4}, {"test.monster.medium", 25, 25}});
    chunk.layers[0][std::size_t(5 * 32 + 6)].flags = world::tile_flags::kDifficult;
    chunk.content_hash = world::compute_content_hash(chunk);
    auto c = Combat::create(reg2, chunk, lib, 1);
    REQUIRE(c);
    Command s;
    s.kind = CommandKind::Step;
    s.to = {5, 4};
    CHECK(c->submit(s).ok);
    CHECK(c->state().budget.movement_ft == 20);
}

TEST_CASE("footprint: line of sight and reach use the nearest tile of a footprint") {
    auto lib = make_lib({"large", "medium"});
    // Large at (4,4)-(5,5); a sight-blocking pillar at (6,4) hides the anchor row from a goblin at (8,4)
    // but not the lower row (6,5 is open).
    ecs::Registry reg;
    auto chunk = arena({{"test.monster.large", 4, 4}, {"test.monster.medium", 8, 4}, {"test.monster.medium", 8, 20}});
    chunk.layers[2][std::size_t(4 * 32 + 6)].flags = world::tile_flags::kBlocksSight;
    chunk.layers[2][std::size_t(3 * 32 + 6)].flags = world::tile_flags::kBlocksSight;
    chunk.content_hash = world::compute_content_hash(chunk);
    auto c = Combat::create(reg, chunk, lib, 1);
    REQUIRE(c);
    const CombatantId L = chunk.spawns[0].stable_id, G = chunk.spawns[1].stable_id, F = chunk.spawns[2].stable_id;
    CHECK_FALSE(c->grid().line_of_sight(c->pos(L), c->pos(G)));   // anchor to anchor is blocked
    CHECK(c->line_of_sight(L, G));                                // but (5,5) sees (8,4)
    CHECK(c->line_of_sight(G, L));                                // symmetric
    CHECK(c->distance_tiles(L, G) == 3);                          // (5,*) to (8,4)
    CHECK(c->distance_tiles(L, F) == 15);                         // (5,5) to (8,20)
    CHECK(c->distance_tiles(G, L) == 3);
}

TEST_CASE("footprint combat: melee reach is measured from the nearest tile") {
    auto lib = make_lib({"large", "medium"});
    // Anchor distance 2 (L at (4,4), goblin at (6,4)) is rect distance 1: in reach of a 5 ft attack.
    Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 6, 4}, {"test.monster.medium", 9, 4}});
    const CombatantId L = f.id(0), near = f.id(1), far = f.id(2);
    f.advance_to(L);
    Command a;
    a.request = {L, rules5e::ActionKind::Attack, near};
    CHECK(f.settle(f.c->submit(a)).ok);
    Command b;
    b.request = {L, rules5e::ActionKind::Attack, far};   // rect distance 3
    auto r = f.c->submit(b);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "target out of reach");
}

TEST_CASE("footprint driver: a Large closes through a gap sized for it and attacks; replay is exact") {
    auto lib = make_lib({"large", "medium"});
    std::vector<Vec2i> walls;
    for (int y = 0; y < 32; ++y) if (y != 8 && y != 9) walls.push_back({10, y});   // two-tile gap
    const std::vector<Spawn> sp = {{"test.monster.large", 4, 8}, {"test.monster.medium", 16, 8}};
    ecs::Registry reg;
    auto chunk = arena(sp, walls);
    auto c = Combat::create(reg, chunk, lib, 3);
    REQUIRE(c);
    NearestEnemyPolicy pol;
    auto rr = run_to_end(*c, [&](CombatantId) { return &pol; }, 400);
    CHECK_MESSAGE(rr.error.empty(), rr.error);
    CHECK(c->ended());
    // The Large crossed the wall column only through the gap: whenever its footprint covered x = 10 its
    // anchor row was 8 (tiles y 8..9).
    bool crossed = false;
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::TileMoved && e.actor == chunk.spawns[0].stable_id && (e.tile.x == 9 || e.tile.x == 10)) {
            CHECK(e.tile.y == 8);
            crossed = true;
        }
    CHECK(crossed);
    ecs::Registry reg2;
    auto rep = replay(reg2, chunk, lib, 3, c->log());
    REQUIRE(rep);
    CHECK(rep->hash() == c->hash());
}

TEST_CASE("footprint driver: a Large cannot reach a target behind a one-tile gap") {
    auto lib = make_lib({"large", "medium"});
    std::vector<Vec2i> walls;
    for (int y = 0; y < 32; ++y) if (y != 8) walls.push_back({10, y});
    Fight f(lib, {{"test.monster.large", 4, 8}, {"test.monster.medium", 16, 8}}, walls);
    const CombatantId L = f.id(0);
    f.advance_to(L);
    const Command cmd = next_command(*f.c, f.id(1));
    CHECK(cmd.kind != CommandKind::Step);   // no path for a 2x2: it never steps toward the gap
}

// ---- opportunity attacks, reach triggers, events (PR C) ------------------------------------------------

namespace {
bool has_leaving_window(const SubmitResult& r) {
    return r.needs && r.needs->trigger == ReactionTrigger::LeavingReach;
}
int count_events(const Combat& c, SimEvent::Kind k) {
    int n = 0;
    for (const auto& e : c.events()) n += e.kind == k;
    return n;
}
} // namespace

TEST_CASE("footprint opportunity attacks: a Large leaves reach only when no footprint tile stays in reach") {
    auto lib = make_lib({"large", "medium"});
    // Large (4,4)-(5,5); goblin at (6,4) is adjacent to its east edge.
    {   // West: new footprint x 3..4, nearest tile 2 away from the goblin: leaves reach -> a window opens.
        Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 6, 4}});
        f.advance_to(f.id(0));
        CHECK(f.c->would_leave_reach(f.id(0), {4, 4}, {3, 4}));
        Command s;
        s.kind = CommandKind::Step;
        s.to = {3, 4};
        auto r = f.c->submit(s);
        CHECK(r.ok);
        CHECK(has_leaving_window(r));
        CHECK(r.needs->reactor == f.id(1));
    }
    {   // North / south: the footprint still touches the goblin's reach: no window.
        Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 6, 4}});
        f.advance_to(f.id(0));
        CHECK_FALSE(f.c->would_leave_reach(f.id(0), {4, 4}, {4, 3}));
        CHECK_FALSE(f.c->would_leave_reach(f.id(0), {4, 4}, {4, 5}));
        auto r = f.step({4, 3});
        CHECK(r.ok);
        CHECK_FALSE(has_leaving_window(r));
    }
    {   // East into the goblin's side is blocked; diagonal away NE keeps the goblin adjacent (rect dist 1).
        Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 6, 4}});
        f.advance_to(f.id(0));
        CHECK_FALSE(f.c->would_leave_reach(f.id(0), {4, 4}, {5, 3}));   // footprint x 5..6, y 3..4: overlaps? no: goblin tile is (6,4)
    }
}

TEST_CASE("footprint opportunity attacks: a goblin leaving a Large's reach, measured from its nearest tile") {
    auto lib = make_lib({"large", "medium"});
    // Goblin at (6,5), east of the Large's lower-right tile (5,5). Stepping to (7,5) puts it 2 tiles from
    // the footprint: out of reach (the anchor (4,4) is 3 away either way, so only the footprint rule is right).
    Fight f(lib, {{"test.monster.large", 4, 4}, {"test.monster.medium", 6, 5}});
    f.advance_to(f.id(1));
    CHECK(f.c->would_leave_reach(f.id(1), {6, 5}, {7, 5}));
    CHECK_FALSE(f.c->would_leave_reach(f.id(1), {6, 5}, {6, 6}));   // still diagonal-adjacent to (5,5)
    Command s;
    s.kind = CommandKind::Step;
    s.to = {7, 5};
    auto r = f.c->submit(s);   // (not f.step: that declines windows)
    CHECK(r.ok);
    REQUIRE(has_leaving_window(r));
    CHECK(r.needs->reactor == f.id(0));
}

TEST_CASE("footprint reach triggers: a readied attack fires when a Large's footprint comes within reach") {
    auto lib = make_lib({"large", "medium"});
    // Goblin at (10,10) readies an attack on the Large (TargetWithin 1). The Large at (7,10) covers x 7..8
    // (rect distance 2); stepping east to anchor (8,10) covers x 8..9, distance 1: the trigger fires.
    // The anchors are 2 apart both before and after, so only a nearest-tile test sees it.
    Fight f(lib, {{"test.monster.large", 7, 10}, {"test.monster.medium", 10, 10}});
    const CombatantId L = f.id(0), G = f.id(1);
    f.advance_to(G);
    Command rd;
    rd.request = {G, rules5e::ActionKind::Ready, L};
    rd.request.ready_kind = rules5e::ActionKind::Attack;
    rd.request.attack_index = 0;
    rd.request.trigger_id = ready_trigger_id(ReadyTriggerKind::TargetWithin, 1);
    auto r = f.settle(f.c->submit(rd));
    REQUIRE_MESSAGE(r.ok, r.error);
    f.advance_to(L);
    auto fired = [&] {
        int n = 0;
        for (const auto& e : f.c->events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::ReadyTriggered;
        return n;
    };
    Command far;
    far.kind = CommandKind::Step;
    far.to = {7, 11};   // anchor moves, footprint x 7..8 still 2 from the goblin: no trigger
    CHECK(f.c->submit(far).ok);
    CHECK(fired() == 0);
    Command near;
    near.kind = CommandKind::Step;
    near.to = {8, 11};   // footprint x 8..9, one tile from the goblin: the readier is asked
    auto sr = f.c->submit(near);
    REQUIRE(sr.status == StepStatus::NeedsDecision);
    CHECK(sr.needs->trigger == ReactionTrigger::ReadiedTrigger);
    sr = f.c->settle([](const Combat&, const NeedsDecision& nd) { return ReactionDecision{nd.window_seq, true, 0}; });
    REQUIRE_MESSAGE(sr.ok, sr.error);
    CHECK(fired() == 1);
}

TEST_CASE("footprint events: Footprint kinds appear only with multi-tile creatures; digest version is the lowest that fits") {
    auto lib = make_lib({"large", "huge", "gargantuan", "medium"});
    {   // No multi-tile creature: no new event, a v4 stream.
        Fight f(lib, {{"test.monster.medium", 4, 4}, {"test.monster.medium", 8, 8}});
        CHECK(count_events(*f.c, SimEvent::Kind::Footprint) == 0);
        CHECK(event_stream_version(f.c->events()) == 4);
    }
    Fight f(lib, {{"test.monster.medium", 2, 2}, {"test.monster.large", 4, 4}, {"test.monster.huge", 10, 10},
                  {"test.monster.gargantuan", 20, 20}});
    CHECK(count_events(*f.c, SimEvent::Kind::Footprint) == 3);
    CHECK(event_stream_version(f.c->events()) == 6);
    // Spawn order, right after the opening rules events; anchor in `tile`, side in rules.a.
    std::vector<SimEvent> fp;
    for (const auto& e : f.c->events()) if (e.kind == SimEvent::Kind::Footprint) fp.push_back(e);
    REQUIRE(fp.size() == 3);
    CHECK(fp[0].actor == f.id(1));
    CHECK(fp[0].tile == Vec2i{4, 4});
    CHECK(fp[0].rules.a == 2);
    CHECK(fp[1].actor == f.id(2));
    CHECK(fp[1].tile == Vec2i{10, 10});
    CHECK(fp[1].rules.a == 3);
    CHECK(fp[2].rules.a == 4);
    // A stream with a spell target but no footprint stays v5; the hash separates the versions.
    std::vector<SimEvent> st(1);
    st[0].kind = SimEvent::Kind::SpellTarget;
    CHECK(event_stream_version(st) == 5);
    st.push_back(fp[0]);
    CHECK(event_stream_version(st) == 6);
}

namespace {
struct Digest { uint64_t events, state, log; uint32_t version, count; };
Digest run_fight(const ContentLibrary& lib, const std::vector<Spawn>& sp, std::vector<Vec2i> walls, uint64_t seed) {
    ecs::Registry reg;
    auto chunk = arena(sp, walls);
    auto c = Combat::create(reg, chunk, lib, seed);
    REQUIRE(c);
    NearestEnemyPolicy pol;
    auto rr = run_to_end(*c, [&](CombatantId) { return &pol; }, 3000);
    REQUIRE_MESSAGE(rr.error.empty(), rr.error);
    REQUIRE(c->ended());
    ecs::Registry reg2;
    auto rep = replay(reg2, chunk, lib, seed, c->log());   // exact replay
    REQUIRE(rep);
    CHECK(rep->hash() == c->hash());
    CHECK(event_stream_hash(rep->events()) == event_stream_hash(c->events()));
    return {event_stream_hash(c->events()), c->hash(), c->log_hash(), event_stream_version(c->events()),
            uint32_t(c->events().size())};
}
} // namespace

// Behaviour goldens, pinned once on the engine that first ran multi-tile footprints. A change here
// means Large+ movement, reach, opportunity attacks or the v6 event stream changed.
TEST_CASE("footprint golden: Large vs goblins round a wall with a two-tile gap") {
    auto lib = make_lib({"large", "medium"});
    std::vector<Vec2i> walls;
    for (int y = 0; y < 32; ++y) if (y != 8 && y != 9) walls.push_back({12, y});
    const Digest d = run_fight(lib, {{"test.monster.large", 3, 8}, {"test.monster.medium", 20, 7}, {"test.monster.medium", 22, 11}},
                               walls, 41);
    MESSAGE("large golden: events=" << d.events << " state=" << d.state << " log=" << d.log << " n=" << d.count);
    CHECK(d.version == 6);
    CHECK(d.events == 17302741854514879656ull);
    CHECK(d.state == 1903385362550943425ull);
    CHECK(d.log == 14977014498367770028ull);
    CHECK(d.count == 160u);
}

TEST_CASE("footprint golden: Huge (3x3) and Gargantuan (4x4) vs a goblin pack") {
    auto lib = make_lib({"huge", "gargantuan", "medium"});
    const Digest d = run_fight(lib, {{"test.monster.huge", 3, 3}, {"test.monster.gargantuan", 3, 20}, {"test.monster.medium", 24, 10},
                                     {"test.monster.medium", 26, 14}, {"test.monster.medium", 27, 6}},
                               {}, 77);
    MESSAGE("huge golden: events=" << d.events << " state=" << d.state << " log=" << d.log << " n=" << d.count);
    CHECK(d.version == 6);
    CHECK(d.events == 7252396575971630989ull);
    CHECK(d.state == 4864805552138262344ull);
    CHECK(d.log == 5232406441536582572ull);
    CHECK(d.count == 359u);
}
