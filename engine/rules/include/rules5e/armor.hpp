#pragma once
// Row 15: armor class from equipment (SRD 5.1 "Armor and Shields", "Unarmored Defense", natural armor).
// Pure: a plain input struct in, a plain result out. No loader / item@1 dependency; Engine Core fills the input from
// the resolved `item@1` armor/shield and the stat block, and applies the result at archetype build.
#include <cstdint>
#include "rules5e/abilities.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

enum class ArmorCategory : uint8_t { Light, Medium, Heavy };

struct ArmorPiece {                    // item@1 category "armor"
    ArmorCategory category = ArmorCategory::Light;
    int16_t base_ac = 10;
    bool dex_bonus = true;             // false for heavy armor
    bool has_dex_cap = false;          // medium armor: cap 2
    int8_t dex_cap = 2;
    int8_t str_min = 0;                // Str score needed; below it speed drops by 10 ft
    bool stealth_disadvantage = false;
    int8_t magic_bonus = 0;            // 0..3
};

struct ShieldPiece {                   // item@1 category "shield"
    int8_t ac_bonus = 2;
    int8_t magic_bonus = 0;
};

struct AcInput {
    AbilityScores abilities{};
    bool has_armor = false; ArmorPiece armor{};
    bool has_shield = false; ShieldPiece shield{};
    // No armor worn: AC = unarmored_base + Dex mod, or the better of that and one alternative formula
    //   (Unarmored Defense: Barbarian 10 + Dex + Con, Monk 10 + Dex + Wis => `alt_ability`).
    int16_t unarmored_base = 10;
    bool has_alt_ability = false; Ability alt_ability = Ability::Con;
    // Natural armor (monster stat blocks): fixed base, optionally plus Dex. 0 = none. Used when no armor is worn
    // and it beats the unarmored formulas.
    int16_t natural_base = 0; bool natural_dex_bonus = false;
    int16_t flat_bonus = 0;            // other static bonuses (fighting style Defense, ring of protection)
    int16_t base_speed_ft = 0;         // speed before the Str penalty
};

struct AcResult {
    int16_t ac = 10;
    int16_t speed_ft = 0;              // base_speed_ft minus 10 when armor str_min exceeds Str (never below 0)
    bool str_shortfall = false;
    bool stealth_disadvantage = false; // worn armor imposes it
};

// Dex modifier honouring the armor's rules: none for dex_bonus == false, capped by dex_cap when present.
int armor_dex_term(const ArmorPiece&, int dex_mod);
AcResult compute_ac(const AcInput&);

} // namespace rules5e
