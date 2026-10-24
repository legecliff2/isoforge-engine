#pragma once
// docs/proposals/EQUIPMENT.md (Director-approved): rules-side predicates and math for equipment. Pure functions of the
// EncounterState plus Engine Core's read-only views (EncounterHooks::loadout_of, item_def); rules builds no loadout or ground-item
// state (9.1). With no loadout_of hook, or a nullptr view for a creature (legacy content, CombatOptions::equipment off), every
// predicate answers "no constraint" and nothing changes. Integer math only.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "rules5e/encounter.hpp"
#include "rules5e/item.hpp"

namespace rules5e {

// ---- Weapon attacks (2.1) ----
struct WielderView {
    AbilityScores abilities{};
    int8_t pb = 2;
    bool proficient = true;            // weapon proficiency (monsters: always)
    int8_t attack_rows = 0;            // Archery +2 (ranged), other additive attack bonuses
    int8_t damage_rows = 0;            // Dueling +2, other additive damage bonuses
    uint8_t dice_multiplier = 1;       // monster@5 weapon_dice_multiplier (weapons only, never natural attacks)
    uint8_t extra_melee_dice = 0;      // Brute: extra weapon dice on melee (not thrown) weapon hits
    bool twf_style = false;            // Two-Weapon Fighting style: the off-hand attack adds a positive modifier
    std::optional<Ability> ability{};  // explicit ability (natural attacks; a finesse choice)
    bool damage_ability_none = false;  // the stat block prints no modifier (Gelatinous Cube Pseudopod)
    std::optional<int8_t> to_hit_override{};   // monster attack_bonus (the total, with a reason)
    int8_t ammo_magic = 0;             // magic ammunition: + attack and damage
};
struct DerivedAttack { Attack attack{}; std::string_view error{}; };
// Errors (exact): "weapon: a two-handed weapon needs both hands", "weapon: not a versatile weapon",
// "weapon: not a thrown weapon", "weapon: the off-hand weapon must be a light melee weapon".
DerivedAttack derive_weapon_attack(const WeaponDef&, const WielderView&, WeaponUse);
// Unarmed strike (SRD "Melee Attacks"): Str + PB to hit, 1 + Str bludgeoning (flat), no hand needed.
Attack unarmed_strike(const AbilityScores&, int pb);
// Ability the derivation uses: explicit, else finesse -> higher of Str / Dex (ties Str), pure ranged -> Dex, else Str.
Ability weapon_ability(const WeaponDef&, const WielderView&);

// ---- Hands and components (3) ----
const LoadoutView* loadout_view(const EncounterState&, CombatantId);   // nullptr = not equipment-gated
int free_hands(const EncounterState&, CombatantId);                     // no view: 2 (unconstrained)
// Attack-time check of Combatant::attacks[attack_index] (via LoadoutView::attacks). "" = allowed. Strings (exact):
// "attack: the weapon is not in hand", "attack: needs both hands", "attack: loading needs a free hand",
// "attack: no ammunition", "attack: nothing to throw", "attack: the torch is not lit".
std::string_view attack_refusal(const EncounterState&, CombatantId, uint8_t attack_index);
bool can_attack_with(const EncounterState&, CombatantId, uint32_t instance, WeaponUse);
// "" = the components can be provided. Strings: "cast: somatic needs a free hand", "cast: material needs a free hand or a focus".
std::string_view components_refusal(const EncounterState&, CombatantId, const SpellDef&);
bool components_ok(const EncounterState&, CombatantId, const SpellDef&);

// ---- Equip operations (4) ----
enum class EquipOp : uint8_t { None, Draw, Stow, Drop, PickUp, DonShield, DoffShield, HandOver, DonArmor, DoffArmor,
                               SetLightState };   // 10 (appended): light / douse / hood a held light, equip_state
// ActionRequest::equip_state for SetLightState: a LightDef::states index (light, or switch state, e.g. hood a lantern), or
// kLightDouse. Engine Core applies it: kHeldLit plus the index in flags bits 4-6 (with_light_state), or clears kHeldLit.
inline constexpr uint8_t kLightDouse = 0xFF;
// The radii a held item sheds now: {0, 0} if it is not a light, not lit, or its stored index is out of range.
LightState light_radii(const ItemDef&, const HeldItem&);
struct EquipCost {
    enum Kind : uint8_t { Free, Interaction, Action, Refused } kind = Refused;
    std::string_view refusal{};
};
// The cost of ActionRequest{UseObject, equip_op, equip_item, equip_hand, equip_to, equip_in_reach} for the current actor, or the
// refusal (exact strings in EQUIPMENT.md 4.2 and equipment.cpp). Interaction = the free object interaction if unspent; when it is
// spent the same op costs the Use an Object action (Action); Free = drop; shield don / doff = Action; armor = refused in combat;
// SetLightState = Interaction (refusals "equip: not a light source", "equip: unknown light state",
// "equip: the light is already in that state"; the light must be in hand).
EquipCost equip_legal(const EncounterState&, const ActionRequest&);
// Armor don / doff time in rounds (SRD "Donning and Doffing Armor": 1 minute = 10 rounds); a helper halves it.
uint16_t armor_change_rounds(ArmorCategory, bool don, bool helper);
// Equip entries for the current actor, in order op, instance, hand (Draw, Stow, Drop, DonShield, DoffShield); legal_actions
// appends them after every existing entry. PickUp / HandOver need positions and are listed by Engine Core.
std::vector<ActionRequest> equip_legal_actions(const EncounterState&);

// ---- Monster reverse calculation (5) ----
int proficiency_for_cr(int cr_num, int cr_den);   // SRD CR table: 0..4 +2, 5..8 +3, ... 29..30 +9
std::string format_dice(DiceExpr);                // "2d8+4", "1d4-1", "3d6", "1"
struct Reasoned { bool present = false; int8_t value = 0; std::string reason{}; };
struct MonsterAttackSpec {
    std::string id;                    // the action id
    WeaponDef weapon{};                // item, monster-local or natural (weapon.natural)
    WeaponUse use = WeaponUse::OneHand; // monster@5 action `use` (#252); has_use = authored. Absent = one_hand, never inferred
    std::optional<Ability> ability{};  // natural attacks: explicit
    bool damage_ability_none = false;
    Reasoned attack_bonus{};           // overrides the total to-hit
    int8_t printed_to_hit = 0;
    DiceExpr printed_damage{0, 0, 0};  // damage[0] only; damage[1..] stay authored and unchecked (5.6)
    bool has_use = true;               // false = `use` absent in content: derive uses one_hand, ignoring `use` (#252)
    bool has_damage_override = false;  // monster@5 action damage_override {dice, reason}: replaces the derived damage[0]
    DiceExpr damage_override{0, 0, 0}; //   (a block whose printed damage the derivation can't reach, e.g. SRD Gladiator thrown Spear)
    std::string damage_override_reason{};
};
struct MonsterSpec {
    std::string id;                    // content id
    AbilityScores abilities{};
    int cr_num = 0, cr_den = 1;
    bool has_pb = false; int8_t pb = 2; std::string pb_reason{};
    bool has_armor = false; ArmorPiece armor{};
    bool has_shield = false; ShieldPiece shield{};
    bool has_natural_armor = false; int8_t natural_base = 10; bool natural_dex = true;
    uint8_t dice_multiplier = 1;       // explicit, never from size (Q12)
    uint8_t brute_dice = 0;            // monster@5-local trait kind weapon_extra_dice (melee only); not a common@2 kind
    int hands = -1;                    // monster@5 `hands`; -1 = absent (no check)
    int held = 0;                      // hand slots the equipped entries take (#249 _held_slots: weapon / shield 1, gear its hands)
    Reasoned ac_adjust{};
    int16_t printed_ac = 10;
    std::vector<MonsterAttackSpec> attacks{};
};
struct MonsterDerivation {
    int8_t pb = 2; int16_t ac = 10;
    std::vector<Attack> attacks{};     // parallel to MonsterSpec::attacks
    std::vector<std::string> errors{}; // the 5.6 strings, in check order; empty = loads
};
MonsterDerivation derive_monster(const MonsterSpec&);
// ---------------------------------------------------------------- character@5 derive: true (T&C #253, checks_v4)
// Pure, integer-only mirror of checks_v4.check_character_proficiencies / check_character_derive and the action weapon checks.
// Strings are #253's, byte for byte. Order: per action the weapon checks (`/actions/<i>/...`), then proficiency errors (weapon,
// then armor, each sorted), then AC, then each weapon action (to-hit, damage). Proficiency extras are warnings.
struct CharacterActionSpec {
    std::string id;                    // action id
    std::string weapon{};              // action `weapon` (an item id); empty = no weapon key (not derived)
    bool weapon_attack_kind = true;    // kind is melee_weapon_attack / ranged_weapon_attack
    bool item_known = true;            // the item document exists (unknown: not derived, like the validator)
    WeaponDef weapon_def{};
    std::string weapon_class{};        // item weapon_class: "simple" / "martial"
    bool item_has_range = false;       // the item has a `range` (Monk Martial Arts excludes it, as checks_v4 does)
    WeaponUse use = WeaponUse::OneHand; // action `use`, default one_hand
    int8_t printed_to_hit = 0;
    DiceExpr printed_damage{0, 0, 0};  // damage[0] (dice, or {flat})
};
struct CharacterItemClass { std::string id; std::string weapon_class; };   // pack items, for "covered by simple / martial"
struct CharacterSpec {
    std::string id;
    AbilityScores abilities{};
    int8_t pb = 2;                     // authored proficiency_bonus
    std::vector<std::string> classes{};    // class ids, e.g. "monk"
    std::string race{};
    std::vector<std::string> features{};   // feature ids, e.g. "unarmored_defense"
    std::vector<std::string> weapon_proficiencies{}, armor_proficiencies{};
    std::vector<std::string> equipment{};  // every equipment[] item id (equipped or not)
    std::vector<CharacterItemClass> item_classes{};
    bool has_armor = false; ArmorPiece armor{};     // the first equipped armor
    bool has_shield = false; ShieldPiece shield{};  // the first equipped shield
    int16_t printed_ac = 10;
    std::vector<CharacterActionSpec> actions{};
};
struct CharacterDerivation {
    int16_t ac = 10;
    std::vector<Attack> attacks{};     // the derived weapon actions, in action order
    std::vector<std::string> errors{}, warnings{};
};
// FEATURE_AC_BASES (#253): (class, feature id) -> AcBaseCandidate. Barbarian unarmored_defense 10 + Dex + Con; Monk
// unarmored_defense 10 + Dex + Wis, forbids a shield; Sorcerer draconic_resilience 13 + Dex; Warlock armor_of_shadows 13 + Dex
// (Mage Armor counted as in force). nullopt = no base.
std::optional<AcBaseCandidate> feature_ac_base(std::string_view class_id, std::string_view feature_id);
CharacterDerivation derive_character(const CharacterSpec&);

// monster@5 / character@5 action `use`, spelled exactly as #252's schema enum: one_hand, two_hands, thrown, off_hand.
std::optional<WeaponUse> weapon_use_from_string(std::string_view);
std::string_view weapon_use_name(WeaponUse);
// The hands strings, byte-identical to the validator (#249 checks_v4): monster `<id>: holds N items but has H hands` (also
// emitted by derive_monster when MonsterSpec::hands >= 0), character `<id>: hand '<hand>' holds N items` (for the loader).
std::string hands_overfull_error(std::string_view id, int held, int hands);
// Hands to hold an item: ItemDef::hands, at least 2 for a two_handed weapon; nullptr (unknown def) = 1.
int hands_needed(const ItemDef*);
// EQUIPMENT.md §2.5.1: ammunition recovered at encounter end = floor(expended / 2); 0 for expended <= 0. Pure, no roll.
int recovered_ammo(int expended);
std::string hand_slot_error(std::string_view id, std::string_view hand, int count);

} // namespace rules5e
