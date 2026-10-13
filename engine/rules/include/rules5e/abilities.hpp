#pragma once
#include "rules5e/types.hpp"

namespace rules5e {

// SRD 5.1: modifier = floor((score - 10) / 2). Same formula as the §5.3 sketch.
constexpr int modifier(int score) { return (score >= 10 ? score - 10 : score - 11) / 2; }
constexpr int modifier(const AbilityScores& s, Ability a) { return modifier(s[a]); }

} // namespace rules5e
