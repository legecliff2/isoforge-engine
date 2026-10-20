# SRD 5.1 classes, first batch (Fighter, Cleric, Wizard, Sorcerer levels 1-5): authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules, section 6), `docs/SRD_MONSTERS_DEFERRED.md` and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Class, level, feature and subclass data come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) commit `a6212be` (`src/2014/en/5e-SRD-Classes.json`, `5e-SRD-Levels.json`, `5e-SRD-Features.json`, `5e-SRD-Subclasses.json`; spell levels and class lists from `5e-SRD-Spells.json`). The fields used are copied verbatim into `tests/validate/data/srd51_classes_5e_database_a6212be.json`, and `tests/validate/test_srd51_classes.py` (ctest `content_validate_srd51_classes`) checks every document against them. The SRD subclasses are Champion (Fighter), Life Domain (Cleric) and School of Evocation (Wizard).

## Representation

`character@4` describes one creature: `classes[]` with a `level` and an optional `subclass`, a total `level`, and a stat block. There is no class or level-progression document type. A class level is therefore authored as **one pre-generated character per class per level**: 15 documents, `srd51.character.<class>_level_<n>` (`characters/<class>_level_<n>.json`). The `_level_` infix is deliberate: `srd51.character.fighter_1` already belongs to `sample.vertical_slice`, which depends on `srd51.core`, so a duplicate id would break the pack set.

Every PC is a human (SRD human: +1 to every ability score) built from the standard array (15, 14, 13, 12, 10, 8). The Ability Score Improvement at level 4 is +2 to the class's main ability (Str, Wis, Int), and level 5 carries it forward. Hit points are the maximum hit die at level 1, then the fixed average (6 / 5 / 4) plus Con for each later level. XP is the threshold for the level. Weapons, armor and shields are `{item, equipped}` references to the pack's `item@1` documents. AC and every attack are derived from those items (base damage, no feature bonus folded in; `docs/CONTENT_EFFECTS.md` section 2) and are pinned by the test. Packs, foci and ammunition stay descriptive strings.

| class | subclass (from) | build | AC | weapons |
|---|---|---|---|---|
| Fighter | Champion (3) | Str 16 (18 at 4), Dex 14, Con 15; Athletics, Perception | 18: chain mail, shield | longsword (one-handed, Dueling), light crossbow |
| Cleric | Life Domain (1) | Wis 16 (18 at 4), Str 14, Con 15; Insight, Religion | 18: chain mail (heavy armor from Bonus Proficiency), shield | mace, light crossbow |
| Wizard | School of Evocation (2) | Int 16 (18 at 4), Dex 15, Con 14; Arcana, History | 12: no armor | dagger (melee and thrown) |
| Sorcerer | Draconic Bloodline (1), red (fire) ancestor | Cha 16 (18 at 4), Con 15, Dex 14; Arcana, Persuasion | 15: no armor, Draconic Resilience 13 + Dex | dagger (melee and thrown) |

## Authored as data

| feature | levels | `character@4` construct | played by the sim on main |
|---|---|---|---|
| Fighting Style: Dueling (Fighter) | 1-5 | `damage_bonus {amount 2, condition one_handed_melee_no_other_weapon}` | yes |
| Second Wind | 1-5 | `self_heal {1d10, plus fighter_level, uses 1, short_rest, bonus_action}` | yes |
| Action Surge (1 use) | 2-5 | `grant_action {cost free, usage per_rest 1 short}` | yes (row 13, #97) |
| Improved Critical (Champion) | 3-5 | `crit_range {min 19}` | yes (R-G5, #107) |
| Extra Attack | 5 | `multiattack` option `longsword_x2` = two longsword attacks with one Attack action | yes (declared sequence, #48); see the partial note below |
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

The validator change in this batch: `attack_bonus`, `crit_range` and `ac_bonus` (sim #107 hooks) are now known effect kinds at `@4` in `tools/validate/checks_v4.py` (until now they produced an "unknown effect kind" warning). Their fields are checked the way the sim checks them at load. The `@<=3` rows are unchanged.

**Partial: Extra Attack.** The sim now loads the character@4 kind `extra_attack {attacks_per_action: 2..4}` into `Combatant::attacks_per_action` (feat/extra-attack); the shipped fighter still uses the multiattack sequence below until T&C switches it. The fighter declares the equivalent multiattack sequence for its main weapon, two longsword attacks (reactions and movement are allowed between them, #48). These are not offered: mixed sequences (longsword then crossbow, which `loading` would limit to one shot anyway), replacing an attack with a grapple or shove (R-G8), and the attack count rising with level. Once a loader kind for `attacks_per_action` lands, swap this effect for it.

**Fighting style choice.** One style is picked: Dueling, which fits the shield build. The other SRD fighter styles would be: Archery `attack_bonus {ranged, 2}` and Defense `ac_bonus {1, requires_armor}` (expressible since #107); Two-Weapon Fighting via the `two_weapon_fighting` feature-id convention (`docs/CONTENT_CONVENTIONS.md` section 2); Protection, a `reactions[]` entry (`ally_attacked` / `impose_disadvantage`, shield required); Great Weapon Fighting, not expressible (rules have `Combatant::great_weapon_fighting`, R-G7 #103, but no content kind or loader sets it).

## Deferred (feature not expressible today)

Each listed feature is **left out of `features[]`**: no descriptive stand-in, same rule as the monster batch. The test checks that none of these ids appear in the documents.

| feature (mirror index) | class / level | missing mechanic | gap id |
|---|---|---|---|
| Disciple of Life (`disciple-of-life`) | Cleric (Life) 1 | Heal bonus of 2 + spell level on the caster's healing spells (level 1+). Nothing in `common@2` adds to a spell's healing; `on_event` has no "cast a heal spell" trigger | R-G4 (heal-spell rider) |
| Channel Divinity (`channel-divinity-1-rest`) | Cleric 2 | The pool itself fits (`resources[]` 1 per short rest), but both of its effects below are deferred. A pool no feature spends would be a check-12 warning and would do nothing | R-G5 |
| Channel Divinity: Turn Undead (`channel-divinity-turn-undead`) | Cleric 2 | The "turned" condition (must move away, Dash only, no reactions, ends on damage) has no condition id or AI behaviour. `save_effect` also cannot spend a feature pool or filter by creature type (undead within 30 ft that can see or hear you) | R-G5, R-G8, R-G10 |
| Channel Divinity: Preserve Life (`channel-divinity-preserve-life`) | Cleric (Life) 2 | Splits a pool of 5 x cleric level HP among any creatures within 30 ft, each capped at half its HP maximum, not undead or constructs. `heal_pool` heals one target from a pool that persists across uses; there is no split, cap, or per-use pool size | R-G5, R-G10, R-G15 |
| Destroy Undead (`destroy-undead-cr-1-2-or-below`) | Cleric 5 | A Turn Undead outcome (destroy at CR 1/2 or lower); depends on Turn Undead | R-G5 |

### Other classes (no character in this batch)

| feature | class / level | status | gap id |
|---|---|---|---|
| Natural Recovery (Circle of the Land) | Druid (Land) 2 | Expressible as `slot_recovery {class druid, max_slot_level 5, uses 1}` (VERSION_BUMP_BATCH 7.2n; the SRD "finish a long rest" reset is the kind's `uses` per long rest), but no druid character ships, so nothing carries it. Deferred to the druid documents. The loader (sim #148) already accepts `class druid` | R-G16 (wiring), 7.2n |

### Spells the documents cannot reference yet

`spellcasting.spells` may only name a `spell@1` the pack ships (validator check 3). Each document lists the shipped spells of its class (or domain) list up to its highest slot level:

| class | levels | spells listed |
|---|---|---|
| Cleric | 1-2 | Sacred Flame; Cure Wounds (domain), Healing Word, Inflict Wounds |
| Cleric | 3-4 | + Hold Person |
| Cleric | 5 | + Mass Healing Word |
| Wizard | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Wizard | 3-4 | + Hold Person, Scorching Ray |
| Wizard | 5 | + Counterspell |
| Sorcerer | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Sorcerer | 3-4 | + Hold Person, Scorching Ray |
| Sorcerer | 5 | + Counterspell |

Deferred until the spells are authored (`docs/SRD_SPELLS_DEFERRED.md` gives the reason for each):

- **Life domain spells** (always prepared): `srd51.spell.bless` (1), `srd51.spell.lesser_restoration` (3), `srd51.spell.spiritual_weapon` (3), `srd51.spell.beacon_of_hope` (5), `srd51.spell.revivify` (5).
- **Cantrips known**: the Cleric knows 3 (4 from level 4) but the pack ships one cleric cantrip (Sacred Flame). The Wizard knows 3 (4 from level 4), and the pack ships three wizard cantrips. Missing: Guidance, Light, Mending, Resistance, Spare the Dying, Thaumaturgy (cleric); Chill Touch, Ray of Frost, Shocking Grasp, True Strike and the utility cantrips (wizard). The Sorcerer knows 4 cantrips (5 from level 4) and 2-6 spells; the pack ships three sorcerer cantrips and five leveled sorcerer spells (Chill Touch, Ray of Frost, Shocking Grasp, Burning Hands, Chromatic Orb, Sleep, Thunderwave, Misty Step and the rest are not shipped), so each document lists fewer.
- **Spellbook and prepared spells**: a level 1 wizard's spellbook holds six 1st-level spells, plus two per level after that; a cleric prepares Wis modifier + level spells. The documents list fewer because the rest are not shipped (Burning Hands, Mage Armor, Sleep, Thunderwave, Misty Step, Fireball, Lightning Bolt, Bless, Guiding Bolt, Shield of Faith, Spiritual Weapon, Spirit Guardians, ...). The `spells` list doubles as the prepared list. `character@4` has no separate spellbook.

## Notes for Rules / Engine Core

* The 20 documents load through the sim's `ContentLibrary` without errors. They use the constructs of the shipped fighter plus `grant_action`, `crit_range` and `multiattack`, all of which the sim already compiles (`static_feature_tests`, `features_tests`, `multiattack_content_tests`). One C++ test hardcodes pack ownership: `engine/core/sim/tests/packset_tests.cpp` assumes every unlisted id that is not `srd51.monster.*` belongs to `sample.vertical_slice`. It needs `srd51.character.*` added to the `srd51.core` default (one-line test change, Engine Core).
* `procgen::spawn_archetypes` and the vertical-slice spawns still name only `srd51.monster.goblin` and `srd51.character.fighter_1`, so procgen outputs and goldens are untouched.
* The cleric's `bonus_proficiency` and the wizard's `evocation_savant` have no `effect`. The sim lists them in `Archetype::unsupported_features` as "(no effect)", the same as descriptive monster traits.
