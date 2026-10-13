#pragma once
// Deterministic PRNGs (ARCHITECTURE.md §4.2). std-only; no <random>.
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace rng {

// SplitMix64 (Steele, Lea, Flood 2014). Advances x and returns the next output.
constexpr uint64_t splitmix64(uint64_t& x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Stateless child-seed derivation. Order-sensitive: {a,b} != {b,a}.
uint64_t derive_seed(uint64_t parent, std::span<const int64_t> keys) noexcept;

class Pcg32 {                                    // PCG-XSH-RR 64/32 (O'Neill)
public:
    struct State { uint64_t state, inc; };

    Pcg32(uint64_t seed, uint64_t stream) noexcept;

    uint32_t next_u32() noexcept;
    uint32_t below(uint32_t bound) noexcept;           // [0, bound), Lemire, unbiased; bound 0 -> 0
    int32_t  range(int32_t lo, int32_t hi) noexcept;   // inclusive; requires lo <= hi
    bool     chance(uint32_t num, uint32_t den) noexcept;  // P = num/den
    int      d(int sides) noexcept { return int(below(uint32_t(sides))) + 1; }

    template <class T> void shuffle(std::span<T> s) noexcept {  // Fisher-Yates
        for (std::size_t i = s.size(); i > 1; --i) {
            std::size_t j = below(uint32_t(i));
            using std::swap;
            swap(s[i - 1], s[j]);
        }
    }

    State save() const noexcept { return {state_, inc_}; }
    void  load(State s) noexcept { state_ = s.state; inc_ = s.inc; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

using Rng = Pcg32;

} // namespace rng
