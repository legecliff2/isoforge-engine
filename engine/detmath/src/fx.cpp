#include "detmath/fx.hpp"

namespace detmath {

namespace {
uint64_t isqrt_u64(uint64_t n) {  // floor(sqrt(n)), Newton iteration
    if (n < 2) return n;
    uint64_t x = n;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}
} // namespace

Fx sqrt(Fx a) {
    if (a.raw <= 0) return {0};
    // sqrt(raw / 2^16) * 2^16 == sqrt(raw * 2^16)
    return {int32_t(isqrt_u64(uint64_t(a.raw) << Fx::kFracBits))};
}

std::optional<Fx> parse_fx(std::string_view s) {
    if (s.empty()) return std::nullopt;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') { neg = s[0] == '-'; ++i; }
    uint64_t ip = 0;
    std::size_t idigits = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++idigits) {
        ip = ip * 10 + uint64_t(s[i] - '0');
        if (ip > 32768) return std::nullopt;
    }
    // Fraction as num / 10^k, k <= 18 significant digits kept exactly.
    uint64_t fnum = 0, fden = 1;
    std::size_t fdigits = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++fdigits) {
            if (fdigits < 18) { fnum = fnum * 10 + uint64_t(s[i] - '0'); fden *= 10; }
        }
    }
    if (i != s.size() || (idigits == 0 && fdigits == 0)) return std::nullopt;
    // frac_raw = round(fnum * 2^16 / fden), half away from zero, via bitwise long division
    // (fnum * 2^16 could overflow 64 bits). Digits beyond the 18th are ignored
    // (an error < 1e-18, far below the 2^-17 rounding granularity).
    uint64_t q = 0, r = fnum;
    for (int b = 0; b < 16; ++b) {
        r <<= 1;  // r < fden <= 1e18 < 2^60, so this fits
        q <<= 1;
        if (r >= fden) { r -= fden; q |= 1; }
    }
    bool round_up = fdigits > 0 && r * 2 >= fden;
    int64_t mag = int64_t(ip) * Fx::kOne + int64_t(q) + (round_up ? 1 : 0);
    int64_t v = neg ? -mag : mag;
    if (v > std::numeric_limits<int32_t>::max() || v < std::numeric_limits<int32_t>::min()) return std::nullopt;
    return Fx{int32_t(v)};
}

} // namespace detmath
