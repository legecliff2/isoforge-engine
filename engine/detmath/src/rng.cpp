#include "rng/rng.hpp"

namespace rng {

uint64_t derive_seed(uint64_t parent, std::span<const int64_t> keys) noexcept {
    uint64_t x = parent;
    uint64_t h = splitmix64(x);
    for (int64_t k : keys) {
        x = h ^ uint64_t(k);
        h = splitmix64(x);
    }
    return h;
}

// Reference seeding from pcg32_srandom_r.
Pcg32::Pcg32(uint64_t seed, uint64_t stream) noexcept : state_(0), inc_((stream << 1u) | 1u) {
    next_u32();
    state_ += seed;
    next_u32();
}

uint32_t Pcg32::next_u32() noexcept {
    uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = uint32_t(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

// Lemire, "Fast Random Integer Generation in an Interval" (2019).
uint32_t Pcg32::below(uint32_t bound) noexcept {
    if (bound == 0) return 0;
    uint64_t m = uint64_t(next_u32()) * bound;
    uint32_t l = uint32_t(m);
    if (l < bound) {
        uint32_t t = (0u - bound) % bound;
        while (l < t) {
            m = uint64_t(next_u32()) * bound;
            l = uint32_t(m);
        }
    }
    return uint32_t(m >> 32);
}

int32_t Pcg32::range(int32_t lo, int32_t hi) noexcept {
    uint32_t span = uint32_t(int64_t(hi) - int64_t(lo) + 1);  // 0 means full 2^32 range
    uint32_t off = span == 0 ? next_u32() : below(span);
    return int32_t(int64_t(lo) + int64_t(off));
}

bool Pcg32::chance(uint32_t num, uint32_t den) noexcept {
    if (den == 0) return false;
    return below(den) < num;
}

} // namespace rng
