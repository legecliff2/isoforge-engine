#pragma once
// Q16.16 fixed point (ARCHITECTURE.md §4.3). int32_t storage, int64_t intermediates.
// Plain operators follow the §4.8 sketch: overflow is a precondition violation
// (UB, caught by UBSan). Use the *_sat helpers where overflow is possible.
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace detmath {

struct Fx {                                      // Q16.16
    int32_t raw;

    static constexpr int     kFracBits = 16;
    static constexpr int32_t kOne = int32_t(1) << kFracBits;

    static constexpr Fx from_raw(int32_t r) { return {r}; }
    static constexpr Fx from_int(int32_t v) { return {int32_t(uint32_t(v) << kFracBits)}; }  // v in [-32768, 32767]
    static constexpr Fx from_ratio(int32_t num, int32_t den) {                              // truncates toward zero
        return {int32_t((int64_t(num) * kOne) / den)};
    }
    static constexpr Fx max() { return {std::numeric_limits<int32_t>::max()}; }
    static constexpr Fx min() { return {std::numeric_limits<int32_t>::min()}; }

    constexpr int32_t to_int_floor() const { return raw >> kFracBits; }   // arithmetic shift (C++20)
    constexpr int32_t to_int_trunc() const { return raw / kOne; }
    constexpr int32_t to_int_round() const { return int32_t((int64_t(raw) + kOne / 2) >> kFracBits); }  // half up

    friend constexpr Fx operator+(Fx a, Fx b) { return {a.raw + b.raw}; }
    friend constexpr Fx operator-(Fx a, Fx b) { return {a.raw - b.raw}; }
    friend constexpr Fx operator-(Fx a) { return {-a.raw}; }
    // Multiply: floor of the exact product (arithmetic right shift).
    friend constexpr Fx operator*(Fx a, Fx b) { return {int32_t((int64_t(a.raw) * b.raw) >> kFracBits)}; }
    // Divide: truncated toward zero. b != 0.
    friend constexpr Fx operator/(Fx a, Fx b) { return {int32_t((int64_t(a.raw) * kOne) / b.raw)}; }
    constexpr Fx& operator+=(Fx o) { return *this = *this + o; }
    constexpr Fx& operator-=(Fx o) { return *this = *this - o; }
    constexpr Fx& operator*=(Fx o) { return *this = *this * o; }
    constexpr Fx& operator/=(Fx o) { return *this = *this / o; }

    auto operator<=>(const Fx&) const = default;
};

namespace detail {
constexpr int32_t clamp_i64(int64_t v) {
    if (v > std::numeric_limits<int32_t>::max()) return std::numeric_limits<int32_t>::max();
    if (v < std::numeric_limits<int32_t>::min()) return std::numeric_limits<int32_t>::min();
    return int32_t(v);
}
} // namespace detail

constexpr Fx add_sat(Fx a, Fx b) { return {detail::clamp_i64(int64_t(a.raw) + b.raw)}; }
constexpr Fx sub_sat(Fx a, Fx b) { return {detail::clamp_i64(int64_t(a.raw) - b.raw)}; }
constexpr Fx mul_sat(Fx a, Fx b) { return {detail::clamp_i64((int64_t(a.raw) * b.raw) >> Fx::kFracBits)}; }
// Checked variants: nullopt on overflow (or division by zero).
constexpr std::optional<Fx> add_checked(Fx a, Fx b) {
    int64_t v = int64_t(a.raw) + b.raw;
    if (v != detail::clamp_i64(v)) return std::nullopt;
    return Fx{int32_t(v)};
}
constexpr std::optional<Fx> mul_checked(Fx a, Fx b) {
    int64_t v = (int64_t(a.raw) * b.raw) >> Fx::kFracBits;
    if (v != detail::clamp_i64(v)) return std::nullopt;
    return Fx{int32_t(v)};
}
constexpr std::optional<Fx> div_checked(Fx a, Fx b) {
    if (b.raw == 0) return std::nullopt;
    int64_t v = (int64_t(a.raw) * Fx::kOne) / b.raw;
    if (v != detail::clamp_i64(v)) return std::nullopt;
    return Fx{int32_t(v)};
}

constexpr Fx abs(Fx a) { return {a.raw < 0 ? -a.raw : a.raw}; }   // a != Fx::min()
constexpr Fx floor(Fx a) { return {int32_t(uint32_t(a.raw) & 0xFFFF0000u)}; }
constexpr Fx frac(Fx a) { return {a.raw & 0xFFFF}; }               // always in [0, 1)

// Integer division with explicit rounding (§4.3).
constexpr int32_t div_floor(int32_t a, int32_t b) {
    int32_t q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

Fx sqrt(Fx a);                                         // integer Newton; a < 0 -> 0. Floor of exact root.
std::optional<Fx> parse_fx(std::string_view decimal);  // "-1.25" -> exact; rounds half away from zero; no strtod

} // namespace detmath
