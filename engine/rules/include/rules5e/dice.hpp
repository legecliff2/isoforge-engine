#pragma once
#include <optional>
#include <string_view>
#include "rng/rng.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// Grammar (whitespace allowed between tokens, 'd' or 'D'):
//   expr  := dice [ ('+'|'-') int ] | int
//   dice  := [count] 'd' sides           count in [1,255] (default 1), sides in [1,255]
// The bonus must fit int16_t. Returns nullopt on any malformed input.
std::optional<DiceExpr> parse_dice(std::string_view text);

// Rolls the dice and adds the bonus. On a critical hit the dice are doubled, the bonus is not
// (SRD 5.1 "Critical Hits"). The raw total may be negative; callers clamp where the SRD says so.
int roll_dice(rng::Rng&, DiceExpr, bool critical = false);
// R-G7 (Great Weapon Fighting): as roll_dice, but each die showing `reroll_at_most` or less is rolled again once (the new
// value is kept). Same draws as roll_dice when no die is rerolled.
int roll_dice_reroll_low(rng::Rng&, DiceExpr, bool critical, int reroll_at_most);

constexpr int dice_min(DiceExpr e) { return e.count + e.bonus; }
constexpr int dice_max(DiceExpr e) { return e.count * e.sides + e.bonus; }

} // namespace rules5e
