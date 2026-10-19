#include "rules5e/dice.hpp"

namespace rules5e {

namespace {
struct Cursor {
    std::string_view s; std::size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }
    bool eof() const { return i >= s.size(); }
    char peek() const { return eof() ? '\0' : s[i]; }
    // Reads up to 6 digits; nullopt if none.
    std::optional<int32_t> num() {
        std::size_t start = i; int32_t v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (i - start >= 6) return std::nullopt;
            v = v * 10 + (s[i] - '0'); ++i;
        }
        if (i == start) return std::nullopt;
        return v;
    }
};
} // namespace

std::optional<DiceExpr> parse_dice(std::string_view text) {
    Cursor c{text};
    c.ws();
    std::optional<int32_t> first;
    if (c.peek() != 'd' && c.peek() != 'D') {
        // Flat value, possibly signed: "5", "-1"
        bool neg = false;
        if (c.peek() == '-' || c.peek() == '+') { neg = c.peek() == '-'; ++c.i; c.ws(); }
        first = c.num();
        if (!first) return std::nullopt;
        c.ws();
        if (c.eof()) {
            int32_t v = neg ? -*first : *first;
            if (v < INT16_MIN || v > INT16_MAX) return std::nullopt;
            return DiceExpr{0, 0, int16_t(v)};
        }
        if (neg) return std::nullopt;
    }
    if (c.peek() != 'd' && c.peek() != 'D') return std::nullopt;
    ++c.i;
    c.ws();
    int32_t count = first.value_or(1);
    auto sides = c.num();
    if (!sides || count < 1 || count > 255 || *sides < 1 || *sides > 255) return std::nullopt;
    c.ws();
    int32_t bonus = 0;
    if (!c.eof()) {
        char op = c.peek();
        if (op != '+' && op != '-') return std::nullopt;
        ++c.i; c.ws();
        auto b = c.num();
        if (!b) return std::nullopt;
        bonus = op == '-' ? -*b : *b;
        c.ws();
        if (!c.eof() || bonus < INT16_MIN || bonus > INT16_MAX) return std::nullopt;
    }
    return DiceExpr{uint8_t(count), uint8_t(*sides), int16_t(bonus)};
}

int roll_dice(rng::Rng& r, DiceExpr e, bool critical) {
    int n = int(e.count) * (critical ? 2 : 1);
    int total = e.bonus;
    for (int k = 0; k < n; ++k) total += r.d(e.sides);
    return total;
}

int roll_dice_reroll_low(rng::Rng& r, DiceExpr e, bool critical, int low) {
    int n = int(e.count) * (critical ? 2 : 1);
    int total = e.bonus;
    for (int k = 0; k < n; ++k) {
        int d = r.d(e.sides);
        if (d <= low) d = r.d(e.sides);
        total += d;
    }
    return total;
}

} // namespace rules5e
