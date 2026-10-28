#pragma once
// VERSION_BUMP_BATCH 7.2zj: every spell@1 key name this batch adds, in one place (T&C will freeze them; a rename is one edit
// here plus schemas/spell/v1.schema.json and tools/validate/checks_v4.py ZJ_KEYS).
// Reused, not listed: save, ability, damage, on_success, condition, area, targets, creature_type, scaling.upcast,
// apply_condition invisible, apply_modifier, scope.from_types, applies_to attackers, attack_roll, mode disadvantage,
// condition_choice (log_hash 0xB1), area_origin (0xC0), grant_fly speed (Combat::hash 0xD6), zone difficult (0xAB).
namespace rules5e_content::zj {
inline constexpr const char* kHostileSaveAdvantage = "hostile_save_advantage"; // save: advantage when the target's team differs
inline constexpr const char* kEndsOnAllyHarm = "ends_on_ally_harm";           // the effect ends if the caster's team damages the target
inline constexpr const char* kSuppressConditions = "suppress_conditions";     // save: charmed and/or frightened, suppressed on a failure
inline constexpr const char* kAlsoDamage = "also_damage";                     // save: second {dice, type} on the same save; not upcast
inline constexpr const char* kDifficultTerrain = "difficult_terrain";         // save: {until: until_end_of_caster_next_turn}
inline constexpr const char* kUntilEndOfCasterNextTurn = "until_end_of_caster_next_turn";
inline constexpr const char* kGrantFly = "grant_fly";                         // effect kind (Fly; first effect only)
inline constexpr const char* kFlyFt = "fly_ft";                               // positive multiple of 5
inline constexpr const char* kRestore = "restore";                            // effect kind (Greater Restoration; first effect only)
inline constexpr const char* kOptions = "options";                            // restore option list (condition_choice is 1 + index)
inline constexpr const char* kEndsOnRestoration = "ends_on_restoration";      // save: Greater Restoration ends the effect
inline constexpr const char* kCharmImmune = "charm_immune";                   // save: immune to charmed skips the save
inline constexpr const char* kCasterSeesTarget = "caster_sees_target";        // refused when a sight hook says the caster cannot see
inline constexpr const char* kRequiresHearing = "requires_hearing";           // a deafened target is unaffected
}  // namespace rules5e_content::zj
