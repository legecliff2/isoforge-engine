# Proposal: SRD 5.1 magic items (metadata, attunement, passive modifiers, consumables, charges, spell-casting items, riders)

Status: **draft for Tech Director review, before any code.** This is docs only (branch `docs/magic-items-proposal`, off main
`76c219e`): no schema, loader, rules, sim or content changes. It follows the structure of `docs/proposals/EQUIPMENT.md`
(approved 2026-10-21), and every mechanic below builds on that proposal's item@2, loadout and `CombatOptions::equipment` model.
Mechanics marked **[Lich]** need Lich (Rules) to own the predicate or math, per the EQUIPMENT 9.1 boundary: Rules owns predicates
and math, Engine Core owns state.

**Scope: SRD 5.1 magic items only.** The source is 5e-bits 5e-database `a6212be`, `src/2014/en/5e-SRD-Magic-Items.json`
(CC-BY-4.0): **362 entries, 239 base items and 123 variants** (`variant: true`, e.g. `potion-of-healing-greater`). Rules text
is cited from the same commit's `5e-SRD-Rule-Sections.json` (*Attunement*, *Wearing and Wielding Items*, *Activating an Item*,
*Sentient Magic Items*). Non-SRD items (DMG-only shields +N, setting items) are out of scope.

**Priority rule (Zach), unchanged: old content must play identically.** No srd51.core character or monster references a magic
item today (survey 0). Every mechanic here is reachable only through a registered loadout (`derive: true` plus
`CombatOptions::equipment`, EQUIPMENT 7.2). Every existing golden therefore stays byte-identical by construction (section 11).

**No approximation.** An SRD property that a phase does not model exactly is **not** half-modelled. Either:
- the item is not authored yet; or
- it is authored with that property named in a file-level `inert_mechanics` list (the existing VERSION_BUMP_BATCH section 8
  convention). The validator then requires every unmodelled property to be named, and `docs/SRD_MAGIC_ITEMS_DEFERRED.md`
  (new, phase M0) records it.

## 0. Survey: what main has today

| Piece | Where | State |
|---|---|---|
| `magic_bonus` 0..3 | `item@2` weapon / armor / shield / ammunition; `attack_magic` / `damage_magic` (EQUIPMENT 2.3) | It is live in validator AC math (`checks_v4.py` `armor.magic_bonus`, `shield.magic_bonus`) and in derived attacks; it implies the `magical` qualifier bit. This is the **only** magic-item mechanic on main. |
| Rarity, attunement, charges, activation | none | No field in any schema. |
| Item categories | `item@2` closed enum: weapon, armor, shield, valuable, gear, ammunition | There is no ring / rod / staff / wand / potion / scroll / wondrous category. |
| Modifiers | `rules5e::ModStat` (`types.hpp` 315): AttackRoll, Save, AbilityCheck, Ac, Speed, Damage, TempHp, Resistance, ... HeldDie, Outlined, BonusDash, ... | Spell and feature modifiers. These are the natural vocabulary for passive item effects. |
| Rests | `rules5e/rest.hpp`, `sim/rest.hpp`: `uses` per long rest, short / long rest results | There is no "dawn" clock. Per-long-rest uses already exist for features. |
| Spells in srd51.core | 79 `spell@1` files | They include magic_missile, fireball, lightning_bolt, web, hold_person, haste, invisibility, heroism, bless, cure_wounds, lesser_restoration, command, protection_from_energy, magic_weapon. **No** hold_monster, wall_of_fire, mass_cure_wounds, polymorph, cone_of_cold, etc. |
| Ground items / loadout | EQUIPMENT 4.4; tags 0xD0..0xD2 (`SIM_EVENTS.md`) | Item instances exist (`instance u32`, `def u32`, `count u16`, `flags u8`), so per-instance state (charges, attunement) has somewhere to live. |
| Highest claimed hash tag | `docs/SIM_EVENTS.md` | 0xD5 (Heat Metal). |
| Magic items in content | srd51.core characters / monsters | **None** (grep of `characters/*.json`: no `+1`, potion, wand or ring item). |
| Sentient items | SRD *Sentient Magic Items* | Rules exist, but **no SRD 5.1 item is sentient** in the 362 entries. |

**Survey totals** (base entries, from the appendix):
- **Attunement:** 125 of 239 require it. 26 have a prerequisite:
  - class: 13 (bard / cleric / druid / paladin / sorcerer / warlock / wizard lists);
  - "a spellcaster": 10;
  - alignment: 2 (Talismans of Pure Good / Ultimate Evil);
  - race: 1 (Dwarven Thrower, "a dwarf");
  - condition: 1 (Ring of Shooting Stars, "outdoors at night").
- **Charges:** 48 have charges.
- **Dawn recharge or once-per-dawn use:** 67.

## 1. Grouping

Each base item has exactly **one primary group**: the hardest mechanic it needs, which decides its phase. Attunement (group A) is
cross-cutting and is counted separately. The full per-item table is the appendix.

| Group | What it needs | Base entries | Files incl. variants |
|---|---|---|---|
| G1 Plain +N | `magic_bonus` only | 3 | 9 (templates; see 2.3) |
| G2 Static numeric modifiers | AC / save / check bonus, resistance / immunity, ability-score override while worn | 38 | 74 |
| G2b Static senses, movement, immunities | darkvision, fly / climb / swim speed, advantage on Stealth, etc. | 21 | 21 |
| G3 Weapon and armor riders | extra damage by creature type, crit riders, activated weapon powers | 26 | 26 |
| G4 Consumables | potions, oils, scrolls, special ammunition, dusts / beads / tokens, manuals and tomes | 39 | 70 |
| G5 Charges, activation, spell-casting | wands, staffs, rods, charged rings and wondrous items | 65 | 72 |
| G6 Cursed and artifact | curses, the Deck of Many Things, the Orb of Dragonkind | 7 | 7 |
| G7 Summons, prisons, planar | figurines, elemental controllers, bottles, gates, the Sphere of Annihilation | 21 | 40 |
| G8 Wondrous utility (no combat effect) | extradimensional storage, boats, ropes, vehicles, illusions | 19 | 22 |
| **Total** | | **239** | **361** |

(361 files and not 362: ioun stones, dragon scale mail, etc. count their variants, and each template base counts once.)

## 2. Cross-cutting schema: the `magic` block (additive `item@2`)

### 2.1 Fields

An optional `magic` object, allowed on **every** `item@2` category arm. It is closed and descriptive, and no rules or sim code
reads it before phase M2:

| Field | Type | SRD source |
|---|---|---|
| `rarity` | enum `common`, `uncommon`, `rare`, `very_rare`, `legendary`, `artifact` | `rarity.name`. The `varies` rarity appears only on bases whose variants carry the real value, so variants are authored instead. |
| `item_type` | enum `armor`, `potion`, `ring`, `rod`, `scroll`, `staff`, `wand`, `weapon`, `wondrous` | `equipment_category` (ammunition maps to `weapon`) |
| `attunement` | `{ "required": true, "by"?: { "classes"?: [class id], "spellcaster"?: true, "alignment"?: "good" \| "evil", "races"?: [race id], "condition"?: "outdoors_at_night" } }` | first desc line "(requires attunement by ...)" |
| `worn_slot` | enum `armor`, `shield`, `cloak`, `head`, `feet`, `hands`, `wrists`, `neck`, `ring`, `belt`, `held`, `carried` | *Wearing and Wielding Items* |
| `paired` | bool | *Paired Items* (boots, bracers, gauntlets, gloves) |
| `variant_of` | item id | 5e-database `variants` |

- **Why additive item@2 and not item@3:** the block is inert metadata, the same pattern as `focus` (#292) and `tool_kind`
  (local `content/equipment-tools`).
- **New category `magic_item`:** `item@2` gains it (enum widening plus one new closed arm) for rings, rods, staffs, wands,
  potions, scrolls and wondrous items. They are not gear, because they carry `magic` and later `effects`, and keeping them off the
  gear arm keeps the gear allow-list small. **Open question Q1:** a single `magic_item` category vs. reusing `gear`.
- Magic weapons, armor and ammunition stay in their existing categories with `magic` added.
- The **Staff** items that are also quarterstaffs (Staff of Power, Striking, the Magi, Thunder and Lightning, Withering, the
  Woodlands, Charming, Fire, Frost, Healing, Swarming Insects) are `weapon` with `magic.item_type: staff`. The SRD says each "can
  be wielded as a magic quarterstaff" where it does; the per-item text decides.

### 2.2 Validator

- Mirror check: `rarity`, `attunement` and its prerequisite against 5e-database `a6212be` (the same pattern as T&C's audit).
  Class ids must resolve to class documents, else they are kept as strings with a warning until classes land.
- `paired` must be true exactly for SRD pairs.
- The tests extend `test_union_arms.py` (new arm) and `test_srd51_equipment.py` (a `MAGIC_ITEMS` table keyed by id).

### 2.3 Templates: "Weapon (any sword)", "Armor (medium or heavy, but not hide)"

The SRD base row is a **template**: *Weapon, +1* applies to any weapon; *Flame Tongue* to any sword; *Mithral Armor* to medium or
heavy armor except hide.

Proposal:
- Template documents (`template: true`) carry an `applies_to` filter:
  - `category`;
  - `weapon_group` from the SRD name: `sword` = greatsword, longsword, rapier, scimitar, shortsword; `axe` = battleaxe,
    greataxe, handaxe; `any_slashing_sword`;
  - `armor_weight`;
  - `exclude` ids.
- A loadout entry names both: `{ "item": "srd51.item.weapon_1", "base": "srd51.item.longsword" }`. That is a `character@5`
  equipment field, additive, so it belongs in the EQUIPMENT bump and needs **no** new version.
- **[Lich]** `resolve_template(template, base) -> rules5e::ItemDef` merges base stats and template overrides. It is a pure
  function. Example: Mithral sets `str_min` 0 and `stealth_disadvantage` false.
- The alternative is expanding every combination as concrete files: 37 weapons × 3 = 111, 12 armor × 3 = 36, 4 ammunition × 3 =
  12, plus each sword or axe rider × its bases. That is roughly 400 files of duplication. **Recommended: templates.**
  **Open question Q2.**

## 3. Attunement (group A, cross-cutting; 125 items) [Lich]

### 3.1 SRD rules modelled (exact)

- **Unattuned:** a creature not attuned to an item that requires it "gains only its nonmagical benefits" (*Attunement*). A magic
  shield is a normal shield and a magic sword is a normal sword. `resolve_item(def, attuned)` returns the nonmagical def when
  `attuned` is false.
- **Limit:** at most 3 attuned items per creature, and at most one copy of a given item id. "Any attempt to attune to a fourth
  item fails". The engine refuses with the exact string; it does not evict an item.
- **Prerequisites:**
  - class: membership. A monster qualifies for a spellcasting class "if it has spell slots and uses that class's spell list".
  - spellcaster: "can cast at least one spell using its traits or features, not using a magic item".
  - alignment: the creature's alignment.
  - race: the race id.
  - "Outdoors at night": a light / time state the engine does not have. It is **deferred**, so Ring of Shooting Stars is not
    authored before it exists.
- **Timing:** attuning takes a short rest and ending it takes another. Attunement is therefore **never changed in combat**. It is
  authored on the loadout (`attuned: true`) and validated at load (prerequisites, the 3 cap and duplicates are load errors). The
  out-of-combat attune action belongs to a later campaign layer and is not in this proposal.
- **Endings:** death, prerequisites lost, or 24 h beyond 100 ft. In combat only death applies, which is already the end of the
  creature's loadout use. Cursed items cannot be voluntarily un-attuned (G6, deferred).

### 3.2 Schema

`character@5` / `monster@5` equipment entries add `attuned?: bool`. This is additive to the EQUIPMENT bump with no new version.
Item `magic.attunement` is described in 2.1.

### 3.3 Rules / sim touchpoints

| Owner | Piece |
|---|---|
| Rules | `attunement_ok(creature_view, item_def) -> {Ok, Refused + string}`, `attuned_count`, `resolve_item(def, attuned)` |
| Engine Core | `ItemInstance::attuned_to` (creature id, 0 = none); folded in the item-state digest (section 11) |
| Validator | prerequisites, the 3 cap, duplicate copies, `attuned` on an item without `attunement.required` (error) |

### 3.4 Tests

- A 4th attunement is refused with the exact string.
- A duplicate Ring of Protection is refused.
- A Staff of the Woodlands on a non-druid is refused.
- A Talisman of Pure Good on a neutral creature is refused.
- Unattuned Cloak of Protection: AC and saves unchanged.
- Unattuned magic shield: AC = shield 2, no bonus.
- Monster class-spell-list qualification (Q4).

## 4. G1 Plain +N (3 entries, 9 variant files): supported today

- **Items:**
  - `weapon-1..3` (*Weapon, +1, +2, or +3*);
  - `armor-1..3` (*Armor, +1, +2, or +3*, "light, medium, or heavy");
  - `ammunition-1..3` (*Ammunition, +1, +2, or +3*).
- **Schema:**
  - existing `magic_bonus` (EQUIPMENT 2.3), plus the `magic` block and the template (2.3);
  - none require attunement;
  - ammunition: "Once it hits a target, the ammunition is no longer magical", which is a per-hit consumption already covered by
    EQUIPMENT ammunition decrement. Recovered magic ammunition is a follow-up and is **deferred**.
- **Rules / sim:** none new beyond template resolution. `compute_ac` and `derive_weapon_attack` already add `magic_bonus`.
- **Not in SRD 5.1:** a *Shield, +1/+2/+3* entry (DMG only). The shield arm keeps `magic_bonus`, but no SRD content uses it.
  **Open question Q3:** keep the field reserved or document it as non-SRD.
- **Determinism:** no new state; derived numbers only.
- **Tests:**
  - a template expands to the exact base + N for every eligible base;
  - Longsword +2 to-hit and damage equal the item math;
  - ammunition +1 stacks with a +1 launcher (different sources, EQUIPMENT 2.3);
  - armor +3 AC; an ineligible base (a template filter miss) is a load error.

## 5. G2 Static numeric modifiers (38 entries, 74 files) [Lich for ability overrides]

Effects that hold "while you wear / hold / have it on your person" (and are attuned where required).

### 5.1 Schema: `effects[]` (item@3; see 11.1)

```json
"effects": [
  { "while": "worn", "kind": "bonus", "stat": "ac", "value": 1 },
  { "while": "worn", "kind": "bonus", "stat": "save", "value": 1 }
]
```

| `kind` | Fields | SRD examples | Maps to |
|---|---|---|---|
| `bonus` | `stat`: `ac`, `save`, `check`, `attack`, `damage`, `spell_attack`, `spell_save_dc`; `value` int −5..+5; optional `ability`, `when` predicate | Cloak / Ring of Protection (+1 AC, +1 saves); Stone of Good Luck (+1 checks, saves); Bracers of Archery (+2 damage, ranged weapon, longbow / shortbow); Robe of the Archmagi (+2 spell attack and DC) | `ModStat::Ac`, `Save`, `AbilityCheck`, `Damage`; spell DC / attack is **new [Lich]** |
| `resistance` / `immunity` | `damage_types[]` | Ring / Armor of Resistance (one type each), Brooch of Shielding (force), Periapt of Proof against Poison (poison immunity) | `ModStat::Resistance`; immunity is **new** |
| `condition_immunity` | `conditions[]` | Periapt of Proof against Poison (poisoned), Ring of Free Action (paralyzed / restrained: **partial**, the rest is deferred) | existing condition gate |
| `spell_immunity` | `spells[]` | Brooch of Shielding ("immunity to damage from the magic missile spell") | **new [Lich]**: a damage source filter by spell id |
| `ability_set` | `ability`, `score`, `max?` | Belts of Giant Strength (21..29), Gauntlets of Ogre Power (Str 19), Headband of Intellect (Int 19), Amulet of Health (Con 19): "no effect if already ≥". Ioun Stones of Strength, etc. ("+2, maximum 20"): `ability_add` with `max` | **new [Lich]**; see 5.2 |
| `when` predicates | `no_armor`, `no_shield`, `vs_spells`, `ranged_weapon_attack`, `wielding:[ids]` | Bracers of Defense (+2 AC "if you are wearing no armor and using no shield"), Mantle of Spell Resistance (advantage on saves vs spells) | `no_armor` / `no_shield` are **[Lich]** `compute_ac` inputs; `vs_spells` needs a save-source tag **[Lich]** |

Armor riders in this group are template overrides and carry no `effects`:
- Adamantine: crits against the wearer become normal hits. **[Lich]**, a new `AcInput`-adjacent flag on the defender.
- Mithral: `str_min` 0, no Stealth disadvantage.
- Elven Chain: +1 and "considered proficient". The proficiency override is **[Lich]**.
- Glamoured Studded Leather: +1. The appearance change is `inert_mechanics`.
- Dwarven Plate: +2. The reduced forced movement is **deferred** to FORCED_MOVEMENT.
- Dragon Scale Mail: +1, resistance (variant), and advantage on saves vs Frightful Presence (**[Lich]** source tag). Locate
  dragons is `inert_mechanics`.

### 5.2 Ability overrides: the hard part [Lich]

Today a PC's ability modifiers are baked into `to_hit`, `damage`, save bonuses and DCs. An override (Belt of Giant Strength Str 21)
must propagate to:
- attacks (Str weapons);
- Str saves, checks, grapple / shove;
- carrying capacity (inert).

This is only possible on the **derive path** (EQUIPMENT 7.2, `derive: true`).

**Proposal:** `derive_creature` takes `effective_abilities = apply_ability_effects(base, effects)`:
- `set` = max(base, score);
- `add` = min(base + n, max).

All derived numbers then follow. For `derive: false` files, any `ability_set` item is a **load error** ("ability override needs
derive"), never a silent no-op. Constitution changes (Amulet of Health) change max HP. The SRD does not say to recompute it here,
and the rule for Con changes is the general one (HP max changes with Con modifier) → **Open question Q5**.

### 5.3 Rules / sim touchpoints

- `compute_ac` gains `item_ac_bonus` (sum of active `bonus ac` rows) and `unarmored_only_bonus` (Bracers). The defaults keep
  today's result.
- `save_bonus`, `check_bonus`, `resistances` / `immunities` are built from effects at loadout registration into the existing
  `Combatant` modifier rows, as `ModifierSpec` rows with source `item` and a new `ModSource::Item` enum value (appended).
- Stacking:
  - Different items stack (Cloak + Ring of Protection = +2, the SRD gives no exclusion).
  - The same item id cannot be attuned twice (3.1).
  - Two non-attunement copies of the same id: "the same kind" worn-slot rule (one cloak, one pair of boots, ...). This is
    enforced by `worn_slot` at load. Rings have **no SRD cap** → **Open question Q6** (10 fingers, 2 by convention).

### 5.4 Determinism

- Effects are static for a loadout. They are folded in `loadout_digest` (0xD0) by item def id, which is already there, so no new
  tag is needed for G2.
- No RNG.

### 5.5 Tests

- Cloak + Ring of Protection: AC +2, every save +2.
- Bracers of Defense with a shield: no bonus.
- Belt of Hill Giant Strength (Str 21) on a Str 16 fighter: to-hit and damage recomputed; on Str 22: unchanged.
- An ability override on `derive: false` is a load error.
- Brooch: magic missile damage 0, force from another source halved.
- Ioun Stone of Strength at Str 19: Str 20, not 21.

### 5.6 Deferred inside G2 (explicitly)

- Ring of Regeneration and Periapt of Wound Closure: out-of-combat HP over hours; death-save stabilization is **[Lich]** later.
- Boots of Speed: a bonus-action toggle, doubled speed, OA disadvantage, 10 min per long rest. This is an activation, so it moves
  to G5 mechanics.
- Cloak of Displacement: disadvantage on attacks against the wearer until hit, reset at turn start. **[Lich]**, its own state.
- Necklace of Adaptation and Ring of Warmth: environment.
- Amulet of Proof against Detection: divination.
- Armor of Invulnerability (nonmagical damage resistance, plus a once-per-dawn immunity action).
- Plate Armor of Etherealness (Etherealness).
- Belt of Dwarvenkind (Con +2, max 20, darkvision, Dwarvish).
- Ioun Stones of Absorption / Greater Absorption (spell absorption), Mastery (+1 PB), Regeneration, Reserve, Sustenance,
  Awareness. Only Agility, Fortitude, Insight, Intellect, Leadership, Strength (`ability_add`) and Protection (+1 AC) are phase
  M3.
- Spellguard Shield (advantage on saves vs spells and on attacks vs you by spells; `vs_spells`).
- Arrow-Catching Shield (+2 AC vs ranged, plus a reaction redirect).
- Robe of the Archmagi (the base-AC-15 path). Its spell DC +2 is in M3, the rest later.
- Ring of Feather Falling and Ring of Mind Shielding: no combat number.

## 6. G2b Static senses, movement, immunities (21 entries)

Darkvision (Goggles of Night), flying, climbing and swimming speeds (Winged Boots, Wings of Flying, Slippers of Spider Climbing,
Ring of Swimming, Cloak of the Manta Ray), Stealth / Perception advantage (Boots and Cloak of Elvenkind, Eyes of the Eagle),
x-ray vision, invisibility (Ring of Invisibility, a bonus-action toggle), and Gloves of Missile Snaring (a reaction).

- **Schema:** `effects[]` kinds `sense` (`darkvision_ft`), `speed` (`mode`: fly / climb / swim, `value_ft` or `equal_walk`),
  `check_advantage` (`skill`).
- **Touchpoints:**
  - vision: #194 sight inputs (LIGHTING section 3);
  - movement modes: not on main;
  - `check_advantage` maps to `ModStat::AbilityCheck` with an advantage flag **[Lich]**.
- **Phase:** M7, after vision and movement modes exist. Until then these items are **not authored**: an authored Winged Boots
  without flight would be an approximation.

## 7. G3 Weapon and armor riders (26 entries) [Lich]

- **Schema** (item@3 `effects[]` on the weapon template):
  - `on_hit_damage`: `dice`, `damage_type`, `vs_types?` (creature types), `save?` {ability, dc}, `condition_on_fail?`;
  - `on_crit`: `extra_dice`, `kind`: `extra_damage`, `sever`, `wound`;
  - `activated` (bonus action ignite: Flame Tongue; light rows under LIGHTING 0xC7);
  - `on_crit_temp_hp` (Sword of Life Stealing).
- **M6a (about 12, exact with existing machinery):**
  - Giant Slayer: +2d6 vs giants and a Str save vs prone. *giant* is a creature type, but the SRD's "giant" here means type
    giant.
  - Dragon Slayer: +3d6 vs dragons.
  - Vicious Weapon: +7 on a natural 20.
  - Mace of Smiting: +7 vs constructs, a further 2d6 on a natural 20, destroying a construct at 25 HP or fewer. The destruction
    clause is **[Lich]**.
  - Mace of Disruption: 2d6 radiant vs fiends / undead, plus a Wis save vs destroyed / frightened.
  - Sword of Life Stealing.
  - Sun Blade: a radiant blade, +1d8 vs undead, light; bonus-action blade.
  - Javelin of Lightning (once per dawn: a 5 ft × 120 ft line).
  - Dagger of Venom (once per dawn, DC 15 Con, 2d10 poison, poisoned).
  - Frost Brand (+1d6 cold, fire resistance) and Flame Tongue (+2d6 fire, light).
- **M6b deferred (about 14):**
  - Vorpal and Sharpness (sever: Rules lacks dismemberment);
  - Holy Avenger (aura);
  - Defender (a per-turn bonus transfer to AC);
  - Dancing Sword (an autonomous weapon);
  - Dwarven Thrower (return);
  - Oathbow (sworn enemy);
  - Scimitar of Speed (a bonus-action attack: may be M6a after Q8);
  - Luck Blade (Wish);
  - Nine Lives Stealer (charges and an instant kill);
  - Hammer of Thunderbolts;
  - Mace of Terror;
  - Trident of Fish Command;
  - Animated Shield.
- **Determinism:**
  - Rider dice use the combat RNG in the existing damage order: base, magic, then riders in `effects[]` order, which is pinned.
  - Once-per-dawn uses are per-instance state (section 11).
- **Tests:**
  - Giant Slayer vs an ogre (giant) adds 2d6 and a save; vs a goblin it adds nothing.
  - Vicious on a natural 20 adds +7, not on a 19.
  - Flame Tongue ignite emits light row 0xC7 and the damage.
  - Fixed seeds: identical rolls.

## 8. G4 Consumables (39 entries, 70 files) [Lich for scrolls]

*Activating an Item → Consumables*: "Once used, a consumable item loses its magic."

### 8.1 Schema (item@3)

```json
"consumable": { "activation": "action" | "bonus_action", "by": "self" | "touch" | "thrown",
                "on_use": <effect>, "uses": 1 }
```

- `on_use` is one of:
  - `heal` {dice};
  - `apply_spell_effect` {spell id, `no_concentration`: bool, `duration_rounds`};
  - `modifier` (an `effects[]` row with `duration_rounds`);
  - `cast_spell` (scrolls).
- Durations convert to integer rounds: 1 minute = 10, 1 hour = 600.

### 8.2 Items by sub-phase

- **M4a, exact with existing machinery (17 files):**
  - Potion of Healing ×4 (2d4+2, 4d4+4, 8d4+8, 10d4+20);
  - Potion of Resistance ×10 (5e-database variants fix the type, so there is no random draw);
  - Potion of Heroism (10 temp HP + `bless`, no concentration, 600 rounds);
  - Potion of Speed (`haste`, no concentration, 10 rounds);
  - Potion of Invisibility (600 rounds, ends on attack or cast; the existing `invisibility` end hook).
  - Drinking is an action (SRD *Potions*: "Drinking a potion or administering a potion to another character requires an
    action"), using `ActionKind::UseObject` (exists, 7.2y) with the free interaction **not** used.
- **M4b (6 files, after M3 ability overrides):** Potion of Giant Strength ×6, with a timed `ability_set`.
- **M4c, spell scrolls (10 variants) [Lich]:**
  - A scroll instance names its spell: `{ "item": "srd51.item.spell_scroll_3rd", "spell": "srd51.spell.fireball" }`.
  - Rules: the spell must be on the reader's class list, else the scroll is unusable. Above the caster's level, an ability check DC
    10 + spell level; "on a failed check, the spell disappears from the scroll with no other effect".
  - Save DC and attack bonus come from the scroll table (13 / +5 for cantrip to 2nd ... 19 / +11 for 9th).
  - Needs class spell lists (Q9). Only scrolls of authored spells are allowed.
- **M4d (about 6):**
  - Necklace of Fireballs (detach a bead and throw: `fireball` at 3rd level, +1 level per extra bead);
  - Oil of Sharpness (+3 for 1 hour on a slashing or piercing weapon or 5 ammunition);
  - Bead of Force;
  - Dust of Sneezing and Choking;
  - Arrow of Slaying (creature-type keyed, DC 17 Con, 6d10 piercing);
  - Restorative Ointment.
- **Deferred:**
  - **Manuals and tomes (6):** permanent ability increase over 48 hours of study, out of combat. Campaign layer.
  - Potion of Flying, Climbing, Diminution and Growth (`enlarge_reduce` is not authored), Gaseous Form, Clairvoyance, Mind
    Reading, Water Breathing and Animal Friendship: spells or modes missing.
  - Potion of Poison: disguised identity plus a variable duration.
  - Philter of Love.
  - Oil of Etherealness and Oil of Slipperiness.
  - Dust of Disappearance and Dust of Dryness.
  - Feather Tokens (6): mixed summons and utility.
  - Bag of Beans, Marvelous Pigments, Universal Solvent, Sovereign Glue.

### 8.3 Determinism and tests

- Healing dice use the combat RNG at use time.
- The consumed flag decrements the stack count (0xD0, as ammunition).
- No new log tag beyond the activation tag (section 11).
- **Tests:**
  - Potion of Healing: exact healing per seed.
  - Potion of Speed grants haste with no concentration for 10 rounds. Whether haste's end-of-effect lethargy applies is **Q10**:
    the SRD grants "the effect of the haste spell", so the lethargy probably applies.
  - A scroll off-list is refused; the higher-level check DC.

## 9. G5 Charges, activation and spell-casting (65 entries, 72 files) [Lich]

### 9.1 SRD rules modelled

- *Charges*: an item has N charges; activating expends a stated number; "regains 1d6 + 1 expended charges daily at dawn"
  (wands) or other dice (1d3, 1d4 + 1, 1d6 + 4, 1d8 + 2, 2d8 + 4, 1d20; appendix).
- Last-charge destruction: "roll a d20. On a 1, the wand crumbles into ashes and is destroyed" (wands; staffs vary).
- *Spells* from items: cast "at the lowest possible spell level, doesn't expend any of the user's spell slots, and requires no
  components", normal casting time, concentration applies. Some items use the item's fixed DC and others "your own spellcasting
  ability" (staffs). Upcasting by extra charges (Wand of Magic Missiles: +1 level per extra charge; Fireballs and Lightning Bolts
  likewise).

### 9.2 Schema (item@3)

```json
"charges": { "max": 7, "recharge": { "when": "dawn", "dice": "1d6+1" }, "on_last": { "d20_destroy_on": 1 } },
"activations": [
  { "id": "magic_missile", "action": "action", "cost": 1, "max_cost": 7,
    "cast": { "spell": "srd51.spell.magic_missile", "level_per_extra_charge": 1,
              "dc": "item" | "wielder", "dc_value?": 15, "attack_bonus?": 7 } }
]
```

Per-dawn uses without charges (Javelin of Lightning, Dagger of Venom, Helm of Telepathy) are `{ "uses": 1, "recharge": {"when":
"dawn"} }`.

### 9.3 Dawn vs. rest [Lich] (Q11)

The engine has short and long rests, not a clock. **Proposal:**
- "Daily at dawn" recharges at the **end of a long rest**, the same hook as per-long-rest feature uses (`rest.hpp`), with a
  documented equivalence and **no** in-combat dawn.
- This is an explicit modelling choice for the Director. The alternative is a campaign clock, which is deferred.
- Recharge dice roll on the **rest RNG stream** (whatever `sim/rest.hpp` uses), never the combat stream, so combat goldens never
  see a recharge.

### 9.4 Touchpoints

| Owner | Piece |
|---|---|
| Rules | `activation_legal(item_state, activation, budget)` (charges ≥ cost, attuned if required, hand / holding per *Wearing and Wielding*: "A weapon must be held", wands / staffs / rods are held); `cast_from_item` → the existing spell resolution with `slot_level` from charges and `components: none`, the DC source, and **no slot spend**; `recharge_roll` |
| Engine Core | `ItemInstance::charges` (u8), `uses_left` (u8), destroyed → instance removed (an `ItemMoved` reason "destroyed", appended); `ActionRequest::item_activation {instance, activation index, charges}` |
| Validator | each `cast.spell` resolves to an authored spell; `max_cost ≤ charges.max`; the dice parse |

### 9.5 Items by sub-phase

- **M5a (7 files, exact with authored spells):**
  - Wand of Magic Missiles (7 charges, `magic_missile`, no attunement);
  - Wand of Fireballs (`fireball`, DC 15, attunement by a spellcaster);
  - Wand of Lightning Bolts (`lightning_bolt`, DC 15);
  - Wand of Web (`web`, DC 15);
  - Wand of the War Mage +1/+2/+3 (spell attack bonus `bonus spell_attack`, plus ignoring half cover: **[Lich]**, the cover
    predicate).
- **M5b (about 10, after Lich adds non-spell activations):**
  - Wand of Fear (`command` "flee" or "grovel" + a 60 ft cone, frightened, DC 15);
  - Wand of Binding (`hold_person` only; `hold_monster` is missing, so it waits until that spell is authored);
  - Wand of Paralysis;
  - Staff of Striking;
  - Ring of the Ram;
  - Ring of Evasion (a reaction);
  - Staff of Withering;
  - Staff of Thunder and Lightning;
  - Scarab of Protection;
  - Rod of Lordly Might (partial: deferred until all its buttons exist).
- **Deferred (about 48):** every item whose spell list includes an unauthored spell:
  - Staff of Fire (`wall_of_fire`), Staff of Frost (`cone_of_cold`, `wall_of_ice`), Staff of Healing (`mass_cure_wounds`),
    Staff of Power, Staff of the Magi, Staff of Charming, Staff of Swarming Insects, Staff of the Woodlands;
  - Wand of Polymorph, Wand of Wonder (random table);
  - Rings of Elemental Command, Spell Storing, Spell Turning, Telekinesis, Three Wishes, Animal Influence;
  - Necklace of Prayer Beads, Pearl of Power (slot recovery: **[Lich]**, possible early);
  - the rods; the helms; Robe of Stars; Robe of Scintillating Colors; Cube of Force; Pipes; Talismans; and more.
  - This is listed per item in `SRD_MAGIC_ITEMS_DEFERRED.md`, and each item becomes M5c work as its spells land.

### 9.6 Determinism

- Charges and uses are per-instance state in a new digest (section 11).
- The d20 destroy roll is drawn on the combat RNG **after** the spell resolves, only when charges hit 0.
- Upcast level = base + extra charges, integer.
- **Tests:**
  - Wand of Magic Missiles with 3 charges casts 3rd-level magic missile (5 darts), with no slot spent.
  - It is refused at 0 charges.
  - Destruction on a seeded natural 1.
  - Recharge at long rest: seeded 1d6+1, capped at max.
  - An unattuned Wand of Fireballs is refused.
  - Combat goldens are identical with equipment off.

## 10. G6 Cursed and artifact, G7 summons and planar, G8 utility

- **G6 (7) deferred, not authored:** Berserker Axe, Armor of Vulnerability, Shield of Missile Attraction, Demon Armor, Bag of
  Devouring, Deck of Many Things, Orb of Dragonkind.
  - Curses need: un-removable attunement (3.1), compulsions (Berserker Axe's Wis save, then attack the nearest creature), hidden
    identification, and *remove curse*.
  - Authoring a cursed item without its curse is an approximation, so **none until a curse model is approved**.
  - **Sentient items:** the SRD 5.1 list has none, so no schema is proposed. If a future pack adds one, it needs its own proposal
    (an NPC controller).
- **G7 (21 entries, 40 files) deferred:** figurines, Horn of Valhalla, the four elemental controllers, Efreeti Bottle, Ring of
  Djinni Summoning, Bag of Tricks, Iron Flask, Manual of Golems, Staff of the Python, Elemental Gems, Iron Bands of Binding,
  Dimensional Shackles, Rope of Entanglement, Sphere of Annihilation, Apparatus of the Crab, Cubic Gate, Well of Many Worlds,
  Mirror of Life Trapping.
  - These depend on `docs/proposals/SUMMONS_TRANSFORMS.md` (summoned creature control) and on monsters not yet in srd51.core.
  - **[Lich]** + Engine Core.
- **G8 (19 entries, 22 files):** extradimensional storage (Bag of Holding, Handy Haversack, Portable Hole), Rope of Climbing,
  Decanter of Endless Water, Folding Boat, Instant Fortress, Immovable Rod, Chime of Opening, Eyes of Minute Seeing, Lantern of
  Revealing, Eversmoking Bottle, Deck of Illusions, Robe of Useful Items, Efficient Quiver, Broom / Carpet of Flying, Horseshoes.
  - **Proposal M1b:** author them as `magic_item` with the `magic` block, the verbatim description, cost / weight only where the
    SRD gives them (it mostly does not: magic items have no SRD price, so `cost_cp` is omitted), and `inert_mechanics` naming
    each property.
  - Excluded from M1b because they have combat-relevant mechanics: Lantern of Revealing (shows invisible creatures, LIGHTING),
    Eversmoking Bottle (heavy obscurement, ZONES_AURAS), Broom, Carpet and Horseshoes (movement), Immovable Rod (a terrain
    object). These wait for those systems. **Open question Q12:** is inert authoring acceptable at all, or is everything deferred
    until modelled.

## 11. Versioning, determinism and hashing

### 11.1 Versions

| Schema | Change | Why this version |
|---|---|---|
| `item@2` (additive, no bump) | `magic` block on every arm; category `magic_item` (new closed arm); `template` + `applies_to` | Inert metadata, the same precedent as `focus` / `tool_kind`. No loader or rules reader, so old behaviour is unchanged by construction. |
| **`item@3`** (justified bump) | `effects[]`, `consumable`, `charges`, `activations`, `inert_mechanics` | These fields **carry mechanics**. Under VERSION_BUMP_BATCH, behaviour-bearing fields arrive with a version, so a loader built before them refuses an `@3` file rather than silently ignoring its effects. Migration `item@2 -> item@3` is identity (no effects = none). |
| `character@5` / `monster@5` | equipment `attuned`, `base` (template base), `spell` (scroll) | Additive optional fields on the EQUIPMENT bump. **Q13:** whether `@5` has shipped too far for additive changes (then `@6`). |
| `common@2` | `damage_type`, `condition` enums are reused | Unchanged. |

### 11.2 Goldens

- Nothing changes for content that does not opt in. No srd51.core character or monster carries a magic item, and every mechanic is
  behind a registered loadout (EQUIPMENT 7.2).
- Every golden listed in EQUIPMENT 7.4 stays byte-identical, including `tests/compat/fl1` (frozen, never edited).
- One new compat test per mechanics phase: load srd51.core with equipment on and **no** magic items, then assert byte-identical
  `Combat::hash` / `log_hash` versus equipment on before the phase.

### 11.3 Determinism

- Integer only: durations in rounds (1 min = 10, 1 h = 600, 24 h is out of combat); recharge and upcast are integer; ability
  overrides are max / min.
- RNG:
  - consumable, rider and activation dice draw from the combat RNG at their pinned point in the existing order;
  - recharge dice draw from the rest stream;
  - template resolution, attunement checks and passive effects draw nothing.
- Random-table items (Wand of Wonder, Deck of Many Things, Bag of Tricks, Potion of Resistance "determines it randomly") are
  deferred. Potion of Resistance is authored only as the 10 fixed variants.

### 11.4 Proposed tags (Director to claim; not claimed by this PR)

| Tag | Stream | Contents | Folded when |
|---|---|---|---|
| 0xD6 | `Combat::hash` | `item_state_digest`: per instance with non-default state, in instance order: instance u32, charges u8, uses_left u8, attuned_to u32, flags u8 (bit0 destroyed-pending) | any instance has charges, uses or attunement |
| 0xD7 | `log_hash` | `item_activation`: instance u32, activation index u8, charges spent u8, upcast level u8 | an activation or consumable use occurs |

Timed item effects (potions, oils) reuse the existing modifier and effect rows, so they need no new tag (source = item, appended
`ModSource`). Static G2 effects are covered by 0xD0 (item def ids in the loadout).

## 12. Boundary and implementation order

- **Rules (Lich) owns:**
  - `resolve_template`, `resolve_item(def, attuned)`, `attunement_ok`;
  - `apply_ability_effects`, the `compute_ac` inputs, `save_bonus` / `check_bonus` from item rows, spell / immunity source
    filters;
  - `activation_legal`, `cast_from_item`, `recharge_roll`;
  - consumable resolution (heal, spell effect, scroll checks);
  - rider math.
- **Engine Core owns:** `ItemInstance` charges / uses / attunement state, `ActionRequest::item_activation`, destroyed-item removal,
  stack decrement, and the 0xD6 / 0xD7 folds.
- **Validator / content (T&C) owns:** schema arms, the mirror checks against `a6212be`, `SRD_MAGIC_ITEMS_DEFERRED.md`, and item
  authoring.

## 13. Phase plan (rough counts; files include variants)

| Phase | Owner | Content | Files (rough) | Depends on |
|---|---|---|---|---|
| **M0** | T&C | `item@2` additive `magic` block, `magic_item` arm, templates; `SRD_MAGIC_ITEMS_DEFERRED.md` ledger seeded with all 239 entries; validator mirror | 0 | Director approval of this doc |
| **M1a** | T&C (+ Lich `resolve_template`) | G1: Weapon / Armor / Ammunition +1/+2/+3 templates | 9 | M0; EQUIPMENT derive + loadout |
| **M1b** | T&C | G8 inert utility (storage, ropes, boat, fortress, chime, ...); every property in `inert_mechanics` | 12 | M0; Q12 |
| **M2** | Lich + EC | Attunement: state, the 3 cap, prerequisites, nonmagical fallback; `attuned` on loadouts | 0 (engine) | M0; Q4 |
| **M3a** | Lich + T&C | `item@3` `effects[]` (bonus / resistance / immunity / condition / spell immunity): Cloak and Ring of Protection, Stone of Good Luck, Ring of Resistance ×10, Periapt of Proof against Poison, Brooch of Shielding, Bracers of Defense, Bracers of Archery, Ioun Stone of Protection; armor templates Adamantine, Mithral, Elven Chain, Glamoured | about 22 | M2 |
| **M3b** | Lich + T&C | Ability overrides: Belts of Giant Strength ×6, Gauntlets of Ogre Power, Headband of Intellect, Amulet of Health, Ioun Stones (Agility, Fortitude, Insight, Intellect, Leadership, Strength) | about 15 | M3a; derive path; Q5 |
| **M4a** | Lich + EC + T&C | Consumables on existing machinery: Healing ×4, Resistance ×10, Heroism, Speed, Invisibility | 17 | M0 (no attunement); `UseObject` |
| **M4b** | T&C | Potion of Giant Strength ×6 | 6 | M3b, M4a |
| **M4c** | Lich + T&C | Spell scrolls (10 levels; instance names an authored spell) | 10 | M4a; class spell lists (Q9) |
| **M4d** | Lich + T&C | Necklace of Fireballs, Oil of Sharpness, Bead of Force, Dust of Sneezing and Choking, Arrow of Slaying, Restorative Ointment | about 6 | M4a |
| **M5a** | Lich + EC + T&C | Charges, dawn = long-rest recharge, cast from item: Wands of Magic Missiles, Fireballs, Lightning Bolts, Web; Wand of the War Mage ×3 | 7 | M2, M4a; Q11; tags 0xD6 / 0xD7 |
| **M5b** | Lich + T&C | Non-spell activations: Wand of Fear / Paralysis, Staff of Striking / Withering / Thunder and Lightning, Ring of the Ram / Evasion, Scarab, ... | about 10 | M5a |
| **M6a** | Lich + T&C | Weapon riders: Giant / Dragon Slayer, Vicious, Mace of Smiting / Disruption, Life Stealing, Sun Blade, Flame Tongue, Frost Brand, Javelin of Lightning, Dagger of Venom, Sword of Wounding | about 12 | M1a, M2, M5a (per-dawn uses) |
| **M7** | Lich + EC | G2b senses / movement (21) | 21 | vision #194, movement modes |
| **Later** | per item | M5c staffs / rings as spells land (about 48), M6b exotic riders (about 14), G6 curses (7), G7 summons (40), manuals / tomes (6), the remaining G2 / G4 deferrals | about 150 | the respective systems |

Planned through M6a: **about 126 files** (fewer than 70 base entries). The other base entries stay explicitly deferred in the ledger.

## 14. Deferred (explicit, no approximation)

All of the following are listed in `SRD_MAGIC_ITEMS_DEFERRED.md` (seeded in M0) with columns Item(s) | SRD mechanic | Needs:
- every G6 (curses, artifacts) and G7 (summons, prisons, planar) entry;
- the G2 / G3 / G4 / G5 items named as deferred in 5.6, 7, 8.2 and 9.5;
- G2b until vision and movement modes exist;
- out-of-combat effects: identification, attuning and un-attuning during rests, 24 h / 100 ft attunement loss, manuals and tomes,
  magic ammunition recovery;
- random tables: Wand of Wonder, Deck of Many Things, Bag of Tricks, Bag of Beans, Potion of Resistance random type;
- sentient items (none in SRD 5.1).

## 15. Open questions

**Director**

- Q1. One `magic_item` category for rings, rods, staffs, wands, potions, scrolls and wondrous items, or several categories, or
  reuse `gear`?
- Q2. Templates (`template` + `applies_to` + loadout `base`) or fully expanded concrete files (about 400)?
- Q3. Shield `magic_bonus` has no SRD 5.1 user (*Shield, +N* is not SRD): keep it reserved or document it as non-SRD?
- Q6. Ring limit: the SRD sets none ("use common sense"). Cap at 2, at 10, or uncapped?
- Q11. "Daily at dawn" = end of a long rest (recommended) or a campaign clock?
- Q12. May items be authored with properties listed in `inert_mechanics` (M1b), or must every item wait until fully modelled?
- Q13. Are `attuned` / `base` / `spell` additive on `character@5` / `monster@5`, or does that need `@6`?
- Tags 0xD6 / 0xD7: approve and claim?

**Lich (Rules)**

- Q4. Monster attunement to class-restricted items: is "has spell slots and uses that class's spell list" derivable from
  `monster@5` spellcasting data?
- Q5. Constitution overrides (Amulet of Health): recompute max HP from the new Con modifier while worn (general 5e rule) or leave
  HP baked?
- Q7. Armor of Resistance has no 5e-database variants (the type is chosen): author ten local variants (`armor_of_resistance_fire`,
  ...) like Ring of Resistance, or a loadout-level `damage_type` choice?
- Q8. Bonus-action extra attack (Scimitar of Speed): reuse the `BonusAttack` (7.2b) path?
- Q9. Spell scrolls need class spell lists: author them from 5e-database `5e-SRD-Classes.json` / spell `classes`, or wait for class
  documents?
- Q10. Potion of Speed / Heroism "no concentration": does haste's lethargy apply when the potion's effect ends?
- Item modifier source: append `ModSource::Item` (or equivalent) so dispel-like effects and stacking can filter by source?

**T&C**

- Validator severity for prerequisite ids that do not resolve yet (warning until class documents exist?).
- Description text: verbatim `desc` joined with "\n\n", minus the first line (the "Wondrous item, rare (requires attunement)"
  header, which becomes `magic`)?

## Appendix: SRD 5.1 magic items by primary group (5e-database `a6212be`, 239 base entries)

Columns are generated from the data:
- **Attunement:** "requires attunement" in the first desc line.
- **Charges:** "has N charges".
- **Recharge:** "regains ... at dawn"; "uses, dawn" means a once-per-dawn property without charges.
- **Curse text:** the word "curse" appears.

### G1 Plain +N (3 entries, 9 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `ammunition` | Varies | 3 |  |  |  |  |
| `armor` | Varies | 3 |  |  |  |  |
| `weapon` | Varies | 3 |  |  |  |  |

### G2 Static numeric modifiers (38 entries, 74 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `adamantine-armor` | Uncommon |  |  |  |  |  |
| `amulet-of-health` | Rare |  | yes |  |  |  |
| `amulet-of-proof-against-detection-and-location` | Uncommon |  | yes |  |  |  |
| `armor-of-invulnerability` | Legendary |  | yes |  | uses, dawn |  |
| `armor-of-resistance` | Rare |  | yes |  |  |  |
| `arrow-catching-shield` | Rare |  | yes |  |  |  |
| `belt-of-dwarvenkind` | Rare |  | yes |  | uses, dawn |  |
| `belt-of-giant-strength` | Varies | 6 | yes |  |  |  |
| `boots-of-speed` | Rare |  | yes |  |  |  |
| `bracers-of-archery` | Uncommon |  | yes |  |  |  |
| `bracers-of-defense` | Rare |  | yes |  |  |  |
| `brooch-of-shielding` | Uncommon |  | yes |  |  |  |
| `cloak-of-displacement` | Rare |  | yes |  |  |  |
| `cloak-of-protection` | Uncommon |  | yes |  |  |  |
| `dragon-scale-mail` | Very Rare | 10 | yes |  | uses, dawn |  |
| `dwarven-plate` | Very Rare |  |  |  |  |  |
| `elven-chain` | Rare |  |  |  |  |  |
| `gauntlets-of-ogre-power` | Uncommon |  | yes |  |  |  |
| `glamoured-studded-leather-armor` | Rare |  |  |  |  |  |
| `headband-of-intellect` | Uncommon |  | yes |  |  |  |
| `ioun-stone` | Varies | 14 | yes |  |  |  |
| `mantle-of-spell-resistance` | Rare |  | yes |  |  |  |
| `mithral-armor` | Uncommon |  |  |  |  |  |
| `necklace-of-adaptation` | Uncommon |  | yes |  |  |  |
| `periapt-of-health` | Uncommon |  |  |  |  |  |
| `periapt-of-proof-against-poison` | Rare |  |  |  |  |  |
| `periapt-of-wound-closure` | Uncommon |  | yes |  |  |  |
| `plate-armor-of-etherealness` | Legendary |  | yes |  | uses, dawn |  |
| `ring-of-feather-falling` | Rare |  | yes |  |  |  |
| `ring-of-free-action` | Rare |  | yes |  |  |  |
| `ring-of-mind-shielding` | Uncommon |  | yes |  |  |  |
| `ring-of-protection` | Rare |  | yes |  |  |  |
| `ring-of-regeneration` | Very Rare |  | yes |  |  |  |
| `ring-of-resistance` | Rare | 10 | yes |  |  |  |
| `ring-of-warmth` | Uncommon |  | yes |  |  |  |
| `robe-of-the-archmagi` | Legendary |  | yes (by a sorcerer, warlock, or wizard) |  |  |  |
| `spellguard-shield` | Very Rare |  | yes |  |  |  |
| `stone-of-good-luck-luckstone` | Uncommon |  | yes |  |  |  |

### G2b Static senses, movement and immunities (21 entries, 21 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `boots-of-elvenkind` | Uncommon |  |  |  |  |  |
| `boots-of-levitation` | Rare |  | yes |  |  |  |
| `boots-of-striding-and-springing` | Uncommon |  | yes |  |  |  |
| `boots-of-the-winterlands` | Uncommon |  | yes |  |  |  |
| `cloak-of-arachnida` | Very Rare |  | yes |  | uses, dawn |  |
| `cloak-of-elvenkind` | Uncommon |  | yes |  |  |  |
| `cloak-of-the-bat` | Rare |  | yes |  | uses, dawn |  |
| `cloak-of-the-manta-ray` | Uncommon |  |  |  |  |  |
| `eyes-of-the-eagle` | Uncommon |  | yes |  |  |  |
| `gloves-of-missile-snaring` | Uncommon |  | yes |  |  |  |
| `gloves-of-swimming-and-climbing` | Uncommon |  | yes |  |  |  |
| `goggles-of-night` | Uncommon |  |  |  |  |  |
| `helm-of-comprehending-languages` | Uncommon |  |  |  |  |  |
| `ring-of-invisibility` | Legendary |  | yes |  |  |  |
| `ring-of-jumping` | Uncommon |  | yes |  |  |  |
| `ring-of-swimming` | Uncommon |  |  |  |  |  |
| `ring-of-water-walking` | Uncommon |  |  |  |  |  |
| `ring-of-x-ray-vision` | Rare |  | yes |  |  |  |
| `slippers-of-spider-climbing` | Uncommon |  | yes |  |  |  |
| `winged-boots` | Uncommon |  | yes |  |  |  |
| `wings-of-flying` | Rare |  | yes |  |  |  |

### G3 Weapon and armor riders (26 entries, 26 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `animated-shield` | Very Rare |  | yes |  |  |  |
| `dagger-of-venom` | Rare |  |  |  | uses, dawn |  |
| `dancing-sword` | Very Rare |  | yes |  |  |  |
| `defender` | Legendary |  | yes |  |  |  |
| `dragon-slayer` | Rare |  |  |  |  |  |
| `dwarven-thrower` | Very Rare |  | yes (by a dwarf) |  |  |  |
| `flame-tongue` | Rare |  | yes |  |  |  |
| `frost-brand` | Very Rare |  | yes |  |  |  |
| `giant-slayer` | Rare |  |  |  |  |  |
| `hammer-of-thunderbolts` | Legendary |  |  | 5 | 1d4 + 1 at dawn |  |
| `holy-avenger` | Legendary |  | yes (by a paladin) |  |  |  |
| `javelin-of-lightning` | Uncommon |  |  |  | uses, dawn |  |
| `luck-blade` | Legendary |  | yes | 1 | uses, dawn |  |
| `mace-of-disruption` | Rare |  | yes |  |  |  |
| `mace-of-smiting` | Rare |  |  |  |  |  |
| `mace-of-terror` | Rare |  | yes | 3 | 1d3 at dawn |  |
| `nine-lives-stealer` | Very Rare |  | yes | 1 |  |  |
| `oathbow` | Very Rare |  | yes |  | uses, dawn |  |
| `scimitar-of-speed` | Very Rare |  | yes |  |  |  |
| `sun-blade` | Rare |  | yes |  |  |  |
| `sword-of-life-stealing` | Rare |  | yes |  |  |  |
| `sword-of-sharpness` | Very Rare |  | yes |  |  |  |
| `sword-of-wounding` | Rare |  | yes |  |  |  |
| `trident-of-fish-command` | Uncommon |  | yes | 3 | 1d3 at dawn |  |
| `vicious-weapon` | Rare |  |  |  |  |  |
| `vorpal-sword` | Legendary |  | yes |  |  |  |

### G4 Consumables (39 entries, 70 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `arrow-of-slaying` | Very Rare |  |  |  |  |  |
| `bag-of-beans` | Rare |  |  |  |  |  |
| `bead-of-force` | Rare |  |  |  |  |  |
| `dust-of-disappearance` | Uncommon |  |  |  |  |  |
| `dust-of-dryness` | Uncommon |  |  |  |  |  |
| `dust-of-sneezing-and-choking` | Uncommon |  |  |  |  |  |
| `feather-token` | Rare | 6 |  |  |  |  |
| `manual-of-bodily-health` | Very Rare |  |  |  |  |  |
| `manual-of-gainful-exercise` | Very Rare |  |  |  |  |  |
| `manual-of-quickness-of-action` | Very Rare |  |  |  |  |  |
| `marvelous-pigments` | Very Rare |  |  |  |  |  |
| `necklace-of-fireballs` | Rare |  |  |  |  |  |
| `oil-of-etherealness` | Rare |  |  |  |  |  |
| `oil-of-sharpness` | Very Rare |  |  |  |  |  |
| `oil-of-slipperiness` | Uncommon |  |  |  |  |  |
| `philter-of-love` | Uncommon |  |  |  |  |  |
| `potion-of-animal-friendship` | Uncommon |  |  |  |  |  |
| `potion-of-clairvoyance` | Rare |  |  |  |  |  |
| `potion-of-climbing` | Common |  |  |  |  |  |
| `potion-of-diminution` | Rare |  |  |  |  |  |
| `potion-of-flying` | Very Rare |  |  |  |  |  |
| `potion-of-gaseous-form` | Rare |  |  |  |  |  |
| `potion-of-giant-strength` | Varies | 6 |  |  |  |  |
| `potion-of-growth` | Uncommon |  |  |  |  |  |
| `potion-of-healing` | Varies | 4 |  |  |  |  |
| `potion-of-heroism` | Rare |  |  |  |  |  |
| `potion-of-invisibility` | Very Rare |  |  |  |  |  |
| `potion-of-mind-reading` | Rare |  |  |  |  |  |
| `potion-of-poison` | Uncommon |  |  |  |  |  |
| `potion-of-resistance` | Uncommon | 10 |  |  |  |  |
| `potion-of-speed` | Very Rare |  |  |  |  |  |
| `potion-of-water-breathing` | Uncommon |  |  |  |  |  |
| `restorative-ointment` | Uncommon |  |  |  |  |  |
| `sovereign-glue` | Legendary |  |  |  |  |  |
| `spell-scroll` | Varies | 10 |  |  |  |  |
| `tome-of-clear-thought` | Very Rare |  |  |  |  |  |
| `tome-of-leadership-and-influence` | Very Rare |  |  |  |  |  |
| `tome-of-understanding` | Very Rare |  |  |  |  |  |
| `universal-solvent` | Legendary |  |  |  |  |  |

### G5 Charges, activation and spell-casting (65 entries, 72 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `amulet-of-the-planes` | Very Rare |  | yes |  |  |  |
| `candle-of-invocation` | Very Rare |  | yes |  |  |  |
| `cape-of-the-mountebank` | Rare |  |  |  | uses, dawn |  |
| `circlet-of-blasting` | Uncommon |  |  |  | uses, dawn |  |
| `crystal-ball` | Very Rare | 3 | yes |  | uses, dawn |  |
| `cube-of-force` | Rare |  | yes | 36 | 1d20 at dawn |  |
| `eyes-of-charming` | Uncommon |  | yes | 3 | all at dawn |  |
| `gem-of-brightness` | Uncommon |  |  | 50 |  |  |
| `gem-of-seeing` | Rare |  | yes | 3 | 1d3 at dawn |  |
| `hat-of-disguise` | Uncommon |  | yes |  |  |  |
| `helm-of-brilliance` | Very Rare |  | yes |  |  |  |
| `helm-of-telepathy` | Uncommon |  | yes |  | uses, dawn |  |
| `helm-of-teleportation` | Rare |  | yes | 3 | 1d3 at dawn |  |
| `horn-of-blasting` | Rare |  |  |  |  |  |
| `medallion-of-thoughts` | Uncommon |  | yes | 3 | 1d3 at dawn |  |
| `necklace-of-prayer-beads` | Rare |  | yes (by a cleric, druid, or paladin) |  | uses, dawn |  |
| `pearl-of-power` | Uncommon |  | yes (by a spellcaster) |  | uses, dawn |  |
| `pipes-of-haunting` | Uncommon |  |  | 3 | 1d3 at dawn |  |
| `pipes-of-the-sewers` | Uncommon |  | yes | 3 | 1d3 at dawn |  |
| `ring-of-animal-influence` | Rare |  |  | 3 | 1d3 at dawn |  |
| `ring-of-elemental-command` | Legendary | 4 | yes | 5 | 1d4 + 1 at dawn |  |
| `ring-of-evasion` | Rare |  | yes | 3 | 1d3 at dawn |  |
| `ring-of-shooting-stars` | Very Rare |  | yes (outdoors at night) | 6 | 1d6 at dawn |  |
| `ring-of-spell-storing` | Rare |  | yes |  |  |  |
| `ring-of-spell-turning` | Legendary |  | yes |  |  |  |
| `ring-of-telekinesis` | Very Rare |  | yes |  |  |  |
| `ring-of-the-ram` | Rare |  | yes | 3 | 1d3 at dawn |  |
| `ring-of-three-wishes` | Legendary |  |  | 3 |  |  |
| `robe-of-eyes` | Rare |  | yes |  |  |  |
| `robe-of-scintillating-colors` | Very Rare |  | yes | 3 | 1d3 at dawn |  |
| `robe-of-stars` | Very Rare |  | yes |  |  |  |
| `rod-of-absorption` | Very Rare |  | yes |  |  |  |
| `rod-of-alertness` | Very Rare |  | yes |  | uses, dawn |  |
| `rod-of-lordly-might` | Legendary |  | yes |  | uses, dawn |  |
| `rod-of-rulership` | Rare |  | yes |  | uses, dawn |  |
| `rod-of-security` | Very Rare |  |  |  |  |  |
| `scarab-of-protection` | Legendary |  | yes | 12 |  |  |
| `staff-of-charming` | Rare |  | yes (by a bard, cleric, druid, sorcerer, warlock, or wizard) | 10 | 1d8 + 2 at dawn |  |
| `staff-of-fire` | Very Rare |  | yes (by a druid, sorcerer, warlock, or wizard) | 10 | 1d6 + 4 at dawn |  |
| `staff-of-frost` | Very Rare |  | yes (by a druid, sorcerer, warlock, or wizard) | 10 | 1d6 + 4 at dawn |  |
| `staff-of-healing` | Rare |  | yes (by a bard, cleric, or druid) | 10 | 1d6 + 4 at dawn |  |
| `staff-of-power` | Very Rare |  | yes (by a sorcerer, warlock, or wizard) | 20 | 2d8 + 4 at dawn |  |
| `staff-of-striking` | Very Rare |  | yes | 10 | 1d6 + 4 at dawn |  |
| `staff-of-swarming-insects` | Very Rare |  | yes (by a bard, cleric, druid, sorcerer, warlock, or wizard) | 10 | 1d6 + 4 at dawn |  |
| `staff-of-the-magi` | Legendary |  | yes (by a sorcerer, warlock, or wizard) | 50 | 4d6 + 2 at dawn |  |
| `staff-of-the-woodlands` | Rare |  | yes (by a druid) | 10 | 1d6 + 4 at dawn |  |
| `staff-of-thunder-and-lightning` | Very Rare |  | yes |  | uses, dawn |  |
| `staff-of-withering` | Rare |  | yes (by a cleric, druid, or warlock) | 3 | 1d3 at dawn |  |
| `talisman-of-pure-good` | Legendary |  | yes (by a creature of good alignment) | 7 |  |  |
| `talisman-of-the-sphere` | Legendary |  | yes |  |  |  |
| `talisman-of-ultimate-evil` | Legendary |  | yes (by a creature of evil alignment) | 6 |  |  |
| `wand-of-binding` | Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-enemy-detection` | Rare |  | yes | 7 | 1d6 + 1 at dawn |  |
| `wand-of-fear` | Rare |  | yes | 7 | 1d6 + 1 at dawn |  |
| `wand-of-fireballs` | Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-lightning-bolts` | Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-magic-detection` | Uncommon |  |  | 3 | 1d3 at dawn |  |
| `wand-of-magic-missiles` | Uncommon |  |  | 7 | 1d6 + 1 at dawn |  |
| `wand-of-paralysis` | Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-polymorph` | Very Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-secrets` | Uncommon |  |  | 3 | 1d3 at dawn |  |
| `wand-of-the-war-mage` | Varies | 3 | yes (by a spellcaster) |  |  |  |
| `wand-of-web` | Uncommon |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wand-of-wonder` | Rare |  | yes (by a spellcaster) | 7 | 1d6 + 1 at dawn |  |
| `wind-fan` | Uncommon |  |  |  | uses, dawn |  |

### G6 Cursed and artifact (7 entries, 7 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `armor-of-vulnerability` | Rare |  | yes |  |  | yes |
| `bag-of-devouring` | Very Rare |  |  |  |  |  |
| `berserker-axe` | Rare |  | yes |  |  | yes |
| `deck-of-many-things` | Legendary |  |  |  |  | yes |
| `demon-armor` | Very Rare |  | yes |  |  | yes |
| `orb-of-dragonkind` | Artifact |  | yes | 7 | 1d4 + 3 at dawn |  |
| `shield-of-missile-attraction` | Rare |  | yes |  |  | yes |

### G7 Summons, prisons and planar (21 entries, 40 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `apparatus-of-the-crab` | Legendary |  |  |  |  |  |
| `bag-of-tricks` | Uncommon | 3 |  |  | uses, dawn |  |
| `bowl-of-commanding-water-elementals` | Rare |  |  |  | uses, dawn |  |
| `brazier-of-commanding-fire-elementals` | Rare |  |  |  | uses, dawn |  |
| `censer-of-controlling-air-elementals` | Rare |  |  |  | uses, dawn |  |
| `cubic-gate` | Legendary |  |  | 3 | 1d3 at dawn |  |
| `dimensional-shackles` | Rare |  |  |  |  |  |
| `efreeti-bottle` | Very Rare |  |  |  |  |  |
| `elemental-gem` | Uncommon | 4 |  |  |  |  |
| `figurine-of-wondrous-power` | Varies | 9 |  | 24 |  |  |
| `horn-of-valhalla` | Varies | 4 |  |  |  |  |
| `iron-bands-of-binding` | Rare |  |  |  | uses, dawn |  |
| `iron-flask` | Legendary |  |  |  |  |  |
| `manual-of-golems` | Very Rare | 4 |  |  |  |  |
| `mirror-of-life-trapping` | Very Rare |  |  |  |  |  |
| `ring-of-djinni-summoning` | Legendary |  | yes |  |  |  |
| `rope-of-entanglement` | Rare |  |  |  |  |  |
| `sphere-of-annihilation` | Legendary |  |  |  |  |  |
| `staff-of-the-python` | Very Rare |  | yes (by a cleric, druid, or warlock) |  |  |  |
| `stone-of-controlling-earth-elementals` | Rare |  |  |  | uses, dawn |  |
| `well-of-many-worlds` | Legendary |  |  |  |  |  |

### G8 Wondrous utility (no combat effect) (19 entries, 22 files incl. variants)

| 5e-database index | Rarity | Variants | Attunement | Charges | Recharge | Curse text |
|---|---|---|---|---|---|---|
| `bag-of-holding` | Uncommon |  |  |  |  |  |
| `broom-of-flying` | Uncommon |  |  |  |  |  |
| `carpet-of-flying` | Very Rare | 4 |  |  |  |  |
| `chime-of-opening` | Rare |  |  |  |  |  |
| `decanter-of-endless-water` | Uncommon |  |  |  |  |  |
| `deck-of-illusions` | Uncommon |  |  |  |  |  |
| `efficient-quiver` | Uncommon |  |  |  |  |  |
| `eversmoking-bottle` | Uncommon |  |  |  |  |  |
| `eyes-of-minute-seeing` | Uncommon |  |  |  |  |  |
| `folding-boat` | Rare |  |  |  |  |  |
| `handy-haversack` | Rare |  |  |  |  |  |
| `horseshoes-of-a-zephyr` | Very Rare |  |  |  |  |  |
| `horseshoes-of-speed` | Rare |  |  |  |  |  |
| `immovable-rod` | Uncommon |  |  |  |  |  |
| `instant-fortress` | Rare |  |  |  |  |  |
| `lantern-of-revealing` | Uncommon |  |  |  |  |  |
| `portable-hole` | Rare |  |  |  |  |  |
| `robe-of-useful-items` | Uncommon |  |  |  |  |  |
| `rope-of-climbing` | Uncommon |  |  |  |  |  |
