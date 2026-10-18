#pragma once
// SRD 5.1 weapon properties (Equipment > Weapons > Weapon Properties) as rules5e sees them.
// Attack::properties carries these bits (T&C-G18). Which ones the engine acts on:
//   Heavy      - Small / Tiny attackers have disadvantage (Combatant::size)
//   Loading    - one shot per Attack action no matter how many attacks it grants (TurnBudget::loading_fired)
//   Lance      - "Special": disadvantage when the target is within 5 ft (the lance's own rule)
//   Finesse    - the better of Str / Dex applies (attack_ability); to_hit/damage stay the authored totals
//   Reach      - Attack::reach_or_range_ft = 10 (data, not a flag)
//   Thrown     - an authored ranged Attack entry with the thrown range (normal/long range); the flag
//                marks it so a sim can tell a thrown melee weapon from a plain ranged one
//   Versatile  - an authored second Attack entry with the two-handed damage; flag marks the pair
//   Light / TwoHanded / Ammunition - flags only: Light for two-weapon fighting (R-G6, not in the
//                engine yet), TwoHanded for the sim's hand-economy checks, Ammunition for inventory
//                (out of scope, no ammo tracking).
#include <cstdint>
#include <optional>
#include <string_view>
#include "rules5e/types.hpp"

namespace rules5e {

enum WeaponProperty : uint16_t {
    kWeaponAmmunition = 1, kWeaponFinesse = 2, kWeaponHeavy = 4, kWeaponLight = 8, kWeaponLoading = 16,
    kWeaponReach = 32, kWeaponSpecial = 64, kWeaponThrown = 128, kWeaponTwoHanded = 256,
    kWeaponVersatile = 512, kWeaponLance = 1024,
};

constexpr bool has_property(const Attack& a, WeaponProperty p) { return (a.properties & p) != 0; }
// Finesse is honoured from either the legacy `finesse` flag or the property bit.
constexpr bool is_finesse(const Attack& a) { return a.finesse || has_property(a, kWeaponFinesse); }

// Closed vocabulary used by content: "ammunition", "finesse", "heavy", "light", "loading", "reach",
// "special", "thrown", "two_handed" (also "two-handed"), "versatile", "lance". "range" is not a
// property here (ranges are data). Unknown -> nullopt.
constexpr std::optional<uint16_t> weapon_property_from_string(std::string_view s) {
    if (s == "ammunition") return kWeaponAmmunition;
    if (s == "finesse") return kWeaponFinesse;
    if (s == "heavy") return kWeaponHeavy;
    if (s == "light") return kWeaponLight;
    if (s == "loading") return kWeaponLoading;
    if (s == "reach") return kWeaponReach;
    if (s == "special") return kWeaponSpecial;
    if (s == "thrown") return kWeaponThrown;
    if (s == "two_handed" || s == "two-handed") return kWeaponTwoHanded;
    if (s == "versatile") return kWeaponVersatile;
    if (s == "lance") return kWeaponLance;
    return std::nullopt;
}

// Ability used for the attack: ranged -> Dex, finesse -> the higher of Str and Dex (ties: Str), else Str.
// (Thrown melee weapons use Str unless finesse, so keying on `ranged` would mis-handle a thrown dagger:
// finesse wins first.) Returns the ability, not the modifier.
Ability attack_ability(const Combatant& c, const Attack& a);

// Disadvantage from the weapon itself: Heavy with a Small / Tiny wielder; Lance with the target within 5 ft.
bool weapon_disadvantage(const Combatant& attacker, const Attack& a, bool target_within_5ft);

} // namespace rules5e
