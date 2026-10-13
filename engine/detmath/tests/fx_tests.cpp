#include <doctest/doctest.h>

#include "detmath/fx.hpp"
#include "golden_hash.hpp"
#include "rng/rng.hpp"

using detmath::Fx;

static Fx fx(int32_t v) { return Fx::from_int(v); }

TEST_CASE("Fx construction and conversion") {
    static_assert(Fx::from_int(1).raw == 65536);
    static_assert(Fx::from_int(-1).raw == -65536);
    CHECK(Fx::from_ratio(1, 2).raw == 32768);
    CHECK(Fx::from_ratio(-3, 4).raw == -49152);
    CHECK(Fx::from_raw(-1).to_int_floor() == -1);
    CHECK(Fx::from_raw(-1).to_int_trunc() == 0);
    CHECK(Fx::from_ratio(3, 2).to_int_round() == 2);
    CHECK(Fx::from_ratio(-3, 2).to_int_round() == -1);  // half up
    CHECK(fx(-32768).to_int_floor() == -32768);
    CHECK(fx(32767).to_int_floor() == 32767);
}

TEST_CASE("Fx arithmetic") {
    CHECK(fx(2) + fx(3) == fx(5));
    CHECK(fx(2) - fx(3) == fx(-1));
    CHECK(-fx(4) == fx(-4));
    CHECK(fx(6) * fx(7) == fx(42));
    CHECK(Fx::from_ratio(1, 2) * Fx::from_ratio(1, 2) == Fx::from_ratio(1, 4));
    CHECK(fx(-3) * Fx::from_ratio(1, 2) == Fx::from_ratio(-3, 2));
    CHECK(fx(7) / fx(2) == Fx::from_ratio(7, 2));
    CHECK(fx(-7) / fx(2) == Fx::from_ratio(-7, 2));
    CHECK((fx(1) / fx(3)).raw == 21845);             // truncation toward zero
    CHECK((fx(-1) / fx(3)).raw == -21845);
    CHECK((Fx::from_raw(1) * Fx::from_raw(-1)).raw == -1);  // product floors
    Fx a = fx(1); a += fx(2); a *= fx(3); a -= fx(1); a /= fx(2);
    CHECK(a == fx(4));
    CHECK(fx(1) < fx(2));
    CHECK(Fx::from_raw(-1) < Fx::from_raw(0));
}

TEST_CASE("Fx saturating and checked helpers") {
    CHECK(detmath::add_sat(Fx::max(), fx(1)) == Fx::max());
    CHECK(detmath::sub_sat(Fx::min(), fx(1)) == Fx::min());
    CHECK(detmath::mul_sat(fx(30000), fx(30000)) == Fx::max());
    CHECK(detmath::mul_sat(fx(-30000), fx(30000)) == Fx::min());
    CHECK_FALSE(detmath::add_checked(Fx::max(), Fx::from_raw(1)).has_value());
    CHECK(detmath::add_checked(fx(1), fx(1)).value() == fx(2));
    CHECK_FALSE(detmath::mul_checked(fx(300), fx(300)).has_value());
    CHECK_FALSE(detmath::div_checked(fx(1), fx(0)).has_value());
    CHECK_FALSE(detmath::div_checked(fx(30000), Fx::from_ratio(1, 4)).has_value());
    CHECK(detmath::div_checked(fx(9), fx(3)).value() == fx(3));
}

TEST_CASE("Fx abs/floor/frac and div_floor") {
    CHECK(detmath::abs(fx(-3)) == fx(3));
    CHECK(detmath::floor(Fx::from_ratio(-1, 2)) == fx(-1));
    CHECK(detmath::floor(Fx::from_ratio(5, 2)) == fx(2));
    CHECK(detmath::frac(Fx::from_ratio(-1, 4)) == Fx::from_ratio(3, 4));
    CHECK(detmath::div_floor(7, 2) == 3);
    CHECK(detmath::div_floor(-7, 2) == -4);
    CHECK(detmath::div_floor(7, -2) == -4);
    CHECK(detmath::div_floor(-8, 2) == -4);
}

TEST_CASE("Fx sqrt") {
    CHECK(detmath::sqrt(fx(4)) == fx(2));
    CHECK(detmath::sqrt(fx(10000)) == fx(100));
    CHECK(detmath::sqrt(Fx::from_ratio(1, 4)) == Fx::from_ratio(1, 2));
    CHECK(detmath::sqrt(fx(2)).raw == 92681);   // floor(sqrt(2) * 65536)
    CHECK(detmath::sqrt(fx(0)) == fx(0));
    CHECK(detmath::sqrt(fx(-5)) == fx(0));
    CHECK(detmath::sqrt(Fx::max()).raw == 11863283);
}

TEST_CASE("parse_fx is exact and rejects garbage") {
    using detmath::parse_fx;
    CHECK(parse_fx("1.25").value() == Fx::from_ratio(5, 4));
    CHECK(parse_fx("-1.25").value() == Fx::from_ratio(-5, 4));
    CHECK(parse_fx("+3").value() == fx(3));
    CHECK(parse_fx("0.5").value() == Fx::from_ratio(1, 2));
    CHECK(parse_fx(".5").value() == Fx::from_ratio(1, 2));
    CHECK(parse_fx("7.").value() == fx(7));
    CHECK(parse_fx("0.1").value().raw == 6554);       // 6553.6 -> 6554
    CHECK(parse_fx("-0.1").value().raw == -6554);
    CHECK(parse_fx("0.00000762939453125").value().raw == 1);  // exactly half of 2^-16 -> away from zero
    CHECK(parse_fx("-32768").value() == Fx::min());
    CHECK(parse_fx("32767.99998474121").value() == Fx::max());
    CHECK_FALSE(parse_fx("32768").has_value());
    CHECK_FALSE(parse_fx("").has_value());
    CHECK_FALSE(parse_fx("-").has_value());
    CHECK_FALSE(parse_fx(".").has_value());
    CHECK_FALSE(parse_fx("1e3").has_value());
    CHECK_FALSE(parse_fx("1.2.3").has_value());
    CHECK_FALSE(parse_fx(" 1").has_value());
}

TEST_CASE("golden: Fx arithmetic hash over a seeded workload") {
    rng::Pcg32 r(0xF1Eull, 16u);
    GoldenHash g;
    Fx acc = fx(0);
    for (int i = 0; i < 5000; ++i) {
        Fx a = Fx::from_raw(r.range(-(100 << 16), 100 << 16));
        Fx b = Fx::from_raw(r.range(1, 50 << 16));
        g.i32((a + b).raw);
        g.i32((a - b).raw);
        g.i32((a * b).raw);
        g.i32((a / b).raw);
        g.i32(detmath::sqrt(detmath::abs(a)).raw);
        acc = detmath::add_sat(acc, detmath::mul_sat(a, b));
        g.i32(acc.raw);
    }
    CHECK(g.h == 0x88A2FA64B6F29C64ull);
}
