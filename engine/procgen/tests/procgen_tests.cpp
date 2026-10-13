#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "procgen/dungeon.hpp"
#include "detmath/noise.hpp"
#include "procgen/seed.hpp"
#include "rng/rng.hpp"

using namespace procgen;
namespace noise = detmath::noise;   // noise lives in detmath (moved from procgen)

// ---------------------------------------------------------------- seed strings (§4.6)

TEST_CASE("seed string: doc example round-trips") {
    auto s = parse_seed("g1-K3QZ7M2XAB4PE");
    REQUIRE(s);
    CHECK(s->gen_version == 1);
    CHECK(format_seed(*s) == "g1-K3QZ7M2XAB4PE");
    CHECK(s->seed == 0xADC33F66AE00F1E4ull);   // golden: decoding is pinned
}

TEST_CASE("seed string: round-trip for edge and random values") {
    std::vector<WorldSeed> v{{1, 0}, {1, ~0ull}, {1, 1}, {65535, 0x8000000000000000ull}, {42, 0x0123456789ABCDEFull}};
    rng::Pcg32 r(7, 7);
    for (int i = 0; i < 1000; ++i)
        v.push_back({uint16_t(1 + r.below(65535)), (uint64_t(r.next_u32()) << 32) | r.next_u32()});
    for (const WorldSeed& w : v) {
        const std::string s = format_seed(w);
        CHECK(s.size() == 3 + std::to_string(w.gen_version).size() - 1 + 13);
        auto back = parse_seed(s);
        REQUIRE(back);
        CHECK(*back == w);
        CHECK(format_seed(*back) == s);
    }
    CHECK(format_seed({1, 0}) == "g1-AAAAAAAAAAAAA");
    CHECK(format_seed({1, ~0ull}) == "g1-P777777777777");
}

TEST_CASE("seed string: rejects non-canonical input") {
    for (const char* bad : {"", "g", "g1", "g1-", "G1-K3QZ7M2XAB4PE", "x1-K3QZ7M2XAB4PE", "1-K3QZ7M2XAB4PE",
                            "g-K3QZ7M2XAB4PE", "g0-K3QZ7M2XAB4PE", "g01-K3QZ7M2XAB4PE", "g65536-K3QZ7M2XAB4PE",
                            "g1K3QZ7M2XAB4PE", "g1_K3QZ7M2XAB4PE", "g1-K3QZ7M2XAB4P", "g1-K3QZ7M2XAB4PEA",
                            "g1-k3qz7m2xab4pe", "g1-K3QZ7M2XAB4P0", "g1-K3QZ7M2XAB4P1", "g1-K3QZ7M2XAB4P8",
                            "g1-K3QZ7M2XAB4P=", "g1-Q777777777777", "g1-Z777777777777", " g1-K3QZ7M2XAB4PE",
                            "g1-K3QZ7M2XAB4PE ", "g+1-K3QZ7M2XAB4PE"})
        CHECK_MESSAGE(!parse_seed(bad), bad);
}

// ---------------------------------------------------------------- chunk seeds (§4.2)

// Goldens cross-checked by an independent Python port of rng::derive_seed.
TEST_CASE("chunk_seed: golden values and sensitivity to every input") {
    const WorldSeed w{1, 0xADC33F66AE00F1E4ull};
    std::set<uint64_t> seen;
    for (int32_t x = -3; x <= 3; ++x)
        for (int32_t y = -3; y <= 3; ++y)
            for (int16_t l = -1; l <= 1; ++l) seen.insert(chunk_seed(w, x, y, l));
    CHECK(seen.size() == 7u * 7u * 3u);
    CHECK(chunk_seed(w, 0, 0, 0) != chunk_seed(WorldSeed{2, w.seed}, 0, 0, 0));
    CHECK(chunk_seed(w, 1, 2, 0) != chunk_seed(w, 2, 1, 0));
    CHECK(chunk_seed(w, 0, 0, 0) == 0x7050A177546B4DA7ull);
    CHECK(chunk_seed(w, -1, -1, -1) == 0xB4A609419C9CFE74ull);
}

// ---------------------------------------------------------------- noise (§4.4)

TEST_CASE("noise: range, lattice exactness, continuity, golden") {
    const uint64_t s = 0x1234;
    int64_t sum = 0;
    for (int64_t y = -64; y < 64; ++y)
        for (int64_t x = -64; x < 64; ++x) {
            const int32_t v = noise::value2(s, x, y, 3).raw;
            CHECK((v >= 0 && v < detmath::Fx::kOne));
            const int32_t f = noise::fbm2(s, x, y, 3, 3).raw;
            CHECK((f >= 0 && f < detmath::Fx::kOne));
            sum += f;
            if ((x & 7) == 0 && (y & 7) == 0) CHECK(v == noise::lattice(s, x >> 3, y >> 3, 0).raw);
            // Value noise is continuous: neighbours differ by at most a cell's worth of fade.
            CHECK(std::abs(noise::value2(s, x + 1, y, 3).raw - v) <= 65536 * 3 / 8 / 2 + 1);
        }
    CHECK(sum / (128 * 128) > 20000);   // roughly centred
    CHECK(sum / (128 * 128) < 45000);
    CHECK(noise::fbm2(s, 0, 0, 3, 3).raw == 49016);
    CHECK(noise::fbm2(s, -1000000007, 99999999999, 3, 3).raw == 34028);
}

// ---------------------------------------------------------------- generator

namespace {
const WorldSeed kSeed = *parse_seed("g1-K3QZ7M2XAB4PE");
using Coord = std::tuple<int32_t, int32_t, int16_t>;
std::vector<Coord> coords() {
    std::vector<Coord> v;
    for (int32_t x = -3; x <= 3; ++x)
        for (int32_t y = -3; y <= 3; ++y) v.emplace_back(x, y, int16_t((x + y) % 3));
    v.emplace_back(-2147483647 - 1, 2147483647, int16_t(-32768));
    v.emplace_back(1000000, -1000000, int16_t(32767));
    return v;
}
uint64_t hash_at(const Coord& c) {
    auto g = generate(kSeed, std::get<0>(c), std::get<1>(c), std::get<2>(c));
    REQUIRE(g);
    return content_hash(*g);
}
} // namespace

TEST_CASE("generator: format invariants and connectivity over many chunks") {
    for (uint64_t sd : {0ull, 1ull, 0xADC33F66AE00F1E4ull, ~0ull})
        for (const Coord& co : coords()) {
            auto g = generate(WorldSeed{1, sd}, std::get<0>(co), std::get<1>(co), std::get<2>(co));
            REQUIRE(g);
            CHECK(g->palette.at(0).empty());
            for (std::size_t i = 1; i < g->palette.size(); ++i) CHECK(!g->palette[i].empty());
            int floors = 0;
            for (const auto& L : g->layers)
                for (const GenCell& c : L) {
                    CHECK(c.tile < g->palette.size());
                    CHECK((c.flags & 0xF0) == 0);
                    if (c.tile == 0) CHECK((c.flags == 0 && c.elevation == 0));
                }
            for (int i = 0; i < kCells; ++i) floors += g->layers[kGround][std::size_t(i)].tile == tiles::kFloor;
            CHECK(floors >= 16);
            // Nothing on the outer ring: chunks never depend on neighbours.
            for (int k = 0; k < kChunkSize; ++k)
                for (const auto& L : g->layers) {
                    CHECK(L[std::size_t(k)].tile == 0);
                    CHECK(L[std::size_t((kChunkSize - 1) * kChunkSize + k)].tile == 0);
                    CHECK(L[std::size_t(k * kChunkSize)].tile == 0);
                    CHECK(L[std::size_t(k * kChunkSize + kChunkSize - 1)].tile == 0);
                }
            CHECK(walkable_connected(*g));
            REQUIRE(g->spawns.size() == 1);
            const std::size_t si = std::size_t(g->spawns[0].y * kChunkSize + g->spawns[0].x);
            CHECK(g->layers[kGround][si].tile == tiles::kFloor);
        }
    CHECK_FALSE(generate(WorldSeed{2, 0}, 0, 0, 0));
    CHECK_FALSE(generate(WorldSeed{0, 0}, 0, 0, 0));
}

TEST_CASE("generator: connectivity check detects a split") {
    auto g = generate(kSeed, 0, 0, 0);
    REQUIRE(g);
    GenChunk c = *g;
    c.layers[kGround][std::size_t(1 * kChunkSize + 1)].tile = tiles::kFloor;   // isolated cell in the wall ring area
    for (auto& L : c.layers) L[std::size_t(1 * kChunkSize + 1)].flags = 0;
    c.layers[kWalls][std::size_t(1 * kChunkSize + 1)] = GenCell{};
    // (1,1) touches only ring/wall cells; make sure its neighbours are not walkable floor.
    bool neighbour_floor = c.layers[kGround][std::size_t(1 * kChunkSize + 2)].tile == tiles::kFloor ||
                           c.layers[kGround][std::size_t(2 * kChunkSize + 1)].tile == tiles::kFloor;
    if (!neighbour_floor) CHECK_FALSE(walkable_connected(c));
}

// Golden hashes: changing any of these requires a gen_version bump (§4.6, §4.7).
TEST_CASE("golden: content_hash per (seed, coord)") {
    struct G { int32_t x, y; int16_t l; const char* hex; };
    const G goldens[] = {
        {0, 0, 0, "633e9eb6f4f4b0b1"},
        {-1, -1, 0, "a693a24bb22871b1"},
        {5, -3, -2, "ca8f557f3a1d00a9"},
        {-1000000, 123456, 7, "463c3724f76b4bb2"},
        {-2147483647 - 1, 2147483647, int16_t(-32768), "eeee3b79d9916ce9"},
        {3, -7, 1, "26aa2d469fe2a0b8"},
    };
    for (const G& g : goldens) {
        auto c = generate(kSeed, g.x, g.y, g.l);
        REQUIRE(c);
        CHECK_MESSAGE(hash_hex(content_hash(*c)) == g.hex, g.x, ",", g.y, ",", g.l);
    }
}

TEST_CASE("order independence: shuffled generation orders give identical hashes") {
    const auto cs = coords();
    std::map<Coord, uint64_t> ref;
    for (const Coord& c : cs) ref[c] = hash_at(c);
    for (uint64_t trial = 0; trial < 4; ++trial) {
        std::vector<Coord> order = cs;
        rng::Pcg32 r(trial, 99);
        r.shuffle(std::span<Coord>(order));
        for (const Coord& c : order) CHECK(hash_at(c) == ref[c]);
    }
}

TEST_CASE("thread independence: parallel generation matches serial") {
    const auto cs = coords();
    std::vector<uint64_t> serial;
    for (const Coord& c : cs) serial.push_back(hash_at(c));
    for (unsigned nthreads : {2u, 3u, 8u}) {
        std::vector<uint64_t> par(cs.size(), 0);
        std::vector<std::thread> ts;
        for (unsigned t = 0; t < nthreads; ++t)
            ts.emplace_back([&, t] {
                // Each thread walks its stripe in reverse to vary interleaving.
                for (std::size_t i = cs.size(); i-- > 0;)
                    if (i % nthreads == t) {
                        auto g = generate(kSeed, std::get<0>(cs[i]), std::get<1>(cs[i]), std::get<2>(cs[i]));
                        par[i] = g ? content_hash(*g) : 0;
                    }
            });
        for (auto& th : ts) th.join();
        CHECK(par == serial);
    }
}

TEST_CASE("to_json: stable bytes") {
    auto g = generate(kSeed, -1, -1, 0);
    REQUIRE(g);
    const std::string a = to_json(*g), b = to_json(*generate(kSeed, -1, -1, 0));
    CHECK(a == b);
    CHECK(a.find("\"origin\": \"procedural\"") != std::string::npos);
    CHECK(a.find("stable_id") == std::string::npos);
    CHECK(a.find("\"content_hash\": \"" + hash_hex(content_hash(*g)) + "\"") != std::string::npos);
}
