#pragma once
// Font of Magic (Sorcerer 2, SRD 5.1): sorcery points and Flexible Casting. Points live in the ResourcePool
// Combatant::sorcery_pool (the R-G14 Metamagic pool; recharge LongRest). In an encounter, conversion is
// ActionKind::FlexibleCasting (a bonus action); outside one, call flexible_casting on the roster's pools.
// VERSION_BUMP_BATCH 7.2o.
#include <cstdint>
#include <vector>
#include "rules5e/encounter.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// Sorcery point maximum: the sorcerer level, from level 2 (0 below).
constexpr int sorcery_points_max(int sorcerer_level) { return sorcerer_level >= 2 ? sorcerer_level : 0; }
// Points to create a slot of `level`: 2 / 3 / 5 / 6 / 7 for 1st..5th; 0 = not creatable (any other level).
constexpr int flexible_casting_cost(int level) {
    constexpr int kCost[6] = {0, 2, 3, 5, 6, 7};
    return level >= 1 && level <= 5 ? kCost[level] : 0;
}

enum class FlexibleDir : uint8_t { None = 0, SlotToPoints = 1, PointsToSlot = 2 };
enum class FlexibleResult : uint8_t {
    Ok,
    NoPool,          // no sorcery_pool (0) or no ResourcePool row for (c.id, c.sorcery_pool)
    BadDirection,    // flexible not 1 or 2
    BadLevel,        // slot to points: not 1..9; points to slot: not 1..5
    NoSlot,          // slot to points: no unexpended spell slot of that level (Pact Magic slots are not used)
    OverMax,         // slot to points: the points would exceed the pool maximum (refused, not capped)
    NotEnoughPoints, // points to slot: fewer points than flexible_casting_cost(level)
};
struct FlexibleOutcome {
    FlexibleResult result = FlexibleResult::BadDirection;
    int16_t points_delta = 0;   // + gained / - spent
    uint16_t points_after = 0;
};
// Validate a conversion without changing anything.
FlexibleOutcome check_flexible_casting(const Combatant&, const std::vector<ResourcePool>&, uint8_t flexible, uint8_t level);
// Out-of-encounter helper (also used by the encounter action): validate and apply. A created slot is an extra spell slot
// (spell_slots[level] may go above spell_slots_max); long_rest restores spell_slots to spell_slots_max, so created slots
// vanish then, and the pool refills on its LongRest recharge. Anything but Ok changes nothing.
FlexibleOutcome flexible_casting(Combatant&, std::vector<ResourcePool>&, uint8_t flexible, uint8_t level);

}  // namespace rules5e
