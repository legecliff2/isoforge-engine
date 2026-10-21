#pragma once
// docs/proposals/EQUIPMENT.md 9.1: the POD item data rules reads (weapon / armor / shield / focus / light / ammunition). Engine Core
// owns the table (ItemDefTable, built from item@N) and passes a lookup in (EncounterHooks::item_def); rules5e never loads items.
#include <cstdint>
#include <string_view>
#include <vector>
#include "rules5e/armor.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

enum class ItemKind : uint8_t { Other, Weapon, Armor, Shield, Focus, Light, Ammunition };
enum class LightShape : uint8_t { Radius, Cone };   // Cone: length = bright_ft, width = length (SRD cone template)
enum AmmoKind : uint8_t { kAmmoNone = 0, kAmmoArrow = 1, kAmmoBolt = 2, kAmmoSlingBullet = 3, kAmmoNeedle = 4 };

// item@N weapon fields (EQUIPMENT.md 2). `dice` is the base die only (never the ability or magic bonus); count 0 = flat
// (blowgun {flat: 1}, a burning torch's 1 fire).
struct WeaponDef {
    std::string_view id{};             // becomes Attack::id (storage owned by the caller's table)
    uint16_t properties = 0;           // WeaponProperty bits (weapon.hpp)
    uint8_t reach_ft = 5;              // melee reach; 0 = a pure ranged weapon (no `reach` in item@N)
    uint16_t range_ft = 0, long_range_ft = 0;   // `range`; 0 = none
    DiceExpr dice{1, 4, 0};
    DiceExpr versatile_dice{0, 0, 0};  // `versatile_damage` (count 0 = none)
    DamageType type = DamageType::Bludgeoning;
    uint8_t qualifiers = 0;            // AttackQualifier bits
    int8_t magic_bonus = 0;            // 0..3, attack and damage
    int8_t attack_magic = -1, damage_magic = -1;   // item@2 split bonuses; -1 = magic_bonus
    bool martial = false;
    bool improvised = false;           // a torch, a bow swung in melee: 1d4, no proficiency
    bool natural = false;              // a monster's natural weapon (bite, claws): no dice multiplier, no Brute die
    uint8_t ammo_kind = kAmmoNone;     // `ammunition` weapons: which stack they draw from
};

struct LightDef {
    uint16_t bright_ft = 0, dim_ft = 0;   // dim is additional beyond bright (chunk@2)
    LightShape shape = LightShape::Radius;
    uint32_t duration_rounds = 0;         // 0 = no burn-down
    bool lit_by_default = false;
    uint8_t extra_states = 0;             // item@2 `states[]` (state 0 = the top-level radii)
};

struct ItemDef {
    uint32_t id = 0;                   // EC's table key (content id hash)
    ItemKind kind = ItemKind::Other;
    uint8_t hands = 1;                 // hands to hold it (worn items are not held)
    WeaponDef weapon{};                // kind Weapon, or has_weapon (a torch's improvised profile)
    bool has_weapon = false;
    bool weapon_requires_lit = false;  // torch: only while lit
    ArmorPiece armor{};                // kind Armor
    ShieldPiece shield{};              // kind Shield
    LightDef light{};                  // kind Light (also a lit torch)
    bool holy_symbol = false;          // kind Focus: can be worn or borne on a shield (EQUIPMENT.md 3.2, Q4)
    uint8_t ammo_kind = kAmmoNone;     // kind Ammunition
    int8_t ammo_magic = 0;             // +1..+3 ammunition: adds to the shot's attack and damage
};

constexpr bool is_weapon(const ItemDef& d) { return d.kind == ItemKind::Weapon || d.has_weapon; }

// How an attack uses its weapon (EQUIPMENT.md 2.1).
enum class WeaponUse : uint8_t { OneHand, TwoHands, Thrown, OffHand };

// Read-only view of one creature's equipment, as Engine Core's Loadout state supplies it (EncounterHooks::loadout_of). Rules
// reads it for the predicates (free_hands, can_attack_with, components_ok, equip_legal) and never stores or mutates it.
inline constexpr uint8_t kHeldLit = 1;      // a light (or torch) is lit
inline constexpr uint8_t kHeldEmblem = 2;   // a shield bearing a holy symbol (a focus, Q4)
inline constexpr uint8_t kHeldWorn = 4;     // a carried holy symbol worn visibly (a focus for M, not a hand)
struct HeldItem { uint32_t instance = 0; uint32_t def = 0; uint16_t count = 1; uint8_t flags = 0; };
// Combatant::attacks[i] -> the weapon it uses (def 0: natural / unarmed / not equipment-gated). Fixed order (EQUIPMENT.md 7.3):
// a stowed weapon's attack stays listed and is refused at attack time.
struct AttackLink { uint32_t def = 0; WeaponUse use = WeaponUse::OneHand; };
struct LoadoutView {
    uint8_t hand_count = 2;            // monster@5 `hands` (0 or 2)
    HeldItem main{}, off{};            // instance 0 = empty hand; a shield is held in one of them
    uint32_t armor_def = 0;            // worn armor (0 = none)
    std::vector<HeldItem> carried{};   // stowed items, stacks (ammunition counts), worn holy symbols
    std::vector<AttackLink> attacks{}; // parallel to Combatant::attacks (shorter = the rest are not gated)
    bool interaction_used = false;     // the free object interaction is spent this turn (EC's TurnBudget bookkeeping)
};

} // namespace rules5e
