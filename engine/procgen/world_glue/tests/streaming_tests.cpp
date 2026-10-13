// Milestone 2 streaming over real providers: a handcrafted chunk (FileChunkProvider, sample pack
// 0_0_0) layered over procedural wilds (ProceduralProvider, gen_version 1). The settled resident
// set must not depend on worker threads, request order, or the walk taken (ARCHITECTURE.md §4).
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "procgen/dungeon.hpp"
#include "world/chunk_streamer.hpp"
#include "world/file_chunk_provider.hpp"
#include "world/layered_provider.hpp"
#include "procgen_world/procedural_provider.hpp"

using namespace eng;
using namespace eng::world;

namespace {
const std::string kChunk = std::string(PROCGEN_SOURCE_ROOT) + "/content/packs/sample.vertical_slice/chunks/0_0_0.json";

struct World {
    FileChunkProvider file{{{ChunkCoord{0, 0, 0}, kChunk}}};
    ProceduralProvider proc{};
    LayeredProvider layered{{&file, &proc}};
};

StreamerConfig cfg(uint64_t shuffle) {
    StreamerConfig c;
    c.world_seed = 0xC0FFEEull;
    c.gen_version = 1;
    c.request_shuffle_seed = shuffle;
    return c;
}

std::vector<uint64_t> walk_digests(const std::vector<Vec2i>& walk, uint32_t threads, uint64_t shuffle) {
    World w;
    ChunkStreamer s(w.layered, threads, cfg(shuffle));
    std::vector<uint64_t> out;
    for (Vec2i f : walk) {
        s.set_focus(f, 0, 2);
        s.wait_idle();
        REQUIRE(s.errors().empty());
        out.push_back(s.digest());
    }
    return out;
}
} // namespace

TEST_CASE("streaming: handcrafted chunk inside procedural wilds; 1 vs N threads, shuffled, walk order") {
    std::vector<Vec2i> walk = {{5, 5}, {40, 5}, {80, -20}, {-50, -60}, {0, 0}, {-130, 70}};
    const auto ref = walk_digests(walk, 0, 0);
    for (uint32_t threads : {1u, 4u, 8u})
        for (uint64_t shuffle : {0ull, 17ull}) {
            CAPTURE(threads);
            CAPTURE(shuffle);
            CHECK(walk_digests(walk, threads, shuffle) == ref);
        }
    auto rev = walk;
    std::reverse(rev.begin(), rev.end());
    auto rd = walk_digests(rev, 6, 3);
    std::reverse(rd.begin(), rd.end());
    CHECK(rd == ref);

    World w;
    ChunkStreamer s(w.layered, 4, cfg(0));
    s.set_focus({5, 5}, 0, 2);
    s.wait_idle();
    REQUIRE(s.get({0, 0, 0}));
    CHECK(s.get({0, 0, 0})->origin == Chunk::Origin::Handcrafted);
    REQUIRE(s.get({1, 0, 0}));
    CHECK(s.get({1, 0, 0})->origin == Chunk::Origin::Procedural);
    CHECK(s.resident(0).size() == 25);

    // Golden (stream): settled digest at each walk focus. Changes only with the chunk content,
    // gen_version 1 output, or the digest definition (ChunkStreamer::digest).
    MESSAGE("stream golden: " << ref[0] << " " << ref[3] << " " << ref[5]);
    CHECK(ref[0] == 6382347445354989004ull);
    CHECK(ref[3] == 16267292006870167738ull);
    CHECK(ref[5] == 16911436643138033795ull);
    CHECK(ref[4] == ref[0]);   // same focus chunk (0,0) => same resident set
}
