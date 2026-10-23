#pragma once
// VERSION_BUMP_BATCH 7.2zh: every spell@1 key name this batch adds, in one place (T&C froze them; a rename is one edit here
// plus schemas/spell/v1.schema.json and tools/validate/checks_v4.py ZH_KEYS).
namespace rules5e_content::zh {
inline constexpr const char* kWeaponSmite = "weapon_smite";       // effect kind (Branding Smite; first effect only)
inline constexpr const char* kRevealInvisible = "reveal_invisible";
inline constexpr const char* kLight = "light";
inline constexpr const char* kDimFt = "dim_ft";
inline constexpr const char* kBrightFt = "bright_ft";
inline constexpr const char* kHeldObject = "held_object";         // sustained_damage (Heat Metal)
inline constexpr const char* kHeld = "held";                      // summon_weapon (Flame Blade)
inline constexpr const char* kSave = "save";
inline constexpr const char* kOnFail = "on_fail";
inline constexpr const char* kWhileHeld = "while_held";
inline constexpr const char* kDisadvantage = "disadvantage";
inline constexpr const char* kUntil = "until";
}  // namespace rules5e_content::zh
