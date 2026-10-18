# SRD 5.1 combat coverage audit (levels 1-5 party vs SRD monsters)

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Goal: a complete level 1-5 party (Fighter, Cleric, Wizard in full; the other nine classes as a lower tier) fighting SRD monsters, deterministically, from content data. This audit lists every combat-relevant SRD element and says what the code does **today** (main at `6012567`, after #23 spellcasting, #34 follow-ups, #41 PackSet). SRD text was cross-checked against the 5e-database mirror at `a6212be` (see 12). It is not a plan of record; section 11 is a proposed priority list that includes the Director's decisions.

## 0. How to read this

Status (first word of the status cell):

- **DATA** = expressible as data with what exists now: a `spell@1` effect (`spell_attack`, `save`, `heal`, `auto_hit`, `ac_bonus`, `counter`), a `common@1` feature effect kind (`damage_bonus`, `bonus_action_grant`, `self_heal`), a stat-block field, a rules5e `ActionKind`, or a rules5e hook.
- **NEW** = needs a new mechanic. The note gives the gap id from section 11. "(wiring)" means rules5e already models it but nothing carries it from content into a running `sim::Combat`.
- **OOS** = out of scope for combat simulation, with the reason.

**Gap ids (one numbering).** `T&C-G1`..`T&C-G19` are Tools & Content's schema/content gaps (`srd_content_inventory.md` section 3). `R-G1`..`R-G17` are rules-only mechanics with no T&C counterpart (R-G1 is the content-to-sim wiring item). Section 11.2 maps every id used in the first draft of this document (A1.., B1.., C1.., and the old "G1") to the unified ids. Director decisions (recorded 2026-10-17) are folded into section 11.3.

Things that apply to many rows (so they are not repeated):

1. **R-G1, content-to-sim wiring (status on main after #45, #47, #48, #49).** `sim::ContentLibrary::add` (engine/core/sim/src/content.cpp) now reads: abilities, AC value, average HP, walk speed, proficiency bonus, saving-throw proficiencies, PC vs monster, `melee_weapon_attack`/`ranged_weapon_attack` actions with **every** `damage[]` entry (#45; `Attack::extra_damage`), `finesse` still always false, feature effects, `damage_resistances` / `damage_immunities` / `damage_vulnerabilities` and `condition_immunities` (#45, monster@3 / character@3; pre-@3 files stay inert via the migration gate), `type` -> creature type, `size` (stored in `Archetype::size`, no behaviour), and the `spellcasting` block (ability, caster level, slots, pact slots, known spells) (#49). `sim::Combat` now has `CastSpell` plumbing: it installs `hooks.spells` (a SpellBook built from the pack set's `spell@1` files), `spell_target_ok`, `targets_within`, `can_see`, `can_provide_components` (V/S always; a material cost is refused, no inventory) and answers BeingHit / SpellCast windows in `in_range`; sim event schema is v5 (`SpellTarget`, hashed only when present). **Still not read:** skills / passive Perception (T&C-G16), `attacks_per_action` and any multiattack declaration (T&C-G1 content side), qualified resistances and `Attack::qualifiers` from data (T&C-G2 / R-G17 content side), alignment/type normalization (T&C-G15), `finesse` and other weapon properties (T&C-G18), multi-tile footprint (T&C-G14; the `size` value is stored only, footprint work is in flight), monster innate spellcasting (T&C-G10, `monster@4`). Area templates are still not computed: callers list the creatures in `targets`. Spell rows marked DATA are DATA in `spell@1` + rules5e and are now castable from content in a sim combat.
2. **Single effect per spell (S6, still deferred; R-G2).** The `spell@1` schema declares `effects` as an array, but `check_spell` (tools/validate) and `rules5e_content/spell_loader.cpp` reject anything other than exactly one effect. A spell is therefore one of: spell attack, save (damage and/or one condition), heal, auto-hit darts, AC bonus reaction, counter reaction. "Damage plus a rider" spells need R-G2/T&C-G5/R-G3.
3. **Area spells.** `spell@1` stores `area` (cone/cube/cylinder/line/sphere + size) and rules5e resolves a caller-supplied `ActionRequest::targets` list with a save each. Nothing computes who is inside a template on the grid, so area spells are NEW (R-G10) even though their dice/save/scaling are data.
4. **Spell durations.** Concentration, repeat saves and `UntilStartOfCasterTurn` (Shield) end effects. Timed `rounds/minutes/hours` durations are stored but never expire on their own (no round counter on `ActiveSpellEffect`). Fine for concentration spells (which end with concentration), a gap for non-concentration timed buffs (R-G3, R-G15).
5. **Authored numbers.** Today characters are authored per level as `character@3` stat blocks. Static numbers such as AC from armor, Unarmored Defense, Unarmored Movement, +1 magic weapon bonuses, ability modifiers and proficiency are baked by the author; that is DATA **today**. Director approved `item@1` + `character@4` with an equipment model and AC recompute (T&C-G17), after which armor/weapons drive AC and attacks and the equipment rows in sections 2-3 become NEW. Level-up / ASI / multiclassing remain OOS (Tools & Content authoring).
6. SRD 5.1 has one subclass per class (Fighter: Champion, Cleric: Life, Wizard: Evocation, Druid: Land, Barbarian: Berserker, Bard: Lore, Monk: Open Hand, Paladin: Devotion, Ranger: Hunter, Rogue: Thief, Sorcerer: Draconic, Warlock: Fiend). Only those are listed. The only feat in the SRD (Grappler) is not audited; it depends on R-G8.

## 1. Core rules engine (what is implemented in rules5e today)

| Element | Status | Notes |
|---|---|---|
| Initiative (d20 + Dex, ties) | DATA | `roll_initiative`, `begin_encounter`. |
| Turn structure: action, bonus action, movement, reaction budgets | DATA | `TurnBudget`; `bonus_action_grants` bitmask per combatant. |
| Attack action, crit (nat 20 doubles dice), nat 1 miss | DATA | `ActionKind::Attack`, `resolve_attack`. |
| Extra Attack (several attacks per action) | DATA | `Combatant::attacks_per_action` (rules); not loaded from content (T&C-G1 content side). Declared multiattack sequences exist in rules (#48), see section 9. |
| Dash, Disengage, Dodge, Help, Ready | DATA | Matching `ActionKind`s. |
| Hide | DATA (partial) | `ActionKind::Hide` rolls an ability check and stores `stealth_total` (> 0 = hidden), granting advantage on the next attack and then clearing. **Nothing compares it to a Perception score**: grep of `engine/` shows no passive-Perception or search code, the sim adapter maps neither skills nor passive Perception (T&C-G16, R-G1), and `Combatant::stealth_total`'s "sim compares to perception" comment (types.hpp) is aspirational. Corrected from the first draft. |
| Search, Use an Object, Influence | OOS | Exploration/social actions; no combat effect to resolve. |
| Opportunity attacks | DATA | `ReactionTrigger::LeavingReach` window, `ActionKind::OpportunityAttack`; sim supplies `in_range`. |
| Reaction windows (BeingHit, SpellCast, ReadiedTrigger) | DATA | Ask / Never / Auto policy per trigger, async `NeedsDecision`, deterministic replay. |
| Ready action with triggers, refreshed when fired | DATA | `ActionKind::Ready`, `hooks.ready_trigger`, `hooks.refresh_ready`. |
| Death saves, stabilize, Spare the Dying, massive damage, instant death | DATA | `ActionKind::Stabilize` / `SpareTheDying`, `uses_death_saves`. |
| Monsters die at 0 HP | DATA | `uses_death_saves=false` for non-PCs. |
| Healing, temp HP (absorbs first) | DATA | `apply_healing`, `apply_damage`. Temp HP can be set on a combatant but no spell/feature grants it (R-G3). |
| Ability checks, saving throws, proficiency, advantage/disadvantage cancel | DATA | `ActionKind::AbilityCheck`, `saves.hpp`, `combine_modes`. |
| Standing up from prone (half movement) | DATA | `ActionKind::StandUp`. |
| Movement on a grid, difficult terrain (2x), LOS | DATA | sim `Grid`, `step_cost_ft`, supercover LOS. |
| Crawling while prone (extra movement cost) | NEW | Not modelled in rules or sim (R-G8). |
| Opportunity-attack exceptions (Disengage, teleport, forced movement) | DATA | Disengage implemented; teleport/forced movement do not exist yet (R-G9). |
| Surprise (skip first turn, no reactions) | NEW | No surprise state in `begin_encounter` (R-G16). |
| Short / long rest: spell slots, Pact slots, feature uses | DATA | `recover_slots_short_rest/long_rest`, `recover_features`. |
| Rests: Hit Dice spending, HP regained | NEW | No Hit Dice on `Combatant` (R-G16). |
| Falling damage, suffocation, starvation, extreme heat | OOS | Hazard/exploration; not part of a fight. |
| Mounted combat | OOS | No mounts in the SRD party we target. |
| Underwater combat | OOS | Environment variant; defer. |
| Cover (+2 / +5 AC **and Dexterity saves**, total cover) | NEW | `AttackContext::cover_ac_bonus` exists and is honored for attack rolls only; there is no cover bonus on Dexterity saves, and sim never computes cover (R-G13). SRD: half cover +2, three-quarters +5, to AC and Dex saves. |
| Lighting, darkness, obscurement, darkvision/blindsight/truesight | NEW | Not modelled; sim has LOS only (R-G13). |
| Flanking | OOS | Optional rule, not SRD core. |
| Two-weapon fighting | NEW | R-G6. |
| Grapple and shove (contested Athletics/Acrobatics) | NEW | R-G8. |
| Flying, swimming, climbing, burrowing movement | NEW | One `speed_ft`; sim reads `speed.walk` only (T&C-G12, T&C-G14). |
| Creature size and space (large creatures occupy 2x2) | NEW | Size is loaded (`Archetype::size`, #49) but only stored; the grid is still one tile per creature (T&C-G12, T&C-G14; footprint model in flight). |
| Dispel / end effects on a creature | NEW | R-G15. |

## 2. Weapons (properties)

| Element | Status | Notes |
|---|---|---|
| Simple/martial melee weapon, damage die + type | DATA | `melee_weapon_attack` action, `to_hit` baked (author applies ability mod + proficiency + magic bonus). All `damage[]` entries are loaded (#45): the first is `Attack::damage`, the rest `Attack::extra_damage`. |
| Simple/martial ranged weapon, normal/long range | DATA | `ranged_weapon_attack` with `range:[normal,long]`; `Attack::range_ft/long_range_ft`; disadvantage beyond normal range, none past long. |
| Ranged attack with hostile within 5 ft = disadvantage | DATA | `ActionRequest::ranged_hostile_within_5ft` (sim computes). |
| Finesse | DATA (baked) / NEW (T&C-G18) | Today the author bakes the better of Str/Dex into `to_hit` and damage. The adapter hard-codes `Attack::finesse = false`, so finesse is **not** honored from data (rules: `kWeaponFinesse` / `Attack::finesse`, `attack_ability()` picks the better of Str/Dex; adapter must set the bit); it matters once equipment drives attacks (T&C-G17 `item@1`, character@4 recompute). |
| Reach (glaive, halberd, whip, pike, lance) | DATA | `reach` on the action; sim reach tiles. |
| Thrown | DATA | A second `ranged_weapon_attack` action with `range`. Item is not removed from inventory (inventory is OOS). |
| Versatile | DATA | Two actions (1-handed die, 2-handed die). |
| Two-handed | DATA | `properties:["two_handed"]`; used by the Dueling predicate `one_handed_melee_no_other_weapon`. |
| Ammunition | DATA | Ranged attack; ammunition count is OOS (no inventory in sim). |
| Light | NEW (wiring) | `kWeaponLight` bit in `Attack::properties` (rules done); only matters for two-weapon fighting (R-G6). |
| Loading | NEW (wiring) | Rules done: `kWeaponLoading`; one shot per Attack action even with Extra Attack / multiattack (`TurnBudget::loading_fired`). Content/adapter must set the bit. |
| Heavy (Small creatures disadvantage) | NEW (wiring) | Rules done: `kWeaponHeavy` + `Combatant::size` (Small/Tiny attacker = disadvantage). Adapter must set the bit and `Combatant::size` (sim stores `Archetype::size` only). |
| Special: net (restrains; Large or smaller, DC 10 Str escape or 5 slashing vs AC 10; one attack per action), lance (disadvantage within 5 ft; two hands unless mounted) | NEW | Net = restrain rider (T&C-G5); lance: `kWeaponLance` gives disadvantage with the target within 5 ft (rules done; two-hands/mounted rule is not modelled). Low priority. |
| Silvered / adamantine weapons, magic weapons vs nonmagical resistance | NEW (wiring) | Rules done (#47): `Attack::qualifiers` (magic / silvered / adamantine) and `Combatant::resist_qualified` / `immune_qualified`. Nothing loads them from content yet, and no weapon data sets the flags (R-G17, T&C-G2, T&C-G17). Magic bonuses themselves are baked: DATA. |
| Improvised weapons, unarmed strike (1 + Str) | DATA | Authored as an action. |
| Weapon proficiency | DATA | Baked into `to_hit`. |

## 3. Armor and shields

| Element | Status | Notes |
|---|---|---|
| Armor AC (light/medium/heavy, shield, Dex cap) | DATA (baked) | Baked into `armor_class.value` today; equipment-derived AC is T&C-G17. |
| Equipment model (item@1, equipped armor/weapons driving AC and attacks) | NEW | Director-approved: `item@1` + `character@4` with equipment model and AC recompute (T&C-G17). Today `character@3.equipment` is free strings and AC is baked. |
| Donning/doffing time, inventory in the middle of a fight | OOS | Not simulated. |
| Strength requirement (speed -10) | NEW | Chain mail 13, splint and plate 15 (SRD). Baked by the author today; derived once equipment is modelled (T&C-G17). |
| Stealth disadvantage (padded, scale mail, half plate, ring mail, chain mail, splint, plate) | NEW | SRD marks exactly these 7 of 13 armor entries. `Hide` has no armor input (T&C-G17 item property, R-G1 skills). Corrected: the first draft omitted padded and ring/chain/splint/plate. |
| Spell: Mage Armor, Shield of Faith, Barkskin (AC changes) | NEW | R-G3. |
| Shield spell (+5 AC reaction) | DATA | `ac_bonus` effect (shipped `srd51.spell.shield`). |

## 4. Conditions (SRD Appendix A)

| Condition | Status | Notes |
|---|---|---|
| Blinded | DATA | Attack disadvantage on self, advantage against. Sight-based checks are not modelled. |
| Charmed | NEW | Needs a source creature (cannot attack charmer, social advantage). `Combatant::conditions` is a bitmask with no source (R-G8). |
| Deafened | DATA | No combat effect beyond the flag. |
| Frightened | NEW | Disadvantage on attacks/checks is implemented. "Cannot willingly move closer to the source" needs source + LOS (R-G8). |
| Grappled | NEW | Speed 0 is implemented. Grapple action, source, escape check, drag are not (R-G8). |
| Incapacitated | DATA | No actions/reactions. |
| Invisible | DATA | Attack advantage / disadvantage against. No "heard/ noticed" rules. |
| Paralyzed | DATA | Incapacitated, auto-fail Str/Dex saves, advantage against, crit within 5 ft. |
| Petrified | DATA | Incapacitated, speed 0, resistance to all damage, auto-fail Str/Dex. Aging/weight not modelled. SRD also grants immunity to poison and disease, which is not modelled (condition-granted immunity). |
| Poisoned | DATA | Attack and check disadvantage. |
| Prone | DATA | Attack disadvantage on self; advantage within 5 ft against, disadvantage beyond. `StandUp` action. |
| Restrained | DATA | Speed 0, attack disadvantage, advantage against, Dex save disadvantage. |
| Stunned | DATA | Incapacitated, auto-fail Str/Dex, advantage against. |
| Unconscious | DATA | Prone-like effects, crit within 5 ft, death saves. |
| Exhaustion levels 1-6 | DATA | `exhaustion_level`: check disadvantage, speed half, attack/save disadvantage, HP max half, speed 0, death. |
| Condition immunities on a stat block | DATA | `Combatant::condition_immunities` (#45): `add_condition` refuses an immune condition, and save-spell conditions skip it. Loaded from `condition_immunities` for monster@3 / character@3. |
| Conditions with a duration or end trigger (end of next turn, until damaged) | NEW | Only spell conditions (concentration/repeat save) have lifetimes (R-G3). |
| Apply a condition from a monster action (ghoul claws, wolf trip, spider poison) | NEW | Monster actions have no rider (T&C-G5). |

## 5. Damage types and damage handling

| Element | Status | Notes |
|---|---|---|
| 13 damage types | DATA | `DamageType`; `common@1 damage_type`. |
| Resistance, vulnerability, immunity | DATA | Masks on `Combatant`; `apply_damage` order is correct. Loaded from `damage_resistances` / `damage_immunities` / `damage_vulnerabilities` for monster@3 / character@3 (#45). |
| "Nonmagical" / "nonmagical, non-silvered / adamantine" qualifiers | NEW (wiring) | Rules done (#47): `QualifiedDamageMask` on `Combatant`, `Attack::qualifiers`, spells are magical, applied to every damage component. Content has no qualified resistance field yet and the adapter sets no `Attack::qualifiers` (R-G17, T&C-G2). Affects many monsters (werewolf, specter, ghost, gargoyle, golems). |
| Multiple damage components in one attack (1d8 slashing + 2d6 fire) | DATA | `Attack::extra_damage` + `apply_damage_parts` (#45): each component is rolled (and doubled on a crit) and modified by its own type, one `DamageDealt` per hit. The adapter loads every `damage[]` entry. |
| Critical hits double dice only | DATA | `roll_dice(..., crit)`. |
| Temp HP | DATA | See section 1. |
| Damage when hit by a save-based effect: full / half | DATA | `Save` effect with `on_success: half`. |
| Evasion (no damage on success, half on fail) | NEW | Rogue 7 / Monk 7, beyond level 5; listed in R-G7 for completeness. |
| Maximum HP reduction (wraith, vampire, specter life drain) | NEW | T&C-G7. |

## 6. Class features, levels 1-5

Existing feature machinery: `damage_bonus` (static), `bonus_action_grant` (static), `self_heal` (active `UseFeature`, with uses and short/long rest recharge). Spells use `Combatant` slots/pact slots/caster level (rules) and R-G1 for wiring.

### 6.1 Fighter (full)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit Die d10, proficiencies, saves (Str/Con), armor/weapons | 1 | DATA | `character@3 classes[].hit_die`, `saving_throws`, baked AC/to_hit. |
| Fighting Style: Dueling | 1 | DATA | `damage_bonus {amount:2, condition: one_handed_melee_no_other_weapon}` (shipped, `fighter_1`). |
| Fighting Style: Archery (+2 ranged attack rolls) | 1 | NEW | No to-hit bonus effect kind (R-G5: `attack_bonus`). |
| Fighting Style: Defense (+1 AC in armor) | 1 | NEW | No AC bonus effect kind; could be baked into AC but then not de-duplicated with armor (R-G3/R-G5 `ac_bonus` static kind). |
| Fighting Style: Great Weapon Fighting (reroll 1-2 on damage dice) | 1 | NEW | Damage reroll hook (R-G7). |
| Fighting Style: Protection (reaction, impose disadvantage on attack vs adjacent ally) | 1 | NEW | Reaction feature (`cost: reaction` is reserved) (R-G7). |
| Fighting Style: Two-Weapon Fighting (add mod to off-hand) | 1 | NEW | R-G6. |
| Second Wind | 1 | DATA | `self_heal {dice 1d10, plus fighter_level, uses 1, recharge short_rest, cost bonus_action}` (shipped). |
| Action Surge | 2 | NEW | Grants an extra action once per rest: new active kind that adds to `TurnBudget` (R-G5). |
| Martial Archetype: Champion, Improved Critical (crit on 19-20) | 3 | NEW | Crit range per attack/combatant (R-G5 `crit_range`). |
| Ability Score Improvement | 4 | DATA | Authored stats. |
| Extra Attack | 5 | NEW (wiring) | `attacks_per_action=2` exists in rules; no effect kind or loader sets it (T&C-G1 content side). |

### 6.2 Cleric (full; Life domain)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit die d8, proficiencies, saves (Wis/Cha) | 1 | DATA | Baked. |
| Spellcasting: slots, DC, attack bonus, prepared spells, cantrips | 1 | DATA | `spell_slots`, `spell_save_dc`, `spell_attack_bonus`, `spells_known`; cantrip scaling by `caster_level`. |
| Ritual casting | 1 | OOS | Rituals take 10 extra minutes; not in an encounter. |
| Divine Domain: Life, bonus proficiency (heavy armor) | 1 | DATA | Baked AC. |
| Life domain spells always prepared (Bless, Cure Wounds, Lesser Restoration, Spiritual Weapon, Beacon of Hope, Revivify) | 1-5 | DATA | Authored `spells_known`; availability of each spell per section 8. |
| Disciple of Life (healing spells heal an extra 2 + spell level) | 1 | NEW | Heal-spell bonus rider on the caster (R-G4). |
| Channel Divinity (1/rest at L2, 2/rest at L6) | 2 | NEW | Pool/uses of an active feature with non-heal effects (R-G5). |
| Channel Divinity: Turn Undead | 2 | NEW | WIS save or turned (frightened-like, must flee, cannot take reactions) for 1 minute; area 30 ft (R-G5, R-G8, R-G10). |
| Channel Divinity: Preserve Life | 2 | NEW | Distribute a 5 x level HP pool among creatures within 30 ft, up to half max HP each (R-G5, R-G10, R-G15). |
| Destroy Undead (CR 1/2 at L5) | 5 | NEW | Turn Undead outcome threshold (R-G5). |
| Ability Score Improvement | 4 | DATA | Authored. |

### 6.3 Wizard (full; School of Evocation)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit die d6, proficiencies, saves (Int/Wis) | 1 | DATA | Baked. |
| Spellcasting, spellbook (known list), prepared spells, cantrips | 1 | DATA | `spells_known`; spellbook copying/prep is OOS (downtime). |
| Arcane Recovery (once per day, short rest, recover slots <= half wizard level) | 1 | NEW | Rest-time feature with a choice of slot levels; `recover_slots_short_rest` only restores Pact slots (R-G16). |
| Arcane Tradition: Evocation; Evocation Savant | 2 | OOS | Downtime spell copying cost. |
| Sculpt Spells (allies auto-succeed and take no damage from your evocation area) | 2 | NEW | Needs area spells (R-G10) plus a per-cast ally choice (R-G14). |
| Ability Score Improvement | 4 | DATA | Authored. |
| Cantrip damage scaling at 5 | 5 | DATA | `scaling.cantrip.levels [5,11,17]` (shipped on Fire Bolt). |
| 3rd-level slots at 5 | 5 | DATA | `spell_slots` table baked on the character. |

### 6.4 Lower tier

| Class / Feature | Lvl | Status | Notes |
|---|---|---|---|
| **Barbarian** hit die d12, proficiencies | 1 | DATA | Baked. |
| Rage (+damage, resist B/P/S, adv on Str checks/saves; bonus action; uses/long rest) | 1 | NEW | Timed self-effect with resistances and damage bonus (R-G3, R-G5). |
| Unarmored Defense (10 + Dex + Con) | 1 | DATA | Baked AC. |
| Reckless Attack | 2 | NEW | Per-attack choice: advantage for you and against you until next turn (R-G7/R-G3). |
| Danger Sense (adv on Dex saves vs visible effects) | 2 | NEW | Conditional save advantage (T&C-G7 style trait). |
| Primal Path: Berserker, Frenzy (extra attack as bonus action, exhaustion after) | 3 | NEW | R-G6, conditions. |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| Fast Movement (+10 ft) | 5 | DATA | Baked speed (`speed.walk`). Condition "no heavy armor" is the author's. |
| **Bard** spellcasting (full caster) | 1 | DATA | |
| Bardic Inspiration (bonus action, d6/d8; ally adds to a roll within 10 min) | 1 | NEW | Roll-modifier token with a pool of uses (R-G5, R-G7). |
| Jack of All Trades (+half proficiency to checks) | 2 | NEW | Check modifier (R-G7/small). |
| Song of Rest | 2 | OOS | Out of combat healing. |
| Expertise | 3 | DATA | Authored skills. |
| College of Lore: Cutting Words (reaction, subtract inspiration die from enemy roll) | 3 | NEW | Reaction roll modifier (R-G7). |
| Font of Inspiration (short rest recharge) | 5 | NEW | Recharge change on the same pool (R-G5). |
| **Druid** spellcasting (full caster) | 1 | DATA | |
| Wild Shape (CR 1/4 at L2, no fly/swim; CR 1/2 at L4; CR 1 at L8) | 2 | NEW | Replace stat block temporarily; HP overflow carries over (R-G12). |
| Circle of the Land: Natural Recovery | 2 | NEW | Same family as Arcane Recovery (R-G16). |
| Druidic, Land bonus cantrip | 1-2 | OOS | Language / one extra cantrip (authored). |
| **Monk** Unarmored Defense (10 + Dex + Wis) | 1 | DATA | Baked AC. |
| Martial Arts (d4 die, Dex for unarmed, unarmed bonus action attack) | 1 | NEW | Bonus-action attack (R-G6). Die is baked. |
| Ki (pool = monk level, short rest) | 2 | NEW | Pool (R-G5). |
| Flurry of Blows, Patient Defense, Step of the Wind | 2 | NEW | Ki-cost bonus actions (R-G5, R-G6). Patient Defense and Step of the Wind could use `bonus_action_grant` without a ki cost only. |
| Unarmored Movement (+10 ft) | 2 | DATA | Baked speed. |
| Deflect Missiles (reaction: reduce ranged damage by 1d10 + Dex + level) | 3 | NEW | Reaction feature (R-G7). |
| Way of the Open Hand: Open Hand Technique (rider on Flurry: prone / push / no reactions) | 3 | NEW | Rider on hit (T&C-G5, R-G9). |
| Slow Fall | 4 | OOS | Falling damage not simulated. |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| Stunning Strike (ki; CON save or stunned until next turn) | 5 | NEW | Rider (T&C-G5) + ki pool (R-G5). |
| Ki-Empowered Strikes (magic for resistance, L6) | 6 | NEW | R-G17, T&C-G2 (beyond L5, listed for completeness). |
| **Paladin** Divine Sense | 1 | OOS | Detection. |
| Lay on Hands (pool = 5 x level; also cures disease/poison for 5 points) | 1 | NEW | Pool and variable heal amount (R-G5). `self_heal` heals only the user. |
| Divine Smite (expend slot after a hit; 2d8 + 1d8 per slot above 1st, +1d8 vs undead/fiend) | 2 | NEW | Post-hit rider that spends a spell slot and doubles on crit (R-G4). |
| Fighting Style, Spellcasting (half caster) | 2 | NEW / DATA | Styles as in 6.1; slots as a half caster are DATA (R-G1). |
| Divine Health | 3 | OOS | Disease immunity. |
| Oath of Devotion: Channel Divinity Sacred Weapon (+Cha to attacks, light) | 3 | NEW | Timed buff (R-G3, R-G5). |
| Oath of Devotion: Turn the Unholy | 3 | NEW | R-G5/R-G8. |
| Oath spells (Protection from Evil and Good, Sanctuary, ...) | 3 | NEW | See section 8. |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| **Ranger** Favored Enemy / Natural Explorer | 1 | OOS | Tracking, languages, travel. |
| Fighting Style | 2 | NEW / DATA | Ranger options per SRD: Archery, Defense, Dueling, Two-Weapon Fighting (no Great Weapon Fighting or Protection). Only Dueling is DATA; Archery needs `attack_bonus` (R-G5). Paladin options: Defense, Dueling, Great Weapon Fighting, Protection. |
| Spellcasting (half caster) | 2 | DATA | |
| Ranger Archetype: Hunter, Colossus Slayer (+1d8 once per turn vs a creature below max HP) | 3 | NEW | Once-per-turn rider (R-G4). |
| Hunter: Giant Killer, Horde Breaker (alternatives) | 3 | NEW | Reaction/extra attack rider (R-G7, R-G4). |
| Primeval Awareness | 3 | OOS | Detection. |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| **Rogue** Expertise | 1 | DATA | Authored skills. |
| Sneak Attack (xd6 once per turn with advantage or an ally within 5 ft of the target; finesse/ranged only) | 1 | NEW | Conditional once-per-turn damage with sim-supplied "ally adjacent to target" fact (R-G4). |
| Thieves' Cant | 1 | OOS | Language. |
| Cunning Action (bonus Dash / Disengage / Hide) | 2 | DATA | `bonus_action_grant {actions:[dash,disengage,hide]}`. |
| Roguish Archetype: Thief, Fast Hands (bonus-action Use an Object) | 3 | OOS | Use an Object is not simulated. |
| Thief: Second-Story Work | 3 | OOS | Climb/jump. |
| Uncanny Dodge (reaction: halve one attack's damage) | 5 | NEW | Reaction feature (R-G7). |
| **Sorcerer** spellcasting (full caster, spells known) | 1 | DATA | |
| Draconic Bloodline: Draconic Resilience (HP +1/level, AC 13 + Dex unarmored) | 1 | DATA | Baked HP and AC. |
| Draconic Bloodline: Dragon Ancestor | 1 | OOS | Language and Cha checks. |
| Font of Magic / Sorcery Points (convert slots and points) | 2 | NEW | Pool + conversions (R-G5). |
| Metamagic (Careful, Distant, Empowered, Extended, Heightened, Quickened, Subtle, Twinned) | 3 | NEW | Per-cast modifiers on `ActionRequest` (R-G14). |
| **Warlock** Pact Magic (slots recharge on short rest, all same level) | 1 | DATA | `pact_slot_level`, `pact_slots`, short-rest recovery implemented. |
| Otherworldly Patron: The Fiend, Dark One's Blessing (temp HP on kill) | 1 | NEW | On-kill trigger granting temp HP (T&C-G7/R-G3). |
| Eldritch Invocations: Agonizing Blast (+Cha per beam) | 2 | NEW | Per-beam damage bonus on a spell (R-G4). |
| Invocations: Armor of Shadows, Fiendish Vigor, Repelling Blast (push 10 ft), Devil's Sight | 2 | NEW | Mage Armor / False Life at will (R-G3), forced movement (R-G9), darkvision (R-G13). |
| Invocation: Eldritch Spear (Eldritch Blast range 300 ft) | 2 | NEW | Per-spell range modifier from a feature (R-G14). |
| Invocations: Eldritch Sight, Beast Speech, Mask of Many Faces, Beguiling Influence, Book of Ancient Secrets, Misty Visions, etc. | 2 | OOS | Utility. |
| Invocation: Thirsting Blade (Blade pact, attack twice with pact weapon) | 5 | NEW | Extra attack with a specific weapon (T&C-G1). |
| Invocations: Mire the Mind, One with Shadows, Sign of Ill Omen | 5 | NEW | Slot-free cast of Slow, Invisibility, Bestow Curse once per rest (R-G5 limited-use cast; needs the spells per section 8). |
| Pact Boon: Tome (3 cantrips) | 3 | DATA | `spells_known`. |
| Pact Boon: Blade (pact weapon, proficiency, magic) | 3 | NEW | Summoned/bonded weapon (R-G12, R-G17, T&C-G2). |
| Pact Boon: Chain (find familiar variants) | 3 | NEW | Familiar (R-G12). |

### 6.5 Races (party members' combat traits)

| Trait | Status | Notes |
|---|---|---|
| Darkvision | NEW | R-G13. |
| Dwarven Resilience (poison resistance; advantage vs poison) | NEW | Resistance is DATA (loaded by #45); save advantage vs source type is T&C-G7 style. |
| Fey Ancestry (advantage vs charm) / Brave (halfling, vs frightened) | NEW (wiring) | Rules done: `{Save, Advantage, save_vs_conditions}` (Charmed / Frightened). Content loader is T&C's. |
| Halfling Lucky (reroll natural 1) | NEW | R-G7. |
| Relentless Endurance (drop to 1 HP once per day) / Savage Attacks (extra crit die) | NEW | T&C-G7 / R-G7. |
| Dragonborn breath weapon and resistance | NEW | Area save + limited use (R-G10, T&C-G7). |
| Tiefling Hellish Resistance / Infernal Legacy (Thaumaturgy, Hellish Rebuke) | NEW | Resistance is DATA (loaded by #45); Hellish Rebuke trigger (R-G7). |
| Gnome Cunning (advantage on Int/Wis/Cha saves vs magic) | NEW (wiring) | Rules done: `{Save, Advantage, kWhenSourceMagical, save_abilities}`. Content loader is T&C's. |
| Speed, size | DATA | Baked (`speed.walk`); `size` is loaded into `Archetype::size` (stored only; footprint is T&C-G12 / T&C-G14). |

## 7. Core spell mechanics

| Element | Status | Notes |
|---|---|---|
| Spell slots by level, Pact slots, recovery | DATA | `Combatant::spell_slots`, `spend_slot`, rest recovery. |
| Save DC = 8 + prof + mod; spell attack bonus | DATA | `spell_save_dc`, `spell_attack_bonus`. |
| Components V/S/M, material cost/consumed | DATA | Flags in `SpellDef`; `hooks.can_provide_components`, installed by sim (#49): V/S always, a material cost is refused (no inventory yet). |
| Concentration (CON save on damage DC max(10, half), ends on incapacitation / new concentration) | DATA | `damage_creature`, `concentration_spell`. |
| Cantrip tiers (5/11/17), upcast scaling dice/darts/targets/beams | DATA | `scaling.cantrip`, `scaling.upcast`. |
| Reaction spells with triggers (being hit, targeted by named spell, spell cast with sight) | DATA | `casting_time.trigger` {being_hit, spell_cast, or_targeted_by, requires_sight}. |
| Spell with one effect | DATA | Single `effects[0]`. |
| Spell with several effects (damage + rider) | NEW | R-G2 (S6 deferred). |
| Targeting: single, N targets, creature_type filter, max spread | DATA | `targets {count, creature_type, max_spread_ft}`; `hooks.spell_target_ok`, `targets_within` (installed by sim, #49: range + line of sight; no area templates). |
| Area of effect templates | NEW | R-G10. |
| Casting time minute/hour (non-combat) | OOS | Stored; rejected during encounters. |
| Duration expiry for rounds/minutes/hours | NEW | R-G15. |
| Counterspell (auto up to slot level, else ability check DC 10 + level) | DATA | `counter` effect (shipped). |
| Ritual casting | OOS | See above. |
| Spell cast request/validation, slot spend, events, replay | DATA | `ActionKind::CastSpell`, events 30-38, golden replay; sim `CastSpell` command and `SpellTarget` event (sim schema v5, #49). |

## 8. Spells, cantrips through 3rd level (combat-relevant)

Legend for "DATA" rows: kind used in `spell@1`. "Shipped" = already in `srd51.core` (6 spells: Fire Bolt, Magic Missile, Cure Wounds, Shield, Hold Person, Counterspell). Everything else is authoring work (Tools & Content) unless marked NEW.

### 8.1 Cantrips

| Spell | Status | Notes |
|---|---|---|
| Acid Splash | DATA | `save` DEX, 1d6 acid, `on_success none`, `targets {count 2, max_spread_ft 5}`. |
| Chill Touch | NEW | Attack damage is DATA; "can't regain HP, undead disadvantage" rider needs R-G2/R-G3. |
| Dancing Lights, Light, Mage Hand, Mending, Message, Minor Illusion, Prestidigitation, Thaumaturgy, Druidcraft, Guidance, Resistance | OOS | Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. |
| Eldritch Blast | DATA | `spell_attack` with `scaling.cantrip {adds: beams}`; range 120. Agonizing Blast: R-G4. |
| Fire Bolt | DATA | Shipped. Ignites objects: OOS. |
| Poison Spray | DATA | `save` CON, 1d12 poison, range 10, `on_success none`. |
| Produce Flame | DATA | Thrown `spell_attack` 1d8 fire (the light effect is OOS). |
| Ray of Frost | NEW | Damage DATA; speed -10 until next turn is R-G3. |
| Sacred Flame | DATA | `save` DEX, 1d8 radiant, none on success. "Target gains no benefit from cover" is R-G13. |
| Shillelagh | NEW | Weapon-damage buff (R-G3). |
| Shocking Grasp | NEW | Melee spell attack DATA; advantage vs metal armor and "no reactions" rider R-G2/R-G3. |
| Spare the Dying | DATA | `ActionKind::SpareTheDying` (hook `spare_the_dying`). |
| True Strike | NEW | Advantage on next attack (R-G3). |
| Vicious Mockery | NEW | Damage DATA; disadvantage on next attack rider R-G2/R-G3. |

### 8.2 Level 1

| Spell | Status | Notes |
|---|---|---|
| Alarm, Animal Friendship, Comprehend Languages, Create or Destroy Water, Detect Evil and Good, Detect Magic, Detect Poison and Disease, Disguise Self, Feather Fall, Goodberry, Identify, Illusory Script, Jump, Longstrider, Purify Food and Drink, Silent Image, Speak with Animals, Unseen Servant, Floating Disk | OOS | Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). |
| Bane | NEW | Penalty dice on attacks and saves (R-G3). |
| Bless | NEW | Bonus dice on attacks and saves (R-G3). |
| Burning Hands | NEW | Cone (R-G10); dice/save/upcast are DATA. |
| Charm Person | NEW | Charmed with source (R-G8). |
| Color Spray | NEW | HP-pool targeting (R-G15) + area. |
| Command | NEW | Forced action next turn (R-G8). |
| Cure Wounds | DATA | Shipped (`heal`, excludes undead/construct). |
| Divine Favor | NEW | Weapon damage buff (R-G3/R-G4). |
| Entangle | NEW | Area zone + restrained (R-G10, R-G11). |
| Expeditious Retreat | NEW | Bonus-action Dash for duration (R-G3). |
| Find Familiar | NEW | Familiar (R-G12). |
| Fog Cloud | NEW | Obscurement zone (R-G13, R-G11). |
| Faerie Fire | NEW | Area + advantage against (R-G10, R-G3). |
| False Life | NEW | Temp HP effect kind (R-G3). |
| Grease | NEW | Zone + prone (R-G11). |
| Guiding Bolt | NEW | Attack damage DATA; advantage on next attack vs target rider (R-G2/R-G3). |
| Healing Word | DATA | `heal`, `casting_time bonus_action`, range 60, `upcast.dice`. |
| Hellish Rebuke | NEW | Needs a "damaged by creature" reaction trigger (R-G7). |
| Heroism | NEW | Temp HP each turn, frightened immunity (R-G3). |
| Hideous Laughter | NEW | Prone + incapacitated (two conditions) and save-on-damage repeat (R-G2). |
| Hunter's Mark | NEW | R-G4. |
| Inflict Wounds | DATA | Melee `spell_attack`, 3d10 necrotic, `upcast.dice`. |
| Mage Armor | NEW | AC set (R-G3). |
| Magic Missile | DATA | Shipped (`auto_hit`). |
| Protection from Evil and Good | NEW | R-G3. |
| Sanctuary | NEW | Attackers must save to target (R-G3/R-G7). |
| Shield | DATA | Shipped (`ac_bonus`). |
| Shield of Faith | NEW | Non-reaction AC bonus buff (R-G3). `ac_bonus` is reaction-only. |
| Sleep | NEW | HP-pool (R-G15). |
| Thunderwave | NEW | Cube (R-G10) + push (R-G9); dice/save DATA. |

### 8.3 Level 2

| Spell | Status | Notes |
|---|---|---|
| Aid | NEW | Max HP and HP increase (R-G3). |
| Alter Self, Animal Messenger, Arcane Lock, Augury, Continual Flame, Darkvision, Gentle Repose, Knock, Levitate, Locate Animals or Plants, Locate Object, Magic Mouth, Pass without Trace, Prayer of Healing (10 min), Rope Trick, See Invisibility, Spider Climb, Zone of Truth, Find Traps, Arcanist's Magic Aura, Detect Thoughts, Enthrall, Find Steed (10 minutes) | OOS | Utility / out of encounter casting time. |
| Barkskin | NEW | AC floor (R-G3). |
| Blindness/Deafness | NEW | `save` with condition is DATA, but the caster chooses blinded or deafened per cast (parameterized effect, small R-G2). |
| Blur | NEW | Disadvantage against you (R-G3). |
| Branding Smite | NEW | R-G4. |
| Branding Smite | NEW | Weapon-attack rider: extra radiant damage, reveals invisible, concentration (R-G4, T&C-G5). SRD level 2 (the earlier draft also listed it at level 1). |
| Calm Emotions | NEW | Area + suppress condition (R-G10, R-G3). |
| Darkness | NEW | Obscurement (R-G13). |
| Enhance Ability | NEW | Check advantage and bonus per mode (R-G3). |
| Enlarge/Reduce | NEW | Size/damage change (R-G3, T&C-G12, T&C-G14). |
| Flame Blade | NEW | Summoned weapon (R-G12). |
| Flaming Sphere | NEW | Object/zone (R-G11/R-G12). |
| Gust of Wind | NEW | Line + push (R-G9, R-G10). |
| Heat Metal | NEW | Sustained damage (R-G3). |
| Hold Person | DATA | Shipped (`save` WIS, paralyzed, repeat end_of_turn, humanoid, upcast targets). |
| Invisibility | NEW | Condition applied with no save, ends on attack/cast; `save` kind requires a save (R-G2/R-G3). |
| Lesser Restoration | NEW | End a condition (R-G15). |
| Magic Weapon | NEW | Magic bonus (R-G17, T&C-G2/R-G3). |
| Mirror Image | NEW | Duplicates that absorb attacks (R-G3). |
| Misty Step | NEW | Teleport (R-G9). |
| Moonbeam | NEW | Zone (R-G11). |
| Protection from Poison | NEW | R-G3. |
| Ray of Enfeeblement | NEW | Attack + damage halving rider (R-G3). |
| Scorching Ray | DATA | `spell_attack` with `beams 3`, 2d6 fire, `upcast.beams 1`. |
| Shatter | NEW | Sphere (R-G10). |
| Silence | NEW | Zone preventing verbal components (R-G11). |
| Spike Growth | NEW | Zone (R-G11). |
| Spiritual Weapon | NEW | Summoned attacker (bonus action each turn) (R-G12). |
| Suggestion | NEW | Forced behavior (R-G8). |
| Warding Bond | NEW | Shared damage (R-G3). |
| Web | NEW | Area zone + restrained (R-G10, R-G11). |
| Acid Arrow | NEW | Delayed second damage (R-G15). |

### 8.4 Level 3

| Spell | Status | Notes |
|---|---|---|
| Clairvoyance, Create Food and Water, Major Image, Meld into Stone, Nondetection, Phantom Steed, Plant Growth, Sending, Speak with Dead, Speak with Plants, Tongues, Water Breathing, Water Walk, Gaseous Form, Glyph of Warding (trap setup), Remove Curse, Tiny Hut (1 minute), Animate Dead (1 minute), Magic Circle (1 minute) | OOS | Utility / out of encounter casting time (1 minute or longer). |
| Beacon of Hope | NEW | Save advantage, max healing (R-G3). |
| Bestow Curse | NEW | Touch + chosen effect (R-G2/R-G3). |
| Blink | NEW | Phasing (R-G9). |
| Call Lightning | NEW | Zone + repeating action (R-G11). |
| Conjure Animals | NEW | Summons (R-G12). |
| Counterspell | DATA | Shipped (`counter`). |
| Dispel Magic | NEW | End effects, check by level (R-G15). |
| Daylight | NEW | Light/obscurement dispelling (R-G13). |
| Fear | NEW | Cone, frightened, drop items, forced Dash (R-G10, R-G8). |
| Fireball | NEW | Sphere (R-G10); 8d6 DEX half, `upcast.dice 1` are DATA. |
| Fly | NEW | Movement mode (T&C-G12, T&C-G14). |
| Haste | NEW | Speed, AC, extra action (R-G3, R-G5). |
| Hypnotic Pattern | NEW | Area, charmed + incapacitated (R-G10, R-G8). |
| Lightning Bolt | NEW | Line (R-G10); dice/save DATA. |
| Mass Healing Word | DATA | `heal` with `targets {count 6}`, bonus action, range 60. |
| Protection from Energy | NEW | Resistance buff (R-G3). |
| Revivify | NEW | Raise a creature dead within 1 minute (R-G15). |
| Sleet Storm | NEW | Zone (R-G11). |
| Slow | NEW | Speed/AC/attacks (R-G3). |
| Spirit Guardians | NEW | Aura/zone around caster (R-G11). |
| Stinking Cloud | NEW | Zone (R-G11). |
| Vampiric Touch | NEW | Attack damage DATA; heal half and repeat action need R-G2/R-G3. |
| Wind Wall | NEW | Zone (R-G11). |

## 9. Monsters: stat blocks, traits and actions (CR 0 - ~5)

Stat-block data: `monster@3` carries abilities, AC, HP average, speed (walk, fly, swim, ...), saving throws, skills, damage and condition resistances/immunities/vulnerabilities, senses, languages, CR, traits (`trait_active` with an `effect`), actions, bonus actions, reactions, legendary actions, `behavior.ai` Luau script. Only the subset in T&C-G2 reaches sim.

| Trait / action | Examples (SRD, mostly CR 0-5) | Status | Notes |
|---|---|---|---|
| Single weapon attack (melee or ranged) | goblin, kobold, skeleton, zombie, orc, bandit | DATA | One `*_weapon_attack` action; ranged normal/long range. |
| Multiattack (several attacks, possibly different) | owlbear, veteran, knight, gladiator, troll, ettin, manticore, wight | NEW (wiring) | Rules done (#48): `Combatant::multiattacks` (declared, id-addressable sequences of attack indices), `ActionRequest::multiattack`, per-attack steps so reactions/Ready/movement happen between attacks. No content `multiattack` effect, loader, or sim AI option choice yet (T&C-G1 content side). |
| Several damage components in one attack | ankheg (slashing + acid), basilisk (piercing + poison) | DATA | T&C-G6, done in #45. Corrected: giant spider (poison is a Constitution-save rider with save-for-half damage, T&C-G5) and giant scorpion (multiattack + save poison rider) are not multi-component attacks. |
| On-hit saving throw rider (poison, paralysis, prone, grapple, restrain) | ghoul (claws: paralysis, not vs elf/undead), wolf (prone), giant spider (bite: CON save, 2d8 poison, half on save), giant scorpion (sting: CON save, 4d10 poison), crocodile (grapple, restrained), giant toad (swallow) | NEW | T&C-G5, R-G8. |
| Action: Web (ranged restrain, escape DC) | giant spider | NEW | T&C-G5, R-G8. |
| Pack Tactics (advantage if an ally is within 5 ft of the target) | wolf, dire wolf, jackal, kobold, blood hawk | NEW (wiring) | Rules done: `Combatant::roll_traits` `{Attack, Advantage, kWhenAllyNearTarget}`; sim supplies `ActionRequest::ally_near_target`; content loader for the trait is T&C's. |
| Keen Senses / Smell / Hearing (Perception advantage) | wolf, hawk, owl | OOS | Perception checks are not simulated at all (Hide stores a Stealth total but nothing opposes it). |
| Darkvision, Blindsight, Tremorsense, Truesight | most monsters, bat, ankheg | NEW | Lighting and vision (R-G13). |
| Damage resistances / immunities / vulnerabilities | skeleton (poison immune, bludgeoning vulnerable), zombie, fire elementals | DATA | Loaded by #45 (T&C-G2). |
| Nonmagical-attack resistance | werewolf, gargoyle, specter, ghost, wight | NEW (wiring) | Rules done (#47); content lists and `Attack::qualifiers` not loaded (R-G17, T&C-G2). |
| Condition immunities | skeleton, zombie, ghoul, golems, elementals | DATA | Loaded by #45 (T&C-G3). |
| Regeneration (loses it to certain damage) | troll, vampire spawn | NEW | Start-of-turn heal with suppression by damage type (T&C-G8). |
| Recharge (5-6) breath weapon | dragon wyrmlings, hell hound (CR 3, in the mirror; T&C's name list missed it) | NEW | Recharge roll on an action (T&C-G4) + cone/line (R-G10). |
| Limited-use actions (N/day) | knight Leadership, innate spellcasters and various | NEW | Uses/recharge machinery exists only for `self_heal` features (R-G5, T&C-G4). |
| Spellcasting (slots, prepared list) | acolyte (CR 1/4), priest, druid (CR 2), mage (CR 6) | DATA | monster@3 now accepts the `spellcasting` block (#49: ability, caster level, slots, pact, known spells; an engine-minimal addition whose final shape T&C owns) and sim casts through `CastSpell`. Corrected from NEW. At-will / N-per-day Innate Spellcasting is separate (below). |
| Innate Spellcasting (at will, N/day) | couatl, dryad, drow, green hag, lamia, unicorn | NEW | At-will and per-day lists in `monster@4` (T&C-G10). Corrected: pixie is not in SRD 5.1 (absent from the mirror); imp has no Innate Spellcasting in SRD. |
| Legendary actions, lair actions, Legendary Resistance | aboleth (CR 10), mummy lord, adult/ancient dragons, **unicorn (CR 5)** | OOS | Deferred by Director decision (T&C-G11). 32 SRD monsters have legendary actions: 31 are CR 10 or higher, the unicorn is CR 5; none is CR 4 or lower. Corrected from "CR 10+". |
| Charge / Pounce / Trampling Charge (move then bonus damage / knock prone) | boar, lion, panther, tiger, warhorse | NEW | Needs movement history this turn (T&C-G7, T&C-G5). |
| Aggressive (bonus action move toward a hostile) | orc | NEW | `bonus_action_grant` has no Move (T&C-G7). |
| Reckless | minotaur, berserker | NEW | Per-attack advantage trade (T&C-G7). |
| Rampage (bonus attack after reducing a creature to 0 HP) | gnoll | NEW | On-kill trigger (T&C-G7). |
| Nimble Escape | goblin | DATA | `bonus_action_grant [disengage, hide]` (shipped). |
| Parry (reaction, +AC) | gladiator, knight, bandit captain | NEW | Reaction feature (T&C-G9). |
| Flyby (no opportunity attacks) | owl | NEW | Flight (T&C-G12, T&C-G14) + OA exception. |
| Fly / Swim / Climb / Burrow speeds | bat, eagle, gargoyle, crocodile, giant octopus, ankheg | NEW | T&C-G12, T&C-G14; sim reads `speed.walk` only. |
| Large and bigger creatures (multi-tile space, grapple size limits) | ogre, ankheg, owlbear, troll, ettin | NEW | T&C-G12, T&C-G14. |
| Swarm of Tiny creatures (shares space, half damage at half HP, resistances) | swarms of rats, ravens, insects | NEW | T&C-G14, T&C-G2. |
| Undead Fortitude (CON save to drop to 1 HP unless radiant or crit) | zombie | NEW | Drop-to-0 trigger (T&C-G7). |
| Death Burst | magmin | NEW | Death trigger (T&C-G7). |
| Magic Resistance (advantage on saves vs spells) | dryad, satyr, imp | NEW (wiring) | Rules done: `{Save, Advantage, kWhenSourceMagical}`; spell saves pass `SaveSource{magical}`. Content loader is T&C's. |
| Magic Weapons (attacks count as magical) | couatl and many fiends/celestials | NEW (wiring) | `Attack::qualifiers` exists (#47); no trait effect sets it (R-G17, T&C-G2). |
| Sunlight Sensitivity (disadvantage on attacks and sight Perception in sunlight) | kobold, drow | OOS | Descriptive for the slice: needs a sunlight/lighting model (R-G13) that is not planned, so the T&C data keeps it as a plain trait. Not filed as a lighting mechanic. If lighting ever lands it is a conditional-disadvantage trait (T&C-G7). |
| Life Drain / max HP or ability drain | specter, wraith, shadow, vampire spawn | NEW | T&C-G7. |
| Incorporeal Movement | specter, ghost, will-o'-wisp | NEW | Movement through creatures/objects (T&C-G12, T&C-G14). |
| Frightful gaze/visage (save or frightened) | ghost (Horrifying Visage), mummy (Dreadful Glare) | NEW | Save action + condition with source (T&C-G4, R-G8). |
| Leadership (limited-use buff action: allies add 1d4 to attacks and saves for 1 minute) | knight | NEW | Not an aura: a limited-use action granting a timed ally bonus (T&C-G4 usage, R-G3). Corrected from "frightful aura". |
| Engulf / Swallow | gelatinous cube, giant toad | NEW | Contained state (R-G8). |
| Petrifying Gaze / Touch (delayed condition progression) | basilisk, cockatrice | NEW | T&C-G5, R-G3. |
| Teleport bonus action | blink dog | NEW | R-G9. |
| Shapechanger | werewolf, doppelganger | NEW | Stat block swap (R-G12). |
| Rust Metal / Corrode / Stench | rust monster, ghast | NEW | T&C-G5 and aura (R-G11). Disease itself OOS. |
| Blood Frenzy (advantage vs damaged creatures) | sharks | NEW (wiring) | Rules done: `{Attack, Advantage, kWhenTargetDamaged}` (reads target HP). Content loader is T&C's. |
| False Appearance, ambush setups | mimic | OOS | Only surprise (R-G16) matters. |
| Amphibious, Hold Breath, Spider Climb, Web Walker, Web Sense | crocodile, giant spider | OOS | Non-combat movement/sense. |

(Representative lists name examples to size the mechanic, not a complete CR 0-5 roster. Per-monster counts are the Tools & Content task.)

## 10. Summary counts

### 10.1 Row counts by status

Row counts are over the tables in sections 1-9 (one row per mechanic or feature; spell rows that group several OOS utility spells in one cell count as one row, see 10.2 for per-spell counts). They measure **mechanics rows**, not SRD populations.

| Category | DATA | NEW | OOS | Rows |
|---|---|---|---|---|
| 1. Core rules engine | 17 | 10 | 5 | 32 |
| 2. Weapons (properties) | 11 | 5 | 0 | 16 |
| 3. Armor, shields and equipment | 2 | 4 | 1 | 7 |
| 4. Conditions (SRD Appendix A) | 13 | 5 | 0 | 18 |
| 5. Damage types and damage handling | 6 | 3 | 0 | 9 |
| 6.1 Fighter (full) | 4 | 8 | 0 | 12 |
| 6.2 Cleric (full; Life domain) | 5 | 5 | 1 | 11 |
| 6.3 Wizard (full; Evocation) | 5 | 2 | 1 | 8 |
| 6.4 Lower tier (nine classes) | 15 | 42 | 12 | 69 |
| 6.5 Races | 1 | 8 | 0 | 9 |
| 7. Core spell mechanics | 10 | 3 | 2 | 15 |
| 8.1 Cantrips | 7 | 6 | 1 | 14 |
| 8.2 Level 1 | 5 | 25 | 1 | 31 |
| 8.3 Level 2 | 2 | 30 | 1 | 33 |
| 8.4 Level 3 | 2 | 21 | 1 | 24 |
| 9. Monsters: stat blocks, traits, actions | 6 | 33 | 5 | 44 |
| **Total** | **111** | **210** | **31** | **352** |

"NEW (wiring)" rows (rules5e models it, content-to-sim does not carry it) are counted under NEW. Counts are as of main after #45, #47, #48 and #49; before those PRs the totals were DATA 104 / NEW 217 / OOS 31 = 352. "DATA (baked)" / "DATA (partial)" rows count as DATA.

### 10.2 Spells, per spell (SRD 5.1 levels 0-3)

The mirror has **169** SRD spells of level 0-3 (24 cantrips, 49 level 1, 54 level 2, 42 level 3). Every one is now named in section 8 (the first draft missed six and listed 13 that are not in SRD 5.1). Per spell: **DATA 16, NEW 81, OOS 72** (169). DATA: Acid Splash, Eldritch Blast, Fire Bolt, Poison Spray, Produce Flame, Sacred Flame, Spare the Dying, Cure Wounds, Healing Word, Inflict Wounds, Magic Missile, Shield, Hold Person, Scorching Ray, Counterspell, Mass Healing Word.

### 10.3 Reconciliation with the Tools & Content inventory

T&C (`srd_content_inventory.md`, against the same mirror commit `a6212be`) counts **SRD populations** (monsters, weapons, armor, gear); this document counts **mechanics**. They are different units and are not meant to match one-to-one. Population counts below were re-derived from the mirror and agree with T&C's.

| Population | Count | Source / check |
|---|---|---|
| Monsters | 334 (CR <= 2: 181; CR 3-5: 64; CR 6+: 89; CR <= 5: 245) | Mirror recount = T&C. |
| Monsters with Multiattack | 148 | Matches T&C (T&C-G1 impact). |
| Monsters with legendary actions | 32 (31 at CR 10+, Unicorn CR 5; none at CR <= 2) | Matches T&C; fixes this doc's earlier "CR 10+". |
| Weapons | 37 (14 simple, 23 martial; 28 melee, 9 ranged) | Matches T&C. |
| Armor incl. shield | 13 (3 light, 5 medium, 4 heavy, 1 shield); 7 impose stealth disadvantage | Matches T&C; stealth count is new here. |
| Adventuring gear / tools / mounts-vehicles | 116 / 31 / 40 | T&C figures; confirmed by the mirror's equipment categories. Not audited row by row here (combat-irrelevant). |
| Spells level 0-3 | 169 | Not in T&C (monsters + items only); see 10.2. |
| T&C monster readiness tiers | A 16, B 26, C 292 (CR <= 2: 16 / 25 / 140) | T&C classification; this doc does not re-classify monsters. |

Why numbers differ: section 9 has 44 mechanics rows that touch many monsters each (for example the single "Multiattack" row stands for T&C's 148 monsters); T&C's per-monster tiers are the authority on monster counts. The weapon/armor rows in sections 2-3 are property rows (finesse, light, stealth disadvantage), not a 37 + 13 item list. Where this doc's examples name monsters, each was re-checked against the mirror (see 12).

## 11. Missing mechanics, prioritized

Sizes: S = days, M = about a week, L = multi-week / cross-module. "Batch" = **N** no schema bump (adapter wiring or additive effect kinds), **B** the single version bump (`common@2` / `monster@4` / `character@4` plus new `item@1`), **E** engine only, **D** deferred. Priority follows the Director's work order: content-to-sim wiring (with Engine Core), then multiattack, then multiple damage components, then magic-weapon qualifiers; then the rest.

### 11.1 Unified gap table (prioritized)

| Pri | Id | Mechanic | Unlocks | Owner | Size | Batch |
|---|---|---|---|---|---|---|
| 1 | R-G1 + T&C-G2 (plain lists) + T&C-G3 + T&C-G14 + T&C-G15 + T&C-G16 | **Content-to-sim wiring.** `ContentLibrary` loads resistances/immunities/vulnerabilities (for `monster@3`, now), condition immunities (new `Combatant` mask), creature type, size and multi-tile footprint (Engine Core verifies Large+), skills / passive Perception, `attacks_per_action`, and the `CastSpell` plumbing in `Combat` (hooks `spells`, `spell_target_ok`, `targets_within`, `can_see`, `can_provide_components`); T&C normalizes alignment/type (swarm = beast + tag). | 165 monsters with resistances/immunities (T&C), 92 with condition immunities, every caster; Hide actually opposed | Engine Core (adapter) M; Rules S; T&C S (normalization) | M | N |
| 2 | T&C-G1 | **Multiattack as a declared sequence.** An id-addressable multiattack option is an ordered list of attack refs inside one Attack action; each attack resolves separately with reaction windows and Ready allowed between attacks; the AI picks by id (several options per monster). PC Extra Attack reuses `attacks_per_action`; `loading` limits one shot per action (T&C-G18). | 148 monsters; Fighter/Barbarian/Monk/Paladin/Ranger L5; Thirsting Blade | Rules M; T&C S (additive effect + validator) | M | N |
| 3 | T&C-G6 | **Multiple damage components per attack** (`damage[]` beyond the first; crit doubles each die group). Adapter/engine only; schema already allows it. | ankheg, basilisk, flaming/elemental weapons, smites | Rules S; Engine Core S | S | N |
| 4 | R-G17 + T&C-G2 (qualifiers) | **Magic-weapon and material qualifiers**: magic / silvered / adamantine flag on attacks and qualified resistance masks (additive `damage_mitigation` effect). | 70 qualified resistances (werewolf, gargoyle, specter, golems), Magic Weapon, Ki-Empowered | Rules S; T&C S | S | N |
| 5 | T&C-G18 | **Weapon property semantics**: finesse (adapter hard-codes false), light, loading, thrown, reach, heavy, ammunition; closed vocabulary later in `item@1`. **Rules side done**: `rules5e/weapon.hpp` (`WeaponProperty` bits, `weapon_property_from_string`, `attack_ability`, `weapon_disadvantage`), `Attack::properties`, `Combatant::size`, Loading enforced per action. Adapter (Engine Core) still sets `properties`/`size` from content. | equipment-driven attacks | T&C S; Rules S | S | N |
| 6 | T&C-G7 (+ R-G4, R-G5, R-G7 PC analogues) | **Small generic conditional/trigger effect set designed by Rules** (**roll-mode part done in rules**: `RollTrait` conditional advantage/disadvantage on attacks, attacks against, and saves, `rules5e/traits.hpp`; `extra_damage` and `on_event` triggers are still open) (`advantage_when`, `extra_damage`, `on_event`): Pack Tactics, Blood Frenzy, Charge/Pounce, Aggressive, Rampage, Undead Fortitude, Death Burst, Magic Resistance, Reckless, Sneak Attack, Hunter's Mark, Colossus Slayer, Relentless Endurance, Gnome Cunning. | most CR 0-5 trait monsters; Rogue/Ranger/Paladin | Rules M (design); T&C M | M | N |
| 7 | T&C-G8 | **Regeneration / Rejuvenation** (`regen` effect, suppression by damage type; depends on T&C-G7 events). | troll, vampire spawn | Rules S; T&C S | S | N |
| 8 | R-G2 | **Effects array (S6)**: N effects per spell, a no-save apply-condition effect, per-cast option. | Guiding Bolt, Vicious Mockery, Hideous Laughter, Thunderwave, Chill Touch, Shocking Grasp | Rules S-M; T&C S | M | N |
| 9 | T&C-G13 | **Senses/vision data consumed** (darkvision, blindsight, tremorsense, truesight); effective only with R-G13. Data is already in schema. | most monsters | Engine Core | M | N (with R-G13) |
| 10 | T&C-G4 | **Save-based/area monster actions + Recharge 5-6 + limited use** (`usage`): first-class `save_effect` action kind, no interim trait effects. | dragon breath, hell hound, giant spider web, Leadership, Turn Undead-like | Rules M; T&C M-L | M-L | B |
| 11 | T&C-G5 | **On-hit riders** (save/condition/grapple/swallow/prone) as first-class `on_hit` on attack actions; depends on R-G8 for grapple/swallow. | wolf, ghoul, giant spider/scorpion, crocodile, Stunning Strike, net, smites | Rules M; T&C M | M | B |
| 12 | T&C-G9 + R-G7 (PC reaction features, d20 modify/reroll windows) | **Reactions**: unreserve `cost: reaction` behind `monster@4` (and `character@4`); roll-modifier windows. | Parry (gladiator, knight, bandit captain), Uncanny Dodge, Deflect Missiles, Cutting Words, Hellish Rebuke, Protection, Great Weapon Fighting reroll | Rules M; Engine Core M | M | B |
| 13 | T&C-G10 | **Monster spellcasting / Innate Spellcasting** in `monster@4`: slots, known/prepared, at-will and N/day lists, cross-refs to `spell@1`; needs R-G1 cast plumbing. | acolyte, priest, druid, mage, couatl, dryad, drow, green hag | T&C L (only 6 spells authored); Rules S | L | B |
| 14 | T&C-G17 | **`item@1` + `character@4` equipment model with AC recompute**, stealth disadvantage, Strength requirement, weapon/armor properties, magic flag. | all 37 weapons / 13 armor entries driving attacks and AC | T&C L; Rules M; Engine Core M | L | B |
| 15 | R-G3 | **Timed modifier effects** (buff/debuff with duration, source, concentration link; attack/save/check bonus dice, advantage grants, AC set/bonus, speed, temp HP, resistances; expiry of timed durations). | Bless, Bane, Shield of Faith, Mage Armor, Haste, Slow, Faerie Fire, Rage, Leadership | Rules L; T&C M | L | N |
| 16 | R-G4 | **On-hit / once-per-turn damage riders** (Sneak Attack, Divine Smite spending a slot, Hunter's Mark, Colossus Slayer, Agonizing Blast, Disciple of Life). | Rogue, Paladin, Ranger, Warlock, Cleric | Rules M; Engine Core S (adjacency fact) | M | N |
| 17 | R-G5 | **Feature resource pools and more active kinds** (ki, rage, Channel Divinity, Lay on Hands, Bardic Inspiration, sorcery points, Action Surge, `attack_bonus`, `ac_bonus`, `crit_range`). | Fighter 1-5, Barbarian, Monk, Paladin, Bard, Cleric, Sorcerer | Rules M; T&C M | M | N |
| 18 | R-G6 | **Bonus-action attacks** (two-weapon fighting, Martial Arts, Flurry, Frenzy). | Monk, Berserker, dual wielders | Rules M | M | E |
| 19 | R-G10 | **Area templates on the grid** (cone, cube, cylinder, line, sphere): compute targets, LOS/cover for Dex saves, feed `ActionRequest::targets`. | Burning Hands, Fireball, Lightning Bolt, Thunderwave, Shatter, dragon breath, Fear | Engine Core M; Rules S | M | E |
| 20 | R-G8 | **Conditions with a source; grapple/shove; swallow/engulf**; crawl cost. | Charm Person, Command, Fear, Turn Undead, grapple monsters | Rules M; Engine Core S | M | E |
| 21 | R-G13 | **Cover from the grid (AC and Dex saves) and lighting/obscurement/vision.** | cover, Sacred Flame, Darkness, Fog Cloud, darkvision | Engine Core M | M | E |
| 22 | R-G9 | **Forced movement and teleport** with OA rules. | Thunderwave push, Repelling Blast, Misty Step, Open Hand | Engine Core M; Rules S | M | E |
| 23 | R-G15 | **Spell kinds**: HP-pool (Sleep, Color Spray), dispel/cleanse, delayed/sustained damage, resurrection, expiry for non-concentration spells. | Sleep, Lesser Restoration, Revivify, Dispel Magic | Rules S-M each | M | N |
| 24 | R-G14 | **Per-cast options**: Metamagic, Sculpt Spells, chosen condition/mode, Eldritch Spear range. | Sorcerer 3, Evocation Wizard 2 | Rules M | M | N |
| 25 | R-G16 | **Surprise, Hit Dice, Arcane/Natural Recovery.** | round-1 ambush, attrition | Rules S | S | E |
| 26 | R-G11 | **Persistent zones and auras** with enter/start-of-turn triggers. | Spirit Guardians, Moonbeam, Spike Growth, Web, Entangle, Stinking Cloud | Rules L; Engine Core L | L | E |
| 27 | R-G12 | **Summons, familiars, bonded weapons, transforms** (Wild Shape, Shapechanger, Conjure Animals, Spiritual Weapon). | Druid, Warlock Chain, Cleric 2, werewolf | Rules L; Engine Core L; T&C M | L | E |
| parallel | T&C-G19 | **Luau API level 2**, additive only (`engine.level2.d.luau`, manifest `api_level` 2); level 1 stays frozen. Scripts need adjacency/movement facts for Pack/Charge. | AI reach | Engine Core / T&C M | M | N |
| deferred | T&C-G12 | **Non-walk movement / elevation.** Fly, swim, climb are treated as walk speed; a forward-compat flag is recorded in content. | 178 monsters with non-walk speeds | Engine Core (later) | M-L | D |
| deferred | T&C-G11 | **Legendary actions, lair actions, Legendary Resistance.** 32 monsters (31 at CR 10+, Unicorn CR 5). | Not needed for CR 0-4 | Rules L | L | D |

Recommended grouping: priorities 1-4 are the first sequence (content-to-sim wiring, multiattack, multiple damage components, magic-weapon qualifiers); 5-9 finish the no-bump batch; 10-14 are the one version bump (save actions, riders, reactions, monster spellcasting, `item@1`/`character@4`); 15-27 are spell and class-feature depth in the order that unlocks the level 1-5 party.

### 11.4 Status on main (after #45, #47, #48, #49)

| Item | Status | Remaining |
|---|---|---|
| R-G1 + T&C-G2 (plain lists) + T&C-G3 | **Done**: #45 (resistances, immunities, vulnerabilities, condition immunities) and #49 (creature type, size stored, spellcasting block, `CastSpell` plumbing, hooks, event schema v5) | skills / passive Perception (T&C-G16), `attacks_per_action` loading, alignment/type normalization (T&C-G15), footprint (T&C-G14; `size` is stored only) |
| T&C-G1 multiattack | **Rules done** (#48) | content `multiattack` effect + loader, `extra_attack` / `attacks_per_action` loading, sim AI choosing an option by id (T&C / Engine Core) |
| T&C-G6 multiple damage components | **Done** (#45: rules + loader) | none |
| R-G17 + T&C-G2 (qualifiers) | **Rules done** (#47) | content qualified-resistance field, `Attack::qualifiers` from weapon data / "Magic Weapons", additive `damage_mitigation` effect (T&C / Engine Core) |
| T&C-G10 monster spellcasting | **Partial**: slot-based `spellcasting` block on monster@3 / character@3 (#49) | innate (at-will / N per day) lists in `monster@4`; final block shape is T&C's |

### 11.2 Mapping: first-draft id -> unified id

| First draft | Unified | Note |
|---|---|---|
| G1 (wiring) | R-G1; T&C-G2 (plain lists), T&C-G3, T&C-G10 (monster casting), T&C-G14, T&C-G15, T&C-G16 | Old umbrella split; the `CastSpell` plumbing stays R-G1. |
| A1 Multiattack / extra attack / loading | T&C-G1; loading under T&C-G18 | |
| A2 Multiple damage components | T&C-G6 | |
| A3 Attack riders | T&C-G5 (save/condition riders); T&C-G4 (save actions); T&C-G7 (Pounce/Charge) | |
| A4 Nonmagical/silvered qualifiers + magic flag | T&C-G2 (qualifiers) + R-G17 (flag) | |
| B1 Effects array | R-G2 | Rules-only. |
| B2 Timed modifiers | R-G3 | Rules-only. |
| B3 Damage riders | R-G4 | Rules-only; shares T&C-G7's generic set. |
| B4 Resource pools / feature kinds | R-G5 | Rules-only. |
| B5 Bonus-action attacks | R-G6 | Rules-only (light property under T&C-G18). |
| B6 Reactions and roll modifiers | T&C-G9 (monster reactions) + R-G7 (PC reactions, roll windows) | |
| B7 Monster trait hooks | T&C-G7 (conditional/trigger), T&C-G8 (regeneration), T&C-G4 (recharge/limited use), T&C-G10 (innate) | |
| C1 Conditions with source, grapple | R-G8 | T&C-G5 depends on it. |
| C2 Forced movement/teleport | R-G9 | |
| C3 Area templates | R-G10 | Monster area actions: T&C-G4. |
| C4 Zones/auras | R-G11 | |
| C5 Summons/transforms | R-G12 | |
| C6 Movement modes, size/space | T&C-G12 (modes, deferred) + T&C-G14 (footprint) | |
| C7 Cover, lighting, vision | R-G13 (engine) + T&C-G13 (senses data) | |
| C8 Per-cast options | R-G14 | |
| C9 Spell kinds | R-G15 | |
| C10 Surprise, Hit Dice, recoveries | R-G16 | |
| C11 Legendary actions | T&C-G11 | Deferred. |
| (new) | T&C-G15, T&C-G16 (skills), T&C-G17 (item@1), T&C-G18, T&C-G19 | No first-draft counterpart; added from the T&C inventory. |

### 11.3 Director decisions folded in

- **Multiattack** is a declared sequence of attack refs inside one Attack action, with reactions and Ready allowed between attacks; options are id-addressable (T&C-G1).
- **Resistances** are wired now for `monster@3`; the qualifier flags and masks are now in rules (#47); content wiring remains (T&C-G2, R-G17).
- **One version bump** (`common@2` / `monster@4` / `character@4` + `item@1`) carries first-class save actions (T&C-G4), riders (T&C-G5), reactions behind a version gate (T&C-G9), monster spellcasting (T&C-G10) and equipment (T&C-G17); no interim trait effects.
- **Deferred**: legendary actions (T&C-G11) and elevation (T&C-G12; fly/swim/climb act as walk speed, forward-compat flag in content).
- **Luau API level 2** is additive; level 1 stays frozen (T&C-G19).
- T&C-G7 is a small generic effect set designed by Rules; T&C-G14 multi-tile footprints are verified by Engine Core; T&C-G15 normalization is content-side.
- **Work order**: content-to-sim wiring (with Engine Core), multiattack, multiple damage components, magic-weapon qualifiers.

## 12. Source notes and verification

- **SRD source**: SRD 5.1 (CC-BY-4.0) as mirrored by <https://github.com/5e-bits/5e-database> at commit `a6212be` (`src/2014/en`: Spells, Monsters, Features, Subclasses, Classes, Conditions, Equipment, Weapon-Properties, Rule-Sections, Traits). The published PDF was **not** re-read; the first draft's statement that it was checked against the PDF was inaccurate and is removed.
- **Verified against the mirror in this revision**: spell names, levels, casting times and the 0-3 spell list (all 169); class and subclass features through level 5, fighting styles per class, eldritch invocations; conditions (including Petrified's poison/disease immunity); weapon properties and special rules (lance, net); armor stealth disadvantage and Strength requirements; cover bonuses and crawling cost; Hit Dice and surprise rules; every monster named in section 9 (existence, CR, and the trait or action cited), plus population counts (monsters, CR bands, multiattack, legendary, weapons, armor).
- **UNVERIFIED** (not checked against SRD text): Cloud of Daggers is **not** in the mirror (so not listed; a PDF check could confirm whether SRD 5.1 omits it); the exact SRD wording of Hide/Search/Influence as actions (these are 2024-style action names, kept as engine concepts only); the per-row sizes and owners in section 11 (estimates, not SRD facts); gear (116) and tools (31) beyond the category counts.
- **Code claims** ("current code" statements) were verified on `main` at `6012567`: `engine/rules` (types, encounter, spells), `engine/rules/content` (spell loader), `engine/core/sim` (content loader, features, combat), `schemas/{common,character,monster,spell}`, and `tools/validate`. The `Hide` row was re-verified in this revision (no passive-Perception comparison exists).
- Where an example monster list is inexact, treat it as illustrative; Tools & Content owns the authoritative per-monster classification (`srd_content_inventory.md`, `gap_crosswalk.md`).
