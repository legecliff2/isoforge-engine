# SRD 5.1 spells, levels 0-3: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 (“SRD 5.1”) by Wizards of the Coast LLC and available at https://dnd.wizards.com/resources/systems-reference-document. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License available at https://creativecommons.org/licenses/by/4.0/legalcode.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules; section 8 and 10.2) and the `content/packs/srd51.core` pack.

**Source.** Spell text, levels, schools, classes, components, ranges and dice come from the [5e-bits/5e-database](https://github.com/5e-bits/5e-database) JSON mirror (`src/2014/en/5e-SRD-Spells.json`, commit `a6212beb4b278917c2cff41c2b04c4ef4f3e0d6f`; the mirror's code is MIT, the SRD data is CC-BY-4.0), the same snapshot as `docs/SRD_COMBAT_COVERAGE.md`. The validation copy `tests/validate/data/srd51_spells_5e_database_a6212be.json` is that commit with the `hunters-mark` entry removed (PHB-only, not in SRD 5.1); the SRD text stays CC-BY-4.0 and the rest of the file matches `a6212be`. The 9 spells authored here were cross-checked field by field (level, range, casting time, duration, classes) against the Open5e `wotc-srd` document (api.open5e.com). One difference: Open5e lists Cleric and Warlock for Scorching Ray; the mirror (and the SRD 5.1 spell list) gives Sorcerer and Wizard, which is what the file uses. The published PDF was not re-read. The pack manifest's `attribution` string already carries the SRD notice; every spell has `source {ref: "SRD 5.1", license: "CC-BY-4.0"}`.

**Scope rule.** A spell is authored only if `spell@1` on `main` expresses its combat-relevant behavior faithfully with mechanics the engine implements today (`spell_attack`, `save`, `heal`, `auto_hit`, `ac_bonus`, `counter`, `apply_modifier`, `forced_move`, `teleport`, `zone`, the R-G15 kinds, `area`, `targets`, `scaling`). Anything that needs a mechanic reserved for a later row is deferred, not approximated. The first batch deferred area spells under C3 / R-G10; the sim now computes the templates (#78), and the refresh re-checked every deferred row against the merged loaders (timed modifiers, zones, forced_move / teleport, R-G15 kinds, on_event / predicates; #178 cover merged during this PR; adopting `ignores_cover` on Sacred Flame is a follow-up). Spells with no combat effect (utility, exploration, casting time of 1 minute or more) are listed as `OOS`.

## Counts (168 SRD spells of level 0-3)

| level | in `srd51.core` | rules landed, pack pending | deferred: needs a mechanic | deferred: out of combat scope | total |
|---|---|---|---|---|---|
| 0 (cantrips) | 17 | 0 | 0 | 7 | 24 |
| 1 | 27 | 1 | 1 | 19 | 48 |
| 2 | 25 | 2 | 4 | 23 | 54 |
| 3 | 14 | 1 | 8 | 19 | 42 |
| **total** | **83** | **4** | **13** | **68** | **168** |

Hunter's Mark is non-SRD, excluded (PHB-only; its text is not in SRD 5.1). Not NEW and not Authored. An earlier pass counted it, which is why the level-1 total and the overall total are one spell below the old 49 level 1 / 169 overall. Authored is 79: Expeditious Retreat is in the pack, and Hunter's Mark is not. Hex is non-SRD, excluded (PHB-only; not in SRD 5.1). Not NEW and not Authored. It was never in these counts and is not on the warlock lists. Spike Growth is already in the 79 (authored for the druid); putting it back on the ranger level-5 list does not change these totals. The ranger then knows 4 spells at level 5.

7.2zi (Rules #283, `feat/rules-spell-batch-zi`) implements Spare the Dying, True Strike, Expeditious Retreat, Protection from Poison, Ray of Enfeeblement, Beacon of Hope, and Warding Bond. Specimens: `tests/validate/fixtures/spell_batch_zi/`. Six are authored below. Expeditious Retreat is authored after Rules #285 (`dash_on_cast`): the pack grants the Dash on the cast and `bonus_dash` covers later turns.

7.2zj lands the combat mechanics for Charm Person, Calm Emotions, Suggestion, and Fly. They are not in `srd51.core` (packs are Tools & Content), so the in-pack column stays 83. Those four move out of "needs a mechanic" into "rules landed, pack pending" (level 1: 1, level 2: 2, level 3: 1). Needs-a-mechanic is 13. Out of combat scope stays 68. Level totals stay 48 / 54 / 42 / 168. Level 4–5 spells in the same batch are outside this census. Dream and Scrying stay unimplemented.

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

Area templates are computed by the sim (`Combat::area_targets`, #78: cone / line from the caster, cube from the caster or the aim tile, sphere around the aim point), so the area-spell deferral of the first batch no longer applies. Pinned by `tests/validate/test_srd51_spells.py` against `tests/validate/data/srd51_spells_5e_database_a6212be.json` (a6212be with the `hunters-mark` entry removed; SRD 5.1, CC-BY-4.0; see Source).

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

## Authored after Rules #188 and #222 (4)

Invisibility shipped in content #223 (Rules #222, 7.2zc); Command, Fear and Hideous Laughter use the Rules #188 (7.2v) control saves. Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_control_spells`). Hunter's Mark is non-SRD, excluded (not NEW and not Authored).

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Command | 1 | `save` WIS, `control command`, `control_excludes_creature_types [undead]`; `targets {count 1, max_spread_ft 30}`, `upcast.targets 1`; duration 1 round | the word (Approach, Drop, Flee, Grovel, Halt) is chosen at cast; **partial**: language, "directly harmful" commands, other words and dropped items are not modelled |
| Hideous Laughter | 1 | `save` WIS prone, `control laughter` (incapacitated, can't stand), `control_min_int 4`, `repeat_save end_of_turn_or_damage_advantage`; concentration 1 minute | |
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

## Authored after Rules #276 (3)

Spell batch zh rules (7.2zh, Rules #276). Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_spell_batch_zh`) and the negative cases under `tests/validate/cases/` (`weapon_smite_*`, `summon_weapon_held_*`).

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Branding Smite | 2 | `weapon_smite` 2d6 radiant, `reveal_invisible`, `light {dim_ft 5}`; bonus action, self, concentration 1 minute; `upcast.dice 1` | paladin (5); first effect only; brands on the next weapon-attack hit (not a spell attack); a crit doubles the dice |
| Heat Metal | 2 | `sustained_damage` 2d8 fire + `held_object {save con, on_fail drop, while_held {disadvantage [attack_rolls, ability_checks], until start_of_caster_next_turn}}`; concentration 1 minute; `upcast.dice 1` | bard (3-5), druid (4-5); absent material falls back to the category rule (any weapon, armor, or shield counts as metal); only an explicit wood, leather, hide, cloth, or rope removes it as a target; worn armor cannot drop |
| Flame Blade | 2 | `summon_weapon` `held` 3d6 fire, `upcast_every 2`, `reach_ft 5`, `light {bright_ft 10, dim_ft 10}`; bonus action, self, concentration 10 minutes | druid (4-5); free hand to cast; later attacks spend the action; no `move_ft`, no `add_spellcasting_modifier` |

## Authored: Entangle (1)

Promote `tests/validate/fixtures/zones/spells/entangle.json` into `srd51.core` (Rules #277 / #278). Pinned by `tests/validate/test_srd51_spells.py` (`test_zone_and_kind_spells` vs the fixture) and the 5e-database table. Counts from main's 69 (#274 Fog Cloud / Darkness / Color Spray) + Branding Smite / Heat Metal / Flame Blade + Entangle → 73 authored / 24 NEW / 72 OOS.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Entangle | 1 | point `zone` cube 20 (SRD 20-ft square, same cube as Grease), `difficult`; `zone_appear` Strength save or restrained, `while_in_zone`; concentration 1 minute, range 90, V S | druid (2); forced exit also clears the restraint (`while_in_zone`); no new zone key |

## Authored after Rules #283 (6)

Spell batch zi (7.2zi, Rules #283). Each pack's `effects`, casting time, range, duration and `targets` equal the Rules specimen in `tests/validate/fixtures/spell_batch_zi/`. Pinned by `tests/validate/test_srd51_spells.py` (table rows and `test_spell_batch_zi_packs`). Counts from 73 + these 6 → 79 authored / 18 NEW / 72 OOS, then Hunter's Mark is non-SRD, excluded, leaving 78 authored / 18 NEW / 72 OOS of 168.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Spare the Dying | 0 | `stabilize`, `excludes_creature_type [undead, construct]`; touch, instantaneous, V S | cleric; first effect only; does not heal; `ActionKind::SpareTheDying` unchanged |
| True Strike | 0 | `apply_modifier` `attack_roll` advantage, `consume next_attack`, `on_next_turn`, `scope {target marked}`, `until_end_of_caster_next_turn`; concentration 1 round, range 30, S | bard, sorcerer, warlock, wizard; armed on the caster's next turn; an attack on another creature does not spend it |
| Protection from Poison | 2 | `end_condition [poisoned]`; poison `resistance`; `save` advantage with `save_vs_conditions [poisoned]`; `rounds 600`; 1 hour, not concentration, touch | cleric, druid, paladin, ranger; one Poisoned condition flag, so "neutralize one poison" ends it |
| Ray of Enfeeblement | 2 | `spell_attack` ranged `hit_only`; `affected` `halve_weapon_damage` ability str, `repeat_save {ability con}`, `rounds 10`; concentration 1 minute, range 60 | warlock, wizard; no hit points on the hit; save at the end of each of the target's turns |
| Beacon of Hope | 3 | `apply_modifier` `save` wis advantage, `death_saves`, `heal_maximum`, `rounds 10`; `targets.count 255`; concentration 1 minute, range 30 | cleric; temporary hit points are not healing |
| Warding Bond | 2 | target `ac` +1, `save` +1, `resistance` `all_damage`, `share_damage` with `end_beyond_ft 60`, `end_at_zero caster`; `rounds 600`; 1 hour, not concentration, touch, V S M | cleric; material text only (rings, 50 gp each, not consumed); recasting on either creature ends the old bond; wearing the rings and dismissing as an action are not modelled |

## Authored after Rules #285 (1)

Expeditious Retreat (7.2zi follow-up, Rules #285). The pack's `effects` equal `tests/validate/fixtures/spell_batch_zi/expeditious_retreat.json`. Pinned by `tests/validate/test_srd51_spells.py` (table row and `test_spell_batch_zi_packs`). Counts from 78 → 79 authored / 17 NEW / 72 OOS of 168.

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Expeditious Retreat | 1 | `apply_modifier` `dash_on_cast`, `bonus_dash` on self, `rounds 600`; bonus action, self, concentration 10 minutes, V S | sorcerer, warlock, wizard; the cast includes a Dash, later turns Dash as a bonus action |

## Authored: descriptive cantrips (4)

Text-only spells (CONTENT_CONVENTIONS §28; Rules PR #309, feat/rules-spell-descriptive @ cdd2b09). `effects` is `[{"kind": "descriptive"}]`; verbatim SRD 5.1 text, `source {ref: "SRD 5.1", license: "CC-BY-4.0"}`. They load and stay on spell lists, but `legal_actions` never offers them. Pinned by `tests/validate/test_srd51_spells.py` (table rows) and `tests/validate/test_spell_descriptive.py`. Listed as Descriptive (not Authored) in `docs/SRD_COMBAT_COVERAGE.md` 8.1.

| spell | level | `spell@1` shape | classes (5e-database a6212be) | deferred mechanic |
|---|---|---|---|---|
| Light | 0 | `descriptive`; touch, 1 hour, V M (`A firefly or phosphorescent moss`) | bard, cleric, sorcerer, wizard | `emit_light`; the Dex save against a hostile holder is text only |
| Dancing Lights | 0 | `descriptive`; range 120, concentration 1 minute, V S M (`A bit of phosphorus or wychwood, or a glowworm`) | bard, sorcerer, wizard | summoned lights |
| Minor Illusion | 0 | `descriptive`; range 30, 1 minute, S M (`A bit of fleece`) | bard, sorcerer, warlock, wizard | illusion with an Investigation check vs spell DC |
| Thaumaturgy | 0 | `descriptive`; range 30, 1 minute, V | cleric | descriptive only |

No character class list in srd51.core is changed by this batch: the spells are on the SRD class lists above, and the acolyte and cult fanatic (authored on this branch) are their first users.

## Deferred (85 spells; reasons re-checked against main at `8b8d8c9`)

Four of these 85 (Charm Person, Calm Emotions, Suggestion, Fly) have rules landed in 7.2zj and wait on a pack. They stay in this table with kind `7.2zj`. The other 81 rows are unchanged.

**Planned descriptive-only, not yet authored.** `light`, `thaumaturgy`, `dancing_lights` and `minor_illusion` are planned as `{"kind":"descriptive"}` (the spell's only effect; `docs/CONTENT_CONVENTIONS.md` section 28). They are not in `srd51.core`, so the counts in this file are unchanged and the four rows below stay deferred. Light authored this way sheds no light: `append_active_zones` in `engine/core/sim/src/light_driver.cpp` keeps a spell only when its kind is `SpellEffectKind::Zone`. The real light mechanics stay deferred.

Gap ids are Rules' ids from `docs/SRD_COMBAT_COVERAGE.md` (section 11.2 maps the first-draft ids in brackets: B1 = R-G2 effects array, B2 = R-G3 timed modifiers, B3 = R-G4 damage riders, B4 = R-G5 pools, B6 = R-G7 reactions / roll windows, C1 = R-G8 conditions with source, C2 = R-G9 forced movement / teleport, C3 = R-G10 area templates, C4 = R-G11 zones / auras, C5 = R-G12 summons, C7 = R-G13 cover / lighting / vision, C8 = R-G14 per-cast options, C9 = R-G15 spell kinds). `T&C-G12` / `T&C-G14` are movement modes and creature footprint. `OOS` rows have no gap id.

| level | spell | kind | missing mechanic (from the coverage doc) | gap id |
|---|---|---|---|---|
| 0 | Druidcraft | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Guidance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mage Hand | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mending | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Message | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Prestidigitation | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Resistance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 1 | Alarm | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Animal Friendship | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Charm Person | 7.2zj | Rules landed (charmed with source, hostile-save advantage, ends when the caster's team deals damage). Pack pending. The "knows it was charmed" line is narrative. | R-G8 (C1) |
| 1 | Comprehend Languages | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Create or Destroy Water | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Evil and Good | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Magic | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Poison and Disease | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Disguise Self | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
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
| 2 | Calm Emotions | 7.2zj | Rules landed (suppress charmed and frightened on a failed save). Indifference toward chosen hostiles and a voluntary failed save stay deferred. Pack pending. | R-G3, R-G10 (B2, C3) |
| 2 | Continual Flame | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Darkvision | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Detect Thoughts | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Enhance Ability | NEW | Per-cast ability choice has no spell@1 key; temp-HP / carrying options not modelled. | R-G3, R-G14 (B2, C8) |
| 2 | Enlarge/Reduce | NEW | Size/damage change (R-G3, T&C-G12, T&C-G14). | R-G3, T&C-G12, T&C-G14 (B2) |
| 2 | Enthrall | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Steed | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Traps | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Flaming Sphere | NEW | Object/zone (R-G11/R-G12). | R-G11, R-G12 (C4, C5) |
| 2 | Gentle Repose | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Gust of Wind | NEW | Line + push (R-G9, R-G10). | R-G9, R-G10 (C2, C3) |
| 2 | Knock | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Levitate | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Animals or Plants | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Object | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Magic Mouth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Pass Without Trace | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Prayer of Healing | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Rope Trick | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | See Invisibility | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Spider Climb | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Suggestion | 7.2zj | Rules landed (charmed, charm immunity skips the save, hearing, sight). The course of action stays deferred. Pack pending. | R-G8 (C1) |
| 2 | Zone of Truth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 3 | Animate Dead | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Bestow Curse | NEW | Touch + chosen effect (R-G2/R-G3). | R-G2, R-G3 (B1, B2) |
| 3 | Blink | NEW | Phasing (R-G9). | R-G9 (C2) |
| 3 | Call Lightning | NEW | Zone + repeating action (R-G11). | R-G11 (C4) |
| 3 | Clairvoyance | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Conjure Animals | NEW | Summons rules (R-G12 PR 2) exist, but spell@1 has no summon key yet. | R-G12 (C5) |
| 3 | Create Food and Water | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Daylight | NEW | Light/obscurement dispelling (R-G13). | R-G13 (C7) |
| 3 | Fly | 7.2zj | Rules landed (`grant_fly` 60 ft). Willing is the caster's team, including the caster. Pack pending. | T&C-G12, T&C-G14 |
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

## Rules landed, pack pending (7.2zj)

Not in `srd51.core`. Specimens: `tests/validate/fixtures/spell_batch_zj/`. The loader, `spell@1`, and `tools/validate` accept these shapes. Tools & Content copies them into the pack. The four level 0–3 rows above stay in the deferred table with kind `7.2zj`. Cone of Cold, Ice Storm, Greater Invisibility, Dispel Evil and Good, Greater Restoration, and Geas are level 4–5 and were never in the 168 census. Dream and Scrying stay unimplemented.

| spell | level | census | `spell@1` shape |
|---|---|---|---|
| Charm Person | 1 | rules landed, pack pending | Wis save, humanoid, `hostile_save_advantage`, charmed, `ends_on_ally_harm`, `caster_sees_target`; 1 hour; upcast +1 target, `max_spread_ft` 30. V S. |
| Suggestion | 2 | rules landed, pack pending | Wis save, charmed, `ends_on_ally_harm`, `charm_immune`, `caster_sees_target`, `requires_hearing`; concentration 8 hours. V M (text only). Course of action deferred. |
| Calm Emotions | 2 | rules landed, pack pending | Cha save, sphere 20, humanoid, `suppress_conditions` charmed and frightened; concentration 1 minute. `targets.count` 255. Indifference and voluntary fail deferred. V S. |
| Fly | 3 | rules landed, pack pending | `grant_fly` `fly_ft` 60, touch, concentration 10 minutes; upcast +1 target. Willing is the caster's team. V S M (wing feather, text). |
| Cone of Cold | 5 | outside the 0–3 census | Con save, 8d8 cold, half, cone 60, self; upcast dice 1. No new keys. Frozen statue on a kill deferred. V S M (crystal cone, text). |
| Ice Storm | 4 | outside the 0–3 census | Dex save, 2d8 bludgeoning (upcast +1d8) and `also_damage` 4d6 cold (not upcast), half; cylinder radius 20 height 40, range 300. Difficult terrain until the end of the caster's next turn, not concentration, even if every save succeeds. |
| Greater Invisibility | 4 | outside the 0–3 census | `apply_condition` invisible, no `ends_on`. Touch, concentration 1 minute; upcast +1 target. An attack or a cast does not end it. |
| Dispel Evil and Good | 5 | outside the 0–3 census | `apply_modifier` `scope.from_types` celestial, elemental, fey, fiend, undead; `attack_roll` disadvantage, `applies_to` attackers; concentration 1 minute, self. Not charmed or frightened immunity. Break Enchantment, Dismissal, and possession deferred. Material text is not consumed. |
| Greater Restoration | 5 | outside the 0–3 census | `restore` options in JSON order: exhaustion, charmed, petrified, the six ability scores, hit point maximum. `condition_choice` is 1 + index. One charmed or petrified effect. A successful option also ends every `ends_on_restoration` effect. Touch. Diamond dust `cost_cp` 10000, consumed, no item id (validator warning; Tools & Content adds the item). Curses and attunement deferred. |
| Geas | 5 | outside the 0–3 census | Wis save, charmed, `ends_on_ally_harm`, `ends_on_restoration`, `caster_sees_target`; range 60; duration 720 hours with no round timer (the int16 cap is 32767). Casting time is 1 minute and an encounter cast is still refused. 5d10 psychic on disobedience, language, dismiss, and upcast duration deferred. V only. |

Choosing the charmed option of Greater Restoration while both Charm Person and Geas are on the target ends the first charmed effect and then every `ends_on_restoration` effect, so both can end.

## Follow-ups when the mechanics land

- Acid Arrow authored in 7.2zg (#265): `spell_attack` `on_miss half` + `delayed_damage` (upcast reaches the rider).
- Small additive changes that unblock more: a spell@1 condition-choice key (Blindness/Deafness). Faerie Fire, Protection from Energy, Spiritual Weapon, Haste / Slow, and `targets` on modifiers landed in 7.2zg (#263 / #265).
- Every `NEW` row above is additive content once its row lands; ids are `srd51.spell.<snake_case_name>` and are not reserved yet.
- Spare the Dying: authored from the 7.2zi `stabilize` fixture (Rules #283). `ActionKind::SpareTheDying` is unchanged.
- Charm Person, Calm Emotions, Suggestion, and Fly: rules landed in 7.2zj. Kind in the table is `7.2zj`. The pack is Tools & Content. Specimens: `tests/validate/fixtures/spell_batch_zj/`.
