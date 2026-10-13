#pragma once
// Shareable world seeds (ARCHITECTURE.md §4.6) and chunk seed derivation (§4.2).
//
// Seed string:  "g<gen_version>-<base32(seed64)>", e.g. "g1-K3QZ7M2XAB4PE".
//   gen_version  decimal, 1..65535, no leading zeros.
//   base32       RFC 4648 alphabet "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", uppercase, no padding,
//                exactly 13 characters = 65 bits, big-endian (most significant group first).
//                The top bit of the first group is always 0, so the first char is A..P.
//   No checksum (the doc defines none). Parsing is case-insensitive ("G1-k3qz..." is accepted);
//   printing is always the canonical uppercase-base32 form with a lowercase 'g', so
//   format(parse(s)) == s exactly when s is canonical, and parse(format(w)) == w always.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace procgen {

struct WorldSeed {
    uint16_t gen_version = 1;
    uint64_t seed = 0;
    auto operator<=>(const WorldSeed&) const = default;
};

std::string format_seed(WorldSeed s);                       // canonical string
std::optional<WorldSeed> parse_seed(std::string_view s);   // nullopt if not canonical

// Domain tags for rng::derive_seed keys (ASCII, so they are readable in a debugger).
inline constexpr int64_t kTagChunk = 0x43484E4B;   // "CHNK": per-chunk layout stream
inline constexpr int64_t kTagNoise = 0x4E4F4953;   // "NOIS": world-continuous noise field

// Per-chunk seed (§4.2: "derive_seed(world_seed, tag, x, y, level)"):
//   rng::derive_seed(seed, {kTagChunk, gen_version, x, y, level})
// Pure in (seed, gen_version, coord): independent of load order and thread count.
uint64_t chunk_seed(WorldSeed w, int32_t x, int32_t y, int16_t level) noexcept;
// World-level seed for continuous noise: rng::derive_seed(seed, {kTagNoise, gen_version}).
uint64_t noise_seed(WorldSeed w) noexcept;

} // namespace procgen
