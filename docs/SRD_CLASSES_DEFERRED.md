# SRD 5.1 classes (Fighter, Cleric, Wizard, Sorcerer, Rogue, Paladin, Ranger, Barbarian, Bard, Druid levels 1-5): authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules, section 6), `docs/SRD_MONSTERS_DEFERRED.md` and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Class, level, feature and subclass data come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) commit `a6212be` (`src/2014/en/5e-SRD-Classes.json`, `5e-SRD-Levels.json`, `5e-SRD-Features.json`, `5e-SRD-Subclasses.json`; spell levels and class lists from `5e-SRD-Spells.json`). The fields used are copied verbatim into `tests/validate/data/srd51_classes_5e_database_a6212be.json`, and `tests/validate/test_srd51_classes.py` (ctest `content_validate_srd51_classes`) checks every document against them. The SRD subclasses are Champion (Fighter), Life Domain (Cleric), School of Evocation (Wizard), Draconic Bloodline (Sorcerer), Thief (Rogue), Oath of Devotion (Paladin), Hunter (Ranger), Path of the Berserker (Barbarian) and College of Lore (Bard).

## Representation

`character@4` describes one creature: `classes[]` with a `level` and an optional `subclass`, a total `level`, and a stat block. There is no class or level-progression document type. A class level is therefore authored as **one pre-generated character per class per level**: 50 documents, `srd51.character.<class>_level_<n>` (`characters/<class>_level_<n>.json`). The `_level_` infix is deliberate: `srd51.character.fighter_1` already belongs to `sample.vertical_slice`, which depends on `srd51.core`, so a duplicate id would break the pack set.

Every PC is a human (SRD human: +1 to every ability score) built from the standard array (15, 14, 13, 12, 10, 8). The Ability Score Improvement at level 4 is +2 to the class's main ability (Str, Wis, Int, Cha, Dex, Str, Dex, Str, Cha, Wis), and level 5 carries it forward. Hit points are the maximum hit die at level 1, then the fixed average (6 / 5 / 4) plus Con for each later level (d8 rogue: 5). XP is the threshold for the level. Weapons, armor and shields are `{item, equipped}` references to the pack's `item@1` documents. AC and every attack are derived from those items (base damage, no feature bonus folded in; `docs/CONTENT_EFFECTS.md` section 2) and are pinned by the test. Packs, foci and ammunition stay descriptive strings.

| class | subclass (from) | build | AC | weapons |
|---|---|---|---|---|
| Fighter | Champion (3) | Str 16 (18 at 4), Dex 14, Con 15; Athletics, Perception | 18: chain mail, shield | longsword (one-handed, Dueling), light crossbow |
| Cleric | Life Domain (1) | Wis 16 (18 at 4), Str 14, Con 15; Insight, Religion | 18: chain mail (heavy armor from Bonus Proficiency), shield | mace, light crossbow |
| Wizard | School of Evocation (2) | Int 16 (18 at 4), Dex 15, Con 14; Arcana, History | 12: no armor | dagger (melee and thrown) |
| Sorcerer | Draconic Bloodline (1), red (fire) ancestor | Cha 16 (18 at 4), Con 15, Dex 14; Arcana, Persuasion | 15: no armor, Draconic Resilience 13 + Dex | dagger (melee and thrown) |
| Rogue | Thief (3) | Dex 16 (18 at 4), Con 15, Wis 14; Acrobatics, Perception, Sleight of Hand, Stealth (Expertise: Stealth, Perception); climb 30 from 3 (Second-Story Work) | 14 (15 at 4): leather armor | rapier (finesse), shortbow |
| Paladin | Oath of Devotion (3) | Str 16 (18 at 4), Cha 15, Con 14; Athletics, Persuasion | 18: chain mail, shield | longsword (one-handed, Dueling), javelin |
| Ranger | Hunter (3) | Dex 16 (18 at 4), Wis 15, Con 14; Perception, Stealth, Survival | 14 (15 at 4): leather armor | longbow (Archery), shortsword |
| Barbarian | Path of the Berserker (3) | Str 16 (18 at 4), Con 15, Dex 14; Athletics, Perception; walk 40 at 5 (Fast Movement) | 14: no armor, Unarmored Defense 10 + Dex + Con | greataxe, handaxe (thrown) |
| Bard | College of Lore (3) | Cha 16 (18 at 4), Dex 15, Con 14; Deception, Performance, Persuasion (+ Arcana, Insight, Perception at 3; Expertise: Performance, Persuasion at 3) | 13: leather armor | rapier |
| Druid | Circle of the Land: Mountain (2) | Wis 16 (18 at 4), Dex 15, Con 14; Insight, Perception | 15: leather armor, shield (no metal) | scimitar |

## Authored as data

| feature | levels | `character@4` construct | played by the sim on main |
|---|---|---|---|
| Fighting Style: Dueling (Fighter) | 1-5 | `damage_bonus {amount 2, condition one_handed_melee_no_other_weapon}` | yes |
| Second Wind | 1-5 | `self_heal {1d10, plus fighter_level, uses 1, short_rest, bonus_action}` | yes |
| Action Surge (1 use) | 2-5 | `grant_action {cost free, usage per_rest 1 short}` | yes (row 13, #97) |
| Improved Critical (Champion) | 3-5 | `crit_range {min 19}` | yes (R-G5, #107) |
| Extra Attack | 5 | `extra_attack {attacks_per_action 2}` (character@4, EC #179): the Attack action makes two weapon attacks, any weapon, grapple / shove may replace one | yes (sim #179 `apply_extra_attack`, `Combatant::attacks_per_action`) |
| Ability Score Improvement | 4-5 | baked into `abilities` (and every derived number) | yes |
| Spellcasting (Cleric, Wizard) | 1-5 | `spellcasting {ability, caster_level, slots, spells}`; DC and attack bonus derived, no override | yes |
| Domain Spells (Life) | 1-5 | Cure Wounds in `spellcasting.spells` (always prepared); the others are not shipped (below) | yes |
| Divine Domain / Martial Archetype / Arcane Tradition | 1 / 3 / 2 | `classes[0].subclass` = `life` / `champion` / `evocation` | descriptive |
| Bonus Proficiency (Life: heavy armor) | 1-5 | descriptive feature; its only combat effect is the worn chain mail, already in AC | n/a |
| Arcane Recovery (Wizard) | 1-5 | `slot_recovery {class wizard, max_slot_level 5, uses 1}` (VERSION_BUMP_BATCH 7.2n); budget ceil(wizard level / 2) is derived from `classes[]`, the SRD text stays in `description` | yes (sim #148 `apply_slot_recovery`) |
| Evocation Savant | 2-5 | descriptive feature (downtime spell copying cost, out of combat scope) | n/a |
| Spellcasting (Sorcerer) | 1-5 | `spellcasting {ability cha, caster_level, slots, spells}` (spells known: the shipped sorcerer spells, below) | yes |
| Sorcerous Origin | 1 | `classes[0].subclass` = `draconic` | descriptive |
| Dragon Ancestor (Draconic) | 1-5 | descriptive feature (red, fire; the damage type is used only by features of 6th level and higher; Draconic is in `languages`) | n/a |
| Draconic Resilience (Draconic) | 1-5 | baked into the stat block: `hit_points` +1 per sorcerer level, `armor_class` 13 + Dex (no armor); descriptive feature, no `effect` | yes (static numbers) |
| Font of Magic, Flexible Casting (both directions) | 2-5 | `font_of_magic {pool sorcery_points}` (VERSION_BUMP_BATCH 7.2o) + `resources[]` `sorcery_points {max = sorcerer level, recharge long_rest}` | yes (sim #152: pool, `FlexibleCasting` bonus action) |
| Metamagic (Quickened Spell, Twinned Spell) | 3-5 | `metamagic {options [quickened, twinned], pool sorcery_points}` (R-G14), the same pool as Font of Magic | yes (sim #143) |
| Sneak Attack (Rogue) | 1-5 | `on_event {on hit, limit once_per_turn, when [weapon finesse_or_ranged, advantage_or_ally_adjacent], effects [extra_damage 1d6 same_as_weapon, scale.per_levels {class rogue, every 2, add_dice 1d6}]}`: 1d6 / 2d6 / 3d6 at 1 / 3 / 5 (the mirror's column). `advantage_or_ally_adjacent` is net advantage, or an ally of the attacker within 5 ft of the target and no net disadvantage | yes (sim #158 triggers, `trigger_content_tests`) |
| Cunning Action (Rogue) | 2-5 | `bonus_action_grant {actions [dash, disengage, hide]}` | yes |
| Expertise (Rogue) | 1-5 | Stealth and Perception at double proficiency, baked into `skills` and passive Perception (the validator accepts mod + 2 x PB); descriptive feature, no `effect` | yes (static numbers) |
| Roguish Archetype | 3 | `classes[0].subclass` = `thief` | descriptive |
| Second-Story Work (Thief) | 3-5 | **partial**: "climbing no longer costs you extra movement" is `speed.climb` = walking speed (30; read as `Combatant::climb_ft`, #184). The running-jump bonus (+Dex mod feet) stays in the description: jumping is not simulated | climb: yes; jump: no |
| Thieves' Cant (Rogue) | 1-5 | descriptive feature + `languages` entry (no combat rule) | n/a |
| Lay on Hands (Paladin) | 1-5 | `resources[]` `lay_on_hands {max {per_level 5, class paladin}, recharge long_rest}` + `heal_pool {cost action, pool lay_on_hands}`. **Partial**: rules do not check touch range or refuse undead / construct targets (`heal_pool` takes any living target), and the 5-point disease / poison cure stays in the description | yes (sim row 13 `apply_heal_pool`) |
| Fighting Style: Dueling (Paladin) | 2-5 | as the fighter. Defense (`ac_bonus {1, requires_armor}`) also loads; Dueling matches the one-handed longsword + shield build | yes |
| Spellcasting (Paladin) + Oath Spells | 2-5 | `spellcasting {ability cha, caster_level, slots, spells}` from 2nd level (no block at 1st: the half caster has no slots). Spells: Cure Wounds, Shield of Faith; Lesser Restoration (oath, always prepared) at 5 | yes |
| Divine Smite (Paladin) | 2-5 | `on_event {on hit, choice ask, when [weapon melee], effects [spend slot any, extra_damage 2d8 radiant, scale.per_slot_level {base_level 1, add_dice 1d8}]}`. **Partial**: the extra 1d8 against an undead or a fiend is not modelled (a rider `when` inside `on_event effects[]` is ignored, and a second `on_event` cannot know the smite fired). The 5d8 cap is unreachable at these levels | yes (sim #158, `choice: ask` = HitRider window) |
| Sacred Oath (Paladin) | 3 | `classes[0].subclass` = `devotion` | descriptive |
| Extra Attack (Paladin) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Fighting Style: Archery (Ranger) | 2-5 | `attack_bonus {attacks ranged, amount 2}` (R-G5); the longbow `to_hit` stays unbaked (dex + proficiency) | yes (sim #107) |
| Spellcasting (Ranger) | 2-5 | `spellcasting {ability wis, caster_level, slots, spells}` from 2nd level. Spells known: Cure Wounds (the only shipped 1st-level ranger spell, so 1 of 2-3 known at 2-4); at 5 also Barkskin, Lesser Restoration, Spike Growth (4 of 4) | yes |
| Ranger Archetype | 3 | `classes[0].subclass` = `hunter` | descriptive |
| Extra Attack (Ranger) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Unarmored Defense (Barbarian) | 1-5 | baked into `armor_class` (10 + Dex + Con, no armor; the test derives it); descriptive feature, no `effect` | yes (static number) |
| Reckless Attack (Barbarian) | 2-5 | `reckless_attack` (sim #129: `ActionRequest::reckless` on the turn's first attack; advantage on Str melee weapon attacks, attacks against you have advantage until your next turn). The built-in AI never declares it | yes (caller-declared) |
| Primal Path | 3 | `classes[0].subclass` = `berserker` | descriptive |
| Extra Attack (Barbarian) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Fast Movement (Barbarian) | 5 | baked into `speed.walk` 40 (no heavy armor is ever worn); descriptive feature, no `effect` | yes (static number) |
| Spellcasting (Bard) | 1-5 | `spellcasting {ability cha, caster_level, slots, spells}`: Vicious Mockery; Cure Wounds, Healing Word, Thunderwave; + Hold Person, Lesser Restoration, Silence at 3; + Dispel Magic, Stinking Cloud at 5 (every shipped bard spell up to the top slot; 1 of 2-3 cantrips) | yes |
| Cutting Words (Lore) | 3-5 | `reactions[]` `{window roll_attack, effect subtract_roll 1d6 (1d8 at 5), usage {pool bardic_inspiration, cost 1}}` (R-G7, sim #114). **Partial**: attack rolls only; subtracting from an ability check or a damage roll, and the charm-immunity exception, are not modelled | yes |
| Bardic Inspiration pool (Bard) | 3-5 | `resources[]` `bardic_inspiration {max {plus_ability cha, min 1}, recharge long_rest}` (short_rest at 5). Declared from 3rd only, where Cutting Words spends it (a pool nothing spends is a check-12 warning) | yes |
| Font of Inspiration (Bard) | 5 | the pool's `recharge: short_rest`; descriptive feature, no `effect` | yes (pool recharge) |
| Expertise, Bonus Proficiencies (Bard, Lore) | 3-5 | baked into `skills` (Performance and Persuasion doubled; Arcana, Insight, Perception added); descriptive features, no `effect` | yes (static numbers) |
| Bard College | 3 | `classes[0].subclass` = `lore` | descriptive |
| Spellcasting (Druid) | 1-5 | `spellcasting {ability wis, caster_level, slots, spells}`: Produce Flame, Poison Spray; Cure Wounds, Healing Word, Thunderwave; + Barkskin, Hold Person, Lesser Restoration at 3; + Dispel Magic at 5 (every shipped druid spell up to the top slot; the list doubles as the prepared list, within Wis modifier + level) | yes |
| Circle Spells (Land: Mountain) | 3-5 | always prepared, in `spellcasting.spells`: Spike Growth (3), Lightning Bolt (5; not a druid spell, allowed through `subclass_spells`). Mountain is chosen because it gives the most shipped spells that add something (Arctic's two shipped ones are druid spells anyway; Desert, Coast, Forest and Swamp give one; Grassland none). Spider Climb (3) and Meld into Stone (5) are not shipped (see the spells section). Descriptive feature, no `effect` | yes (spell list) |
| Wild Shape (Druid) | 2-5 | `wild_shape {pool wild_shape, forms [jackal, wolf, boar, axe_beak, giant_poisonous_snake, black_bear], max_cr_by_level [{2, 1/4}, {4, 1/2}]}` (R-G12, EC #176) + `resources[]` `wild_shape {max 2, recharge short_rest}`. `no_fly_below` 8 / `no_swim_below` 4 are left at the engine defaults (= SRD). Forms: every shipped beast of CR 1/2 or below without a fly speed; the engine refuses the giant poisonous snake (swim 30, #184) and the black bear (CR 1/2) below 4th. Not listed: the swarm of rats (a swarm of Tiny beasts, not a single beast you have seen), the dire wolf and giant spider (CR 1: from 8th). Duration (half druid level in hours), revert at 0 HP and no casting in beast form are engine-side | yes |
| Natural Recovery (Land) | 2-5 | `slot_recovery {class druid, max_slot_level 5, uses 1}` (sim #148); budget ceil(druid level / 2) derived | yes |
| Druid Circle | 2 | `classes[0].subclass` = `land` | descriptive |

The validator change in this batch: `attack_bonus`, `crit_range` and `ac_bonus` (sim #107 hooks) are now known effect kinds at `@4` in `tools/validate/checks_v4.py` (until now they produced an "unknown effect kind" warning). Their fields are checked the way the sim checks them at load. The `@<=3` rows are unchanged.

**Extra Attack (done).** The fighter 5 feature `extra_attack` now carries the real kind `{"kind": "extra_attack", "attacks_per_action": 2}` (EC #179) instead of the earlier stand-in, a `multiattack` option `longsword_x2` (two longsword attacks). With the real kind, mixed weapons and grapple / shove replacing an attack are covered by the rules (`TurnBudget::attacks_remaining`). The kind is loaded only from `character@4` files: in a `character@3` (or older) file the sim lists it inert, and the 3 -> 4 migration does not carry it (the migration's kind list is frozen), so the validator gives the unknown-kind warning there.

**Fighting style choice.** One style is picked: Dueling, which fits the shield build. The other SRD fighter styles would be: Archery `attack_bonus {ranged, 2}` and Defense `ac_bonus {1, requires_armor}` (expressible since #107); Two-Weapon Fighting via the `two_weapon_fighting` feature-id convention (`docs/CONTENT_CONVENTIONS.md` section 2); Protection, a `reactions[]` entry (`ally_attacked` / `impose_disadvantage`, shield required); Great Weapon Fighting, not expressible (rules have `Combatant::great_weapon_fighting`, R-G7 #103, but no content kind or loader sets it).

## Descriptive only: mechanic missing (Rogue)

The Rogue features below are kept as a `features[]` entry with the SRD text and **no `effect`**. Unlike the cleric rows further down, they are not left out: the SRD text documents the character, and nothing approximates the rule (the test checks the entries carry no `effect` and that no `reactions[]` stand-in exists).

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Uncanny Dodge (`uncanny-dodge`) | Rogue 5 | The only reduce-damage reaction window that is compiled is `damaged` (`compile_reactions`, sim). It opens for **any** damage after mitigation, including save spells, zones and riders, so `{window damaged, effect reduce_damage half}` would also halve a Fireball. `melee_only` / `ranged_only` do not exclude non-attack damage, `being_hit` is not compiled for `reduce_damage`, and `visible` is read only to refuse `false`. Needs either an `attack_only` flag on the `damaged` window (attack damage only, the attacker is the damager) plus a seen-attacker check, or `reduce_damage` compiled on `being_hit` | Rules / Engine Core (row 11 reactions, R-G7; vision 7.2x for "an attacker that you can see") |
| Fast Hands (`fast-hands`) | Rogue (Thief) 3 | Adds Sleight of Hand checks, thieves' tools (traps, locks) and Use an Object to the Cunning Action bonus action. `bonus_action_grant.actions` is closed to dash / disengage / hide / dodge, and the sim has no Use an Object, Sleight of Hand or thieves' tools action | Rules (actions), T&C (enum value when an action exists) |
| Expertise (`rogue-expertise-1`) | Rogue 1 | Not a gap: fully expressed as baked skill numbers; the entry has no `effect` because no hook reads it | n/a |
| Second-Story Work, running jump | Rogue (Thief) 3 | Jump distance: the sim has no jumping | Rules (movement) |

## Descriptive only: mechanic missing (Paladin)

Kept as a `features[]` entry with the SRD text and no `effect` (the test pins that), same rule as the Rogue section.

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Divine Sense (`divine-sense`) | Paladin 1 | Knowing the location and type of celestials / fiends / undead within 60 ft (not behind total cover) until the end of the next turn, 1 + Cha uses per long rest. The sim has no hidden-location or creature-type reveal for a feature; vision (#194, 7.2x) is not on main as a content hook | Rules (vision 7.2x) |
| Divine Health (`divine-health`) | Paladin 3 | Immunity to disease. Diseases are not modelled anywhere (no condition id) | - |
| Channel Divinity (`channel-divinity`) | Paladin 3 | The pool fits (`resources[]` 1 per short rest), but neither option below loads, so a pool nothing spends would be a check-12 warning and do nothing (same as the cleric) | R-G5 |
| Channel Divinity: Sacred Weapon (`channel-divinity-sacred-weapon`) | Paladin (Devotion) 3 | `apply_modifier {attack_roll, bonus = Cha mod (min 1), rounds 10}` would fit, but the sim lists an `apply_modifier` feature with `usage` as unsupported (`apply_apply_modifier`: "usage needs resource pools"), and without `usage` it would be unlimited. Also: no weapon scope (any attack roll), the weapon does not become magical, the light is out of scope | Engine Core (pool `usage` on `apply_modifier`), Rules (weapon-scoped modifier) |
| Channel Divinity: Turn the Unholy (`channel-divinity-turn-the-unholy`) | Paladin (Devotion) 3 | Rules #188 (7.2v) added `control: turned` on the spell@1 `save` effect only. A character feature has no key that reaches it: `save_effect` actions do not take `control`, cannot spend a `resources[]` pool, and cannot filter fiends / undead; the "within 30 ft that can see or hear you" target list is the pending sim hand-off (EC) | Rules / Engine Core (character key for a 7.2v control save, pool usage, creature-type target filter) |

## Descriptive only: mechanic missing (Ranger)

Kept as a `features[]` entry with the SRD text and no `effect` (the test pins that), same rule as the Rogue and Paladin sections.

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Favored Enemy (`favored-enemy-1-type`) | Ranger 1 | Beasts chosen. Advantage on Wisdom (Survival) checks to track and Intelligence checks to recall information about them, plus a language: exploration / social only. No combat rule in SRD 5.1 (no damage bonus), so nothing is missing for combat | - (out of combat scope) |
| Natural Explorer (`natural-explorer-1-terrain-type`) | Ranger 1 | Forest chosen. Doubled proficiency on Int / Wis checks about the terrain, travel pace, foraging, tracking, no getting lost: overland travel, not simulated | - (out of combat scope) |
| Primeval Awareness (`primeval-awareness`) | Ranger 3 | Spend an action and a ranger slot to sense aberrations, celestials, dragons, elementals, fey, fiends and undead within 1 mile (6 in favored terrain) for 1 minute per slot level; no locations or numbers. Beyond any encounter map, and no feature key spends a spell slot outside an `on_event` trigger | Rules (vision 7.2x for a type reveal), Engine Core (a slot-spending active feature) |
| Hunter's Prey: Colossus Slayer (`hunters-prey`, `hunters-prey-colossus-slayer`) | Ranger (Hunter) 3 | Fits `on_event {on hit, limit once_per_turn, when [weapon_attack, <target below its hit point maximum>], effects [extra_damage 1d8 same_as_weapon]}` except the predicate: no predicate tests the **target's** HP (`self_hp_below` is the owner's; rules has `kWhenTargetDamaged` only as a monster attack trait flag, not as a `when[]` kind). Giant Killer (a reaction to a Large+ attacker's attack, hit or miss) and Horde Breaker (an extra attack against a second target within 5 ft) are not chosen; neither has a key either | Rules (predicate `target_below_max_hp`; T&C adds the schema value) |

## Descriptive only: mechanic missing (Barbarian)

Kept as a `features[]` entry with the SRD text and no `effect` (the test pins that), same rule as the sections above. No `resources[]` rage pool is declared: nothing loaded would spend it (check 12).

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Rage (`rage`) | Barbarian 1 | The shape exists (the `tests/validate/fixtures/v4` barbarian: `apply_modifier {cost bonus_action, usage {pool rage}, modifiers [damage bonus_table 2/3/4, resistance bludgeoning / piercing / slashing], rounds 10}` + `resources[]` rage 2/3/4/...), but the sim lists an `apply_modifier` feature with `usage` as unsupported (`apply_apply_modifier`: "usage needs resource pools"), and without `usage` Rage would be unlimited. Also missing even with the pool: the damage bonus only on **melee weapon attacks using Strength** (a `damage` modifier adds to every damage roll), advantage on Strength checks and saves (`save` / `ability_check` modifiers with `mode advantage, ability str` would load), no spellcasting or concentration while raging, the early end (no attack or damage taken since your last turn, unconscious), ending it as a bonus action, and "not wearing heavy armor" | Engine Core (pool `usage` on `apply_modifier`), Rules (melee-Str damage scope, rage end conditions) |
| Danger Sense (`danger-sense`) | Barbarian 2 | Advantage on Dexterity saves against effects you can see, unless blinded, deafened or incapacitated. No static (always-on) save modifier key: `apply_modifier` is an active feature (action / bonus action), and no predicate tests "the effect's source is visible" or the owner's conditions | Rules (static save advantage with condition gates; vision 7.2x) |
| Frenzy (`frenzy`) | Barbarian (Berserker) 3 | A single melee weapon attack as a bonus action on each later turn of a rage, then one level of exhaustion when the rage ends. **Note:** `frenzy` is also a sim feature-id convention (`docs/CONTENT_CONVENTIONS.md` section 2: a bonus melee attack while the `rage` feature's first modifier is in force), so the descriptive entry already compiles that option, but it is never available while Rage has no live `apply_modifier` (the validator warns, by design). It turns on by itself once Rage is mechanical. Still missing then: the exhaustion when the rage ends (`exhaustion` is a condition id, but `on_event.on` has no effect-ended event) | Engine Core (Rage first), Rules (exhaustion on effect end) |

## Descriptive only: mechanic missing (Bard)

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Bardic Inspiration, granting the die (`bardic-inspiration-d6`, `-d8`) | Bard 1, 5 | A bonus action gives one creature within 60 ft that can hear you a d6 (d8 at 5) to add to one ability check, attack roll or save within 10 minutes, after seeing the d20. No content key grants a held die to another creature: `apply_modifier {stat attack_roll / save, die 1d6, consume next_attack}` would apply it unconditionally and to one roll kind per row, not as the ally's choice; and `apply_modifier` with a pool `usage` is not loaded (same blocker as Rage). So levels 1-2 declare no pool | Engine Core (pool `usage` on `apply_modifier`), Rules (an inspiration token: held die, any of three roll kinds, spent by choice) |
| Jack of All Trades (`jack-of-all-trades`) | Bard 2 | Half proficiency (rounded down) on ability checks without proficiency. Its only combat effect is initiative (a Dexterity check) and contested checks such as escaping a grapple; no content key adds to unproficient checks or initiative | Rules (check bonus / initiative bonus key) |
| Song of Rest (`song-of-rest-d6`) | Bard 2 | Extra 1d6 HP to allies who spend Hit Dice during a short rest: rests and Hit Dice are not simulated inside an encounter | - (out of combat scope) |

## Descriptive only: no combat rule (Druid)

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Druidic (`druidic`) | Druid 1 | A secret language and hidden messages (DC 15 Perception to spot): no combat rule. `Druidic` is also in `languages` | - (out of combat scope) |
| Bonus Cantrip (`bonus-cantrip`) | Druid (Land) 2 | One extra druid cantrip. Both shipped druid cantrips (Produce Flame, Poison Spray) are already known from 1st; Druidcraft, Guidance, Mending, Resistance and Shillelagh are not shipped, so there is nothing to add | T&C (author a druid cantrip, e.g. Shillelagh) |
| Circle of the Land terrain (`circle-of-the-land-mountain`) | Druid (Land) 2 | The land choice only selects the Circle Spells list; no rule of its own | - |

## Deferred (feature not expressible today)

Each listed feature is **left out of `features[]`**: no descriptive stand-in, same rule as the monster batch. The test checks that none of these ids appear in the documents.

| feature (mirror index) | class / level | missing mechanic | gap id |
|---|---|---|---|
| Disciple of Life (`disciple-of-life`) | Cleric (Life) 1 | Heal bonus of 2 + spell level on the caster's healing spells (level 1+). Nothing in `common@2` adds to a spell's healing; `on_event` has no "cast a heal spell" trigger | R-G4 (heal-spell rider) |
| Channel Divinity (`channel-divinity-1-rest`) | Cleric 2 | The pool itself fits (`resources[]` 1 per short rest), but both of its effects below are deferred. A pool no feature spends would be a check-12 warning and would do nothing | R-G5 |
| Channel Divinity: Turn Undead (`channel-divinity-turn-undead`) | Cleric 2 | The "turned" condition (must move away, Dash only, no reactions, ends on damage) has no condition id or AI behaviour. `save_effect` also cannot spend a feature pool or filter by creature type (undead within 30 ft that can see or hear you) | R-G5, R-G8, R-G10 |
| Channel Divinity: Preserve Life (`channel-divinity-preserve-life`) | Cleric (Life) 2 | Splits a pool of 5 x cleric level HP among any creatures within 30 ft, each capped at half its HP maximum, not undead or constructs. `heal_pool` heals one target from a pool that persists across uses; there is no split, cap, or per-use pool size | R-G5, R-G10, R-G15 |
| Destroy Undead (`destroy-undead-cr-1-2-or-below`) | Cleric 5 | A Turn Undead outcome (destroy at CR 1/2 or lower); depends on Turn Undead | R-G5 |

### Spells the documents cannot reference yet

`spellcasting.spells` may only name a `spell@1` the pack ships (validator check 3). Each document lists the shipped spells of its class (or domain) list up to its highest slot level:

| class | levels | spells listed |
|---|---|---|
| Cleric | 1-2 | Sacred Flame; Cure Wounds (domain), Healing Word, Inflict Wounds |
| Cleric | 3-4 | + Hold Person, Lesser Restoration (domain) |
| Cleric | 5 | + Mass Healing Word, Revivify (domain) |
| Paladin | 2-4 | Cure Wounds, Shield of Faith |
| Paladin | 5 | + Lesser Restoration (oath) |
| Ranger | 2-4 | Cure Wounds |
| Ranger | 5 | + Barkskin, Lesser Restoration, Spike Growth |
| Wizard | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Wizard | 3-4 | + Hold Person, Scorching Ray |
| Wizard | 5 | + Counterspell |
| Druid | 1-2 | Produce Flame, Poison Spray; Cure Wounds, Healing Word, Thunderwave |
| Druid | 3-4 | + Barkskin, Hold Person, Lesser Restoration, Spike Growth (circle) |
| Druid | 5 | + Dispel Magic, Lightning Bolt (circle) |
| Sorcerer | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Sorcerer | 3-4 | + Hold Person, Scorching Ray |
| Sorcerer | 5 | + Counterspell |

Deferred until the spells are authored (`docs/SRD_SPELLS_DEFERRED.md` gives the reason for each):

- **Life domain spells** (always prepared): `srd51.spell.bless` (1), `srd51.spell.spiritual_weapon` (3), `srd51.spell.beacon_of_hope` (5). Lesser Restoration (3) and Revivify (5) ship since spells batch 2 and are listed in the cleric documents.
- **Oath of Devotion spells** (always prepared): `srd51.spell.protection_from_evil_and_good` (3), `srd51.spell.sanctuary` (3), `srd51.spell.zone_of_truth` (5) are not authored; Lesser Restoration (5) ships and is listed.
- **Circle of the Land (Mountain) spells** (always prepared): `srd51.spell.spider_climb` (3), `srd51.spell.meld_into_stone` (5) are not authored; Spike Growth (3) and Lightning Bolt (5) ship and are listed.
- **Cantrips known**: the Cleric knows 3 (4 from level 4) but the pack ships one cleric cantrip (Sacred Flame). The Wizard knows 3 (4 from level 4), and the pack ships three wizard cantrips. Missing: Guidance, Light, Mending, Resistance, Spare the Dying, Thaumaturgy (cleric); Chill Touch, Ray of Frost, Shocking Grasp, True Strike and the utility cantrips (wizard). The Sorcerer knows 4 cantrips (5 from level 4) and 2-6 spells; the pack ships three sorcerer cantrips and five leveled sorcerer spells (Chill Touch, Ray of Frost, Shocking Grasp, Burning Hands, Chromatic Orb, Sleep, Thunderwave, Misty Step and the rest are not shipped), so each document lists fewer.
- **Spellbook and prepared spells**: a level 1 wizard's spellbook holds six 1st-level spells, plus two per level after that; a cleric prepares Wis modifier + level spells. The documents list fewer because the rest are not shipped (Burning Hands, Mage Armor, Sleep, Thunderwave, Misty Step, Fireball, Lightning Bolt, Bless, Guiding Bolt, Shield of Faith, Spiritual Weapon, Spirit Guardians, ...). The `spells` list doubles as the prepared list. `character@4` has no separate spellbook.

## Notes for Rules / Engine Core

* The 20 documents load through the sim's `ContentLibrary` without errors. They use the constructs of the shipped fighter plus `grant_action`, `crit_range` and `multiattack`, all of which the sim already compiles (`static_feature_tests`, `features_tests`, `multiattack_content_tests`). One C++ test hardcodes pack ownership: `engine/core/sim/tests/packset_tests.cpp` assumes every unlisted id that is not `srd51.monster.*` belongs to `sample.vertical_slice`. It needs `srd51.character.*` added to the `srd51.core` default (one-line test change, Engine Core).
* `procgen::spawn_archetypes` and the vertical-slice spawns still name only `srd51.monster.goblin` and `srd51.character.fighter_1`, so procgen outputs and goldens are untouched.
* The cleric's `bonus_proficiency` and the wizard's `evocation_savant` have no `effect`. The sim lists them in `Archetype::unsupported_features` as "(no effect)", the same as descriptive monster traits.
