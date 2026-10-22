#pragma once
// VERSION_BUMP_BATCH 7.2zg: every spell@1 key name this batch adds, in one place (T&C froze them; a rename is one edit here
// plus schemas/spell/v1.schema.json and tools/validate/checks_v4.py ZG_KEYS).
namespace rules5e_content::zg {
inline constexpr const char* kHaste = "haste";                    // effect kind (first effect only, concentration)
inline constexpr const char* kSlow = "slow";                      // effect kind (first effect only, concentration)
inline constexpr const char* kSummonWeapon = "summon_weapon";     // effect kind (Spiritual Weapon)
inline constexpr const char* kTempHp = "temp_hp";                 // effect kind (False Life, Heroism)
inline constexpr const char* kDice = "dice";                      // temp_hp: rolled amount (+ flat in the dice string)
inline constexpr const char* kPerSlotAbove = "per_slot_above";    // temp_hp: + n per slot level above the spell's
inline constexpr const char* kAbility = "ability";                // temp_hp: "spellcasting" (the caster's modifier)
inline constexpr const char* kPerTurn = "per_turn";               // temp_hp: refreshed at the start of each of the target's turns
inline constexpr const char* kConditionImmunity = "condition_immunity";   // temp_hp: conditions the target is immune to
inline constexpr const char* kOnMiss = "on_miss";                 // spell_attack: "half" (the initial damage only)
inline constexpr const char* kTriggerDamaged = "damaged";         // casting_time.trigger
inline constexpr const char* kDamageTypeChoices = "damage_type_choices";  // apply_modifier (resistance): per-cast choice
inline constexpr const char* kExemptSenses = "exempt_senses";     // attacks_against modifier / duplicates
inline constexpr const char* kOutlined = "outlined";              // modifier stat: the outlined state (Faerie Fire)
inline constexpr const char* kShakeAwake = "shake_awake";         // ends_on: another creature's action
}  // namespace rules5e_content::zg
