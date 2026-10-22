#pragma once
// docs/proposals/EQUIPMENT.md 9 (Engine Core owns this header; Rules reviews): equipment STATE carried by EncounterState. Pure
// data, no logic. Rules reads it only through EncounterHooks::loadout_of (a LoadoutView) and item_def; Engine Core
// (sim/equipment.hpp) builds it at setup when CombatOptions::equipment is on and mutates it in apply_equip. Empty by default, so
// every pre-equipment encounter is unchanged (Combat::hash folds 0xD0 / 0xD2 only when present).
#include <cstdint>
#include <vector>
#include "rules5e/item.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// One creature's equipment: item instance ids, their def ids and slots (hands, worn armor, carried). The ItemDef data lives in
// Engine Core's ItemDefTable (looked up by def id); nothing here copies it.
struct Loadout {
    CombatantId owner = 0;
    LoadoutView view{};                 // hands / armor / carried / attack links / interaction flag (what rules reads)
    // Setup-time calibration (EQUIPMENT.md 7.4, "derive == baked"): printed AC / to-hit / damage bonus minus the derived values
    // at setup, so recompute_derived reproduces the stat block exactly until something changes. One per view.attacks entry.
    int8_t ac_adjust = 0;
    std::vector<int8_t> hit_adjust{}, dmg_adjust{};
    uint32_t interaction_turn = 0;      // sim's TurnStarted count when the free object interaction was spent (0 = never)
};

// An item lying on the battlefield (EQUIPMENT.md 4.4): dropped, or left by a creature. The tile is the sim's (world tile).
struct GroundItem {
    HeldItem item{};                    // instance, def, count, flags (a dropped lit torch keeps kHeldLit)
    int32_t x = 0, y = 0;
};

} // namespace rules5e
