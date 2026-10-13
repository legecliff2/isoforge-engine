#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ecs/registry.hpp"
#include "world/chunk_codec.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng::world;
using eng::assets::Json;

namespace {
const std::string kChunkFile = std::string(ENGINE_CORE_FIXTURES) + "/chunks/0_-1_0.json";
constexpr ChunkCoord kCoord{0, -1, 0};

Json fixture_json() {
    std::ifstream in(kChunkFile);
    std::stringstream ss; ss << in.rdbuf();
    return Json::parse(ss.str());
}
Json minimal() {
    return Json::parse(R"({"$schema":"chunk@1","coord":{"x":3,"y":4,"level":-1},"origin":"handcrafted",
        "palette":[""],"layers":[{},{},{},{}],"spawns":[],"triggers":[]})");
}
HandcraftedProvider fixture_provider() { return HandcraftedProvider({{kCoord, kChunkFile}}); }
} // namespace

TEST_CASE("HandcraftedProvider loads the fixture chunk") {
    auto p = fixture_provider();
    CHECK(p.name() == "handcrafted");
    CHECK(p.covers(kCoord));
    CHECK_FALSE(p.covers(ChunkCoord{0, 0, 0}));
    ChunkResult r = p.load(ChunkRequest{kCoord, 0, 1});
    REQUIRE_MESSAGE(r.status == ChunkStatus::Ready, r.error);
    const Chunk& c = *r.chunk;
    CHECK(c.format_version == kChunkFormatVersion);
    CHECK(c.coord == kCoord);
    CHECK(c.origin == Chunk::Origin::Handcrafted);
    REQUIRE(c.palette.size() == 5);
    CHECK(c.palette[0].empty());
    CHECK(c.palette[2] == "tiles.dungeon.wall_01");
    CHECK(c.layers[0][std::size_t(cell_index(0, 0))].tile == 0);
    CHECK(c.layers[0][std::size_t(cell_index(5, 5))].tile == 1);
    CHECK(c.layers[0][std::size_t(cell_index(17, 9))].elevation == 1);
    CHECK(c.layers[0][std::size_t(cell_index(7, 15))].flags == tile_flags::kDifficult);
    const TileCell wall = c.layers[2][std::size_t(cell_index(4, 10))];
    CHECK(wall.tile == 2);
    CHECK(wall.flags == (tile_flags::kBlocksMove | tile_flags::kBlocksSight));
    CHECK(c.layers[2][std::size_t(cell_index(12, 19))].tile == 4);   // door
    CHECK(c.layers[2][std::size_t(cell_index(12, 19))].flags == 0);
    CHECK(c.layers[1][std::size_t(cell_index(10, 10))].tile == 0);  // omitted layer = empty
    REQUIRE(c.spawns.size() == 2);
    CHECK(c.spawns[0].archetype == "srd51.monster.goblin");
    CHECK(c.spawns[0].tile == eng::Vec2i{10, 8});
    CHECK(c.spawns[0].overrides_json.empty());
    CHECK(c.spawns[1].overrides_json == R"({"hit_points":{"average":9},"name":"Goblin Boss-in-training"})");
    CHECK(c.spawns[0].stable_id == spawn_stable_id(kCoord, 0));
    CHECK(c.spawns[1].stable_id == spawn_stable_id(kCoord, 1));
    REQUIRE(c.triggers.size() == 1);
    CHECK(Json::parse(c.triggers[0])["id"] == "door_south");
    CHECK(c.content_hash == compute_content_hash(c));
}

TEST_CASE("golden: stable ids and content hash (XXH3-64 canonical bytes)") {
    CHECK(spawn_stable_id(ChunkCoord{0, 0, 0}, 0) == 0xE9CA1F53u);
    CHECK(spawn_stable_id(ChunkCoord{-1, 7, -2}, 3) == 0x3660E539u);
    auto r = fixture_provider().load(ChunkRequest{kCoord, 0, 1});
    REQUIRE(r.chunk);
    CHECK(hash_to_hex(r.chunk->content_hash) == "1e0ef519c7654a57");
}

TEST_CASE("stable_id depends on coord and index only, not on Chunk::Origin") {
    std::set<uint32_t> ids;
    for (int32_t x = -4; x <= 4; ++x)
        for (int32_t y = -4; y <= 4; ++y)
            for (int16_t lv = -1; lv <= 1; ++lv)
                for (uint32_t i = 0; i < 4; ++i) ids.insert(spawn_stable_id(ChunkCoord{x, y, lv}, i));
    CHECK(ids.size() == 9u * 9u * 3u * 4u);   // no collisions in a small neighbourhood
    Json a = minimal(), b = minimal();
    a["spawns"] = Json::parse(R"([{"archetype":"srd51.monster.goblin","tile":{"x":1,"y":1}}])");
    b = a; b["origin"] = "procedural";
    auto ca = chunk_from_json(a), cb = chunk_from_json(b);
    REQUIRE(ca.chunk); REQUIRE(cb.chunk);
    CHECK(ca.chunk->spawns[0].stable_id == cb.chunk->spawns[0].stable_id);
    CHECK(ca.chunk->content_hash == cb.chunk->content_hash);   // origin excluded from content hash
}

TEST_CASE("content hash is independent of JSON key order / formatting") {
    Json j = fixture_json();
    auto a = chunk_from_json(j);
    auto b = chunk_from_json(Json::parse(j.dump()));   // re-serialized, keys sorted
    REQUIRE(a.chunk); REQUIRE(b.chunk);
    CHECK(a.chunk->content_hash == b.chunk->content_hash);
    j["layers"][0]["tiles"][0] = 1;
    j.erase("content_hash");
    auto c = chunk_from_json(j);
    REQUIRE(c.chunk);
    CHECK(c.chunk->content_hash != a.chunk->content_hash);
}

TEST_CASE("decode errors") {
    auto err = [](Json j) { auto r = chunk_from_json(j); CHECK(r.status == ChunkStatus::Error); return r.error; };
    CHECK(chunk_from_json(minimal()).status == ChunkStatus::Ready);
    Json j;
    j = minimal(); j["palette"] = {"tiles.x"};                CHECK(err(j).find("palette[0]") != std::string::npos);
    j = minimal(); j["layers"] = Json::array({Json::object()}); err(j);
    j = minimal(); j["layers"][0]["tiles"] = Json::array({1}); err(j);       // wrong length
    j = minimal(); j["layers"][0]["tiles"] = std::vector<int>(1024, 1); CHECK(err(j).find("out of range") != std::string::npos);
    j = minimal(); j["layers"][0]["flags"] = std::vector<int>(1024, 0x10); err(j);   // reserved bit
    j = minimal(); j["layers"][0]["cells"] = Json::array(); err(j);           // unknown field
    j = minimal(); j["origin"] = "Handcrafted";                              err(j);
    j = minimal(); j["coord"]["level"] = 40000;                              err(j);
    j = minimal(); j["spawns"] = Json::parse(R"([{"archetype":"a.b","tile":{"x":32,"y":0}}])"); err(j);
    j = minimal(); j["spawns"] = Json::parse(R"([{"archetype":"a.b","tile":{"x":0,"y":0},"stable_id":5}])");
    CHECK(err(j).find("computed by the loader") != std::string::npos);
    j = minimal(); j["spawns"] = Json::parse(R"([{"archetype":"a.b","tile":{"x":0,"y":0},"overrides":"{}"}])"); err(j);
    j = minimal(); j["triggers"] = Json::parse(R"([{"id":"t"}])");          err(j);   // kind required
    j = minimal(); j["triggers"] = Json::parse(R"(["raw string"])");        err(j);
    j = minimal(); j["content_hash"] = "deadbeefdeadbeef";                   CHECK(err(j).find("mismatch") != std::string::npos);
    j = minimal(); j["format_version"] = 2;                                  err(j);
}

TEST_CASE("provider errors: missing file, coord mismatch, wrong origin, uncovered coord") {
    HandcraftedProvider p({{ChunkCoord{9, 9, 0}, kChunkFile},
                           {ChunkCoord{1, 1, 0}, std::string(ENGINE_CORE_FIXTURES) + "/chunks/nope.json"}});
    CHECK(p.load(ChunkRequest{ChunkCoord{0, 0, 0}, 0, 1}).status == ChunkStatus::NotFound);
    auto mism = p.load(ChunkRequest{ChunkCoord{9, 9, 0}, 0, 1});
    CHECK(mism.status == ChunkStatus::Error);
    CHECK(mism.error.find("coord does not match") != std::string::npos);
    CHECK(p.load(ChunkRequest{ChunkCoord{1, 1, 0}, 0, 1}).status == ChunkStatus::Error);
}

TEST_CASE("concurrent loads produce identical chunks") {
    auto p = fixture_provider();
    std::vector<uint64_t> hashes(8, 0);
    std::vector<std::thread> ts;
    for (std::size_t i = 0; i < hashes.size(); ++i)
        ts.emplace_back([&, i] { auto r = p.load(ChunkRequest{kCoord, i, 1}); if (r.chunk) hashes[i] = r.chunk->content_hash; });
    for (auto& t : ts) t.join();
    for (uint64_t h : hashes) CHECK(h == hashes[0]);
    CHECK(hashes[0] != 0);
}

TEST_CASE("spawning chunk entities into the ECS iterates in stable_id order") {
    auto r = fixture_provider().load(ChunkRequest{kCoord, 0, 1});
    REQUIRE(r.chunk);
    struct Archetype { std::string id; };
    eng::ecs::Registry reg;
    for (auto it = r.chunk->spawns.rbegin(); it != r.chunk->spawns.rend(); ++it)   // reverse on purpose
        reg.emplace(reg.create(it->stable_id), Archetype{it->archetype});
    std::vector<eng::ecs::StableId> seen;
    reg.each<Archetype>([&](eng::ecs::Entity e, Archetype&) { seen.push_back(*reg.stable_id(e)); });
    REQUIRE(seen.size() == 2);
    CHECK(seen[0] < seen[1]);
}

TEST_CASE("content_hash test vectors match tools/chunk_hash/chunk_hash.py") {
    eng::assets::SchemaRegistry reg;
    register_chunk_schema(reg);
    const std::pair<const char*, const char*> vectors[] = {
        {"v1_minimal.json", "01fa4a2fde49834b"},
        {"v2_small.json", "963a7d22563ff71a"},
    };
    for (auto [file, hex] : vectors) {
        auto lr = reg.load_file(std::string(CHUNK_HASH_VECTORS) + "/" + file);
        REQUIRE_MESSAGE(lr.ok, lr.error);
        auto r = chunk_from_json(lr.data);
        REQUIRE_MESSAGE(r.chunk, r.error);
        CHECK_MESSAGE(hash_to_hex(r.chunk->content_hash) == hex, file);
    }
}

TEST_CASE("sample.vertical_slice chunk 0_0_0 (pack and frozen fl1 copy) loads via HandcraftedProvider") {
    const std::string root = ENGINE_SOURCE_ROOT;
    for (const char* rel : {"/content/packs/sample.vertical_slice/chunks/0_0_0.json",
                            "/tests/compat/fl1/sample.vertical_slice/chunks/0_0_0.json"}) {
        CAPTURE(rel);
        HandcraftedProvider p({{ChunkCoord{0, 0, 0}, root + rel}});
        ChunkResult r = p.load(ChunkRequest{ChunkCoord{0, 0, 0}, 0, 1});
        REQUIRE_MESSAGE(r.status == ChunkStatus::Ready, r.error);
        CHECK(hash_to_hex(r.chunk->content_hash) == "f044f2c4660809ed");
        REQUIRE(r.chunk->spawns.size() == 2);
        CHECK(r.chunk->spawns[0].stable_id == 0xE9CA1F53u);
        CHECK(r.chunk->spawns[1].stable_id == 0x8654AAD2u);
    }
}
