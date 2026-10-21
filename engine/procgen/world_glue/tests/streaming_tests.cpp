// Milestone 2 streaming over real providers: a handcrafted chunk (FileChunkProvider, sample pack
// 0_0_0) layered over procedural wilds (ProceduralProvider, gen_version 1). The settled resident
// set must not depend on worker threads, request order, or the walk taken (ARCHITECTURE.md §4).
#include <doctest/doctest.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
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

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) { out.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    out.push_back(cur);
    return out;
}
bool parse_i64(const std::string& s, long long& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    v = std::strtoll(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}
bool parse_i32(const std::string& s, int32_t& v) {
    long long t = 0;
    if (!parse_i64(s, t) || t < INT32_MIN || t > INT32_MAX) return false;
    v = int32_t(t);
    return true;
}
bool parse_int(const std::string& s, int& v) {
    int32_t t = 0;
    if (!parse_i32(s, t)) return false;
    v = t;
    return true;
}
bool parse_u64(const std::string& s, uint64_t& v) {
    if (s.empty() || s[0] == '-') return false;
    char* end = nullptr;
    errno = 0;
    v = std::strtoull(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
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
// "<world_seed_hex> <focus_x> <focus_y> <radius> <digest_u64>" (or the multi-focus form
// "<seed> <x>,<y>,<r>[;...] [pins=<cx>,<cy>;...] <digest>") must reproduce, with 0 and 4
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
            std::vector<std::string> tok;
            for (std::string t; ls >> t;) tok.push_back(t);
            REQUIRE_MESSAGE(tok.size() >= 3, "bad line: " << line);
            REQUIRE_MESSAGE(!world_kind.empty(), "missing @world directive before data lines");
            const std::string& seed_hex = tok.front();
            uint64_t digest = 0;
            REQUIRE_MESSAGE(parse_u64(tok.back(), digest), "bad digest: " << line);
            const std::vector<std::string> mid(tok.begin() + 1, tok.end() - 1);
            std::vector<Focus> foci;
            std::vector<ChunkCoord> pins;
            if (mid.size() == 3 && mid[0].find(',') == std::string::npos) {
                // Legacy single-focus form: <x> <y> <radius>.
                Focus fc;
                fc.level = level;
                REQUIRE_MESSAGE((parse_i32(mid[0], fc.world_tile.x) && parse_i32(mid[1], fc.world_tile.y) &&
                                     parse_int(mid[2], fc.radius_chunks)),
                                "bad line: " << line);
                foci.push_back(fc);
            } else {
                // Multi-focus form: <x>,<y>,<r>[;<x>,<y>,<r>...] [pins=<cx>,<cy>[;<cx>,<cy>...]]
                REQUIRE_MESSAGE((mid.size() == 1 || mid.size() == 2), "bad line: " << line);
                for (const auto& part : split(mid[0], ';')) {
                    const auto f3 = split(part, ',');
                    Focus fc;
                    fc.level = level;
                    REQUIRE_MESSAGE((f3.size() == 3 && parse_i32(f3[0], fc.world_tile.x) &&
                                     parse_i32(f3[1], fc.world_tile.y) && parse_int(f3[2], fc.radius_chunks)),
                                    "bad focus '" << part << "': " << line);
                    foci.push_back(fc);
                }
                if (mid.size() == 2) {
                    REQUIRE_MESSAGE(mid[1].rfind("pins=", 0) == 0, "expected pins=...: " << line);
                    for (const auto& part : split(mid[1].substr(5), ';')) {
                        const auto c2 = split(part, ',');
                        int32_t cx = 0, cy = 0;
                        REQUIRE_MESSAGE((c2.size() == 2 && parse_i32(c2[0], cx) && parse_i32(c2[1], cy)),
                                        "bad pin '" << part << "': " << line);
                        pins.push_back(ChunkCoord{cx, cy, level});
                    }
                    REQUIRE_MESSAGE(!pins.empty(), "empty pins=: " << line);
                }
            }
            const uint64_t seed = std::strtoull(seed_hex.c_str(), nullptr, 16);
            for (uint32_t threads : {0u, 4u}) {
                World w;
                StreamerConfig c;
                c.world_seed = seed;
                c.gen_version = gen_version;
                IChunkProvider& prov = world_kind == "procgen" ? static_cast<IChunkProvider&>(w.proc) : w.layered;
                ChunkStreamer s(prov, threads, c);
                if (!pins.empty()) s.pin(1, pins);
                s.set_foci(foci);
                s.wait_idle();
                REQUIRE(s.errors().empty());
                for (const auto& pc : pins) CHECK(s.halo_state(pc) == HaloState::Complete);
                std::ostringstream echo;
                for (std::size_t i = 1; i + 1 < tok.size(); ++i) echo << tok[i] << ' ';
                MESSAGE(f.filename().string() << ": " << seed_hex << " " << echo.str() << s.digest());
                CHECK(s.digest() == digest);
            }
            ++checked;
        }
    }
    CHECK(checked >= 3);
}
