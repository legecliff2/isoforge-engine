# SRD 5.1 monsters, CR 0-2 first batch: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 (“SRD 5.1”) by Wizards of the Coast LLC and available at https://dnd.wizards.com/resources/systems-reference-document. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License available at https://creativecommons.org/licenses/by/4.0/legalcode.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules) and `docs/SRD_SPELLS_DEFERRED.md`; pack `content/packs/srd51.core`.

**Source.** Stat blocks come from [5e-bits/5e-database](https://github.com/5e-bits/5e-database) `src/2014/en/5e-SRD-Monsters.json`, commit `a6212be` (the same snapshot as the coverage audit). The key stats of every authored monster are pinned against a verbatim copy of those fields in `tests/validate/data/srd51_monsters_5e_database_a6212be.json` by `tests/validate/test_srd51_monsters.py`.

**Scope rule.** The batch is the 30 CR <= 2 candidates of the T&C content inventory (section 1.3). A monster is authored as `monster@4` only if every combat-relevant SRD mechanic is either (a) carried as data by a construct that `monster@4` / `common@2` and the validator accept today, or (b) a descriptive sense/flavor trait with no combat rule (Keen Smell, False Appearance, Amphibious, ...). Anything else is **deferred, not approximated** (no descriptive stand-in for a mechanic). `monster@4` documents are *validated, not (fully) played*: see "What the sim plays today".

## Counts

| | count |
|---|---|
| inventory candidates | 30 |
| authored now (`monster@4`, all converted to `monster@5` in §14 of CONTENT_CONVENTIONS) | 84 (17 in batch 1, 4 Pack Tactics monsters after #127, orc and berserker after #129, boar, hobgoblin and bugbear after #158, ghoul and giant spider after #170, basilisk and medusa after #153, swarm of rats after #172, gelatinous cube and giant toad after Rules #189; 9 bestiary v0.1 batch 1 monsters as `monster@5` with `derive: true`: deer, pony, awakened shrub, hyena, cultist, giant weasel, mastiff, noble, tribal warrior; 9 bestiary v0.1 batch 2 monsters, same form: giant fire beetle, goat, shrieker, giant rat, thug, bandit captain, veteran, knight, mule; rat and scorpion after flat damage, #295; bestiary v0.1 batch 3: satyr, ghast; gladiator after the riders unblock, CONTENT_CONVENTIONS 21; wight, vampire spawn, death dog, violet fungus and shadow in batch 3b after feat/monster-batch3-riders + feat/monster-strength-drain; 11 tier B batch d1 monsters as `monster@5` with `derive: true`: baboon, camel, draft horse, riding horse, giant lizard, giant centipede, giant wolf spider, ape, worg, brown bear, giant hyena; 10 batch D2 monsters as `monster@5` with `derive: true`: owlbear, awakened tree, hill giant, stone giant, giant ape, ettercap, merfolk, plesiosaurus, reef shark and killer whale (swim-only `walk: 0` after feat/monster-swim-only); acolyte and cult fanatic after the utility cantrips, CONTENT_CONVENTIONS 28) |
| goblin (already shipped, left at `monster@3`) | 1 |
| deferred (mechanic not expressible) | 21 (rust monster, mummy: batch 3; blink dog: batch 3b; duergar, mimic, doppelganger, green hag: tier-A triage; 14 tier B batch d1) |

## Authored (84; bestiary v0.1 batch 1 and 2, tier B batch d1, batch D2, acolyte and cult fanatic rows below at `monster@5` + `derive: true`)

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
| Deer | 0 | bite (natural 1d4, Str) | |
| Pony | 1/8 | hooves (natural 2d4, Str) | |
| Awakened Shrub | 0 | rake (natural 1d4, Dex), vulnerability fire, resistance piercing | False Appearance descriptive |
| Hyena | 0 | Pack Tactics (`pack_tactics`), bite (natural 1d6, Str) | |
| Cultist | 1/8 | leather armor, scimitar | **partial**: Dark Devotion (advantage on saving throws against being charmed or frightened) is description only: no monster trait construct grants save advantage keyed on the condition (`advantage_on.saves` is per ability and feature-only; the rules `save_vs_conditions` row of SRD_COMBAT_COVERAGE has no content loader yet), so the cultist saves against charm / fear normally |
| Giant Weasel | 1/8 | bite (natural 1d4, Dex), darkvision 60 | Keen Hearing and Smell descriptive |
| Mastiff | 1/8 | bite (natural 1d6, Str) with knock-prone rider (Str DC 11, prone untimed, as wolf) | Keen Hearing and Smell descriptive |
| Noble | 1/8 | breastplate, rapier; Parry as `reactions[]` (`being_hit`, `melee_only`, `requires_sight`, `ac_bonus 2`) | "be wielding a melee weapon" is not a predicate; the noble's only weapon (rapier) is always equipped |
| Tribal Warrior | 1/8 | Pack Tactics (`pack_tactics`), hide armor, spear (1h / 2h / thrown, as guard) | |
| Baboon | 0 | Pack Tactics (`pack_tactics`), bite (Str, 1d4 - 1), climb 30 | batch d1, `monster@5` |
| Camel | 1/8 | bite with `natural.damage_ability: "none"` (SRD prints 2 (1d4) with Str 16) | batch d1, Large |
| Draft Horse | 1/4 | hooves | batch d1, Large |
| Riding Horse | 1/4 | hooves | batch d1, Large; XP 50 per SRD 5.1 (CR 1/4); 5e-database a6212be lists 25 XP, documented in `DEVIATIONS` |
| Giant Lizard | 1/4 | bite, climb 30 | batch d1, Large; darkvision 30 recorded in `senses` |
| Giant Centipede | 1/4 | bite + `on_hit save_condition` (Con DC 11, 3d6 poison, `on_success none`) with `zero_hp` (stable, poisoned 600 rounds, paralyzed while poisoned; as giant spider #174), climb 30 | batch d1; blindsight 30 in `senses` |
| Giant Wolf Spider | 1/4 | bite + `on_hit save_condition` (Con DC 11, 2d6 poison, half) with `zero_hp` (as giant spider), climb 40 | batch d1; Spider Climb / Web Sense / Web Walker description only (as giant spider) |
| Ape | 1/2 | Multiattack fist x2; rock as ranged natural attack (Str, 25/50) | batch d1 |
| Worg | 1/2 | bite knock-prone rider (Str DC 13, prone untimed; as dire wolf) | batch d1, Large; Keen Hearing and Smell descriptive |
| Brown Bear | 1 | Multiattack bite + claws; `attack_bonus` 5 on both (SRD prints +5; Str 19 + PB 2 = +6) | batch d1, Large; Keen Smell descriptive |
| Giant Hyena | 1 | Rampage as gnoll (`on_event reduce_to_zero` -> `bonus_attack bite`) | batch d1, Large; as gnoll, the half-speed move is not encoded |
| Giant Fire Beetle | 0 | bite (natural 1d6, Str); `natural_armor` 13; Illumination as `emits_light` (bright 10, dim 10; LIGHTING 3.4); blindsight 30 | |
| Goat | 0 | ram (natural 1d4, Str); Charge as `on_event hit` once per turn (`weapon melee` + `moved_at_least_ft 20`: +1d4 bludgeoning, Str DC 10 prone untimed), as boar | **partial**: "straight toward" isn't checked (as boar); Sure-Footed (advantage on Str / Dex saves against being knocked prone) is description only: no construct grants save advantage keyed on the effect (as cultist Dark Devotion) |
| Shrieker | 0 | AC 5 (10 + Dex -5), speed 0, blindsight 30, condition immunities blinded / deafened / frightened; no attacks | **partial**: Shriek (an SRD reaction: a 300 ft alarm while light or a creature is within 30 ft) is a description-only trait: `reactions[]` needs a window / effect pair and there is no noise / alert model. The table lists blinded twice and omits deafened; the content follows the SRD line (test DEVIATIONS). False Appearance descriptive |
| Giant Rat | 1/8 | Pack Tactics (`pack_tactics`), bite (natural 1d4, Dex), darkvision 60 | Keen Smell descriptive |
| Thug | 1/2 | leather armor, mace, heavy crossbow (`crossbow_heavy`); Multiattack mace x2; Pack Tactics (`pack_tactics`) | |
| Bandit Captain | 2 | studded leather, scimitar, dagger (melee and thrown); Multiattack: scimitar x2 + dagger, or 2 thrown daggers; Parry as `reactions[]` (`ac_bonus 2`, as noble); saving throws | "be wielding a melee weapon" is not a predicate (scimitar always equipped) |
| Veteran | 3 | splint, longsword (1h / 2h), shortsword, heavy crossbow; Multiattack: longsword x2 + shortsword, or two-handed longsword x2 | "if it has a shortsword drawn" is not a predicate: the shortsword is always equipped |
| Knight | 3 | plate, greatsword (two hands), heavy crossbow; Multiattack greatsword x2; Parry (`ac_bonus 2`); saving throws | **partial**: Brave (advantage on saves vs frightened) is description only (no condition-keyed save advantage construct; SRD_COMBAT_COVERAGE `save_vs_conditions` has no content loader). Leadership is a `special` action, description only: its rules (AlliesWithin30 1d4, #89/#91) have no content shape yet (T&C-G4 / R-G3), so the knight never uses it |
| Gladiator | 5 | studded leather + shield, spear (1h / 2h / thrown), Shield Bash (monster-local weapon 1d4); Brute as `weapon_extra_dice 1` (`melee_only`), so melee spear 2d6 + 4, two-handed 2d8 + 4, Shield Bash 2d4 + 4; Shield Bash `on_hit save_condition` (Str DC 15, prone untimed, `max_size medium`: larger targets make no save, CONTENT_CONVENTIONS 21); thrown spear with an approved `damage_override` 2d6 + 4 (the SRD prints one damage line for both uses; the thrown derivation has no Brute die, 1d6 + 4; test DEVIATIONS); Multiattack: every mix of three melee attacks (spear / shield bash) or two thrown spears; Parry as `reactions[]` (`ac_bonus 3`, as noble); saving throws; proficiency bonus +3 | **partial**: Brave (advantage on saves vs frightened) is description only (as knight). Was deferred in batch 2 until the riders unblock (feat/monster-gladiator-riders `9ef0603`) |
| Mule | 1/8 | hooves (natural 1d4, Str) with an approved `attack_bonus` override of **+2** (the SRD print; the derivation gives +4 = Str +2 and PB +2; test DEVIATIONS) | **partial**: Sure-Footed is description only (as goat); Beast of Burden is descriptive (no carrying-capacity model) |
| Rat | 0 | bite as flat damage `{"flat": 1, "type": "piercing"}` (CONTENT_CONVENTIONS 19), to-hit +0 as printed, darkvision 30 | no `natural` block: the derivation needs a die, so a flat-only attack keeps its printed to-hit and damage (as the #295 validator fixture). Keen Smell descriptive. Was deferred in batch 1 |
| Scorpion | 0 | sting as flat 1 piercing + `on_hit save_condition` (Con DC 9, 1d8 poison, half); `natural_armor` 11; blindsight 10 | as rat (no `natural` block). Was deferred in batch 1 |
| Satyr | 1/2 | leather armor, shortsword, shortbow; ram (natural 2d4, Str) | Magic Resistance as trait `save_advantage {magical: true}` (batch 3b) |
| Wight | 3 | studded leather, longsword (1h / 2h, `ability str`), longbow (`ability dex`); Life Drain natural 1d6 + 2 necrotic with `drain {max_hp, save Con 13}`; Multiattack 2 longsword / 2 longbow / Life Drain + longsword; resist necrotic + `damage_mitigation nonmagical_not_silvered`; immune poison, exhaustion, poisoned | **partial**: zombie rising (24 h) and Sunlight Sensitivity (no light model) are description only |
| Vampire Spawn | 5 | `natural_armor` 12 + Dex; Bite 1d6 + 3 piercing + 2d6 necrotic, `drain {max_hp, damage_type necrotic, heal_self}`, `target_requires_any [willing, grappled_by_self, incapacitated, restrained]`; Claws 2d4 + 3; Multiattack claws ×2 / one bite + claws; resist necrotic + `damage_mitigation nonmagical` | **partial**: Regeneration, Spider Climb, Vampire Weaknesses description only; Claws "grapple instead of dealing damage" not carried (on-hit grapple always deals damage too, so Claws deals damage only) |
| Death Dog | 1 | Bite ×2 (+4, 1d6 + 2), `on_hit save_condition` Con DC 12 poisoned `until_cured`; Two-Headed as `save_advantage` vs blinded / charmed / deafened / frightened / stunned / unconscious; Perception +5 (expertise) | **partial**: the disease's 24-hour 1d10 max-HP loss is deferred (24 h effects); Two-Headed's Perception advantage is description only |
| Violet Fungus | 1/4 | Rotting Touch (reach 10, 1d8 necrotic, `damage_ability none`, approved `attack_bonus` +2); Multiattack `count_dice 1d4` | complete (False Appearance is descriptive). DEVIATIONS: attack bonus, condition immunities as shrieker |
| Shadow | 1/2 | Strength Drain natural 2d6 + 2 necrotic (`ability dex`) + `ability_drain {str, 1d4}`; vulnerable radiant; resist acid / cold / fire / lightning / thunder + `damage_mitigation nonmagical`; immune necrotic, poison | **partial**: shadow spawning on a humanoid's death, Amorphous, Shadow Stealth and Sunlight Weakness are description only |
| Ghast | 2 | bite (natural 2d8, Dex) with an approved `attack_bonus` override of **+3** (SRD print; derived +5 = Dex +3 and PB +2; test DEVIATIONS); claws (natural 2d6, Dex) + `on_hit save_condition` (Con DC 10, paralyzed 10 rounds, repeat at end of turn) gated by `not_creature_type undead` (as ghoul, no elf clause); necrotic resistance, poison immunity, poisoned / charmed / exhaustion immunity, darkvision 60 | **partial**: Stench as trait `aura` {radius 5, start_turn, Con DC 10, poisoned until_start_of_next_turn, success_immunity_hours 24} (batch 3b, exact SRD). Turn Defiance is description only (no turn-undead effect in the pack) |
| Acolyte | 1/4 | Club (+2, 1d4 bludgeoning); `spellcasting` Wis, caster level 1, slots 1st x3, DC 12, +4: light, sacred flame, thaumaturgy, bless, cure wounds, sanctuary (light and thaumaturgy are `descriptive`, CONTENT_CONVENTIONS 28) | complete; Spellcasting trait text verbatim |
| Cult Fanatic | 2 | Dagger ×2 (melee +4, 1d4 + 2, or thrown 20/60); Dark Devotion as `save_advantage` vs charmed / frightened; `spellcasting` Wis, caster level 4, slots 1st x4 / 2nd x3, DC 11, +3: light, sacred flame, thaumaturgy, command, inflict wounds, shield of faith, hold person, spiritual weapon | complete. DEVIATIONS: HP 33 (6d8 + 6) as the SRD prints (Elf ruling); the a6212be mirror's 22 (6d8-5) is a mirror error (test HP_ERRATA) |
| Owlbear | 3 | Multiattack beak + claws | Keen Sight and Smell descriptive |
| Awakened Tree | 2 | vulnerability fire, resistances bludgeoning / piercing, slam reach 10 | False Appearance descriptive |
| Hill Giant | 5 | Multiattack greatclub x2; greatclub as the item with `weapon_dice_multiplier` 3 (Huge), reach 10 as printed; rock (ranged natural 3d10, 60/240) | |
| Stone Giant | 7 | saving throws Dex +5 / Con +8 / Wis +4; Multiattack greatclub x2 (item, `weapon_dice_multiplier` 3, reach 15 as printed); rock with `on_hit save_condition` (Str DC 17, prone untimed) | Stone Camouflage descriptive; Rock Catching (reaction, DC 10 Dex to catch a hurled rock, no bludgeoning damage) descriptive: no reaction window for incoming missiles |
| Giant Ape | 7 | Multiattack fist x2 (reach 10); rock (ranged 7d6, 50/100); climb 40 | |
| Ettercap | 2 | Multiattack bite + claws; bite (piercing + poison) with `on_hit save_condition` (Con DC 11, poisoned 10 rounds, repeat at end of turn); Web as `attack_effect` (ranged +4, 30/60, restrained, `max_size large`, Str DC 11 escape, webbing AC 10 / hp 5 / vulnerable fire / immune bludgeoning, poison, and psychic, recharge 5-6) | Spider Climb / Web Sense / Web Walker descriptive |
| Merfolk | 1/8 | spear: one-handed, two-handed, thrown (guard convention); walk 10, swim 40 | Amphibious descriptive |
| Plesiosaurus | 2 | bite reach 10; walk 20, swim 40 | Hold Breath descriptive (as lizardfolk) |
| Reef Shark | 1/2 | Pack Tactics (`pack_tactics`); bite; swim-only speed `{walk: 0, swim: 40}`; blindsight 30 | Water Breathing descriptive |
| Killer Whale | 3 | bite 5d6 + 4; swim-only speed `{walk: 0, swim: 60}`; blindsight 120 | Echolocation / Hold Breath / Keen Hearing descriptive |

The goblin keeps `$schema: monster@3`, its id (`srd51.monster.goblin`, stable id derived from it) and bytes. It is referenced by C++ tests that assert its `@3` form (`content_migration_tests`, `pack_set_tests`, compat goblin tests) and by the fl1 compat corpus, and a schema change could shift digests and goldens; migrating it buys nothing until a `@4`-only feature is needed.

Not set: `presentation` (the goblin has none; sprite assets for these monsters do not exist yet). `behavior.ai`: only the scout (above).

### What the sim plays today (main at `0645ace`)

Played: stats, attacks with every `damage[]` entry (#45), resistances / immunities / vulnerabilities / condition immunities (#45), `multiattack` options (#48/#59), `damage_mitigation` (#47/#58), `bonus_action_grant`, size footprint (#50/#55).
Carried and validated but **not applied** (the registry reserves the `@4` kinds; the archetype lists them in `unsupported_features` and ignores them): `on_event` (Undead Fortitude, Rampage, Sneak Attack), `on_hit[]` riders (snake poison), `save_effect` actions with `usage` (Acid Breath). Until Engine Core lands the row 6 compile / row 9-10 trigger hooks these monsters play without those effects. No content change is needed when the hooks land.

## Deferred (21)

The last pre-v0.1 deferred monster, `srd51.monster.swarm_of_rats` (T&C-G7, T&C-G14, T&C-G15), was authored after sim #172 (swarm tag, `half_hp_damage`).

| monster | CR | blocking mechanic |
|---|---|---|
| Cat (`srd51.monster.cat`) | 0 | Claws deal a flat 1 slashing damage: `damage[].dice` / `natural.dice` accept dice only, no flat-damage form for an attack |
| Crab (`srd51.monster.crab`) | 0 | Claw deals a flat 1 bludgeoning damage (as cat) |
| Poisonous Snake (`srd51.monster.poisonous_snake`) | 1/8 | Bite deals a flat 1 piercing damage (as cat); the poison rider itself is expressible |
| Quipper (`srd51.monster.quipper`) | 0 | Blood Frenzy (advantage on melee attacks vs a creature missing hit points): no construct; also swim-only (schema requires `speed.walk`, SRD lists none); bite flat 1 damage |
| Giant Crab (`srd51.monster.giant_crab`) | 1/8 | Claw grapple with two claws each grappling one target (per-limb grapple capacity) |
| Constrictor Snake (`srd51.monster.constrictor_snake`) | 1/4 | Constrict: grappled and restrained until the grapple ends, can't constrict another target |
| Crocodile (`srd51.monster.crocodile`) | 1/2 | Bite: grappled and restrained until the grapple ends, can't bite another target; Hold Breath |
| Giant Frog (`srd51.monster.giant_frog`) | 1/4 | Bite grapple + restrained, can't bite another target; Swallow (bite against its grappled target, Small or smaller) |
| Elk (`srd51.monster.elk`) | 1/4 | Charge (straight-line approach); Hooves targets only a prone creature (no target-prone gate) |
| Giant Goat (`srd51.monster.giant_goat`) | 1/2 | Charge (straight-line approach, prone DC 13); Sure-Footed (advantage on Str/Dex saves vs being knocked prone) |
| Giant Sea Horse (`srd51.monster.giant_sea_horse`) | 1/2 | Charge (straight-line approach); walk 0 / swim-only |
| Warhorse (`srd51.monster.warhorse`) | 1/2 | Trampling Charge (prone save, then a bonus-action hooves attack vs the prone target) |
| Lion (`srd51.monster.lion`) | 1 | Pounce (prone save, then a bonus-action bite vs the prone target); Running Leap |
| Tiger (`srd51.monster.tiger`) | 1 | Pounce (as lion) |
| Blink Dog (`srd51.monster.blink_dog`) | 1/4 | Waiting on an optional `passive_perception` override (Elf has asked Beholder for one defaulting to 10 + skill): the SRD prints Perception +3 with passive Perception 10, and the validator requires 10 + skill. Everything else is expressible (Bite, Teleport action with `with_attack bite`; bite-then-teleport deferred, AI does not pick Teleport). **Re-author note:** when the override lands, re-author with the printed Perception +3 and passive Perception 10 (no 13). |
| Duergar (`srd51.monster.duergar`) | 1 | Enlarge and Invisibility are innate (recharge / short-rest) self-effects with no monster action shape (enlarge: size + damage dice doubling; invisibility until it attacks or concentration ends). Enlarge is part of its core damage (war pick / javelin "or 2d8 + 2 while enlarged"). Deferred per triage rule (innate enlarge / invisibility). |
| Mimic (`srd51.monster.mimic`) | 2 | Shapechanger (polymorph into an object, statistics unchanged) plus Adhesive (sticks to and grapples on contact) and Grappler: shapechange has no shape; deferred per triage rule. |
| Doppelganger (`srd51.monster.doppelganger`) | 3 | Shapechanger (polymorph into a seen Small / Medium humanoid) and Read Thoughts (magical mind reading); Surprise Attack / Ambusher depend on surprise. Deferred per triage rule (shapechange). |
| Green Hag (`srd51.monster.green_hag`) | 3 | Innate Spellcasting: `dancing_lights` and `minor_illusion` are not in `srd51.core/spells` (vicious mockery is); Illusory Appearance (magical disguise) and Invisible Passage (magical invisibility) have no shape. Deferred per triage rule. |
| Rust Monster (`srd51.monster.rust_monster`) | 1/2 | Rust Metal (weapons that hit it corrode: cumulative -1 damage, destroyed at -5; ammunition destroyed) and Antennae (corrode / destroy a ferrous object, DC 11 Dex save; armor AC -1 per touch) are its signature mechanics: no item-degradation or object-destruction effect exists. Iron Scent descriptive. |
| Mummy (`srd51.monster.mummy`) | 3 | Dreadful Glare is part of the Multiattack and paralyzes on a failure by 5 or more: action `save_effect` has no fail-by-margin (`escalate_margin` exists only on gaze traits), and its 24-hour immunity is deferred. Mummy rot's curse (`curse {no_regain_hp}`) is now expressible, but its 3d6 max-HP loss every 24 hours (dust at 0) is a 24-hour effect and stays deferred. The core action (the Multiattack) can't be carried faithfully, so the mummy stays deferred. |

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
