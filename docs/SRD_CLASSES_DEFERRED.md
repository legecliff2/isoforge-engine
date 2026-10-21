# SRD 5.1 classes (Fighter, Cleric, Wizard, Sorcerer, Rogue, Paladin, Ranger, Barbarian, Bard, Druid, Monk, Warlock levels 1-5): authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules, section 6), `docs/SRD_MONSTERS_DEFERRED.md` and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Class, level, feature and subclass data come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) commit `a6212be` (`src/2014/en/5e-SRD-Classes.json`, `5e-SRD-Levels.json`, `5e-SRD-Features.json`, `5e-SRD-Subclasses.json`; spell levels and class lists from `5e-SRD-Spells.json`). The fields used are copied verbatim into `tests/validate/data/srd51_classes_5e_database_a6212be.json`, and `tests/validate/test_srd51_classes.py` (ctest `content_validate_srd51_classes`) checks every document against them. The SRD subclasses are Champion (Fighter), Life Domain (Cleric), School of Evocation (Wizard), Draconic Bloodline (Sorcerer), Thief (Rogue), Oath of Devotion (Paladin), Hunter (Ranger), Path of the Berserker (Barbarian) and College of Lore (Bard).

## Representation

`character@4` describes one creature: `classes[]` with a `level` and an optional `subclass`, a total `level`, and a stat block. There is no class or level-progression document type. A class level is therefore authored as **one pre-generated character per class per level**: 60 documents, `srd51.character.<class>_level_<n>` (`characters/<class>_level_<n>.json`). The `_level_` infix is deliberate: `srd51.character.fighter_1` already belongs to `sample.vertical_slice`, which depends on `srd51.core`, so a duplicate id would break the pack set.

Every PC is a human (SRD human: +1 to every ability score) built from the standard array (15, 14, 13, 12, 10, 8). The Ability Score Improvement at level 4 is +2 to the class's main ability (Str, Wis, Int, Cha, Dex, Str, Dex, Str, Cha, Wis, Dex, Cha), and level 5 carries it forward. Hit points are the maximum hit die at level 1, then the fixed average (6 / 5 / 4) plus Con for each later level (d8 rogue: 5). XP is the threshold for the level. Weapons, armor and shields are `{item, equipped}` references to the pack's `item@1` documents. AC and every attack are derived from those items (base damage, no feature bonus folded in; `docs/CONTENT_EFFECTS.md` section 2) and are pinned by the test. Packs, foci and ammunition stay descriptive strings.

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
| Monk | Way of the Open Hand (3) | Dex 16 (18 at 4), Wis 15, Con 14; Acrobatics, Insight; walk 40 from 2 (Unarmored Movement) | 15 (16 at 4): no armor, Unarmored Defense 10 + Dex + Wis | unarmed strike (martial arts die), shortsword (monk weapon) |
| Warlock | The Fiend (1) | Cha 16 (18 at 4), Dex 15, Con 14; Arcana, Deception | 13: leather armor at 1; 15 from 2: no armor, Armor of Shadows (mage armor, 13 + Dex) | dagger, light crossbow |

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
| Disciple of Life (Life) | 1-5 | `heal_bonus {flat 2, per_spell_level true}` (7.2zc, #222): 2 + the slot level more per creature a slot-cast healing spell heals (Cure Wounds, Healing Word, Mass Healing Word). Innate casts do not count (SRD: any spell of 1st level or higher; no innate healing on our documents) | yes |
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
| Fast Hands (Thief) | 3-5 | `bonus_action_grant {actions [use_object]}` (7.2y, #200): Use an Object as the Cunning Action bonus action; the Sleight of Hand and thieves' tools checks are Use an Object variants (`ActionRequest::object_use`, thieves' tools proficiency from the carried `thieves_tools`) | yes |
| Uncanny Dodge (Rogue) | 5 | `reactions[]` `{window {damaged, requires_sight}, effect {reduce_damage, fraction half}}` (7.2y): attack damage only, from an attacker you can see, rounded down | yes |
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
| Spellcasting (Ranger) | 2-5 | `spellcasting {ability wis, caster_level, slots, spells}` from 2nd level. Spells known: Cure Wounds, Hunter's Mark (the only shipped 1st-level ranger spells, so 2 of 2-3 known at 2-4); at 5 also Barkskin, Lesser Restoration (4 of 4; Spike Growth swapped out for Hunter's Mark). Hunter's Mark (7.2zc, #222): the extra 1d6 on weapon hits against the marked creature and the bonus-action move after it drops to 0 are data; the Perception / Survival advantage and the 8 / 24 hour upcast durations are not | yes |
| Hunter's Prey: Colossus Slayer (Hunter) | 3-5 | `on_event {on hit, limit once_per_turn, when [weapon_attack, target_below_max_hp], effects [extra_damage 1d8 same_as_weapon]}` (7.2y, #200) | yes |
| Ranger Archetype | 3 | `classes[0].subclass` = `hunter` | descriptive |
| Extra Attack (Ranger) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Unarmored Defense (Barbarian) | 1-5 | baked into `armor_class` (10 + Dex + Con, no armor; the test derives it); descriptive feature, no `effect` | yes (static number) |
| Reckless Attack (Barbarian) | 2-5 | `reckless_attack` (sim #129: `ActionRequest::reckless` on the turn's first attack; advantage on Str melee weapon attacks, attacks against you have advantage until your next turn). The built-in AI never declares it | yes (caller-declared) |
| Rage (Barbarian) | 1-5 | `resources[]` `rage {max 2 (3 at 3), recharge long_rest}` + `apply_modifier {cost bonus_action, usage {pool rage}, scope {attack melee_weapon, ability str}, ends_early [no_attack_or_damage_since_last_turn, unconscious], blocks [casting, concentration], advantage_on {checks [str], saves [str]}, modifiers [damage bonus_table 2/3/4, resistance bludgeoning / piercing / slashing], rounds 10}` (7.2z, #211). **Partial**: the heavy armor check (none is worn) and ending it as a bonus action are not modelled | yes |
| Danger Sense (Barbarian) | 2-5 | `apply_modifier {passive, advantage_on {saves [dex]}, when {save dex, source_seen, not_conditions [blinded, deafened, incapacitated]}}` (7.2z) | yes |
| Frenzy (Berserker) | 3-5 | `bonus_attack {requires_effect rage}` plus Rage's `on_end {exhaustion 1}` from 3rd: one level of exhaustion when a rage in which you frenzied ends (7.2z). The choice to frenzy is taking the bonus attack | yes |
| Primal Path | 3 | `classes[0].subclass` = `berserker` | descriptive |
| Extra Attack (Barbarian) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Fast Movement (Barbarian) | 5 | baked into `speed.walk` 40 (no heavy armor is ever worn); descriptive feature, no `effect` | yes (static number) |
| Spellcasting (Bard) | 1-5 | `spellcasting {ability cha, caster_level, slots, spells}`: Vicious Mockery; Cure Wounds, Healing Word, Thunderwave; + Hold Person, Lesser Restoration, Silence at 3; + Dispel Magic, Stinking Cloud at 5 (every shipped bard spell up to the top slot; 1 of 2-3 cantrips) | yes |
| Cutting Words (Lore) | 3-5 | `reactions[]` `{window roll_attack, effect {subtract_roll 1d6 (1d8 at 5), applies_to [attack, check, damage]}, usage {pool bardic_inspiration, cost 1}}` (R-G7, sim #114; applies_to 7.3a, #214). **Partial**: the can't-hear and charm-immunity exceptions are not modelled | yes |
| Bardic Inspiration (Bard) | 1-5 | `features[]` `bardic_inspiration` `{grant_die, die 1d6, scale [{5, 1d8}], class bard, range_ft 60, duration {rounds, 100}, applies_to [attack, save, check], cost bonus_action, usage {pool bardic_inspiration, cost 1}}` (7.3a, #214: an ally other than you, not deafened; one held die per creature) + `resources[]` `bardic_inspiration {max {plus_ability cha, min 1}, recharge long_rest}` (short_rest at 5), also spent by Cutting Words from 3rd | yes |
| Jack of All Trades (Bard) | 2-5 | `half_proficiency {on [checks, initiative]}` (7.3a, #214) | yes |
| Font of Inspiration (Bard) | 5 | the pool's `recharge: short_rest`; descriptive feature, no `effect` | yes (pool recharge) |
| Expertise, Bonus Proficiencies (Bard, Lore) | 3-5 | baked into `skills` (Performance and Persuasion doubled; Arcana, Insight, Perception added); descriptive features, no `effect` | yes (static numbers) |
| Bard College | 3 | `classes[0].subclass` = `lore` | descriptive |
| Spellcasting (Druid) | 1-5 | `spellcasting {ability wis, caster_level, slots, spells}`: Produce Flame, Poison Spray; Cure Wounds, Healing Word, Thunderwave; + Barkskin, Hold Person, Lesser Restoration at 3; + Dispel Magic at 5 (every shipped druid spell up to the top slot; the list doubles as the prepared list, within Wis modifier + level) | yes |
| Circle Spells (Land: Mountain) | 3-5 | always prepared, in `spellcasting.spells`: Spike Growth (3), Lightning Bolt (5; not a druid spell, allowed through `subclass_spells`). Mountain is chosen because it gives the most shipped spells that add something (Arctic's two shipped ones are druid spells anyway; Desert, Coast, Forest and Swamp give one; Grassland none). Spider Climb (3) and Meld into Stone (5) are not shipped (see the spells section). Descriptive feature, no `effect` | yes (spell list) |
| Wild Shape (Druid) | 2-5 | `wild_shape {pool wild_shape, forms [jackal, wolf, boar, axe_beak, giant_poisonous_snake, black_bear], max_cr_by_level [{2, 1/4}, {4, 1/2}]}` (R-G12, EC #176) + `resources[]` `wild_shape {max 2, recharge short_rest}`. `no_fly_below` 8 / `no_swim_below` 4 are left at the engine defaults (= SRD). Forms: every shipped beast of CR 1/2 or below without a fly speed; the engine refuses the giant poisonous snake (swim 30, #184) and the black bear (CR 1/2) below 4th. Not listed: the swarm of rats (a swarm of Tiny beasts, not a single beast you have seen), the dire wolf and giant spider (CR 1: from 8th). Duration (half druid level in hours), revert at 0 HP and no casting in beast form are engine-side | yes |
| Natural Recovery (Land) | 2-5 | `slot_recovery {class druid, max_slot_level 5, uses 1}` (sim #148); budget ceil(druid level / 2) derived | yes |
| Druid Circle | 2 | `classes[0].subclass` = `land` | descriptive |
| Unarmored Defense, Unarmored Movement (Monk) | 1-5, 2-5 | baked into `armor_class` (10 + Dex + Wis; no armor or shield is worn) and `speed.walk` 40; descriptive features, no `effect` | yes (static numbers) |
| Martial Arts (Monk) | 1-5 | Dex and the martial arts die (1d4, 1d6 at 5) baked into the `unarmed_strike` action (`properties [unarmed, monk]`); the shortsword is tagged `monk` (a monk weapon). The bonus-action unarmed strike after an Attack action made with either is the sim's `martial_arts` feature-id convention (`docs/CONTENT_CONVENTIONS.md` section 2). The shortsword keeps its own d6 (no better than the die at 1-4, equal at 5) | yes |
| Ki, Flurry of Blows (Monk) | 2-5 | `resources[]` `ki {max = monk level, recharge short_rest}`; Flurry of Blows is the `flurry_of_blows` convention (2 unarmed strikes as a bonus action after the Attack action, 1 ki). Ki save DC (8 + proficiency + Wis) is baked into Stunning Strike's DC | yes |
| Patient Defense, Step of the Wind (Monk) | 2-5 | `bonus_action_grant {actions [dodge], usage {pool ki, cost 1}}` and `{actions [dash, disengage], usage {pool ki, cost 1}}` (7.2zb, #218). **Partial**: Step of the Wind's doubled jump distance is not modelled (jumping is not simulated) | yes |
| Deflect Missiles (Monk) | 3-5 | `reactions[]` `{window {damaged, ranged_only}, effect {reduce_damage, dice 1d10 + Dex + monk level, applies_to_attack [ranged_weapon]}}` (baked: +6 at 3, +8 at 4, +9 at 5; the weapon-only filter is 7.2zb, #218, so ranged spell attacks no longer open it). **Partial**: catching the missile and throwing it back for 1 ki is not modelled. The reduction applies after resistances (the window comes after mitigation) | yes |
| Open Hand Technique (Monk, Open Hand) | 3-5 | `on_event {on hit, choice ask, pick_one, when [attack_from_feature flurry_of_blows], effects [save_condition {dex, DC 8 + pb + Wis, apply prone}, forced_move {save str, push 15}, apply_modifier {no_reactions, until_end_of_caster_next_turn}]}` (7.2zb, #218). The push is a fixed 15 ft (SRD: up to 15 ft) | yes |
| Extra Attack (Monk) | 5 | `extra_attack {attacks_per_action 2}` (EC #179) | yes |
| Stunning Strike (Monk) | 5 | `on_event {on hit, choice ask, when [weapon is melee], usage {pool ki, cost 1}, effects [save_condition {save con DC 13 (8 + pb + Wis), apply [stunned, duration until_end_of_caster_next_turn]}]}`: the duration loads since EC #212 (ends at the end of the monk's next turn; the current turn does not count) | yes |
| Monastic Tradition | 3 | `classes[0].subclass` = `open-hand` | descriptive |
| Pact Magic (Warlock) | 1-5 | `spellcasting {ability cha, caster_level, pact {level, slots}, spells}`: 1 slot of 1st at 1, 2 of 1st at 2, 2 of 2nd at 3-4, 2 of 3rd at 5 (short-rest recovery is the rules' pact-slot rest path). Spells: Eldritch Blast, Chill Touch (+ Poison Spray at 4); Burning Hands (Fiend expanded); + Hold Person, Misty Step, Scorching Ray (expanded) at 3; + Fireball (expanded), Counterspell at 5, within spells known (2-6). At 1-2 the only shipped 1st-level option is Burning Hands, so fewer spells than known are listed | yes |
| Pact Boon: Pact of the Tome | 3-5 | three cantrips from any list in `spellcasting.spells`: Fire Bolt, Ray of Frost, Sacred Flame. Tome is chosen because it is the only boon that is data today: Pact of the Blade needs a summoned, bonded magic weapon (R-G12 / R-G17), and Pact of the Chain needs a familiar the sim cannot load (`pact_of_chain` is rules-only, 7.2s). Descriptive feature, no `effect` | yes (spell list) |
| Agonizing Blast, Repelling Blast (Warlock invocations) | 2-5, 5 | `spell_mod {spell srd51.spell.eldritch_blast, damage_bonus_ability cha}` and `spell_mod {spell srd51.spell.eldritch_blast, on_hit_push_ft 10}` (7.2zb, #218), per beam that hits. The push is a fixed 10 ft (SRD: up to 10 ft) | yes |
| Armor of Shadows (invocation) | 2-5 | baked into `armor_class` 13 + Dex with no armor worn (mage armor at will, 8 hours, no slot: always on). Mage Armor itself is not shipped. Descriptive feature, no `effect` | yes (static number) |
| Dark One's Blessing (Fiend) | 1-5 | `on_event {on reduce_to_zero, when [subject_hostile], effects [temp_hp {amount_from {ability cha, class warlock}}]}` (EC #212): Cha modifier + warlock level, minimum 1, computed at load; temporary HP never stacks (the larger pool is kept) | yes |
| Otherworldly Patron | 1 | `classes[0].subclass` = `fiend` | descriptive |

The validator change in this batch: `attack_bonus`, `crit_range` and `ac_bonus` (sim #107 hooks) are now known effect kinds at `@4` in `tools/validate/checks_v4.py` (until now they produced an "unknown effect kind" warning). Their fields are checked the way the sim checks them at load. The `@<=3` rows are unchanged.

**Extra Attack (done).** The fighter 5 feature `extra_attack` now carries the real kind `{"kind": "extra_attack", "attacks_per_action": 2}` (EC #179) instead of the earlier stand-in, a `multiattack` option `longsword_x2` (two longsword attacks). With the real kind, mixed weapons and grapple / shove replacing an attack are covered by the rules (`TurnBudget::attacks_remaining`). The kind is loaded only from `character@4` files: in a `character@3` (or older) file the sim lists it inert, and the 3 -> 4 migration does not carry it (the migration's kind list is frozen), so the validator gives the unknown-kind warning there.

**Fighting style choice.** One style is picked: Dueling, which fits the shield build. The other SRD fighter styles would be: Archery `attack_bonus {ranged, 2}` and Defense `ac_bonus {1, requires_armor}` (expressible since #107); Two-Weapon Fighting via the `two_weapon_fighting` feature-id convention (`docs/CONTENT_CONVENTIONS.md` section 2); Protection, a `reactions[]` entry (`ally_attacked` / `impose_disadvantage`, shield required); Great Weapon Fighting, not expressible (rules have `Combatant::great_weapon_fighting`, R-G7 #103, but no content kind or loader sets it).

## Descriptive only: mechanic missing (Rogue)

The Rogue features below are kept as a `features[]` entry with the SRD text and **no `effect`**. Unlike the cleric rows further down, they are not left out: the SRD text documents the character, and nothing approximates the rule (the test checks the entries carry no `effect` and that no `reactions[]` stand-in exists).

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
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

## Descriptive only: mechanic missing (Bard)

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Song of Rest (`song-of-rest-d6`) | Bard 2 | Extra 1d6 HP to allies who spend Hit Dice during a short rest: rests and Hit Dice are not simulated inside an encounter | - (out of combat scope) |

## Descriptive only: no combat rule (Druid)

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Druidic (`druidic`) | Druid 1 | A secret language and hidden messages (DC 15 Perception to spot): no combat rule. `Druidic` is also in `languages` | - (out of combat scope) |
| Bonus Cantrip (`bonus-cantrip`) | Druid (Land) 2 | One extra druid cantrip. Both shipped druid cantrips (Produce Flame, Poison Spray) are already known from 1st; Druidcraft, Guidance, Mending, Resistance and Shillelagh are not shipped, so there is nothing to add | T&C (author a druid cantrip, e.g. Shillelagh) |
| Circle of the Land terrain (`circle-of-the-land-mountain`) | Druid (Land) 2 | The land choice only selects the Circle Spells list; no rule of its own | - |

## Descriptive only: mechanic missing (Monk)

Kept as a `features[]` entry with the SRD text and no `effect` (the test pins that).

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Slow Fall (`slow-fall`) | Monk 4 | Reduce falling damage by 5 x monk level: falling and fall damage are not simulated (#184 deferred falling: the grid has no altitude) | - (out of scope until falling) |

## Descriptive only: mechanic missing (Warlock)

Kept as a `features[]` entry with the SRD text and no `effect` (the test pins that). Invocations chosen: Agonizing Blast and Armor of Shadows at 2, Repelling Blast at 5 (Agonizing and Repelling Blast are authored, above) (SRD 5.1 table: 2 invocations at 2nd, 3 at 5th; the mirror's `invocations_known` lists 3 from 4th, which the documents do not follow).

| feature (mirror index) | class / level | why it is not mechanical | gap id / owner |
|---|---|---|---|
| Eldritch Invocations (`eldritch-invocations`) | Warlock 2 | Container for the chosen invocations above; no rule of its own | - |

## Deferred (feature not expressible today)

Each listed feature is **left out of `features[]`**: no descriptive stand-in, same rule as the monster batch. The test checks that none of these ids appear in the documents.

| feature (mirror index) | class / level | missing mechanic | gap id |
|---|---|---|---|
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
| Ranger | 2-4 | Cure Wounds, Hunter's Mark |
| Ranger | 5 | + Barkskin, Lesser Restoration (Spike Growth swapped out for Hunter's Mark) |
| Wizard | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Wizard | 3-4 | + Hold Person, Scorching Ray |
| Wizard | 5 | + Counterspell |
| Druid | 1-2 | Produce Flame, Poison Spray; Cure Wounds, Healing Word, Thunderwave |
| Druid | 3-4 | + Barkskin, Hold Person, Lesser Restoration, Spike Growth (circle) |
| Druid | 5 | + Dispel Magic, Lightning Bolt (circle) |
| Warlock | 1-2 | Eldritch Blast, Chill Touch; Burning Hands (expanded) |
| Warlock | 3 | + Fire Bolt, Ray of Frost, Sacred Flame (Tome); Hold Person, Misty Step, Scorching Ray (expanded) |
| Warlock | 4 | + Poison Spray |
| Warlock | 5 | + Fireball (expanded), Counterspell |
| Sorcerer | 1-2 | Acid Splash, Fire Bolt, Poison Spray; Magic Missile, Shield |
| Sorcerer | 3-4 | + Hold Person, Scorching Ray |
| Sorcerer | 5 | + Counterspell |

Deferred until the spells are authored (`docs/SRD_SPELLS_DEFERRED.md` gives the reason for each):

- **Life domain spells** (always prepared): `srd51.spell.bless` (1), `srd51.spell.spiritual_weapon` (3), `srd51.spell.beacon_of_hope` (5). Lesser Restoration (3) and Revivify (5) ship since spells batch 2 and are listed in the cleric documents.
- **Oath of Devotion spells** (always prepared): `srd51.spell.protection_from_evil_and_good` (3), `srd51.spell.sanctuary` (3), `srd51.spell.zone_of_truth` (5) are not authored; Lesser Restoration (5) ships and is listed.
- **Circle of the Land (Mountain) spells** (always prepared): `srd51.spell.spider_climb` (3), `srd51.spell.meld_into_stone` (5) are not authored; Spike Growth (3) and Lightning Bolt (5) ship and are listed.
- **Fiend expanded spells** (choices for spells known, not always prepared): Command (1) and Blindness/Deafness (3) are not shipped; Stinking Cloud (5) ships but is not picked (six known at 5th).
- **Cantrips known**: the Cleric knows 3 (4 from level 4) but the pack ships one cleric cantrip (Sacred Flame). The Wizard knows 3 (4 from level 4), and the pack ships three wizard cantrips. Missing: Guidance, Light, Mending, Resistance, Spare the Dying, Thaumaturgy (cleric); Chill Touch, Ray of Frost, Shocking Grasp, True Strike and the utility cantrips (wizard). The Sorcerer knows 4 cantrips (5 from level 4) and 2-6 spells; the pack ships three sorcerer cantrips and five leveled sorcerer spells (Chill Touch, Ray of Frost, Shocking Grasp, Burning Hands, Chromatic Orb, Sleep, Thunderwave, Misty Step and the rest are not shipped), so each document lists fewer.
- **Spellbook and prepared spells**: a level 1 wizard's spellbook holds six 1st-level spells, plus two per level after that; a cleric prepares Wis modifier + level spells. The documents list fewer because the rest are not shipped (Burning Hands, Mage Armor, Sleep, Thunderwave, Misty Step, Fireball, Lightning Bolt, Bless, Guiding Bolt, Shield of Faith, Spiritual Weapon, Spirit Guardians, ...). The `spells` list doubles as the prepared list. `character@4` has no separate spellbook.

## Notes for Rules / Engine Core

* The 20 documents load through the sim's `ContentLibrary` without errors. They use the constructs of the shipped fighter plus `grant_action`, `crit_range` and `multiattack`, all of which the sim already compiles (`static_feature_tests`, `features_tests`, `multiattack_content_tests`). One C++ test hardcodes pack ownership: `engine/core/sim/tests/packset_tests.cpp` assumes every unlisted id that is not `srd51.monster.*` belongs to `sample.vertical_slice`. It needs `srd51.character.*` added to the `srd51.core` default (one-line test change, Engine Core).
* `procgen::spawn_archetypes` and the vertical-slice spawns still name only `srd51.monster.goblin` and `srd51.character.fighter_1`, so procgen outputs and goldens are untouched.
* The cleric's `bonus_proficiency` and the wizard's `evocation_savant` have no `effect`. The sim lists them in `Archetype::unsupported_features` as "(no effect)", the same as descriptive monster traits.
