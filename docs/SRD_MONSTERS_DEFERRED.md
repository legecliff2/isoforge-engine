# SRD 5.1 monsters, CR 0-2 first batch: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules) and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Stat blocks come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) `src/2014/en/5e-SRD-Monsters.json`, commit `a6212be` (the same snapshot as the coverage audit). The key stats of every authored monster are pinned against a verbatim copy of those fields in `tests/validate/data/srd51_monsters_5e_database_a6212be.json` by `tests/validate/test_srd51_monsters.py`.

**Scope rule.** The batch is the 30 CR <= 2 candidates of the T&C content inventory (section 1.3). A monster is authored as `monster@4` only if every combat-relevant SRD mechanic is either (a) carried as data by a construct that `monster@4` / `common@2` and the validator accept today, or (b) a descriptive sense/flavor trait with no combat rule (Keen Smell, False Appearance, Amphibious, ...). Anything else is **deferred, not approximated** (no descriptive stand-in for a mechanic). `monster@4` documents are *validated, not (fully) played*: see "What the sim plays today".

## Counts

| | count |
|---|---|
| inventory candidates | 30 |
| authored now as `monster@4` | 33 (17 in batch 1, 4 Pack Tactics monsters after #127, orc and berserker after #129, boar, hobgoblin and bugbear after #158, ghoul and giant spider after #170, basilisk and medusa after #153, swarm of rats after #172, gelatinous cube and giant toad after Rules #189) |
| goblin (already shipped, left at `monster@3`) | 1 |
| deferred (mechanic not expressible) | 0 |

## Authored (33, all `monster@4`)

| monster | CR | mechanics carried as data | notes |
|---|---|---|---|
| Commoner | 0 | club | |
| Bandit | 1/8 | scimitar, light crossbow | |
| Guard | 1/8 | spear: one-handed, two-handed (`two_handed`), thrown | the SRD "melee or ranged" spear is three actions, as in the equipment/versatile convention of the coverage doc |
| Skeleton | 1/4 | vulnerability bludgeoning, immunity poison, condition immunities poisoned/exhaustion | |
| Zombie | 1/4 | Undead Fortitude as `on_event drop_to_zero` (not radiant, not crit, Con save DC 5 + damage taken, `set_hp 1`), immunity poison, poisoned | trigger not played yet (see below) |
| Giant Poisonous Snake | 1/4 | bite reach 10 + `on_hit save_condition` (Con DC 11, 3d6 poison, half on success) | rider not played yet |
| Axe Beak | 1/4 | beak (Large) | |
| Grimlock | 1/4 | two damage components (bludgeoning + piercing), condition immunity blinded | Blind Senses / Keen senses / Stone Camouflage descriptive (no vision/lighting model, R-G13) |
| Gnoll | 1/2 | Rampage as `on_event reduce_to_zero` -> `bonus_attack bite` (melee, once per turn); bite, spear (1h / 2h / thrown), longbow | the "move up to half its speed" part of Rampage is the creature's own movement choice and is not encoded; trigger not played yet |
| Lizardfolk | 1/2 | Multiattack: six options = every pair of the four melee weapons (bite, heavy club, javelin, spiked shield; "each with a different weapon") | AC 15 (natural armor 13, or 15 with the shield; the table lists both, the SRD line is 15) |
| Black Bear | 1/2 | Multiattack bite + claws | Perception +3 added (SRD block; the table omits it but gives passive 13, and the validator requires 10 + skill) |
| Scout | 1/2 | Multiattack: 2 melee or 2 ranged (two options); `behavior.ai` = `skirmisher.luau` | only monster given the pack's existing AI script (target selection; fits a ranged skirmisher) |
| Animated Armor | 1 | Multiattack slam x2, immunities poison/psychic, 8 condition immunities | Antimagic Susceptibility is descriptive (no antimagic field / dispel in the pack's spells) |
| Spy | 1 | Multiattack shortsword x2, Cunning Action (`bonus_action_grant` dash/disengage/hide), Sneak Attack as `on_event hit` once per turn, `advantage_or_ally_adjacent`, `extra_damage` 2d6 `same_as_weapon` | no `weapon` predicate: both of the spy's weapons (finesse shortsword, ranged crossbow) qualify; trigger not played yet |
| Ogre | 2 | greatclub, javelin (melee and thrown) | |
| Gargoyle | 2 | `damage_mitigation` (nonmagical not adamantine: bludgeoning, piercing, slashing), immunity poison, condition immunities, Multiattack bite + claws, fly 60 | plain `damage_resistances` stays empty on purpose (it would be unqualified) |
| Black Dragon Wyrmling | 2 | bite with two damage components; Acid Breath as `save_effect` (Dex DC 11, 5d8 acid, half, line 15 x 5 ft, recharge 5-6), immunity acid | `save_effect` not played yet |
| Jackal | 0 | Pack Tactics as trait effect `{ "kind": "pack_tactics" }` (sim #127), bite | Keen Hearing and Smell descriptive (no perception checks in the sim) |
| Kobold | 1/8 | Pack Tactics (`pack_tactics`), dagger, sling | **partial**: Sunlight Sensitivity (disadvantage on attacks in sunlight) is description only: there is no light / sunlight model or supported effect for it (R-G13), so the kobold attacks normally in sunlight |
| Wolf | 1/4 | Pack Tactics (`pack_tactics`), bite (knock-prone rider) | authored: the bite carries `on_hit [{"kind": "save_condition", "save": {"ability": "str", "dc": 11}, "apply": [{"condition": "prone"}]}]`, prone untimed (sim #133: applied untracked, ends when the target stands up). Keen Hearing and Smell descriptive |
| Dire Wolf | 1 | Pack Tactics (`pack_tactics`), bite (knock-prone rider) | authored, as wolf (Str DC 13, prone untimed). Keen Hearing and Smell descriptive |
| Orc | 1/2 | Aggressive as trait effect `{ "kind": "aggressive" }` (sim #129: bonus-action Dash toward a visible hostile; the built-in AI uses it), greataxe, javelin (melee and thrown) | |
| Berserker | 2 | Reckless as trait effect `{ "kind": "reckless_attack" }` (sim #129), greataxe | the sim applies Reckless when the caller declares `ActionRequest::reckless` on the first attack; the built-in AI never declares it, so an AI-driven berserker attacks without it (a choice the AI does not make yet, not a content gap) |
| Boar | 1/4 | Charge as `on_event hit` once per turn (`weapon melee` + `moved_at_least_ft 20`: +1d6 slashing, Str DC 11 prone untimed); Relentless as `on_event drop_to_zero` (`damage_at_most 7`, `usage per_rest 1 short`, `set_hp 1`) (#158), tusk | **partial**: "straight toward the target" isn't checked (the predicate counts movement only) |
| Hobgoblin | 1/2 | Martial Advantage as `on_event hit` once per turn (`weapon_attack` + `ally_near_target`, +2d6) (#158); longsword (1h / 2h), longbow | the sim's ally fact skips incapacitated allies (the SRD's "isn't incapacitated" clause) |
| Bugbear | 1 | Surprise Attack as `on_event hit` (`target_surprised`, +2d6) (#158); morningstar, javelin (melee and thrown) | Brute is baked into the printed dice (description only). **partial**: "during the first round" relies on surprise lasting only for round 1 |
| Ghoul | 1 | claws `on_hit save_condition` (Con DC 10, paralyzed 10 rounds, repeat at end of turn) gated by rider `when` [`not_creature_type undead`, `not_target_tag elf`] (#170: a character race ending in `elf` adds the `elf` tag, `half_elf` does not); bite | |
| Giant Spider | 1 | Web as `attack_effect` (ranged +5, 30/60 ft via `long_range_ft` (#174), restrained with a Str DC 12 escape, recharge 5-6) (#170); the webbing as `apply[].object` (AC 10, hp 5, vulnerable fire, immune bludgeoning / poison / psychic) (#174); bite + `on_hit save_condition` (Con DC 11, 2d8 poison, half) with `zero_hp` (stable, poisoned 600 rounds = 1 hour, paralyzed while poisoned) (#174) | Spider Climb / Web Sense / Web Walker are description only |
| Swarm of Rats | 1/4 | `tags: ["swarm"]` (#172: shares spaces both ways, no healing / temp HP), `type: beast` (T&C-G15 convention for "swarm of Tiny beasts"), BPS resistances, 8 condition immunities; bites 2d6 with `half_hp_damage: "1d6"` (#172), reach 0 ft (SRD: a target in the swarm's space) | Keen Smell descriptive; Tiny openings: nothing to model (no sub-tile openings) |
| Basilisk | 3 | Petrifying Gaze as trait effect `gaze` (7.2p, sim #153): 30 ft, Con DC 12, restrained, repeat save at the end of its turn, petrified on a failed repeat save (`escalates`, `escalate_margin` 0); bite with two damage components (piercing + poison) | **partial**: the reflection clause (it targets itself when it sees its reflection in bright light) and "until freed by greater restoration" are description only (no mirror / light model, no Greater Restoration). Averting the eyes and the sight check are the sim's (`GazeResponse`, `gaze_seen`) |
| Medusa | 6 | Petrifying Gaze as `gaze` (Con DC 14, 30 ft, `escalate_margin` 5: failing by 5 or more petrifies at once; otherwise restrained, then petrified on a failed repeat save); Multiattack: snake hair + 2 shortswords, or 2 longbows; snake hair and longbow with a poison damage component | **partial**: as basilisk, the reflection curse is description only; proficiency bonus +3 (CR 6) |
| Gelatinous Cube | 2 | Engulf as `save_effect` (Dex DC 12, 3d6 acid, no effect on a success) + `apply` restrained, `escape` Str DC 12, `max_size large`, `contain {engulf, 6d6 acid}` (Rules #189, 7.2w: acid at the start of the cube's turns, freed when it dies); Transparent as trait effect `{kind: transparent, dc: 15}`; pseudopod; 6 condition immunities, blindsight 60 | **partial**: the cube's move through spaces, the "pushed instead" choice on a success, the Ooze Cube trait (entering its space, pulling a creature out, capacity of one Large or four Medium) and moving engulfed creatures with the cube are description only (sim movement pending, coverage row 11) |
| Giant Toad | 1 | bite with two damage components + `on_hit [{kind: grapple, escape_dc: 13, restrains: true, exclusive: true}]` (Druid gap G: restrained while grappled, can't bite another target); Swallow as `attack_effect` (melee +4, 5 ft) + `apply` restrained, `max_size medium`, `contain {swallow, 3d6 acid}` (Rules #189: blinded, total cover from outside, one at a time, exits prone when the toad dies) | **partial**: Swallow being a bite against the creature it is grappling (with the bite's damage) is not modelled (Druid gap S) (no grappled-by-self predicate; attack_effect carries no damage). Amphibious / Standing Leap descriptive |

The goblin keeps `$schema: monster@3`, its id (`srd51.monster.goblin`, stable id derived from it) and bytes. It is referenced by C++ tests that assert its `@3` form (`content_migration_tests`, `pack_set_tests`, compat goblin tests) and by the fl1 compat corpus, and a schema change could shift digests and goldens; migrating it buys nothing until a `@4`-only feature is needed.

Not set: `presentation` (the goblin has none; sprite assets for these monsters do not exist yet). `behavior.ai`: only the scout (above).

### What the sim plays today (main at `0645ace`)

Played: stats, attacks with every `damage[]` entry (#45), resistances / immunities / vulnerabilities / condition immunities (#45), `multiattack` options (#48/#59), `damage_mitigation` (#47/#58), `bonus_action_grant`, size footprint (#50/#55).
Carried and validated but **not applied** (the registry reserves the `@4` kinds; the archetype lists them in `unsupported_features` and ignores them): `on_event` (Undead Fortitude, Rampage, Sneak Attack), `on_hit[]` riders (snake poison), `save_effect` actions with `usage` (Acid Breath). Until Engine Core lands the row 6 compile / row 9-10 trigger hooks these monsters play without those effects. No content change is needed when the hooks land.

## Deferred (0)

None. The last deferred monster, `srd51.monster.swarm_of_rats` (T&C-G7, T&C-G14, T&C-G15), was authored after sim #172 (swarm tag, `half_hp_damage`).

### Re-check at `cfb5eed` (2026-10-19)

All 12 stayed deferred at that point; jackal, kobold, wolf and dire wolf were authored afterwards once #127 landed the `pack_tactics` loader kind. Checked against the sim loader (`engine/core/sim/src/content.cpp`, `features.cpp`), the rules content loader (`engine/rules/content/src/spell_loader.cpp`), `schemas/common/v2.schema.json` / `schemas/monster/v4.schema.json` and `tools/validate_packs.py` / `tools/checks_v4.py`:

| landed mechanic | loads from content? | shape | unblocks |
|---|---|---|---|
| Undead Fortitude | yes | `on_event drop_to_zero` + `save` predicate (`dc_from_damage`) + `set_hp` | (zombie, already authored) |
| Rampage | yes | `on_event reduce_to_zero` + `bonus_attack` rider | (gnoll, already authored) |
| Sneak Attack | yes | `on_event hit` + `advantage_or_ally_adjacent`, `limit once_per_turn` | Martial Advantage (hobgoblin) uses `weapon_attack` + `ally_near_target` (#158) |
| breath weapons | yes | `save_effect` with `area`, `usage recharge` | (wyrmling, already authored); not Web (attack roll) |
| reactions | yes | `reactions[]` (#114/#115) | none of the 12 |
| grapple | sim only | `on_hit [{"kind":"grapple","escape_dc":N}]` (#104); not in the `common@2` rider enum (validator rejects it) | none of the 12 (Web restrains) |
| timed modifiers, Metamagic, Haste/Slow, Sleep, dispel | spell / `apply_modifier` mechanics | not used by any of the 12 stat blocks | none of the 12 |
| Pack Tactics | **yes** (#127) | trait effect `{ "kind": "pack_tactics" }` (common@1 open kind) | jackal, kobold, wolf, dire wolf: authored (kobold partial, Sunlight Sensitivity descriptive) |
| Aggressive, Reckless (#111) | **yes** (#129) | trait effects `{ "kind": "aggressive" }` / `{ "kind": "reckless_attack" }` (common@1 open kinds) | orc, berserker: authored |
| surprise (#108) | yes (`target_surprised`, #158) | | bugbear Surprise Attack |

## Other notes for Rules / Engine Core

* `srd51.core` is now `api_level` 2 (needed by every `monster@4` document, VERSION_BUMP_BATCH 8.5; PR #72 makes the same one-line change). A C++ host whose maximum API level is 1 refuses the pack; Engine Core is adding level 2.
* `procgen::spawn_archetypes(1)` is the fixed table `{srd51.monster.goblin}`; nothing here changes it, so procgen outputs and `tests/goldens/stream/*.txt` are untouched. These monsters are not spawned by procgen.
* The AI chooses an attack/multiattack option by declaration order: lizardfolk opens bite + heavy club; scout opens the melee option (the sim AI never picks a multiattack option containing a ranged attack).
* The adapter reads `damage[0]` plus extra components and hard-codes `finesse = false`; weapon `properties` here (`two_handed`, `thrown`) are descriptive for the adapter.
