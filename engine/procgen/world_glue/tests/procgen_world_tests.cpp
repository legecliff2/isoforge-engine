#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "procgen/dungeon.hpp"
#include "procgen_world/procedural_provider.hpp"
#include "world/chunk_codec.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng::world;

namespace {
const procgen::WorldSeed kSeed = *procgen::parse_seed("g1-K3QZ7M2XAB4PE");
}

TEST_CASE("procgen spawn_stable_id == world spawn_stable_id") {
    for (int32_t x : {0, -1, 7, -2147483647 - 1})
        for (int16_t l : {int16_t(0), int16_t(-2), int16_t(32767)})
            for (uint32_t i : {0u, 3u, 1000u})
                CHECK(procgen::spawn_stable_id(x, -x, l, i) == spawn_stable_id(ChunkCoord{x, -x, l}, i));
}

TEST_CASE("procgen JSON decodes through world::chunk_from_json with the same hash") {
    for (int32_t x : {0, -1, 5, -1000000})
        for (int16_t l : {int16_t(0), int16_t(-2)}) {
            auto g = procgen::generate(kSeed, x, -x, l);
            REQUIRE(g);
            auto r = chunk_from_json(eng::assets::Json::parse(procgen::to_json(*g)));   // checks content_hash
            REQUIRE_MESSAGE(r.status == ChunkStatus::Ready, r.error);
            CHECK(r.chunk->origin == Chunk::Origin::Procedural);
            CHECK(r.chunk->content_hash == procgen::content_hash(*g));
            auto direct = to_chunk(*g);
            REQUIRE(direct.chunk);
            CHECK(direct.chunk->content_hash == r.chunk->content_hash);
            CHECK(direct.chunk->spawns[0].stable_id == r.chunk->spawns[0].stable_id);
        }
}

TEST_CASE("ProceduralProvider: loads, deterministic across threads, rejects unknown gen_version") {
    ProceduralProvider p(-4, 4);
    CHECK(p.name() == "procedural");
    CHECK(p.covers(ChunkCoord{123, -9, 4}));
    CHECK_FALSE(p.covers(ChunkCoord{0, 0, 5}));
    CHECK(p.load(ChunkRequest{ChunkCoord{0, 0, 9}, kSeed.seed, 1}).status == ChunkStatus::NotFound);
    CHECK(p.load(ChunkRequest{ChunkCoord{0, 0, 0}, kSeed.seed, 2}).status == ChunkStatus::Error);
    auto r = p.load(ChunkRequest{ChunkCoord{-1, -1, 0}, kSeed.seed, 1});
    REQUIRE_MESSAGE(r.status == ChunkStatus::Ready, r.error);
    CHECK(r.chunk->origin == Chunk::Origin::Procedural);
    std::vector<uint64_t> h(8, 0);
    std::vector<std::thread> ts;
    for (std::size_t i = 0; i < h.size(); ++i)
        ts.emplace_back([&, i] { auto q = p.load(ChunkRequest{ChunkCoord{-1, -1, 0}, kSeed.seed, 1}); if (q.chunk) h[i] = q.chunk->content_hash; });
    for (auto& t : ts) t.join();
    for (uint64_t v : h) CHECK(v == r.chunk->content_hash);
}

TEST_CASE("HandcraftedProvider currently rejects origin=procedural files (documented for Engine Core)") {
    auto g = procgen::generate(kSeed, 2, 3, 0);
    REQUIRE(g);
    const auto path = std::filesystem::temp_directory_path() / "procgen_world_tests_2_3_0.json";
    std::ofstream(path, std::ios::binary) << procgen::to_json(*g);
    HandcraftedProvider hp({{ChunkCoord{2, 3, 0}, path}});
    auto r = hp.load(ChunkRequest{ChunkCoord{2, 3, 0}, 0, 1});
    CHECK(r.status == ChunkStatus::Error);
    CHECK(r.error.find("origin must be \"handcrafted\"") != std::string::npos);
    std::filesystem::remove(path);
}
