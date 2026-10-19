# SRD 5.1 monsters, CR 0-2 first batch: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules) and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Stat blocks come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) `src/2014/en/5e-SRD-Monsters.json`, commit `a6212be` (the same snapshot as the coverage audit). The key stats of every authored monster are pinned against a verbatim copy of those fields in `tests/validate/data/srd51_monsters_5e_database_a6212be.json` by `tests/validate/test_srd51_monsters.py`.

**Scope rule.** The batch is the 30 CR <= 2 candidates of the T&C content inventory (section 1.3). A monster is authored as `monster@4` only if every combat-relevant SRD mechanic is either (a) carried as data by a construct that `monster@4` / `common@2` and the validator accept today, or (b) a descriptive sense/flavor trait with no combat rule (Keen Smell, False Appearance, Amphibious, ...). Anything else is **deferred, not approximated** (no descriptive stand-in for a mechanic). `monster@4` documents are *validated, not (fully) played*: see "What the sim plays today".

## Counts

| | count |
|---|---|
| inventory candidates | 30 |
| authored now as `monster@4` | 17 |
| goblin (already shipped, left at `monster@3`) | 1 |
| deferred (mechanic not expressible) | 12 |

## Authored (17, all `monster@4`)

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

The goblin keeps `$schema: monster@3`, its id (`srd51.monster.goblin`, stable id derived from it) and bytes. It is referenced by C++ tests that assert its `@3` form (`content_migration_tests`, `pack_set_tests`, compat goblin tests) and by the fl1 compat corpus, and a schema change could shift digests and goldens; migrating it buys nothing until a `@4`-only feature is needed.

Not set: `presentation` (the goblin has none; sprite assets for these monsters do not exist yet). `behavior.ai`: only the scout (above).

### What the sim plays today (main at `0645ace`)

Played: stats, attacks with every `damage[]` entry (#45), resistances / immunities / vulnerabilities / condition immunities (#45), `multiattack` options (#48/#59), `damage_mitigation` (#47/#58), `bonus_action_grant`, size footprint (#50/#55).
Carried and validated but **not applied** (the registry reserves the `@4` kinds; the archetype lists them in `unsupported_features` and ignores them): `on_event` (Undead Fortitude, Rampage, Sneak Attack), `on_hit[]` riders (snake poison), `save_effect` actions with `usage` (Acid Breath). Until Engine Core lands the row 6 compile / row 9-10 trigger hooks these monsters play without those effects. No content change is needed when the hooks land.

## Deferred (12)

Gap ids: `T&C-Gn` = Tools & Content, `R-Gn` = Rules (see the crosswalk in the coverage doc).

| monster (`srd51.monster.<id>`) | CR | missing mechanic | gap id |
|---|---|---|---|
| `srd51.monster.jackal` | 0 | Pack Tactics: conditional advantage on attacks (ally within 5 ft). Rules has the `RollTrait` `kWhenAllyNearTarget` and sim supplies `ally_near_target` (#54, #61), but `common@2` has no effect/trait shape to declare it | T&C-G7 (R-G7); content shape |
| `srd51.monster.kobold` | 1/8 | Pack Tactics (as above). Sunlight Sensitivity would be descriptive (R-G13) | T&C-G7 |
| `srd51.monster.wolf` | 1/4 | Pack Tactics (as above). The prone bite rider alone is expressible | T&C-G7 |
| `srd51.monster.dire_wolf` | 1 | Pack Tactics (as above) | T&C-G7 |
| `srd51.monster.boar` | 1/4 | Relentless: `drop_to_zero` only when the damage is 7 or less (no damage-threshold predicate), once per short/long rest. Charge (`moved_at_least_ft` 20, 1d6, Str DC 11 prone) is expressible but is not worth shipping without Relentless | T&C-G7 (predicate: damage taken <= N) |
| `srd51.monster.hobgoblin` | 1/2 | Martial Advantage: +2d6 only if an ally is within 5 ft of the target. The only ally predicate is `advantage_or_ally_adjacent` (advantage OR ally), which would also fire with advantage and no ally | T&C-G7 (predicate: ally adjacent only) |
| `srd51.monster.orc` | 1/2 | Aggressive: bonus-action move toward a visible hostile. `bonus_action_grant dash` has no direction constraint | T&C-G7 (coverage doc: "`bonus_action_grant` has no Move") |
| `srd51.monster.bugbear` | 1 | Surprise Attack: +2d6 when it surprises a creature in round 1. No surprise state or predicate. (Brute is already baked into the printed attack dice per SRD text) | R-G16 (surprise) + T&C-G7 |
| `srd51.monster.ghoul` | 1 | Claws paralysis rider applies only to creatures other than elves and undead; riders have no `when` / target predicate (`on_hit` rider shape is closed) | T&C-G5 (rider target predicate) |
| `srd51.monster.giant_spider` | 1 | Web: a ranged *attack roll* that only restrains (no damage; `ranged_weapon_attack` requires `damage`, `save_effect` is a save not an attack) with Recharge 5-6; Strength-check escape and destructible web. Also the bite's "stable but poisoned and paralyzed at 0 HP" clause | T&C-G4 / T&C-G5 (+ R-G8 for restrain source) |
| `srd51.monster.berserker` | 2 | Reckless: free per-turn choice of advantage on own melee attacks and advantage against it. `apply_modifier` needs a cost and a duration shape that does not fit a free start-of-turn toggle | T&C-G7 / R-G7 |
| `srd51.monster.swarm_of_rats` | 1/4 | Swarm: shares space with other creatures (T&C-G14), cannot regain HP / gain temp HP; bite damage 2d6 or 1d6 at half HP (no HP-conditional damage swap); creature type "swarm of Tiny beasts" is not in the `creature_type` enum (`beast` + tag `swarm` is the convention, T&C-G15) | T&C-G7, T&C-G14, T&C-G15 |

## Other notes for Rules / Engine Core

* `srd51.core` is now `api_level` 2 (needed by every `monster@4` document, VERSION_BUMP_BATCH 8.5; PR #72 makes the same one-line change). A C++ host whose maximum API level is 1 refuses the pack; Engine Core is adding level 2.
* `procgen::spawn_archetypes(1)` is the fixed table `{srd51.monster.goblin}`; nothing here changes it, so procgen outputs and `tests/goldens/stream/*.txt` are untouched. These monsters are not spawned by procgen.
* The AI chooses an attack/multiattack option by declaration order: lizardfolk opens bite + heavy club; scout opens the melee option (the sim AI never picks a multiattack option containing a ranged attack).
* The adapter reads `damage[0]` plus extra components and hard-codes `finesse = false`; weapon `properties` here (`two_handed`, `thrown`) are descriptive for the adapter.
