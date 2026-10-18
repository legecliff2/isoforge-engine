// Multi-tile footprints for Large+ creatures (docs/proposals/LARGE_CREATURE_FOOTPRINT_AUDIT.md):
// size -> footprint loading (monster@3 / character@3 only), anchor convention, occupancy and spawns.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/combat.hpp"
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
// 32x32 floor arena; `walls` block movement (layer 2), `holes` have no ground.
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> walls = {}, std::vector<Vec2i> holes = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksMove;
    for (Vec2i h : holes) c.layers[0][std::size_t(h.y * 32 + h.x)].tile = 0;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
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
