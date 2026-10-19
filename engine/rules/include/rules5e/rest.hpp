#pragma once
// R-G16: rests and Hit Dice. Pure functions over a combatant and the per-actor tables (features, pools, recharge actions)
// kept by the caller between encounters (the same row types EncounterState uses). No events, no encounter state.
#include <array>
#include <vector>
#include "rng/rng.hpp"
#include "rules5e/encounter.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// Tables a rest refreshes; any pointer may be null. Rows of other actors are left alone (actor 0 = every row).
struct RestTables {
    std::vector<FeatureUses>* features = nullptr;
    std::vector<ResourcePool>* pools = nullptr;
    std::vector<ActionUse>* action_uses = nullptr;
};

struct HitDiceSpent {
    uint8_t spent = 0;           // dice spent
    int16_t healed = 0;          // HP actually regained (capped at effective_hp_max)
    std::vector<int16_t> gains;  // per die: roll + Con modifier, at least 0 (before the cap)
};

// Spends up to `n` Hit Dice, one at a time: each rolls 1d(hit_die) + Con modifier (minimum 0) and heals that much. Stops early
// once HP is at the (exhaustion-adjusted) maximum, so no die is wasted. Nothing for a dead creature or one with hit_die == 0.
// One rng draw per die spent. Healing from 0 HP resets death saves and ends Unconscious (commit_healing).
HitDiceSpent spend_hit_dice(Combatant&, rng::Rng&, uint8_t n);

struct RestResult {
    bool ok = false;             // false: no rest (dead; long rest: below 1 HP)
    HitDiceSpent hit_dice;       // short rest
    int16_t hp_regained = 0;     // long rest
    uint8_t hit_dice_regained = 0;
    bool exhaustion_reduced = false;
};

// Short rest: spend up to `hit_dice_to_spend` Hit Dice, regain Pact Magic slots, recharge ShortRest features and pools.
// Recharge actions (Recharge 5-6) are regained on any rest.
RestResult short_rest(Combatant&, rng::Rng&, uint8_t hit_dice_to_spend, const RestTables& = {});

// Long rest (SRD 5.1): needs at least 1 HP. Exhaustion -1 if `food_and_drink`; then all HP (to the adjusted maximum), temp HP
// lost, death saves reset; regain spent Hit Dice up to half the maximum (minimum 1); all spell and Pact slots; innate uses;
// ShortRest and LongRest features and pools; recharge actions. No rng.
RestResult long_rest(Combatant&, const RestTables& = {}, bool food_and_drink = true);

// The table part of a rest for `actor` (0 = all rows): features and pools by recharge kind, recharge actions always.
void recharge_tables(CombatantId actor, bool long_rest, const RestTables&);

// Arcane Recovery / Natural Recovery: regain expended slots, `add[level]` of each level, whose combined level is at most
// `max_total_levels` (half the class level, rounded up: recovery_budget) and none above `max_slot_level` (5 in SRD 5.1: "none
// can be 6th level or higher"). False (no change) when over budget, above the level limit, or above spell_slots_max. The
// once-per-day limit is the caller's FeatureUses row.
bool recover_spell_slots(Combatant&, const std::array<uint8_t, 10>& add, int max_total_levels, int max_slot_level = 5);
constexpr int recovery_budget(int class_level) { return (class_level + 1) / 2; }

// ---- Slot recovery feature kind (Arcane Recovery, Natural Recovery; VERSION_BUMP_BATCH 7.2n) ----
// Feature data: budget = recovery_budget(class_level) slot levels, no slot above max_slot_level, `uses` per long rest.
// The once-per-day limit is a FeatureUses row (recharge LongRest) the caller registers with slot_recovery_uses and keeps in
// its RestTables, so long_rest / recharge_tables reset it and short_rest leaves it alone. Rests are outside Combat::hash.
struct SlotRecoverySpec {
    uint32_t feature_id = 0;     // stable id of the feature (FeatureUses::feature_id)
    uint8_t class_level = 0;     // wizard level (Arcane Recovery) / druid level (Natural Recovery), 1..20
    uint8_t max_slot_level = 5;  // SRD 5.1: "none of the slots can be 6th level or higher"
    uint8_t uses = 1;            // per long rest ("once per day")
};
constexpr int slot_recovery_budget(const SlotRecoverySpec& s) { return recovery_budget(s.class_level); }
// The FeatureUses row to register for `actor` (max = spec.uses, recharge LongRest).
FeatureUses slot_recovery_uses(CombatantId actor, const SlotRecoverySpec&);

enum class SlotRecoveryResult : uint8_t {
    Ok,
    NoFeature,      // no FeatureUses row for (c.id, spec.feature_id), or a spec with class_level 0
    NoUsesLeft,     // already used since the last long rest
    Empty,          // no slot chosen
    LevelTooHigh,   // a chosen slot above spec.max_slot_level
    OverBudget,     // combined level above slot_recovery_budget(spec)
    AboveMax,       // would exceed spell_slots_max at some level (only expended slots come back)
};
// Use the feature when `c` finishes a short rest (the caller calls it after short_rest; the choice of slots is the caller's
// input from sim / UI). Validates `add` (as recover_spell_slots), then spends one use. Anything but Ok changes nothing.
SlotRecoveryResult apply_slot_recovery(Combatant& c, const SlotRecoverySpec&, std::vector<FeatureUses>& features,
                                       const std::array<uint8_t, 10>& add);

}  // namespace rules5e
