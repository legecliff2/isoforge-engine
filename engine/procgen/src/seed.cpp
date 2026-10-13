#include "procgen/seed.hpp"

#include <array>

#include "rng/rng.hpp"

namespace procgen {

namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr int kChars = 13;

int decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';   // case-insensitive parse; printing is uppercase
    if (c >= '2' && c <= '7') return 26 + (c - '2');
    return -1;
}
} // namespace

std::string format_seed(WorldSeed s) {
    std::string out = "g" + std::to_string(unsigned(s.gen_version)) + "-";
    for (int i = kChars - 1; i >= 0; --i) {
        const int shift = 5 * i;   // 65-bit big-endian; shift 60 yields the top 4 bits
        out.push_back(kAlphabet[(s.seed >> shift) & 31u]);
    }
    return out;
}

std::optional<WorldSeed> parse_seed(std::string_view s) {
    if (s.size() < 2 || (s[0] != 'g' && s[0] != 'G')) return std::nullopt;
    std::size_t i = 1;
    uint32_t gen = 0;
    const std::size_t digits_begin = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        gen = gen * 10 + uint32_t(s[i] - '0');
        if (gen > 65535) return std::nullopt;
        ++i;
    }
    const std::size_t ndigits = i - digits_begin;
    if (ndigits == 0 || gen == 0 || s[digits_begin] == '0') return std::nullopt;
    if (i >= s.size() || s[i] != '-') return std::nullopt;
    ++i;
    if (s.size() - i != std::size_t(kChars)) return std::nullopt;
    uint64_t v = 0;
    for (int k = 0; k < kChars; ++k) {
        const int d = decode_char(s[i + std::size_t(k)]);
        if (d < 0) return std::nullopt;
        if (k == 0 && d > 15) return std::nullopt;   // would overflow 64 bits
        v = (v << 5) | uint64_t(d);
    }
    return WorldSeed{uint16_t(gen), v};
}

uint64_t chunk_seed(WorldSeed w, int32_t x, int32_t y, int16_t level) noexcept {
    const std::array<int64_t, 5> keys{kTagChunk, int64_t(w.gen_version), int64_t(x), int64_t(y), int64_t(level)};
    return rng::derive_seed(w.seed, keys);
}

uint64_t noise_seed(WorldSeed w) noexcept {
    const std::array<int64_t, 2> keys{kTagNoise, int64_t(w.gen_version)};
    return rng::derive_seed(w.seed, keys);
}

} // namespace procgen
