#pragma once
// VERSION_BUMP_BATCH 7.2zi: every spell@1 key name this batch adds, in one place (T&C will freeze them; a rename is one edit
// here plus schemas/spell/v1.schema.json, schemas/common/v2.schema.json, and tools/validate/checks_v4.py ZI_KEYS).
// Reused, not listed: apply_modifier, attack_roll, mode, consume, applies_to, scope.target marked, end_condition,
// resistance, damage_type, save, ability, ac, bonus, spell_attack, repeat_save (new object shape on apply_modifier only),
// excludes_creature_type, duration until_end_of_caster_next_turn / rounds / concentration.
namespace rules5e_content::zi {
inline constexpr const char* kStabilize = "stabilize";                 // effect kind (Spare the Dying; first effect only)
inline constexpr const char* kHitOnly = "hit_only";                    // spell_attack: the hit deals no hit-point damage (Ray of Enfeeblement)
inline constexpr const char* kOnNextTurn = "on_next_turn";             // apply_modifier: inactive until the caster's next turn (True Strike)
inline constexpr const char* kBonusDash = "bonus_dash";                // modifier stat (Expeditious Retreat)
inline constexpr const char* kDashOnCast = "dash_on_cast";             // apply_modifier: the cast itself takes the Dash action (Expeditious Retreat)
inline constexpr const char* kSaveVsConditions = "save_vs_conditions"; // apply_modifier: save mode only vs these conditions
inline constexpr const char* kHalveWeaponDamage = "halve_weapon_damage"; // modifier stat (Ray of Enfeeblement)
inline constexpr const char* kDeathSaves = "death_saves";              // apply_modifier: advantage on death saving throws
inline constexpr const char* kHealMaximum = "heal_maximum";            // apply_modifier: healing dice are maximized (no rng draw)
inline constexpr const char* kShareDamage = "share_damage";            // modifier stat (Warding Bond)
inline constexpr const char* kAllDamage = "all_damage";                // resistance: every damage type, not a single damage_type
inline constexpr const char* kEndBeyondFt = "end_beyond_ft";           // apply_modifier: end the source when farther than this
inline constexpr const char* kEndAtZero = "end_at_zero";               // apply_modifier: whose drop to 0 HP ends the source
inline constexpr const char* kCaster = "caster";                       // the only end_at_zero value (SRD: the caster, not the target)
}  // namespace rules5e_content::zi
