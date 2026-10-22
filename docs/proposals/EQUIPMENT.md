# Proposal: equipment and items (derived AC and weapon attacks, hand slots, equip actions, monster reverse calculation, carried lights)

Status: **approved by Director 2026-10-21** (section 10); this PR is docs only (Rules, `feat/rules-equipment-proposal`, off main `7f156c2`). No rules, schema, loader or sim code changes
in this PR; the only code is the read-only audit script `tools/audit/equipment_monster_audit.py` (section 5.7). Priority from Zach
via Director. Inputs: this repo's survey (section 0) and T&C's equipment / items audit on main `7f156c2` (SRD 5.1 via 5e-bits
5e-database `a6212be`), reconciled in section 5.8.

SRD citations name the SRD 5.1 section (CC-BY-4.0): *Equipment* (Armor and Shields, Weapons, Weapon Properties, Adventuring
Gear), *Combat* (Your Turn / Other Activity on Your Turn, Actions in Combat, Making an Attack, Two-Weapon Fighting), *Spellcasting*
(Components), *Classes* (Barbarian, Monk, Sorcerer), *Spells* (Mage Armor, Barkskin, Light), *Monsters* (Statistics).

**Priority rule (Zach), unchanged: old content must play identically.** Nothing in this proposal changes a number for a file that
does not opt in (section 7), and every derived number is checked against the printed one at load (section 5.6).

## 0. Survey: what main has today

| Piece | Where | State |
|---|---|---|
| `item@1` (weapon / armor / shield / valuable) | `schemas/item/v1.schema.json`, VERSION_BUMP_BATCH section 10 | Closed union on `category`. All 50 SRD weapons, armor and the shield are authored in `srd51.core/items` (37 weapons, 12 armor, shield, plus the Diamond: 51 files) and match the SRD tables (T&C audit section 1; the blowgun's `{flat: 1}` is the one representational difference). **No** light / gear category, ammunition, unarmed strike, improvised weapon, attunement or charges. |
| AC in stat blocks | `common@1 $defs/armor_class` `{value, note}` | Baked integer plus free-text note (`"leather armor, shield"`, `"natural armor"`, `"Unarmored Defense"`, `"armor scraps"`). |
| AC in rules | `Combatant::ac` (int8), `rules5e/armor.hpp` `compute_ac(AcInput)` (row 15) | `compute_ac` implements armor + Dex (cap) + magic, unarmored / Unarmored Defense / natural armor as a max, shield, `armored_bonus`, `flat_bonus`, Str speed penalty. **Not wired at load**: the only caller is sim `features.cpp` (`ac_bonus` feature: the delta of two `compute_ac` calls added to the baked AC). The sim never loads `item@1`. Mage Armor / Barkskin are `ModStat::Ac` modifiers with `ac_floor` / `ac_floor_dex` (row 12). |
| Attacks | `Attack` (`types.hpp`): `to_hit` total, `damage` `DiceExpr` with the ability mod baked, `properties` (`weapon.hpp` bits), `range_ft` / `long_range_ft`, `qualifiers`, `extra_damage` | Baked per action in `actions[]` (`to_hit`, `damage[0].dice = "1d6+2"`); versatile and thrown uses are separate actions (`_two_handed`, `_thrown`). `attack_ability` / `weapon_disadvantage` (Heavy, Lance) and `TurnBudget::loading_fired` (Loading) are live. No link from an action to an `item@1` (only the action id). |
| Off-hand / bonus attacks | VERSION_BUMP_BATCH 7.2b `BonusAttack` (`different_weapon`, `has_damage` + `damage`) | The hand-off says "the off-hand dice without the ability modifier"; that is wrong for a negative modifier (section 2.4). |
| Use an Object | `ActionKind::UseObject`, `ActionRequest::object_use` 1..3 (7.2y, log_hash 0xCB) | The action exists; there is **no free object interaction** in `TurnBudget` and no hand state. |
| Characters | `character@4 equipment[]`: bare strings (descriptive, inert) or `{item, equipped?, value_gp?}` | 63 PCs in srd51.core, 164 item references. Used by sim only for the shield check (by id segment), thieves' tools and priced materials. |
| Monsters | `monster@4` (33) and `monster@3` (goblin), `proficiency_bonus` printed | 34 SRD monsters, CR 0..6. |
| Lights | `docs/proposals/LIGHTING.md` (R-G13), `sim/light.hpp` `AttachedLight {carrier Creature/Item/Tile, bright_ft, dim_ft, flags, state}`, `LightProviderInputs::attached_sources`; `chunk@2` static lights (#236); #233 (R&P, open): render glows from a **generic producer list** (`pc_torches` + `Session::Options::carried_lights`) | Attached lights are produced by a callback; no item drives them yet. LIGHTING section 3.4 already plans `emits_light` + `lit_by_default` in `item@2` and `monster@5`, tags 0xC7..0xCA. The Light spell is not in srd51.core. |
| Versioning | VERSION_BUMP_BATCH sections 1, 8, 9 | Bumps are one batch; `@N -> @N+1` migrations are identity plus `inert_mechanics` stamps; new state folds only when non-empty, one tag byte per section. |

Headline facts this design rests on (both audits agree, section 5.8): **38/38 armored PCs and 111/111 PC weapon to-hits already equal
the item math**, and **31/34 monsters reproduce exactly** from the structured fields below with no adjustment.

## 1. Derived AC

### 1.1 SRD rules modelled

- *Armor and Shields*: armor sets a base; Light: base + Dex; Medium: base + Dex (max 2); Heavy: base, no Dex. "If the Armor table
  shows 'Str 13' or 'Str 15' in the Strength column for an armor type, the armor reduces the wearer's speed by 10 feet unless the
  wearer has a Strength score equal to or higher than the listed score." Stealth column: "the wearer has disadvantage on Dexterity
  (Stealth) checks." Shield: "carried in one hand. Wielding a shield increases your Armor Class by 2. You can benefit from only one
  shield at a time."
- *Armor Proficiency*: "If you wear armor that you lack proficiency with, you have disadvantage on any ability check, saving throw,
  or attack roll that involves Strength or Dexterity, and you can't cast spells."
- Alternative base formulas: Barbarian *Unarmored Defense* ("While you are not wearing any armor, your Armor Class equals 10 + your
  Dexterity modifier + your Constitution modifier. You can use a shield and still gain this benefit."); Monk *Unarmored Defense*
  ("while you are wearing no armor and not wielding a shield, your AC equals 10 + your Dexterity modifier + your Wisdom modifier");
  Sorcerer *Draconic Resilience* ("when you aren't wearing armor, your AC equals 13 + your Dexterity modifier"); *Mage Armor*
  ("a willing creature who isn't wearing armor ... The target's base AC becomes 13 + its Dexterity modifier"); natural armor
  (*Monsters, Armor Class*: "natural armor" in parentheses). *Barkskin* is not a base: "the target's AC can't be less than 16,
  regardless of what kind of armor it is wearing" (a floor).

### 1.2 The stacking rule (exact)

AC is computed in five ordered steps. Every term is an integer; there is no division except the ability modifier `floor((score - 10) / 2)`.

1. **Base: pick one, highest.** Build the candidate set; each candidate has an eligibility predicate; AC base = the maximum value
   over eligible candidates (ties: any, the value is the same). Candidates:

   | Candidate | Value | Eligible when | Source |
   |---|---|---|---|
   | `unarmored` | 10 + Dex | always | *Combat, Armor Class* default |
   | `armor` | `base_ac` + Dex term (light: Dex; medium: min(Dex, `dex_cap`); heavy: 0) + armor `magic_bonus` | armor worn (then it is the **only** eligible candidate besides features that say "regardless of armor"; none in SRD 5.1) | *Armor and Shields* |
   | `natural` | `natural_armor.base` + (`natural_armor.dex` ? Dex : 0) | no armor worn | *Monsters* |
   | `unarmored_defense(ability)` | 10 + Dex + mod(ability) | no armor; for `wis` (Monk) also no shield | Barbarian (Con), Monk (Wis) |
   | `fixed_plus_dex(13)` | 13 + Dex | no armor | Draconic Resilience, Mage Armor (while the modifier is in force) |

   A negative Dex is never capped away (medium armor `min` only caps the top; this is `armor_dex_term` today).
   "Pick one" is what makes Mage Armor not stack with Unarmored Defense: both are bases, the higher wins. A creature wearing armor
   has exactly one base (the armor); Mage Armor / Unarmored Defense / Draconic Resilience / natural armor are all ineligible.
2. **Shield.** + `ac_bonus` (2) + shield `magic_bonus`, if a shield is wielded (section 3) and the base candidate allows it
   (all but Monk Unarmored Defense; a Monk wielding a shield loses that candidate in step 1, it does not lose the shield). Only one
   shield counts.
3. **Additive bonuses**, each a `(source_id, amount)` row; rows with the same `source_id` do **not** stack (highest applies,
   *Spellcasting, Combining Magical Effects* for spells; the same rule is applied to named items and features); different sources
   add. SRD rows: fighting style *Defense* (+1, `requires_armor`), *Shield of Faith* (+2), *Shield* spell (+5, reaction, until the
   start of the caster's next turn), *Haste* (+2), ring / cloak of protection (+1, magic items, not in srd51.core yet). Cover is
   **not** an AC row (it stays per attack, 7.2t).
4. **Explicit adjustment**: + `ac_adjust.value` (monsters only, section 5.5; 0 for every derivable creature).
5. **Floors**: AC = max(AC, each active floor) (Barkskin 16). Then clamp 0..100 (`compute_ac` today).

Penalties computed alongside, not part of AC: Str below `str_min` gives speed - 10 ft (never below 0); `stealth_disadvantage`;
armor without proficiency sets the non-proficiency flags above.

### 1.3 Rules API (extends `rules5e/armor.hpp`; additive)

```cpp
enum class AcBaseKind : uint8_t { Unarmored, Armor, Natural, UnarmoredDefense, FixedPlusDex };
struct AcBaseCandidate { AcBaseKind kind; int8_t fixed = 10; Ability ability = Ability::Con; bool forbids_shield = false; uint32_t source = 0; };
struct AcBonusRow { uint32_t source = 0; int8_t amount = 0; bool requires_armor = false; };
struct AcInput {            // existing fields kept; appended:
    std::vector<AcBaseCandidate> bases{};     // features / modifiers; Unarmored is implicit
    std::vector<AcBonusRow> bonuses{};        // same source => max, not sum
    int8_t ac_adjust = 0;                     // monsters only, documented (5.5)
    int8_t floor = 0;                         // max over active floors (Barkskin 16)
    bool armor_proficient = true;
};
struct AcResult {           // existing fields kept; appended:
    AcBaseKind base = AcBaseKind::Unarmored;  // which candidate won (diagnostics, AI, character sheet)
    bool shield_counted = false, nonproficient_armor = false;
};
AcResult compute_ac(const AcInput&);          // unchanged signature; old callers fill no new field => same result
```

`compute_ac` with the new fields at their defaults returns exactly what it returns today (the existing `has_alt_ability`,
`natural_base`, `armored_bonus`, `flat_bonus` stay and map onto candidates / rows internally). A unit test pins that.

**Live AC.** Equipment changes and modifiers change AC during play, so `Combatant::ac` becomes the cached output of
`recompute_ac(state, id)` (called on equip / unequip / don / doff and on `ModStat::Ac` apply / expire), not a constant. The
existing `ac_floor` / `ac_floor_dex` modifier rows map to `FixedPlusDex` candidates (Mage Armor: `ac_floor_dex`) and floors
(Barkskin) with identical results; this is a refactor test, not a behaviour change.

## 2. Weapon attacks derived from equipped weapons

### 2.1 Formula (exact)

For a weapon `W` used by wielder `C` in use mode `U` (`one_hand`, `two_hands`, `thrown`, `off_hand`):

- **Ability** (*Weapon Properties*, *Making an Attack*): ranged weapon (has `range`, no `reach`): Dex. Finesse: "you can use your
  choice of your Strength or Dexterity modifier for the attack and damage rolls. You must use the same modifier for both rolls"
  (derivation picks the higher, ties Str, as `attack_ability` does; an explicit `ability` on the entry overrides). Thrown: "If the
  weapon is a melee weapon, you use the same ability modifier for that attack roll and damage roll that you would use for a melee
  attack with the weapon" (Str, or the finesse choice). Otherwise Str.
- **To-hit** = mod(ability) + (proficient ? PB : 0) + `W.magic_bonus` (or `attack_magic` when split, 2.3) + additive attack rows
  (fighting style *Archery* +2 on ranged weapons) [+ `attack_bonus` override, monsters only, section 5.5, which *replaces* the total].
- **Damage** = weapon dice (versatile die when `U = two_hands`) x `weapon_dice_multiplier` (monsters, 5.3) + extra dice riders
  (Brute, 5.3) + **ability term** + `W.magic_bonus` (or `damage_magic`) + additive damage rows (fighting style *Dueling* +2 with
  one melee weapon in one hand and no other weapon).
- **Ability term**: mod(ability), except the **off-hand attack of two-weapon fighting**: *Two-Weapon Fighting*: "You don't add
  your ability modifier to the damage of the bonus attack, unless that modifier is negative." So `off_hand` ability term =
  min(0, mod) (the Two-Weapon Fighting style restores the full mod). 7.2b's hand-off ("dice without the ability modifier") is
  corrected by the derivation: the loader must pass the negative modifier.
- **Proficiency**: *Weapon Proficiency*: "Proficiency with a weapon allows you to add your proficiency bonus to the attack roll for
  any attack you make with that weapon." Characters: class / race `weapon_proficiencies` (simple, martial, named weapons) on
  `character@5`; monsters are proficient with every attack in their stat block (*Monsters*: the to-hit includes PB).
- **Unarmed strike** (*Melee Attacks*): to-hit Str + PB ("You are proficient with your unarmed strikes"), damage 1 + Str mod,
  bludgeoning; no hand needed. **Improvised weapon** (*Improvised Weapons*): 1d4, no PB unless proficient, thrown 20/60. A burning
  torch: "If you make a melee attack with a burning torch and hit, it deals 1 fire damage" (*Adventuring Gear, Torch*). An
  `ammunition` weapon used in melee is improvised (*Ammunition*).

### 2.2 Properties

| Property | Derived effect | Existing engine piece |
|---|---|---|
| Finesse | ability choice (above) | `is_finesse`, `attack_ability` |
| Versatile | `two_hands` use: `versatile_damage`; needs both hands (section 3) | separate `_two_handed` action today; derivation emits it only when the off hand is free |
| Two-Handed | "This weapon requires two hands when you attack with it." Holding it needs one hand; **attacking** needs both | flag only today; becomes a hand check at attack time |
| Light | eligible for the two-weapon bonus attack: "When you take the Attack action and attack with a light melee weapon that you're holding in one hand, you can use a bonus action to attack with a different light melee weapon that you're holding in the other hand ... If either weapon has the thrown property, you can throw the weapon" | 7.2b `BonusAttack` (`requires_attacks`, `different_weapon`); derivation fills it |
| Thrown | ranged attack with `range`; the weapon leaves the hand and lands on the target's tile (hit or miss) as a ground item (section 4) | `_thrown` action today |
| Ammunition | "you can use a weapon that has the ammunition property to make a ranged attack only if you have ammunition ... Each time you attack with the weapon, you expend one piece of ammunition. Drawing the ammunition ... is part of the attack (you need a free hand to load a one-handed weapon). At the end of the battle, you can recover half your expended ammunition by taking a minute to search the battlefield." | new: ammunition stacks (2.5) |
| Loading | "you can fire only one piece of ammunition from it when you use an action, bonus action, or reaction to fire it, regardless of the number of attacks you can normally make" | `TurnBudget::loading_fired` (live) |
| Reach | +5 ft reach | `reach_or_range_ft = 10` (data) |
| Heavy | "Small creatures have disadvantage on attack rolls with heavy weapons" | `weapon_disadvantage` (live) |
| Special (lance, net) | Lance: disadvantage within 5 ft, two hands unless mounted; Net: Restrained on hit, no damage | Lance live; Net is content |

### 2.3 Magic bonus slots

`item@2` keeps `magic_bonus` (0..3, *Weapon, +1, +2, or +3*: "You have a bonus to attack and damage rolls made with this magic
weapon") and appends optional `attack_magic` / `damage_magic` (0..3 each; when present they replace `magic_bonus` for that roll) so
an item with unequal bonuses can be authored without a fudge. Either implies the `magical` qualifier (the loader sets bit 1, as
today). Ammunition `magic_bonus` (*Ammunition, +1, +2, or +3*) adds to the shot's attack and damage and stacks with the
launcher's (different sources).

### 2.4 Rules API

```cpp
// rules5e/equipment.hpp (new, pure; no loader dependency)
enum class WeaponUse : uint8_t { OneHand, TwoHands, Thrown, OffHand };
struct WeaponDef {          // filled from item@2 (or a monster-local weapon, 5.4)
    uint32_t def_id; uint16_t properties; bool ranged; uint8_t reach_ft; uint16_t range_ft, long_range_ft;
    DiceExpr dice; DiceExpr versatile_dice; DamageType type; uint8_t qualifiers;
    int8_t attack_magic = 0, damage_magic = 0; bool martial = false; bool improvised = false;
};
struct WielderView { AbilityScores abilities; int8_t pb; bool proficient; int8_t attack_rows = 0, damage_rows = 0;
                     uint8_t dice_multiplier = 1; uint8_t extra_melee_dice = 0; bool twf_style = false;
                     std::optional<Ability> ability_override{}; std::optional<int8_t> to_hit_override{}; };
Attack derive_weapon_attack(const WeaponDef&, const WielderView&, WeaponUse);   // fills to_hit, damage, properties, ranges
```

`derive_weapon_attack` produces the same `Attack` struct the engine consumes today, so every downstream path (riders, Sneak Attack
`finesse`, Loading, Heavy, cover, range) is unchanged. Derived attacks are rebuilt by `rebuild_attacks(state, id)` whenever the
hand state changes (section 3); the `Combatant::attacks` order is fixed (section 7.3) so `attack_index` stays stable.

### 2.5 Ammunition

`item@2 category "ammunition"` (`ammo_kind`: `arrow`, `bolt`, `sling_bullet`, `needle`; `magic_bonus`); weapons gain
`ammo_kind`. Inventory entries gain `count` (integer). An attack with an `ammunition` weapon needs `count > 0` of a matching stack
(the first equipped matching stack, then the first unequipped one) and decrements it. Recovery of half (rounded down) is an
out-of-combat step (`recover_ammunition`, `floor(spent / 2)`), never during `Combat`. **Default when no stack is listed: unlimited**
(today's behaviour; every existing file plays the same; T&C decides when srd51.core PCs get explicit stacks).

## 3. Hand slots

### 3.1 Model

Every creature with hands has two hand slots, `main` and `off` (creatures without hands, e.g. beasts, have none and use natural
attacks only: `monster@5 hands: 0|2`, explicit; if omitted, 2 when the monster holds a weapon or shield, else 0;
more held items than hands is an error; never inferred from creature type, Q9). Held objects occupy slots:

| Object | Slots | Notes |
|---|---|---|
| one-handed weapon, versatile weapon held in one hand | 1 | |
| two-handed weapon | 1 to hold, **2 to attack** | *Two-Handed*: "requires two hands when you attack with it"; so a wizard can hold a longbow in one hand and still cast; the attack needs the other hand free at that moment (no interaction needed to grip it) |
| versatile weapon used two-handed | 2 at attack time | same treatment as two-handed |
| shield | 1 (`off` by convention) | donning / doffing it is an action (section 4) |
| torch, lamp, hand-held lantern | 1 | light producer (section 6) |
| spellcasting focus (arcane focus, holy symbol, druidic focus, component pouch) | 1 if held | *Holy Symbol*: "the cleric or paladin must hold it in hand, wear it visibly, or bear it on a shield"; worn / on a shield = 0 slots |
| ammunition | 0 | drawing is part of the attack; loading a **one-handed** ammunition weapon (hand crossbow) needs a free hand at attack time (*Ammunition*) |

`HandState { uint32_t main = 0, off = 0; }` holds item **instance** ids (section 4.4); a two-handed grip is not a stored state
(it is checked at attack time), which keeps the state small and makes "drop the off-hand item and swing two-handed" one free action.

### 3.2 Spell components

*Spellcasting, Components*: Somatic: "the caster must have free use of at least one hand to perform these gestures." Material:
"A spellcaster must have a hand free to access these components, but it can be the same hand that he or she uses to perform
somatic components." A focus replaces non-costly materials; a costly material (`cost_cp`) must be held.

Rule (exact): a spell with S needs one hand that is empty **or** holds the focus / material used for M (the shared-hand clause)
when the spell also has M; S without M needs an empty hand. M needs a hand that holds the focus / component pouch or a hand
that is empty (to reach the pouch), or a holy symbol worn / on a wielded shield (cleric, paladin). **A holy symbol on a shield is a
focus** (Director, Q4, decided): the shield hand covers S+M spells whose material is the focus (no `cost_cp` material); S-only
spells still need an empty hand. V needs nothing. **War Caster is not SRD 5.1** and is not modelled. New refusal strings:
`cast: somatic needs a free hand`, `cast: material needs a free hand or a focus`.

### 3.3 Rules API

```cpp
struct HandState { uint32_t main = 0, off = 0; };                  // item instance ids, 0 = empty
struct Loadout { HandState hands; uint32_t armor = 0, shield = 0;  // shield is also one of the hands
                 std::vector<ItemStack> carried; uint8_t hand_count = 2; };
int free_hands(const EncounterState&, CombatantId);
bool can_attack_with(const EncounterState&, CombatantId, uint32_t instance, WeaponUse);   // two-handed / versatile / loading
bool components_ok(const EncounterState&, CombatantId, const SpellDef&);
```

`Loadout` lives in a new `EncounterState::loadouts` vector keyed by combatant (empty for every existing archetype, section 7).

## 4. Equip, unequip, drop, pick up: action economy

### 4.1 SRD rules

- *Other Activity on Your Turn*: "You can also interact with one object or feature of the environment for free, during either your
  move or your action ... If you want to interact with a second object, you need to use your action." Examples given: draw or
  sheathe a sword, pick up a dropped axe, take a bauble from a table, hand an item to another character.
- *Use an Object*: "When an object requires your action for its use, you take the Use an Object action. This action is also useful
  when you want to interact with more than one object on your turn."
- *Donning and Doffing Armor* table: Light 1 minute / 1 minute; Medium 5 minutes / 1 minute; Heavy 10 minutes / 5 minutes; Shield
  1 action / 1 action.
- Dropping an item: **not in SRD 5.1 text**. Ruling (Director, Q1, decided): free, not an interaction.

### 4.2 Operations

`ActionRequest::equip_op` (appended, default 0) on `ActionKind::UseObject` or as a free interaction attached to any turn step:

| `equip_op` | Meaning | Cost |
|---|---|---|
| 1 `draw` | move a carried item into a free hand (`hand` 1 main / 2 off) | the free interaction, else Use an Object (action), else refused |
| 2 `stow` | move a held item to carried | free interaction, else Use an Object |
| 3 `drop` | held item to the ground at the actor's tile (ground item, 4.4) | free, no interaction, any time on own turn (also during a move) |
| 4 `pick_up` | ground item within 5 ft into a free hand | free interaction, else Use an Object |
| 5 `don_shield` / 6 `doff_shield` | shield to or from the off hand, AC recomputed | an action (*Donning and Doffing Armor*: 1 action) |
| 7 `hand_over` | held item to a willing creature within 5 ft with a free hand | free interaction (*Other Activity* example) |

`TurnBudget` gains `bool object_interaction = true` (one free interaction per turn, reset with the turn). Refusals (exact):
`equip: no free hand`, `equip: no object interaction left`, `equip: the item is not carried`, `equip: the item is out of reach`, `equip: unknown ground item`,
`equip: armor cannot be changed in combat`.

Attacking with a weapon not in hand is refused (`attack: the weapon is not in hand`), except thrown weapons: **drawing a thrown
weapon is part of each attack** (Director, Q2, decided), so Extra Attack can throw two javelins without spending the free
interaction; the draw needs a free hand (or the hand the previous throw emptied) and a carried weapon of that kind.

### 4.3 Armor timing (integer rounds)

1 minute = 10 rounds. Don / doff in rounds: Light 10 / 10, Medium 50 / 10, Heavy 100 / 50, Shield: one action. A `Combat` refuses
armor don / doff (`equip: armor cannot be changed in combat`), because the shortest change (10 rounds) exceeds any action; the
exploration layer runs it as a timed task (`armor_task {op, rounds_left}`, integer, decremented per exploration round). **A helper
halves the time** (Director, Q6, decided): `rounds / 2` (all table values are even). Sleeping in armor is not SRD and is out.

### 4.4 Item instances

Equipment state needs identities: `ItemInstance { uint32_t id; uint32_t def; uint16_t count; uint8_t flags; }` (flags: lit,
hooded, magical) allocated by sim from a monotonic `next_item_id` in setup order (combatant order, then `equipment[]` order). A
ground item is `GroundItem { uint32_t instance; Vec2i tile; }`. Thrown weapons and dropped items become ground items; death drops
**nothing** automatically (the corpse keeps its loadout), and a held light source stays lit where it lies: it follows #233 (carrier
stays `Item`, `holder_id` 0, tile = the owner's tile; `Carrier::Tile` is for unowned objects only). Basic ground items (drop, pick up, a dropped torch stays lit) are in scope now (Director,
Q7, decided); a full looting UI comes later.

## 5. Monsters: reverse calculation

### 5.1 Goal

A monster file names its gear and its traits; the loader derives AC, to-hit and damage; the printed numbers stay in the file as
**assertions**. If anything cannot be derived, the file carries an explicit, documented adjustment with a reason string. Never a
silent fudge.

### 5.2 Proficiency bonus

SRD 5.1 stat blocks do not print a proficiency bonus; the repo's `proficiency_bonus` field is authored. The standard CR table (as
used by 5e; SRD 5.1 gives XP by CR only, and the PB line appears in later SRD versions) is pinned as data: CR 0..4 = +2, 5..8 = +3,
9..12 = +4, 13..16 = +5, 17..20 = +6, 21..24 = +7, 25..28 = +8, 29..30 = +9. `proficiency_bonus` becomes optional at `monster@5`
(default: the table); when present it must equal the table unless `proficiency_bonus_reason` is given. All 34 monsters match the
table today.

### 5.3 Structured fields (no adjustments)

```json
"equipment": [ {"item": "srd51.item.hide_armor", "equipped": true}, {"item": "srd51.item.shield", "equipped": true},
               {"item": "srd51.item.javelin", "count": 3} ],
"natural_armor": { "base": 13, "dex": true },              // AC candidate 'natural' (1.2); base = printed - Dex - shield
"weapon_dice_multiplier": 2,                               // integer, default 1; weapon (item or local) dice count x N
"armor_class": { "value": 15, "note": "natural armor, shield" },   // kept: the assertion
"actions": [
  { "id": "greatclub", "weapon": "srd51.item.greatclub", "to_hit": 6, "damage": [{"dice": "2d8+4", "type": "bludgeoning"}] },
  { "id": "bite", "kind": "melee_weapon_attack", "natural": {"dice": "2d4", "ability": "dex"}, "to_hit": 4,
    "damage": [{"dice": "2d4+2", "type": "piercing"}] }
]
```

- `natural_armor` (11 SRD monsters): the SRD prints only the total; the base is authored once. `dex` defaults to `true` (the
  monster's Dex is included, which reproduces all 11).
- `weapon_dice_multiplier` (Ogre: 2). SRD 5.1 has **no rules text** for size-scaled weapon dice (it is a stat-block convention);
  it is an explicit integer on the monster, never inferred from `size` by the engine. The audit suggests it when Large+ dice fit.
- Damage-die rider (Bugbear *Brute*: "A melee weapon deals one extra die of its damage when the bugbear hits with it (included in
  the attack)"): a **monster@5-local** trait kind (common@2 stays unchanged) `{"kind": "weapon_extra_dice", "dice": 1, "melee_only": true}` read by the derivation
  (`WielderView::extra_melee_dice`); thrown uses are ranged and do not get it (the javelin's 1d6+2 thrown line).
- Natural attacks (`natural {dice, ability, damage_ability?}`): dice are authored (they are not derivable); `ability` is explicit
  (`str` or `dex`; the Wolf, Giant Spider, Snake, Swarm and Medusa's snake hair use Dex). `damage_ability: "none"` for blocks
  that print no modifier (Gelatinous Cube *Pseudopod* 3d6).

### 5.4 Monster-local weapons

A stat-block weapon with no SRD item (Lizardfolk *Heavy Club* 1d6, *Spiked Shield* 1d6; Grimlock *Spiked Bone Club* 1d4 + 1d4
rider) is a `weapons[]` entry on the monster with the `item@2` weapon fields (`weapon_class` optional) and a local id
(`"weapon": "#heavy_club"`). It derives exactly like an item weapon. Mapping the Lizardfolk's Heavy Club to the SRD Club (1d4)
is wrong (T&C's audit shows that mismatch); the SRD text is a distinct weapon.

### 5.5 Explicit adjustments (documented, never silent)

```json
"ac_adjust": { "value": 1, "reason": "SRD Skeleton 'armor scraps': 13 = 10 + Dex 2 + 1; no item@1 armor" },
"actions": [ { "id": "bite", "natural": {"dice": "1d6", "ability": "str"},
               "attack_bonus": { "value": 3, "reason": "SRD Black Bear prints +3; Str 15 + PB 2 = +4" }, ... } ]
```

- `ac_adjust {value: integer, reason: string (minLength 1)}`: added in step 4 of 1.2.
- `attack_bonus {value: integer, reason}`: **overrides the total to-hit** (not a delta), so the printed number is the value.
- No `damage_adjust` (T&C review): none of the 34 needs one, and fields are hard to remove later. Added only when a monster needs it.
- Validator: every adjustment needs a non-empty `reason`; a warning lists every adjusted monster (so the set stays visible);
  an adjustment that is not needed (derivation already matches) is an **error**.

### 5.6 Load-time assertion

At `monster@5` with `derive: true`, the loader derives AC and each derived action and requires equality with the printed
`armor_class.value`, `to_hit` and `damage[0].dice`. The validator runs the same check with the same strings (one shared case file
keeps the C++ loader and the Python validator in sync). A file that fails cannot load, so a derivation bug can never change a fight
silently. Exact strings (`<id>` = the content id, `<action>` = the action id; to-hit signed, dice as `NdM+K` / `NdM-K` / `NdM` /
a bare flat integer), produced by `rules5e::derive_monster` (hands strings also via `hands_overfull_error` / `hand_slot_error`):

| Case | String |
|---|---|
| AC mismatch | `<id>: derived AC 14 != printed 15` |
| to-hit mismatch | `<id>.<action>: derived to-hit +4 != printed +3` |
| damage mismatch | `<id>.<action>: derived damage 1d8+4 != printed 2d8+4`, with the suffix ` (weapon_dice_multiplier 2 would match)` when a multiplier 2..4 reproduces it |
| adjustment not needed | `<id>: ac_adjust is not needed`, `<id>.<action>: attack_bonus is not needed` |
| structure not needed | `<id>: weapon_dice_multiplier is not needed` (every weapon derives with 1), `<id>: natural_armor is not needed` (armor is worn, or 10 + Dex already matches) |
| missing reason | `<id>: ac_adjust needs a reason`, `<id>.<action>: attack_bonus needs a reason` |
| PB off the CR table | `<id>: proficiency_bonus 3 != CR table 2` (unless `proficiency_bonus_reason` is given) |
| hands (monster@5) | `<id>: holds 3 items but has 2 hands` (equipped hand slots: weapon / shield 1, gear its `hands`; plural kept: `holds 1 items but has 0 hands`) |
| hands (character@5) | `<id>: hand 'main' holds 2 items` (`'main'` / `'off'`, N = the count) |

Reasons (`ac_adjust`, `attack_bonus`, `proficiency_bonus_reason`) have schema `minLength 1` (#249), so a missing or empty reason
fails in the schema; only a whitespace-only reason reaches the `needs a reason` string (and a whitespace-only
`proficiency_bonus_reason` counts as absent). The hands strings match #249's `checks_v4` byte for byte; rules exposes them as
`hands_overfull_error` / `hand_slot_error`.

**Second damage components stay authored and unchecked in v1** (T&C review): Basilisk 2d6 poison, Black Dragon Wyrmling 1d4 acid,
Giant Toad 1d10 poison, Medusa Snake Hair 4d6 and Longbow 2d6 poison, Grimlock 1d4 piercing. Only `damage[0]` is derived and
asserted; `damage[1..]` load as written.

### 5.7 Audit script and results

`tools/audit/equipment_monster_audit.py` (read-only; `--markdown` prints the table; no gate yet) applies sections 1.2, 2.1 and
5.2..5.5 to every monster in `content/packs/srd51.core/monsters`. Monster-local weapons and the Large dice multiplier are
recognised by the script's tables (they become authored fields in `monster@5`).

**Headline: 34 monsters; 31 reproduce exactly from structured fields; 3 need explicit adjustments (4 fields): Skeleton
`ac_adjust` +1, Black Bear Bite and Claws `attack_bonus` +3, Ghoul Bite `attack_bonus` +2.** Of the 31 clean: 22 AC from items /
10 + Dex, 11 need `natural_armor`, Bugbear needs *Brute* (`weapon_extra_dice`), Ogre needs `weapon_dice_multiplier` 2, Lizardfolk
and Grimlock need monster-local weapons, the Gelatinous Cube needs `damage_ability: none`.


### 5.8 Reconciliation with T&C's audit (main `7f156c2`)

| Point | T&C audit | This script | Resolution |
|---|---|---|---|
| AC derivable | 22 exact, 12 "not derivable" (11 natural armor, Skeleton) | 33 derive (22 + 11 with `natural_armor`), Skeleton `ac_adjust` +1 | Same facts: natural armor needs an authored base (T&C's bases match: Animated Armor 18, Basilisk 13, Black Bear 11, Wyrmling 15, Boar 11, Dire Wolf 12, Gargoyle 15, Giant Spider 11, Lizardfolk 13, Medusa 13, Wolf 11). With the field they derive; Skeleton is the one AC adjustment (10 + Dex + 1, not an item, as T&C recommends). |
| Weapon attacks | 38 resolved to items: 35 match, 3 mismatch (Bugbear x2 Brute, Lizardfolk Heavy Club as Club) | 37 item weapons all derive with Brute and the multiplier; Heavy Club, Spiked Shield and Spiked Bone Club are monster-local | T&C applies the Large x2 rule implicitly; here it is an explicit `weapon_dice_multiplier` (no SRD 5.1 rules text). Heavy Club is a local weapon (1d6), not the Club. |
| Natural attacks | 25; to-hit fits Str or Dex + PB except Black Bear Bite / Claws (+3) and Ghoul Bite (+2) | Same 3; the rest derive with an explicit `ability` | The 3 get `attack_bonus` overrides with reasons. The Ghoul's Bite damage (2d6+2) fits Dex; its to-hit does not (+4 derived). |
| Gelatinous Cube *Pseudopod* | counted natural (no flag) | prints 3d6 with no Str mod | `damage_ability: "none"` (structured, not an adjustment). |
| PB | all match the CR table | all match | `proficiency_bonus` optional at @5, default from the table. |
| PCs | 38/38 armored AC, 111/111 weapon to-hits match | re-checked here: 38/38, 111/111 | PC derivation is a no-op for srd51.core (7.4). |

Full table (generated by `python3 tools/audit/equipment_monster_audit.py --markdown`, main `7f156c2`):

| Monster | CR | PB | AC | AC derivation | Attacks (source, ability) | Result |
|---|---|---|---|---|---|---|
| animated_armor | 1 | 2 | 18 | natural_armor.base 18 + Dex | slam: natural, str | clean |
| axe_beak | 1/4 | 2 | 11 | unarmored 10 + Dex | beak: natural, str | clean |
| bandit | 1/8 | 2 | 12 | leather_armor (11 + Dex) | scimitar: item scimitar, dex; light_crossbow: item crossbow_light, dex | clean |
| basilisk | 3 | 2 | 12 | natural_armor.base 13 + Dex | bite: natural, str | clean |
| berserker | 2 | 2 | 13 | hide_armor (12 + Dex max 2) | greataxe: item greataxe, str | clean |
| black_bear | 1/2 | 2 | 11 | natural_armor.base 11 + Dex | bite: natural, str; claws: natural, str | bite attack_bonus override +3 (derived +4); claws attack_bonus override +3 (derived +4) |
| black_dragon_wyrmling | 2 | 2 | 17 | natural_armor.base 15 + Dex | bite: natural, str | clean |
| boar | 1/4 | 2 | 11 | natural_armor.base 11 + Dex | tusk: natural, str | clean |
| bugbear | 1 | 2 | 16 | hide_armor (12 + Dex max 2) + shield | morningstar: item morningstar, str [trait brute: +1 weapon die in melee]; javelin: item javelin, str [trait brute: +1 weapon die in melee]; javelin_thrown: item javelin (thrown), str | clean |
| commoner | 0 | 2 | 10 | unarmored 10 + Dex | club: item club, str | clean |
| dire_wolf | 1 | 2 | 14 | natural_armor.base 12 + Dex | bite: natural, str | clean |
| gargoyle | 2 | 2 | 15 | natural_armor.base 15 + Dex | bite: natural, str; claws: natural, str | clean |
| gelatinous_cube | 2 | 2 | 6 | unarmored 10 + Dex | pseudopod: natural, str, damage_ability none | clean |
| ghoul | 1 | 2 | 12 | unarmored 10 + Dex | bite: natural, dex; claws: natural, dex | bite attack_bonus override +2 (derived +4) |
| giant_poisonous_snake | 1/4 | 2 | 14 | unarmored 10 + Dex | bite: natural, dex | clean |
| giant_spider | 1 | 2 | 14 | natural_armor.base 11 + Dex | bite: natural, dex | clean |
| giant_toad | 1 | 2 | 11 | unarmored 10 + Dex | bite: natural, str | clean |
| gnoll | 1/2 | 2 | 15 | hide_armor (12 + Dex max 2) + shield | bite: natural, str; spear: item spear, str; spear_two_handed: item spear (versatile), str; spear_thrown: item spear (thrown), str; longbow: item longbow, dex | clean |
| goblin | 1/4 | 2 | 15 | leather_armor (11 + Dex) + shield | scimitar: item scimitar, dex; shortbow: item shortbow, dex | clean |
| grimlock | 1/4 | 2 | 11 | unarmored 10 + Dex | spiked_bone_club: local, str | clean |
| guard | 1/8 | 2 | 16 | chain_shirt (13 + Dex max 2) + shield | spear: item spear, str; spear_two_handed: item spear (versatile), str; spear_thrown: item spear (thrown), str | clean |
| hobgoblin | 1/2 | 2 | 18 | chain_mail (16) + shield | longsword: item longsword, str; longsword_two_handed: item longsword (versatile), str; longbow: item longbow, dex | clean |
| jackal | 0 | 2 | 12 | unarmored 10 + Dex | bite: natural, str | clean |
| kobold | 1/8 | 2 | 12 | unarmored 10 + Dex | dagger: item dagger, dex; sling: item sling, dex | clean |
| lizardfolk | 1/2 | 2 | 15 | natural_armor.base 13 + Dex + shield | bite: natural, str; heavy_club: local, str; javelin: item javelin, str; javelin_thrown: item javelin (thrown), str; spiked_shield: local, str | clean |
| medusa | 6 | 3 | 15 | natural_armor.base 13 + Dex | snake_hair: natural, dex; shortsword: item shortsword, dex; longbow: item longbow, dex | clean |
| ogre | 2 | 2 | 11 | hide_armor (12 + Dex max 2) | greatclub: item greatclub, str [weapon_dice_multiplier 2 (large)]; javelin: item javelin, str [weapon_dice_multiplier 2 (large)]; javelin_thrown: item javelin (thrown), str [weapon_dice_multiplier 2 (large)] | clean |
| orc | 1/2 | 2 | 13 | hide_armor (12 + Dex max 2) | greataxe: item greataxe, str; javelin: item javelin, str; javelin_thrown: item javelin (thrown), str | clean |
| scout | 1/2 | 2 | 13 | leather_armor (11 + Dex) | shortsword: item shortsword, dex; longbow: item longbow, dex | clean |
| skeleton | 1/4 | 2 | 13 | unarmored 10 + Dex + ac_adjust | shortsword: item shortsword, dex; shortbow: item shortbow, dex | ac_adjust +1 (armor scraps: no item@1 armor (SRD prints 13 = 10 + Dex 2 + 1)) |
| spy | 1 | 2 | 12 | unarmored 10 + Dex | shortsword: item shortsword, dex; hand_crossbow: item crossbow_hand, dex | clean |
| swarm_of_rats | 1/4 | 2 | 10 | unarmored 10 + Dex | bites: natural, dex | clean |
| wolf | 1/4 | 2 | 13 | natural_armor.base 11 + Dex | bite: natural, dex | clean |
| zombie | 1/4 | 2 | 8 | unarmored 10 + Dex | slam: natural, str | clean |

34 monsters: 31 reproduce cleanly, 3 need explicit adjustments (4 adjustment fields)

## 6. Lights: equipment is one producer

### 6.1 Principle

Light attachment stays **generic** (LIGHTING section 3.4, #233's producer list). The light model knows only `AttachedLight`
(carrier Creature / Item / Tile, radii, flags, state). Producers write rows; the light model never asks *why* a light exists:

| Producer | Carrier | Lifetime |
|---|---|---|
| **Equipment** (this proposal): a held or worn item whose `item@2` carries a `light` block and is lit | `Item` (`holder_id` = the creature holding it) | while lit and fuelled; dropped => still `Item`, `holder_id` 0, the owner's tile (#233); picked up => `holder_id` = the new holder, **same light id** |
| Light spell: any object, held, worn or lying on a tile | `Item` for an item instance, `Tile` for an unowned object, `Creature` never (SRD: an object) | 600 rounds (1 hour), recast ends the previous, dismissal is an action |
| Daylight, Produce Flame, Dancing Lights, glowing monsters (`monster@5 emits_light`) | as LIGHTING | as LIGHTING |
| `pc_torches`, `Session::Options::carried_lights` (#233, vertical slice) | Creature | session option; to be replaced by equipment once torches are items (no behaviour change for the slice: the option stays until T&C ships torch items on the slice PC) |

The Light spell does **not** depend on `item@2 light`: it targets an object (*Light*: "You touch one object that is no larger than
10 feet in any dimension. Until the spell ends, the object sheds bright light in a 20-foot radius and dim light for an additional
20 feet ... If you target an object held or worn by a hostile creature, that creature must succeed on a Dexterity saving throw to
avoid the spell."), so a sword, a rock or a shield can carry it. Its row is `spell_id`-tagged and independent of any equipment
light on the same item (two rows, composition takes the brighter, LIGHTING section 4).

### 6.2 `item@2` light block

```json
{ "$schema": "item@2", "id": "srd51.item.lantern_hooded", "name": "Lantern, hooded", "category": "gear",
  "cost_cp": 500, "weight_oz": 32, "hands": 1,
  "light": { "bright_ft": 30, "dim_ft": 30, "shape": "radius",
             "duration_rounds": 3600, "fuel": "srd51.item.oil_flask",
             "lit_by_default": false,
             "states": [ {"id": "hooded", "bright_ft": 0, "dim_ft": 5, "toggle": "action"} ] } }
```

- `bright_ft`, `dim_ft` (dim is **additional** beyond bright, as chunk@2; sum <= 120), `shape` `radius | cone` (a cone's length is
  `bright_ft`, its width equals its length, the SRD cone template; bullseye 60 + 60; facing per Q8), `duration_rounds`
  (integer; torch / candle 600, lamp / lantern 3600 per flask), `fuel` (an item id consumed to refill to `duration_rounds`;
  absent for torches and candles, which are consumed), `lit_by_default`, `states[]` (only the *other* states: state 0 is the
  top-level radii, `states[i]` is `AttachedLight::state` i + 1; hooded lantern per LIGHTING R6, "As an action, you can lower the hood, reducing the light to dim light in a 5-foot radius").
- `torch_attack`: a torch also carries an improvised melee profile in the existing `item_damage` shape (as the blowgun):
  `{"damage": [{"flat": 1, "type": "fire"}], "requires_lit": true}`
  (*Torch*: "1 fire damage").
- SRD light gear to author (T&C): torch (20 / 20, 1 hour), candle (5 / 5, 1 hour), lamp (15 / 30, 6 hours per flask), hooded
  lantern (30 / 30; hooded dim 5), bullseye lantern (cone 60 / 60), oil flask (fuel, plus its own splash / throw effect, later).
- Burn-down is counted in rounds during combat and in exploration rounds outside it, integer; reaching 0 clears `kLit` and emits
  `LightChanged` (LIGHTING section 8). **Lighting takes an action** (*Adventuring Gear, Tinderbox*: "Using it to light a torch--or anything
  else with abundant, exposed fuel--takes an action. Lighting any other fire takes 1 minute."): torch, candle, lamp and lanterns,
  with a tinderbox carried or a lit source adjacent (Q8); raising or lowering the hooded lantern's hood is an action (SRD text above).

### 6.3 Hand interaction

A held light occupies a hand (section 3.1); stowing it in a pack douses nothing but moves the light carrier to the creature with
the light blocked (*Light*: "Completely covering the object with something opaque blocks the light"): the row stays, flag `lit`
cleared while stowed, restored when drawn (no new id). Dropping keeps it lit where it lies (#233: carrier `Item`, `holder_id` 0).

## 7. Versioning and migration

### 7.1 One bump, shared with lighting phase 2

LIGHTING already plans `item@2` and `monster@5` (`emits_light`). This proposal joins that bump rather than adding another:

| Schema | Adds (all optional) |
|---|---|
| `item@2` | categories `gear` (with `light`, `hands`), `ammunition` (`ammo_kind`, `magic_bonus`); weapon `ammo_kind`, `attack_magic`, `damage_magic`, `improvised`; `unarmed` is an engine built-in, not an item |
| `monster@5` | `derive`, `equipment[]` (as character@4 plus `count`), `natural_armor`, `weapon_dice_multiplier`, `weapons[]` (local), action `weapon` / `natural`, `ac_adjust`, `attack_bonus`, `hands`, `proficiency_bonus` optional; LIGHTING's `emits_light` |
| `character@5` | `derive`, equipment `hand` (`main`/`off`) and `count`, `weapon_proficiencies` / `armor_proficiencies` (or derived from class data, Q5) |
| `common@2` | unchanged (no shared def moves) |

### 7.2 Migration

- `item@1 -> item@2`: identity (every `@1` field means the same).
- `monster@4 -> monster@5`, `character@4 -> character@5`: identity, **`derive` absent (= false)**. With `derive` false the loader
  uses `armor_class.value`, `to_hit` and `damage` exactly as written, ignores `equipment[]` for AC and attacks (it is still used for
  the shield id check, thieves' tools and priced materials as today) and registers **no** loadout. `@<=4` files can never set
  `derive`.
- With `derive: true` the printed numbers become assertions (5.6); a file only derives if it reproduces its own printed numbers,
  so turning `derive` on for srd51.core changes no number (38/38, 111/111, 34/34 with the fields above).
- Hand state, the free interaction, ammunition counts and ground items exist only for creatures with a registered loadout (`derive`
  true **and** the encounter opts into equipment play, `CombatOptions::equipment`, default off). So even a derived srd51.core PC
  plays exactly as today until a scenario enables equipment play. `CombatOptions::equipment` becomes default-on **only** after
  T&C's character / monster migrations land with every golden byte-identical (Director, Q3, decided); until then it stays off.

### 7.3 Attack order

Derived attacks must land in `Combatant::attacks` in the order the printed `actions[]` list them (versatile and thrown uses keep
their separate entries), so `attack_index`, multiattack sequences and logged requests are unchanged. Extra derived uses (an
off-hand attack) are appended after the printed ones.

### 7.4 Goldens that must stay byte-identical

All of them; specifically (`NEVER update these` cases and pinned digests):

- `engine/rules/tests/golden_tests.cpp` (dice, d20 modes, initiative, attacks, damage);
- `engine/core/sim/tests/compat_fl1_tests.cpp`, `compat_fl1_goblin_tests.cpp` (frozen `tests/compat/fl1`, never edited);
- `compat_defenses_tests.cpp` (all three cases, including the `@3 -> @4` migrations);
- `casting_tests.cpp` caster golden; `multiattack_content_tests.cpp` multiattack golden; `footprint_tests.cpp` (Large, Huge /
  Gargantuan); `events_tests.cpp` locked event digests; `world_tests.cpp` v0 and cross-chunk goldens;
- the vertical slice `log_hash` / `Combat::hash` (`game/vertical_slice` tests) and #233's light tests;
- `tests/goldens/stream/procgen_gen1.txt`, `sample_layered_gen1.txt`; detmath goldens;
- validator and effect cases (`tests/validate`), unchanged expected strings.

Plus one new compat test per bump PR: load every srd51.core PC and monster with `derive` forced on and assert identical
`Combatant` AC / attacks to the baked load (the audit, as a test).

## 8. Determinism and hashing

- Integer math only: ability modifiers by floor division, dice as `DiceExpr` (count x multiplier, integer), durations in rounds,
  weights in ounces, costs in copper. No division anywhere else; `floor(spent / 2)` for ammunition recovery (out of combat).
- RNG: no new draws. Derivation and equip operations are deterministic; the only roll added is the Light spell's existing Dex save
  path (spell machinery).
- **Tags 0xD0..0xD2 are claimed** in `docs/SIM_EVENTS.md` (Director-approved; status pending until the sim folds them):

| Tag | Stream | Contents | Folded when |
|---|---|---|---|
| 0xD0 | `Combat::hash` | `loadout_digest`: `next_item_id`, then per loadout in combatant order: hands (u32 x2), armor, shield, each stack (instance u32, def u32, count u16, flags u8) | any loadout registered |
| 0xD1 | `log_hash` | `equip_op` byte, item instance u32, `hand` byte, target tile i32 x2 (pick up / hand over) | `equip_op != 0` |
| 0xD2 | `Combat::hash` | ground items (instance u32, tile i32 x2) in instance order | any ground item exists or has existed |

  Light rows produced by equipment stay under LIGHTING's 0xC7 (no new light tag). `TurnBudget::object_interaction` is not hashed
  (TurnBudget is not, 9.3).

## 9. Boundary and implementation order

### 9.1 Boundary (agreed with Engine Core)

**Rules owns predicates and math only:**
- `compute_ac` (signature unchanged; every new `AcInput` field defaults to today's result), `derive_weapon_attack`;
- `free_hands`, `can_attack_with`, `components_ok`; `equip_legal(state, req)` returning `Free` / `Interaction` / `Action` /
  `Refused` plus the exact refusal string; `legal_actions` entries for equip ops, appended after the existing entries;
- the POD `rules5e::ItemDef` (weapon / armor / shield / light data) in a rules header; EC's `ItemDefTable` stores them and passes a
  lookup in;
- the monster derivation (`derive_monster`: PB table, natural armor, local weapons, multiplier, Brute, adjustments, the 5.6 strings).

**Engine Core owns state and mechanics:** `CombatOptions` (Rules does not add it), `Loadout` / `ItemInstance` / `GroundItem` state,
`recompute_derived`, `apply_equip`, ground-item entities, the free-interaction bookkeeping, thrown weapons landing, ammunition
decrement, and the 0xD0..0xD2 folds. Rules builds **no** ground-item or loadout state.

**Rules decisions** (recorded for EC):
- A stowed weapon's attack **stays listed** in `Combatant::attacks` (fixed order, 7.3) and is refused at attack time
  (`attack: the weapon is not in hand`).
- Drop is free **on the actor's own turn only**.
- Ammunition: **unlimited when there is no stack** (monsters permanently, Q10).
- `recompute_derived` triggers (EC calls it on each): an equip change (draw, stow, drop, pick up, don / doff shield, hand over);
  an AC effect starting or ending (`ModStat::Ac` modifiers: Mage Armor, Barkskin, Shield, Shield of Faith, Haste); a change to Str,
  Dex, Con or Wis; a proficiency or level change.

### 9.1a Rules API as implemented (`feat/rules-equipment-core`) and hand-off

Headers: `rules5e/armor.hpp` (extended), `rules5e/item.hpp` (new, POD data + the read-only loadout view), `rules5e/equipment.hpp`
(new, functions). No state, no hash tag, no schema; goldens unchanged (every check is gated on `EncounterHooks::loadout_of`).

| API | For | Notes |
|---|---|---|
| `compute_ac(AcInput)` | EC `recompute_derived` | Signature unchanged. Appended `AcInput::bases` (`AcBaseCandidate {kind Natural / UnarmoredDefense / FixedPlusDex, fixed, ability, natural_dex, forbids_shield}`), `bonuses` (`AcBonusRow {source, amount, requires_armor}`, same source = highest), `ac_adjust`, `floor`, `armor_proficient`; `AcResult::base`, `shield_counted`, `nonproficient_armor`. Defaults reproduce the old result (test). |
| `ItemDef`, `WeaponDef`, `LightDef`, `ItemKind`, `AmmoKind` | EC `ItemDefTable` | POD filled from item@N; EC owns storage (`WeaponDef::id` is a `string_view` into it). |
| `LoadoutView {hand_count, main, off, armor_def, carried[], attacks[], interaction_used}`, `HeldItem {instance, def, count, flags kHeldLit / kHeldEmblem / kHeldWorn}`, `AttackLink {def, use}` | EC | What EC's `Loadout` state exposes to rules. `attacks[i]` maps `Combatant::attacks[i]` to its weapon (def 0 = not gated). |
| `EncounterHooks::loadout_of(state, id) -> const LoadoutView*`, `item_def(def) -> const ItemDef*`, `apply_equip(state, req, cost_kind, events)`, `ground_item_def(instance) -> const ItemDef*` | EC | Empty / nullptr = legacy (no check, no equip legal action). `ground_item_def` resolves a ground item **instance** id (PickUp's `equip_item`) to its def; empty or nullptr refuses PickUp with `equip: unknown ground item`. |
| `derive_weapon_attack(WeaponDef, WielderView, WeaponUse) -> {Attack, error}`, `weapon_ability`, `unarmed_strike` | EC `recompute_derived`, loader | `WielderView {abilities, pb, proficient, attack_rows, damage_rows, dice_multiplier, extra_melee_dice, twf_style, ability?, damage_ability_none, to_hit_override?, ammo_magic}`. Errors: `weapon: a two-handed weapon needs both hands`, `weapon: not a versatile weapon`, `weapon: not a thrown weapon`, `weapon: the off-hand weapon must be a light melee weapon`. |
| `free_hands`, `can_attack_with`, `attack_refusal(state, id, attack_index)` | step (wired), EC, AI | `attack: the weapon is not in hand`, `attack: needs both hands`, `attack: loading needs a free hand`, `attack: no ammunition`, `attack: nothing to throw`, `attack: the torch is not lit`. Thrown: drawn per attack (held, or carried with a free hand). No ammunition stack = unlimited. |
| `components_ok`, `components_refusal(state, id, SpellDef)` | step (wired, after `can_provide_components`) | `cast: somatic needs a free hand`, `cast: material needs a free hand or a focus`; a held focus or a shield with `kHeldEmblem` covers S+M with a non-priced material; a worn holy symbol (`kHeldWorn`) covers M only. |
| `equip_legal(state, req) -> EquipCost {Free / Interaction / Action / Refused, refusal}`, `EquipOp` | step (wired), EC | `ActionRequest` appended `equip_op, equip_item, equip_hand, equip_to, equip_in_reach` (on `UseObject`). Interaction falls back to Action (Use an Object) once `interaction_used`. `equip_item` is always an instance id: held / carried instances resolve via `LoadoutView` then `item_def(HeldItem::def)`, PickUp via `ground_item_def`. Hands needed = `ItemDef::hands`, at least 2 for a `two_handed` weapon; applies to Draw, PickUp, DonShield and the HandOver recipient. Refusals: `equip: equipment play is off`, `equip: unknown equip op`, `equip: not your turn`, `equip: the creature cannot act`, `equip: armor cannot be changed in combat`, `equip: no object interaction left`, `equip: the item is already in hand`, `equip: the item is not carried`, `equip: the item is not in hand`, `equip: use don_shield for a shield`, `equip: use doff_shield for a shield`, `equip: no free hand`, `equip: the item is already carried`, `equip: the item is out of reach`, `equip: not a shield`, `equip: a shield is already wielded`, `equip: no action left`, `equip: bad recipient`, `equip: the recipient has no free hand`. In step: Refused fails with the string; no `apply_equip` hook fails `equip: equipment play is off`; Action cost spends the action, then `apply_equip` runs (Interaction / Free bookkeeping is EC's). |
| `equip_legal_actions(state)` | `legal_actions` (wired) | Draw, Stow, Drop, DonShield, DoffShield for the current actor, order op / instance / hand, appended after every existing entry. PickUp / HandOver need positions: EC lists them. |
| `armor_change_rounds(category, don, helper)` | EC exploration layer | 10 / 10, 50 / 10, 100 / 50 rounds; a helper halves. |
| `derive_monster(MonsterSpec) -> {pb, ac, attacks, errors}`, `proficiency_for_cr`, `format_dice`, `hands_overfull_error(id, held, hands)`, `hand_slot_error(id, hand, n)` | EC loader, T&C validator (same strings) | `MonsterSpec` / `MonsterAttackSpec` carry the 5.3..5.5 fields plus `hands` (-1 = absent) and `held`; `brute_dice` is the monster@5-local `weapon_extra_dice` trait. `errors` are the 5.6 strings in check order (PB, hands, AC, attacks); whitespace-only reasons count as missing. |

### 9.2 Sequencing (T&C review)

1. **Rules:** the derivation and its messages (5.6) and the predicates (9.1).
2. **Engine Core (`engine/core/assets`):** `kItemSchemaVersion` 2, `kMonsterSchemaVersion` 5, `kCharacterSchemaVersion` 5 and identity
   migrations (`derive` absent); then the loader derivation and assertions behind `derive`, the forced-derive compat test (7.4),
   `CombatOptions`, loadouts, `apply_equip` and the 0xD0..0xD2 folds.
3. **T&C:** the `item@2` / `monster@5` / `character@5` schemas and the validator (the loader's strings; the audit script becomes the
   check), then content in this order: light gear (torch, candle, lamp, hooded and bullseye lanterns, oil flask, tinderbox), then
   the 34 monsters at `monster@5`, then the PCs at `character@5` (proficiencies, ammunition stacks).
4. **Engine Core + R&P:** equipment as a light producer. **Lights render only after #244**; before that, torch items validate but
   shed no light.

## 10. Open questions

Director answered Q1..Q4, Q6, Q7 (2026-10-21) and approved the three monster adjustments with their reasons (5.5, 5.7) and tags
0xD0..0xD2 (8). T&C answered Q5, Q8..Q12 in the #245 review.

**Director (decided)**

- Q1. Dropping an item: **free**, not an interaction (4.1, 4.2).
- Q2. Thrown weapons: **drawn as part of each attack**; Extra Attack can throw two javelins (4.2).
- Q3. `CombatOptions::equipment`: default-on **only** once T&C's character / monster migrations land and the goldens are
  byte-identical; off until then (7.2).
- Q4. A holy symbol on a shield **counts as a focus**: the shield hand covers S+M spells whose material is the focus; S-only spells
  still need a free hand (3.2).
- Q6. A helper **halves** don / doff time; sleeping in armor is not SRD and is out (4.3).
- Q7. Basic ground items (drop, pick up, a dropped torch stays lit) are **in scope now**; a full looting UI later (4.4).
- Monster adjustments (Skeleton `ac_adjust` +1, Black Bear Bite / Claws `attack_bonus` +3, Ghoul Bite `attack_bonus` +2) and their
  reasons: **approved**. Tags 0xD0..0xD2: **approved**, claimed.

**T&C (answered in the #245 review)**

- Q5. Proficiencies are **authored** on `character@5` (`weapon_proficiencies` / `armor_proficiencies`) and cross-checked by the
  validator against the class mirror (5e-database `5e-SRD-Classes.json`, plus race weapon training): a missing SRD-granted
  proficiency is an error, an extra one is a warning (a documented override). Class documents become the default later.
- Q8. Lighting a torch, candle, lamp or lantern takes **an action** with a tinderbox or an adjacent flame (*Tinderbox*); the hood is
  an action. The bullseye cone uses the SRD cone template (width = length). Facing is an integer 8-direction value on the light row,
  set when lit, defaulting to the holder's last move direction, else toward its current target; hashed with the row (0xC7).
- Q9. `monster@5` declares `hands` explicitly; if omitted, 2 when it holds a weapon or shield, else 0. Equipped held items needing
  more hands than declared is an error. **Never inferred from creature type.**
- Q10. PC ammunition uses the SRD starting counts (20 arrows, 20 bolts, 20 sling bullets, 50 blowgun needles) in the later
  PC-derivation PR; monsters stay **unlimited permanently** (an explicit `count` is opt-in).
- Q11. The three reason strings are **verified** against 5e-database `a6212be`. The Ghoul's reads "SRD Ghoul Bite prints +2;
  damage 2d6 + 2 fits Dex 15 (+2), Dex + PB 2 = +4 (Str + PB = +3)".
- Q12. `weapon_dice_multiplier` stays **explicit per monster**; the damage-mismatch error hints at it (5.6) and the audit suggests it.
