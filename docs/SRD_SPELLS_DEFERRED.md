# SRD 5.1 spells, levels 0-3: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules; section 8 and 10.2) and the `content/packs/srd51.core` pack.

**Source.** Spell text, levels, schools, classes, components, ranges and dice come from the [5e-bits/5e-database](https://github.com/5e-bits/5e-database) JSON mirror (`src/2014/en/5e-SRD-Spells.json`, commit `a6212beb4b278917c2cff41c2b04c4ef4f3e0d6f`; the mirror's code is MIT, the SRD data is CC-BY-4.0), the same snapshot as `docs/SRD_COMBAT_COVERAGE.md`. The 9 spells authored here were cross-checked field by field (level, range, casting time, duration, classes) against the Open5e `wotc-srd` document (api.open5e.com). One difference: Open5e lists Cleric and Warlock for Scorching Ray; the mirror (and the SRD 5.1 spell list) gives Sorcerer and Wizard, which is what the file uses. The published PDF was not re-read. The pack manifest's `attribution` string already carries the SRD notice; every spell has `source {ref: "SRD 5.1", license: "CC-BY-4.0"}`.

**Scope rule.** A spell is authored only if `spell@1` on `main` expresses its combat-relevant behavior faithfully with mechanics the engine implements today (`spell_attack`, `save`, `heal`, `auto_hit`, `ac_bonus`, `counter`, `apply_modifier`, `forced_move`, `teleport`, `zone`, the R-G15 kinds, `area`, `targets`, `scaling`). Anything that needs a mechanic reserved for a later row is deferred, not approximated. The first batch deferred area spells under C3 / R-G10; the sim now computes the templates (#78), and the refresh re-checked every deferred row against the merged loaders (timed modifiers, zones, forced_move / teleport, R-G15 kinds, on_event / predicates; #178 cover merged during this PR; adopting `ignores_cover` on Sacred Flame is a follow-up). Spells with no combat effect (utility, exploration, casting time of 1 minute or more) are listed as `OOS`.

## Counts (169 SRD spells of level 0-3 in the mirror)

| level | in `srd51.core` | deferred: needs a mechanic | deferred: out of combat scope | total |
|---|---|---|---|---|
| 0 (cantrips) | 11 | 2 | 11 | 24 |
| 1 | 27 | 3 | 19 | 49 |
| 2 | 19 | 12 | 23 | 54 |
| 3 | 13 | 10 | 19 | 42 |
| **total** | **70** | **27** | **72** | **169** |

## Authored in this change (9)

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Acid Splash | 0 | `save` DEX, 1d6 acid, none on success; `targets {count 2, max_spread_ft 5}`; cantrip dice | |
| Eldritch Blast | 0 | `spell_attack` ranged 1d10 force; cantrip `adds: beams` (5/11/17) | Agonizing Blast / Eldritch Spear are invocations (R-G4, R-G14) |
| Poison Spray | 0 | `save` CON, 1d12 poison, none on success; range 10; cantrip dice | |
| Produce Flame | 0 | `spell_attack` ranged 1d8 fire, range 30 (the hurl), `duration minutes 10`; cantrip dice | SRD range is Self (the flame in hand); the light is out of scope. Modeled as the hurl, as in the coverage doc |
| Sacred Flame | 0 | `save` DEX, 1d8 radiant, none on success; cantrip dice | "no benefit from cover" is not modeled (cover save bonus, R-G13); the SRD text is kept in `description` |
| Healing Word | 1 | `heal` 1d4 + spellcasting modifier, excludes undead/construct; bonus action, range 60; `upcast.dice 1` | |
| Inflict Wounds | 1 | melee `spell_attack` 3d10 necrotic; `upcast.dice 1` | |
| Scorching Ray | 2 | `spell_attack` ranged, `beams 3`, 2d6 fire; `upcast.beams 1` | |
| Mass Healing Word | 3 | `heal` 1d4 + spellcasting modifier, excludes undead/construct; `targets {count 6}`, bonus action, range 60; `upcast.dice 1` | |

Already shipped (unchanged): Fire Bolt, Magic Missile, Cure Wounds, Shield, Hold Person, Counterspell.

## Authored in the spells refresh (8, main at `6df51a2`)

Area templates are computed by the sim (`Combat::area_targets`, #78: cone / line from the caster, cube from the caster or the aim tile, sphere around the aim point), so the area-spell deferral of the first batch no longer applies. Pinned by `tests/validate/test_srd51_spells.py` against `tests/validate/data/srd51_spells_5e_database_a6212be.json`.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Ray of Frost | 0 | ranged `spell_attack` 1d8 cold + `apply_modifier` rider (`applies_to: affected`; `speed`, `speed_sub` 10, `until_start_of_caster_turn`); cantrip dice | `speed_scale` 1/1 (the loader requires it on speed) |
| Burning Hands | 1 | `save` DEX 3d6 fire half; `area` cone 15 (self); `upcast.dice 1` | igniting objects OOS |
| Shield of Faith | 1 | `apply_modifier` `ac` +2, concentration (10 min), bonus action, range 60 | |
| Thunderwave | 1 | `save` CON 2d8 thunder half + `forced_move` push 10 ft (`applies_to: affected`: failed saves only); `area` cube 15 (self); `upcast.dice 1` | unsecured objects and the boom OOS |
| Barkskin | 2 | `apply_modifier` `ac` `ac_floor` 16, concentration (1 hour), touch | "regardless of armor" is exactly the floor |
| Misty Step | 2 | `teleport` 30 ft (needs sight), bonus action, self | the built-in AI does not cast it |
| Fireball | 3 | `save` DEX 8d6 fire half; `area` sphere 20 at a point within 150 ft; `upcast.dice 1` | spreads around corners: the sphere ignores line of sight; igniting objects OOS |
| Lightning Bolt | 3 | `save` DEX 8d6 lightning half; `area` line 100 x 5 (self); `upcast.dice 1` | igniting objects OOS |

## Authored in spells batch 2 (13, main at `8b8d8c9`)

Rules #183 (7.2u) added the next-roll rider keys (`docs/CONTENT_EFFECTS.md`, "Rules #183 additions"). The zone and R-G15 spells are the `tests/validate/fixtures` documents re-identified as `srd51.spell.*`, with the mirror's classes and material text. Pinned by `tests/validate/test_srd51_spells.py` (table rows added from the same mirror commit). Ray of Frost already had the #183 shape (`speed_sub` 10, `speed_scale` 1/1, `until_start_of_caster_turn`) and is unchanged.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Vicious Mockery | 0 | `save` WIS 1d4 psychic, none on success + rider (`attack_roll` disadvantage, `consume: next_attack`, `until_end_of_target_turn`); cantrip dice | **partial**: "if the target can hear you" is not gated |
| Shocking Grasp | 0 | melee `spell_attack` 1d8 lightning + rider `no_reactions`, `until_start_of_target_turn`; cantrip dice | **partial**: advantage vs metal armor not modelled (no armor material) |
| Chill Touch | 0 | ranged `spell_attack` 1d8 necrotic + `no_healing` until the start of the caster's turn + `attack_roll` disadvantage `until_end_of_caster_next_turn` when `owner_creature_type [undead]` and `subject_is_caster`; cantrip dice | |
| Guiding Bolt | 1 | ranged `spell_attack` 4d6 radiant + rider (`attacks_against` advantage, `consume: next_attack_against`, `until_end_of_caster_next_turn`); `upcast.dice 1` | |
| Grease | 1 | point `zone` cube 10, difficult; Dex save or prone on `zone_appear` / `zone_enter` / `zone_end_turn` (group 0) | |
| Lesser Restoration | 2 | `end_condition [blinded, deafened, paralyzed, poisoned]` | diseases OOS; the engine ends the first listed condition present (no per-cast choice) |
| Silence | 2 | point `zone` sphere 20, `silence` flag (thunder immunity, deafened, no verbal components; rules PR 2); ritual | |
| Spike Growth | 2 | point `zone` cylinder 20 (height 20), difficult, `zone_move_within` 2d4 piercing (group 0) | camouflage OOS; the fixture's sphere became the mirror's cylinder (same ground area) |
| Web | 2 | point `zone` cube 20, difficult + lightly obscured; Dex save or restrained on start of turn / own-turn enter, `while_in_zone`; Str check to break free | anchoring and flammability OOS |
| Dispel Magic | 3 | `dispel {check {spellcasting, dc_base 10, plus spell_level}}` | |
| Revivify | 3 | `revive {within_rounds 10}`; `components.m` 300 gp diamond (`srd51.item.diamond`), consumed | |
| Spirit Guardians | 3 | creature-anchored `zone` sphere 15, `halve_speed`, `chooses_excluded`, `needs_conscious_owner`; Wis save 3d8 radiant half on enter / start of turn (group 1: once per turn), `evil_damage_type` necrotic; `upcast.dice 1` | |
| Stinking Cloud | 3 | point `zone` sphere 20, heavily obscured; start of turn completely within: Con save or `loses_action` (auto-success for poison-immune and no-breath) | wind dispersal OOS |

## Authored after Rules #188 and #222 (5)

Hunter's Mark and Invisibility shipped in content #223 (Rules #222, 7.2zc); Command, Fear and Hideous Laughter use the Rules #188 (7.2v) control saves. Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_control_spells`).

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Command | 1 | `save` WIS, `control command`, `control_excludes_creature_types [undead]`; `targets {count 1, max_spread_ft 30}`, `upcast.targets 1`; duration 1 round | the word (Approach, Drop, Flee, Grovel, Halt) is chosen at cast; **partial**: language, "directly harmful" commands, other words and dropped items are not modelled |
| Hideous Laughter | 1 | `save` WIS prone, `control laughter` (incapacitated, can't stand), `control_min_int 4`, `repeat_save end_of_turn_or_damage_advantage`; concentration 1 minute | |
| Hunter's Mark | 1 | `apply_modifier` `scope {target marked}` damage 1d6 `applies_to self`, `when [weapon_attack]`; `move_mark {action bonus_action}`; concentration 1 hour | **partial**: the Perception / Survival advantage and the 8 / 24 hour upcast durations are not modelled |
| Invisibility | 2 | `apply_condition` invisible, `ends_on [attack, cast]`; touch, concentration 1 hour; `upcast.targets 1` | the spell ends per target (SRD) |
| Fear | 3 | `save` WIS frightened, `control fear` (Dash away), `repeat_save end_of_turn_unseen`; `area` cone 30 (self); concentration 1 minute | **partial**: dropping held items is not modelled |

## Authored in spells batch 3 (5, main at `1a172d5`)

Pinned by the table rows and `test_batch_3_mechanics` in `tests/validate/test_srd51_spells.py`.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Mage Armor | 1 | `apply_modifier` `ac` `ac_floor 13` + `ac_floor_dex`, touch, 8 hours (modifier `rounds 4800`) | **partial**: "not wearing armor", ending on donning armor and dismissal are not checked |
| Divine Favor | 1 | `apply_modifier` `damage` 1d4 radiant, `applies_to self`, `when [weapon_attack]`, concentration; bonus action | |
| Sleep | 1 | `hp_pool` 5d8 unconscious, `ends_on_damage`, `excludes_creature_type [undead]`; sphere 20 ft; `upcast.dice 2` | **partial**: creatures immune to charmed are not skipped; waking by an action is not modelled |
| Shatter | 2 | `save` Con 3d8 thunder, half; sphere 10 ft; `upcast.dice 1` | **partial**: inorganic-creature disadvantage and object damage are not modelled |
| Moonbeam | 2 | `zone` cylinder 5 ft x 40 ft, point; Enter + StartTurn Con save 2d10 radiant half (`shapechanger_disadvantage`); `move {action, 60}`; `upcast.dice 1` | **partial**: dim light and forced reversion of shapechangers are not modelled |

## Authored after Rules #228 (3)

Spell batch 2 rules (7.2zd). Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_spell_batch2`).

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Protection from Evil and Good | 1 | `apply_modifier` `scope {from_types [aberration, celestial, elemental, fey, fiend, undead]}` attack_roll disadvantage `applies_to attackers`; `condition_immunity_from {conditions [charmed, frightened], types}`; touch, concentration 10 minutes | **partial**: possession and the advantage on a new save are not modelled. `components.m.consumed` with no `cost_cp` (#234): nothing is checked or removed |
| Sanctuary | 1 | `apply_condition` warded, `ward {save wis, on_fail retarget_or_lose}`, `ends_on [attack, harmful_cast]`; bonus action, range 30, 1 minute | any creature but the warded one meets the ward with an attack, a Grapple / Shove, or a harmful spell without an area (damage, a save, or a negative condition or modifier); areas are not blocked (SRD) |
| Aid | 2 | `max_hp_bonus {flat 5, per_slot_above 5}`; `targets {count 3}`, range 30, 8 hours | |

## Authored after Rules #237 (3)

Spell batch 3 rules (7.2ze). Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_spell_batch3`).

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Bane | 1 | `apply_modifier` `save {ability cha, dc caster}`, modifier `{stat attack_roll, die 1d4, sign minus, applies_to [attack, save]}`; `targets {count 3}`, `upcast.targets 1`; concentration 1 minute | a target that succeeds is not affected |
| Bless | 1 | `apply_modifier` modifier `{stat attack_roll, die 1d4, sign plus, applies_to [attack, save]}`; `targets {count 3}`, `upcast.targets 1`; concentration 1 minute | Life domain spell (cleric 1-5) |
| Blindness/Deafness | 2 | `save` CON, `condition chosen`, `choices [blinded, deafened]`, `repeat_save end_of_turn`; range 30, 1 minute; `upcast.targets 1` | the caster picks at cast (`condition_choice`) |

## Authored after Rules #257 (3)

Spell imbue / mirror rules (7.2zf). Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_spell_imbue_mirror`) and `tests/validate/test_spell_imbue_mirror.py`.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Shillelagh | 0 | `weapon_imbue {weapons [club, quarterstaff], ability spellcasting, die 8, magical, ends_on_release}`; bonus action, touch, 1 minute | druid; casting it again ends the earlier imbue; ends on Stow / Drop / HandOver |
| Magic Weapon | 2 | `weapon_imbue {magical, magic_bonus {base 1, at_slot [[4, 2], [6, 3]]}, requires_nonmagical}`; bonus action, touch, concentration up to 1 hour | paladin, wizard; refused on a magical weapon (`magic_weapon: the weapon is already magical`) |
| Mirror Image | 2 | `duplicates {count 3, thresholds [11, 8, 6], ac {base 10, ability dex}}`; self, 1 minute | sorcerer, warlock, wizard; thresholds indexed by remaining - 1; attackers that can't see, rely on blindsight, or have truesight are unaffected; the dismiss action is not modelled |

## Authored after Rules #263 (11)

Spell batch zg rules (7.2zg, Rules #263 at `302f92f`). Pinned by `tests/validate/test_srd51_spells.py` (table rows, `test_spell_batch_zg` and `test_spell_batch_zg_rest`, which also checks the pack files against Rules' specimens in `tests/validate/fixtures/spell_batch_zg/`) and `tests/validate/test_spell_batch_zg.py`. Mirror Image (authored after #257) now lists `exempt_senses [blindsight, truesight]` explicitly.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| False Life | 1 | `temp_hp {dice 1d4+4, per_slot_above 5}`; self, 1 hour | sorcerer, wizard |
| Heroism | 1 | `temp_hp {ability spellcasting, per_turn, condition_immunity [frightened]}`; touch, concentration 1 minute; `upcast.targets 1` | bard, paladin; also granted at the cast; the larger temp HP pool is kept |
| Acid Arrow | 2 | `spell_attack` ranged 4d4 acid `on_miss half` + `delayed_damage` 2d4 `applies_to affected`, `expiry until_end_of_target_next_turn`; range 90; `upcast.dice 1` | a miss halves only the initial damage; the 2d4 needs a hit |
| Spiritual Weapon | 2 | `summon_weapon {damage 1d8 force, add_spellcasting_modifier, upcast_every 2, reach_ft 5, move_ft 20}`; bonus action, range 60, 1 minute; `upcast.dice 1` | cleric (Life domain, 3-5); a token, not a creature; later attacks take a bonus action |
| Haste | 3 | `haste`; range 30, concentration 1 minute | sorcerer, wizard; +2 AC, advantage on Dex saves, speed x2, extra action, lethargy (Rules' marker) |
| Slow | 3 | `slow {ability wis}`; `area cube 40`, `targets {count 6}`, range 120, concentration 1 minute | sorcerer, wizard; save repeats at the end of each turn; the spell-failure d20 loses the spell (v1; the SRD's next-turn completion is deferred) |
| Faerie Fire | 1 | `apply_modifier {stat outlined}` + `save` DEX (caster DC); `area cube 20`, range 60, concentration 1 minute | druid (1-5); the mirror lists druid only; advantage when the attacker can see the target, no invisibility benefit; light and objects not modelled |
| Hellish Rebuke | 1 | reaction, `trigger {kind damaged, requires_sight}`; `save` DEX 2d10 fire half; range 60; `upcast.dice 1` | warlock (no class list room: every shipped warlock is at spells known) |
| Blur | 2 | `apply_modifier attacks_against disadvantage` + `exempt_senses [blindsight, truesight]`; self, concentration 1 minute | sorcerer (4-5), wizard (full at 3-5) |
| Hypnotic Pattern | 3 | `save` WIS `charmed` + `apply_condition incapacitated applies_to affected`, both `ends_on [damage, shake_awake]` (the Wake action); `area cube 30`, range 120, concentration 1 minute | bard, sorcerer, warlock, wizard (none have room at 5); speed 0 while charmed (Rules #263 @ `302f92f`); the mirror has no area_of_effect (cube 30 from the SRD text) |
| Protection from Energy | 3 | `apply_modifier {stat resistance, damage_type acid}` + `damage_type_choices [acid, cold, fire, lightning, thunder]`; touch, concentration 1 hour | cleric (5), druid (5); the mirror spells it "Protection From Energy" |

## Authored: Fog Cloud, Darkness, Color Spray (3)

Zone / `hp_pool` promotions from `tests/validate/fixtures/zones/spells/` and `tests/validate/fixtures/spell_kinds/spells/`. Pinned by `tests/validate/test_srd51_spells.py` (`test_zone_and_kind_spells` vs the fixtures) and the 5e-database table.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Fog Cloud | 1 | `zone` sphere 20, `flags [heavy_obscured]`, `upcast_size_ft 20`; concentration 1 hour, range 120 | druid (2), ranger (3-4), sorcerer (2), wizard (2); wind dispersal and obscurement's combat effect (R-G13) not modelled |
| Darkness | 2 | `zone` sphere 15, `flags [heavy_obscured, magical_darkness]`; concentration 10 minutes, range 60 | warlock (4), wizard (4); sorcerer has no class list room at 3+; object casting, covering the source, and dispelling lower-level light not expressible |
| Color Spray | 1 | `hp_pool` 6d10 `blinded`, self cone 15, `rounds 1`; `upcast.dice 2` | wizard (1); sorcerer has no class list room (Fog Cloud took the L2 known slot); "until the end of your next turn" ≈ `rounds 1` (R-G15); skipping unconscious / can't-see not expressible |

## Authored: Entangle (1)

Promote `tests/validate/fixtures/zones/spells/entangle.json` into `srd51.core` (Rules #277). Pinned by `tests/validate/test_srd51_spells.py` (`test_zone_and_kind_spells` vs the fixture) and the 5e-database table. Counts from main's 69 (#274 Fog Cloud / Darkness / Color Spray) + Entangle → 70 authored / 27 NEW / 72 OOS.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Entangle | 1 | point `zone` cube 20 (SRD 20-ft square, same cube as Grease), `difficult`; `zone_appear` Strength save or restrained, `while_in_zone`; concentration 1 minute, range 90, V S | druid (2); forced exit also clears the restraint (`while_in_zone`); no new zone key |

## Deferred (99 spells; reasons re-checked against main at `8b8d8c9`)

Gap ids are Rules' ids from `docs/SRD_COMBAT_COVERAGE.md` (section 11.2 maps the first-draft ids in brackets: B1 = R-G2 effects array, B2 = R-G3 timed modifiers, B3 = R-G4 damage riders, B4 = R-G5 pools, B6 = R-G7 reactions / roll windows, C1 = R-G8 conditions with source, C2 = R-G9 forced movement / teleport, C3 = R-G10 area templates, C4 = R-G11 zones / auras, C5 = R-G12 summons, C7 = R-G13 cover / lighting / vision, C8 = R-G14 per-cast options, C9 = R-G15 spell kinds). `T&C-G12` / `T&C-G14` are movement modes and creature footprint. `OOS` rows have no gap id.

| level | spell | kind | missing mechanic (from the coverage doc) | gap id |
|---|---|---|---|---|
| 0 | Dancing Lights | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Druidcraft | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Guidance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Light | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mage Hand | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mending | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Message | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Minor Illusion | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Prestidigitation | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Resistance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Spare the Dying | NEW | spell@1 has no stabilize effect kind; Rules models it only as `ActionKind::SpareTheDying` + `hooks.spare_the_dying` (coverage doc marks it DATA via that ActionKind, not via spell@1) | - |
| 0 | Thaumaturgy | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | True Strike | NEW | `attack_roll` advantage with `consume: next_attack` loads, but the advantage applies to any target; the SRD limits it to the chosen target (no per-target modifier scope). | R-G3 (B2) |
| 1 | Alarm | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Animal Friendship | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Charm Person | NEW | Charmed with source (R-G8). | R-G8 (C1) |
| 1 | Comprehend Languages | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Create or Destroy Water | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Evil and Good | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Magic | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Poison and Disease | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Disguise Self | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Expeditious Retreat | NEW | No spell@1 way to grant a bonus-action Dash for the duration (`grant_action` is a feature effect). | R-G3 (B2) |
| 1 | Feather Fall | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Find Familiar | NEW | Summons rules (R-G12 PR 2) exist but spell@1 has no summon key; 1-hour cast. | R-G12 (C5) |
| 1 | Floating Disk | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Goodberry | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Identify | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Illusory Script | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Jump | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Longstrider | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Purify Food and Drink | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Silent Image | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Speak with Animals | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Unseen Servant | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 2 | Alter Self | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Animal Messenger | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Arcane Lock | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Arcanist's Magic Aura | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Augury | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Branding Smite | Done (7.2zh) | `weapon_smite`: bonus action, self, concentration. Extra damage on the next weapon-attack hit (not a spell attack); the target is revealed and sheds dim light for the rest of the duration. Hash 0xD4. | R-G4, T&C-G5 (B3) |
| 2 | Calm Emotions | NEW | Area + suppress condition (R-G10, R-G3). | R-G3, R-G10 (B2, C3) |
| 2 | Continual Flame | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Darkvision | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Detect Thoughts | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Enhance Ability | NEW | Per-cast ability choice has no spell@1 key; temp-HP / carrying options not modelled. | R-G3, R-G14 (B2, C8) |
| 2 | Enlarge/Reduce | NEW | Size/damage change (R-G3, T&C-G12, T&C-G14). | R-G3, T&C-G12, T&C-G14 (B2) |
| 2 | Enthrall | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Steed | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Traps | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Flame Blade | Done (7.2zh) | `summon_weapon` with `held: true` and light 10/10. Free hand to cast, later attacks spend the action, no move, no spellcasting modifier. Folded into 0xAF (held sub-byte), not a new tag. | R-G12 (C5) |
| 2 | Flaming Sphere | NEW | Object/zone (R-G11/R-G12). | R-G11, R-G12 (C4, C5) |
| 2 | Gentle Repose | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Gust of Wind | NEW | Line + push (R-G9, R-G10). | R-G9, R-G10 (C2, C3) |
| 2 | Heat Metal | Done (7.2zh) | `sustained_damage` plus `held_object`: CON save, drop a held Weapon/Armor/Shield (worn armor cannot drop) or disadvantage while still in contact. Metal is the item category in v1, not a metal flag. Hash 0xD5. | R-G15 (C9) |
| 2 | Knock | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Levitate | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Animals or Plants | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Object | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Magic Mouth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Pass Without Trace | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Prayer of Healing | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Protection from Poison | NEW | R-G3. | R-G3 (B2) |
| 2 | Ray of Enfeeblement | NEW | Attack + damage halving rider (R-G3). | R-G3 (B2) |
| 2 | Rope Trick | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | See Invisibility | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Spider Climb | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Suggestion | NEW | Forced behavior (R-G8). | R-G8 (C1) |
| 2 | Warding Bond | NEW | Shared damage (R-G3). | R-G3 (B2) |
| 2 | Zone of Truth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 3 | Animate Dead | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Beacon of Hope | NEW | Save advantage, max healing (R-G3). | R-G3 (B2) |
| 3 | Bestow Curse | NEW | Touch + chosen effect (R-G2/R-G3). | R-G2, R-G3 (B1, B2) |
| 3 | Blink | NEW | Phasing (R-G9). | R-G9 (C2) |
| 3 | Call Lightning | NEW | Zone + repeating action (R-G11). | R-G11 (C4) |
| 3 | Clairvoyance | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Conjure Animals | NEW | Summons rules (R-G12 PR 2) exist, but spell@1 has no summon key yet. | R-G12 (C5) |
| 3 | Create Food and Water | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Daylight | NEW | Light/obscurement dispelling (R-G13). | R-G13 (C7) |
| 3 | Fly | NEW | Movement mode (T&C-G12, T&C-G14). | T&C-G12, T&C-G14 |
| 3 | Gaseous Form | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Glyph of Warding | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Magic Circle | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Major Image | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Meld Into Stone | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Nondetection | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Phantom Steed | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Plant Growth | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Remove Curse | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Sending | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Sleet Storm | NEW | Zone (R-G11). | R-G11 (C4) |
| 3 | Speak with Dead | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Speak with Plants | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Tiny Hut | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Tongues | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Vampiric Touch | NEW | Attack damage DATA; heal half and repeat action need R-G2/R-G3. | R-G2, R-G3 (B1, B2) |
| 3 | Water Breathing | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Water Walk | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Wind Wall | NEW | Zone (R-G11). | R-G11 (C4) |

## Follow-ups when the mechanics land

- Acid Arrow authored in 7.2zg (#265): `spell_attack` `on_miss half` + `delayed_damage` (upcast reaches the rider).
- Small additive changes that unblock more: a spell@1 condition-choice key (Blindness/Deafness). Faerie Fire, Protection from Energy, Spiritual Weapon, Haste / Slow, and `targets` on modifiers landed in 7.2zg (#263 / #265).
- Every `NEW` row above is additive content once its row lands; ids are `srd51.spell.<snake_case_name>` and are not reserved yet.
- Spare the Dying: `spell@1` has no stabilize effect, only the Rules `ActionKind::SpareTheDying` hook. If Rules adds an effect kind (additive), it is a one-file change.
