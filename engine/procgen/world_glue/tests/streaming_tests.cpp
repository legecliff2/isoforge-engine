// Milestone 2 streaming over real providers: a handcrafted chunk (FileChunkProvider, sample pack
// 0_0_0) layered over procedural wilds (ProceduralProvider, gen_version 1). The settled resident
// set must not depend on worker threads, request order, or the walk taken (ARCHITECTURE.md §4).
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    // Goldens live in tests/goldens/stream/*.txt (see the golden-file test below).
    CHECK(ref[4] == ref[0]);   // same focus chunk (0,0) => same resident set
}

// Stream golden files (docs/STREAMING.md): every tests/goldens/stream/*.txt line
// "<world_seed_hex> <focus_x> <focus_y> <radius> <digest_u64>" must reproduce, with 0 and 4
// worker threads. "@world sample_layered|procgen" (required), "@gen_version N" / "@level N" directives apply to following lines; '#' comments.
TEST_CASE("stream golden files reproduce") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(PROCGEN_SOURCE_ROOT) / "tests/goldens/stream";
    REQUIRE(fs::is_directory(dir));
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".txt") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    REQUIRE_FALSE(files.empty());
    std::size_t checked = 0;
    for (const auto& f : files) {
        std::ifstream in(f);
        uint16_t gen_version = 1;
        int16_t level = 0;
        std::string world_kind;   // "@world": sample_layered | procgen (required before data lines)
        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            CAPTURE(f.filename().string());
            CAPTURE(lineno);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            if (line[0] == '@') {
                std::string key;
                ls >> key;
                if (key == "@world") {
                    REQUIRE((ls >> world_kind));
                    REQUIRE_MESSAGE((world_kind == "sample_layered" || world_kind == "procgen"), "unknown @world " << world_kind);
                    continue;
                }
                long v = 0;
                REQUIRE((ls >> v));
                if (key == "@gen_version") gen_version = uint16_t(v);
                else if (key == "@level") level = int16_t(v);
                else FAIL("unknown directive " << key);
                continue;
            }
            std::string seed_hex;
            int32_t fx = 0, fy = 0;
            int radius = 0;
            uint64_t digest = 0;
            REQUIRE_MESSAGE((ls >> seed_hex >> fx >> fy >> radius >> digest), "bad line: " << line);
            std::string rest;
            REQUIRE_FALSE((ls >> rest));
            REQUIRE_MESSAGE(!world_kind.empty(), "missing @world directive before data lines");
            const uint64_t seed = std::strtoull(seed_hex.c_str(), nullptr, 16);
            for (uint32_t threads : {0u, 4u}) {
                World w;
                StreamerConfig c;
                c.world_seed = seed;
                c.gen_version = gen_version;
                IChunkProvider& prov = world_kind == "procgen" ? static_cast<IChunkProvider&>(w.proc) : w.layered;
                ChunkStreamer s(prov, threads, c);
                s.set_focus({fx, fy}, level, radius);
                s.wait_idle();
                REQUIRE(s.errors().empty());
                MESSAGE(f.filename().string() << ": " << seed_hex << " " << fx << " " << fy << " " << radius << " " << s.digest());
                CHECK(s.digest() == digest);
            }
            ++checked;
        }
    }
    CHECK(checked >= 3);
}
