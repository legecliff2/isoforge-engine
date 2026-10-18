# SRD 5.1 combat coverage audit (levels 1-5 party vs SRD monsters)

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Goal: a complete level 1-5 party (Fighter, Cleric, Wizard in full; the other nine classes as a lower tier) fighting SRD monsters, deterministically, from content data. This audit lists every combat-relevant SRD element and says what the code does **today** (main at `6012567`, after #23 spellcasting, #34 follow-ups, #41 PackSet). It is not a plan of record; the last section is a proposed priority list.

## 0. How to read this

Status (first word of the status cell):

- **DATA** = expressible as data with what exists now: a `spell@1` effect (`spell_attack`, `save`, `heal`, `auto_hit`, `ac_bonus`, `counter`), a `common@1` feature effect kind (`damage_bonus`, `bonus_action_grant`, `self_heal`), a stat-block field, a rules5e `ActionKind`, or a rules5e hook.
- **NEW** = needs a new mechanic. The note gives the mechanic id from section 11. "(wiring)" means rules5e already models it but nothing carries it from content into a running `sim::Combat`.
- **OOS** = out of scope for combat simulation, with the reason.

Things that apply to many rows (so they are not repeated):

1. **G1, content-to-sim wiring gap.** `sim::ContentLibrary::add` (engine/core/sim/src/content.cpp) currently reads only: abilities, AC value, average HP, walk speed, proficiency bonus, saving-throw proficiencies, PC vs monster, `melee_weapon_attack`/`ranged_weapon_attack` actions (first `damage` entry only, `finesse` always false), and feature effects. It does **not** read: resistances / immunities / vulnerabilities, condition immunities, creature type, skills, `attacks_per_action`, spellcasting (slots, caster level, known spells), or other action kinds. `sim::Combat` has no `CastSpell` plumbing: it never sets `hooks.spells`, `spell_target_ok`, `targets_within`, `can_see`, or `can_provide_components`. So every spell row marked DATA is DATA in `spell@1` + rules5e (covered by `rules5e_tests`), and still needs G1 before a content-authored caster can cast in a sim combat.
2. **Single effect per spell (S6, still deferred).** The `spell@1` schema declares `effects` as an array, but `check_spell` (tools/validate) and `rules5e_content/spell_loader.cpp` reject anything other than exactly one effect. A spell is therefore one of: spell attack, save (damage and/or one condition), heal, auto-hit darts, AC bonus reaction, counter reaction. "Damage plus a rider" spells need B1/A3/B2.
3. **Area spells.** `spell@1` stores `area` (cone/cube/cylinder/line/sphere + size) and rules5e resolves a caller-supplied `ActionRequest::targets` list with a save each. Nothing computes who is inside a template on the grid, so area spells are NEW (C3) even though their dice/save/scaling are data.
4. **Spell durations.** Concentration, repeat saves and `UntilStartOfCasterTurn` (Shield) end effects. Timed `rounds/minutes/hours` durations are stored but never expire on their own (no round counter on `ActiveSpellEffect`). Fine for concentration spells (which end with concentration), a gap for non-concentration timed buffs (B2).
5. **Authored numbers.** Characters are authored per level as `character@3` stat blocks (not generated from a level-up builder). Static numbers such as AC from armor, Unarmored Defense, Unarmored Movement, +1 magic weapon bonuses, ability modifiers and proficiency are baked by the author; that is DATA. Level-up / ASI / multiclassing / equipment management is OOS (Tools & Content authoring).
6. SRD 5.1 has one subclass per class (Fighter: Champion, Cleric: Life, Wizard: Evocation, Druid: Land, Barbarian: Berserker, Bard: Lore, Monk: Open Hand, Paladin: Devotion, Ranger: Hunter, Rogue: Thief, Sorcerer: Draconic, Warlock: Fiend). Only those are listed. The only feat in the SRD (Grappler) is not audited; it depends on C1.

## 1. Core rules engine (what is implemented in rules5e today)

| Element | Status | Notes |
|---|---|---|
| Initiative (d20 + Dex, ties) | DATA | `roll_initiative`, `begin_encounter`. |
| Turn structure: action, bonus action, movement, reaction budgets | DATA | `TurnBudget`; `bonus_action_grants` bitmask per combatant. |
| Attack action, crit (nat 20 doubles dice), nat 1 miss | DATA | `ActionKind::Attack`, `resolve_attack`. |
| Extra Attack (several attacks per action) | DATA | `Combatant::attacks_per_action`; not loaded from content (see A1, G1). |
| Dash, Disengage, Dodge, Help, Hide, Ready | DATA | Matching `ActionKind`s; Hide rolls Stealth, sim compares to passive Perception. |
| Search, Use an Object, Influence | OOS | Exploration/social actions; no combat effect to resolve. |
| Opportunity attacks | DATA | `ReactionTrigger::LeavingReach` window, `ActionKind::OpportunityAttack`; sim supplies `in_range`. |
| Reaction windows (BeingHit, SpellCast, ReadiedTrigger) | DATA | Ask / Never / Auto policy per trigger, async `NeedsDecision`, deterministic replay. |
| Ready action with triggers, refreshed when fired | DATA | `ActionKind::Ready`, `hooks.ready_trigger`, `hooks.refresh_ready`. |
| Death saves, stabilize, Spare the Dying, massive damage, instant death | DATA | `ActionKind::Stabilize` / `SpareTheDying`, `uses_death_saves`. |
| Monsters die at 0 HP | DATA | `uses_death_saves=false` for non-PCs. |
| Healing, temp HP (absorbs first) | DATA | `apply_healing`, `apply_damage`. Temp HP can be set on a combatant but no spell/feature grants it (B2). |
| Ability checks, saving throws, proficiency, advantage/disadvantage cancel | DATA | `ActionKind::AbilityCheck`, `saves.hpp`, `combine_modes`. |
| Standing up from prone (half movement) | DATA | `ActionKind::StandUp`. |
| Movement on a grid, difficult terrain (2x), LOS | DATA | sim `Grid`, `step_cost_ft`, supercover LOS. |
| Crawling while prone (extra movement cost) | NEW | Not modelled in rules or sim (C1). |
| Opportunity-attack exceptions (Disengage, teleport, forced movement) | DATA | Disengage implemented; teleport/forced movement do not exist yet (C2). |
| Surprise (skip first turn, no reactions) | NEW | No surprise state in `begin_encounter` (C10). |
| Short / long rest: spell slots, Pact slots, feature uses | DATA | `recover_slots_short_rest/long_rest`, `recover_features`. |
| Rests: Hit Dice spending, HP regained | NEW | No Hit Dice on `Combatant` (C10). |
| Falling damage, suffocation, starvation, extreme heat | OOS | Hazard/exploration; not part of a fight. |
| Mounted combat | OOS | No mounts in the SRD party we target. |
| Underwater combat | OOS | Environment variant; defer. |
| Cover (+2 / +5 AC, total cover) | NEW | `AttackContext::cover_ac_bonus` exists and is honored, but sim never computes cover (C7). |
| Lighting, darkness, obscurement, darkvision/blindsight/truesight | NEW | Not modelled; sim has LOS only (C7). |
| Flanking | OOS | Optional rule, not SRD core. |
| Two-weapon fighting | NEW | B5. |
| Grapple and shove (contested Athletics/Acrobatics) | NEW | C1. |
| Flying, swimming, climbing, burrowing movement | NEW | One `speed_ft`; sim reads `speed.walk` only (C6). |
| Creature size and space (large creatures occupy 2x2) | NEW | Grid is one tile per creature (C6). |
| Dispel / end effects on a creature | NEW | C9. |

## 2. Weapons (properties)

| Element | Status | Notes |
|---|---|---|
| Simple/martial melee weapon, damage die + type | DATA | `melee_weapon_attack` action, `damage[0]`, `to_hit` baked (author applies ability mod + proficiency + magic bonus). Only the first damage entry is loaded (A2). |
| Simple/martial ranged weapon, normal/long range | DATA | `ranged_weapon_attack` with `range:[normal,long]`; `Attack::range_ft/long_range_ft`; disadvantage beyond normal range, none past long. |
| Ranged attack with hostile within 5 ft = disadvantage | DATA | `ActionRequest::ranged_hostile_within_5ft` (sim computes). |
| Finesse | DATA | Author bakes the better of Str/Dex into `to_hit` and damage; property string kept in `properties`. `Attack::finesse` is not used by sim. |
| Reach (glaive, halberd, whip, pike, lance) | DATA | `reach` on the action; sim reach tiles. |
| Thrown | DATA | A second `ranged_weapon_attack` action with `range`. Item is not removed from inventory (inventory is OOS). |
| Versatile | DATA | Two actions (1-handed die, 2-handed die). |
| Two-handed | DATA | `properties:["two_handed"]`; used by the Dueling predicate `one_handed_melee_no_other_weapon`. |
| Ammunition | DATA | Ranged attack; ammunition count is OOS (no inventory in sim). |
| Light | NEW | Only matters for two-weapon fighting (B5). |
| Loading | NEW | One shot per action even with Extra Attack; needs per-attack limit (A1). |
| Heavy (Small creatures disadvantage) | NEW | Needs size on the combatant (C6). Low priority. |
| Special: net (restrains), lance (disadvantage within 5 ft) | NEW | Net = restrain rider (A3); lance needs adjacency rule. Low priority. |
| Silvered / adamantine weapons, magic weapons vs nonmagical resistance | NEW | A4. Magic bonuses themselves are baked: DATA. |
| Improvised weapons, unarmed strike (1 + Str) | DATA | Authored as an action. |
| Weapon proficiency | DATA | Baked into `to_hit`. |

## 3. Armor and shields

| Element | Status | Notes |
|---|---|---|
| Armor AC (light/medium/heavy, shield, Dex cap) | DATA | Baked into `armor_class.value`. No equipment-derived AC in sim. |
| Equipment management, donning/doffing time | OOS | Inventory is outside combat. |
| Strength requirement (speed -10) | OOS | Author bakes speed. |
| Stealth disadvantage (scale mail, half plate, heavy armor) | NEW | `Hide` has no armor input (B7/small). |
| Spell: Mage Armor, Shield of Faith, Barkskin (AC changes) | NEW | B2. |
| Shield spell (+5 AC reaction) | DATA | `ac_bonus` effect (shipped `srd51.spell.shield`). |

## 4. Conditions (SRD Appendix A)

| Condition | Status | Notes |
|---|---|---|
| Blinded | DATA | Attack disadvantage on self, advantage against. Sight-based checks are not modelled. |
| Charmed | NEW | Needs a source creature (cannot attack charmer, social advantage). `Combatant::conditions` is a bitmask with no source (C1). |
| Deafened | DATA | No combat effect beyond the flag. |
| Frightened | NEW | Disadvantage on attacks/checks is implemented. "Cannot willingly move closer to the source" needs source + LOS (C1). |
| Grappled | NEW | Speed 0 is implemented. Grapple action, source, escape check, drag are not (C1). |
| Incapacitated | DATA | No actions/reactions. |
| Invisible | DATA | Attack advantage / disadvantage against. No "heard/ noticed" rules. |
| Paralyzed | DATA | Incapacitated, auto-fail Str/Dex saves, advantage against, crit within 5 ft. |
| Petrified | DATA | Incapacitated, speed 0, resistance to all damage, auto-fail Str/Dex. Aging/weight not modelled. |
| Poisoned | DATA | Attack and check disadvantage. |
| Prone | DATA | Attack disadvantage on self; advantage within 5 ft against, disadvantage beyond. `StandUp` action. |
| Restrained | DATA | Speed 0, attack disadvantage, advantage against, Dex save disadvantage. |
| Stunned | DATA | Incapacitated, auto-fail Str/Dex, advantage against. |
| Unconscious | DATA | Prone-like effects, crit within 5 ft, death saves. |
| Exhaustion levels 1-6 | DATA | `exhaustion_level`: check disadvantage, speed half, attack/save disadvantage, HP max half, speed 0, death. |
| Condition immunities on a stat block | NEW (wiring) | `condition_immunities` is in `common@1 stat_block`; `Combatant` has no mask and sim ignores it (G1). |
| Conditions with a duration or end trigger (end of next turn, until damaged) | NEW | Only spell conditions (concentration/repeat save) have lifetimes (B2). |
| Apply a condition from a monster action (ghoul claws, wolf trip, spider poison) | NEW | Monster actions have no rider (A3). |

## 5. Damage types and damage handling

| Element | Status | Notes |
|---|---|---|
| 13 damage types | DATA | `DamageType`; `common@1 damage_type`. |
| Resistance, vulnerability, immunity | NEW (wiring) | Masks on `Combatant`; `apply_damage` order is correct. Not loaded from `damage_resistances` etc. (G1). |
| "Nonmagical" / "nonmagical, non-silvered / adamantine" qualifiers | NEW | Needs a magic/silvered flag on attacks and qualified masks (A4). Affects many monsters (werewolf, specter, ghost, gargoyle, golems). |
| Multiple damage components in one attack (1d8 slashing + 2d6 fire) | NEW | `Attack` has one `DiceExpr`+type; sim loads `damage[0]` only (A2). |
| Critical hits double dice only | DATA | `roll_dice(..., crit)`. |
| Temp HP | DATA | See section 1. |
| Damage when hit by a save-based effect: full / half | DATA | `Save` effect with `on_success: half`. |
| Evasion (no damage on success, half on fail) | NEW | Rogue 7 / Monk 7, beyond level 5; listed in B6 for completeness. |
| Maximum HP reduction (wraith, vampire, specter life drain) | NEW | B7. |

## 6. Class features, levels 1-5

Existing feature machinery: `damage_bonus` (static), `bonus_action_grant` (static), `self_heal` (active `UseFeature`, with uses and short/long rest recharge). Spells use `Combatant` slots/pact slots/caster level (rules) and G1 for wiring.

### 6.1 Fighter (full)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit Die d10, proficiencies, saves (Str/Con), armor/weapons | 1 | DATA | `character@3 classes[].hit_die`, `saving_throws`, baked AC/to_hit. |
| Fighting Style: Dueling | 1 | DATA | `damage_bonus {amount:2, condition: one_handed_melee_no_other_weapon}` (shipped, `fighter_1`). |
| Fighting Style: Archery (+2 ranged attack rolls) | 1 | NEW | No to-hit bonus effect kind (B4: `attack_bonus`). |
| Fighting Style: Defense (+1 AC in armor) | 1 | NEW | No AC bonus effect kind; could be baked into AC but then not de-duplicated with armor (B2/B4 `ac_bonus` static kind). |
| Fighting Style: Great Weapon Fighting (reroll 1-2 on damage dice) | 1 | NEW | Damage reroll hook (B6). |
| Fighting Style: Protection (reaction, impose disadvantage on attack vs adjacent ally) | 1 | NEW | Reaction feature (`cost: reaction` is reserved) (B6). |
| Fighting Style: Two-Weapon Fighting (add mod to off-hand) | 1 | NEW | B5. |
| Second Wind | 1 | DATA | `self_heal {dice 1d10, plus fighter_level, uses 1, recharge short_rest, cost bonus_action}` (shipped). |
| Action Surge | 2 | NEW | Grants an extra action once per rest: new active kind that adds to `TurnBudget` (B4). |
| Martial Archetype: Champion, Improved Critical (crit on 19-20) | 3 | NEW | Crit range per attack/combatant (B4 `crit_range`). |
| Ability Score Improvement | 4 | DATA | Authored stats. |
| Extra Attack | 5 | NEW (wiring) | `attacks_per_action=2` exists; no effect kind or loader sets it (A1). |

### 6.2 Cleric (full; Life domain)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit die d8, proficiencies, saves (Wis/Cha) | 1 | DATA | Baked. |
| Spellcasting: slots, DC, attack bonus, prepared spells, cantrips | 1 | DATA (G1) | `spell_slots`, `spell_save_dc`, `spell_attack_bonus`, `spells_known`; cantrip scaling by `caster_level`. |
| Ritual casting | 1 | OOS | Rituals take 10 extra minutes; not in an encounter. |
| Divine Domain: Life, bonus proficiency (heavy armor) | 1 | DATA | Baked AC. |
| Life domain spells always prepared (Bless, Cure Wounds, Lesser Restoration, Spiritual Weapon, Beacon of Hope, Revivify) | 1-5 | DATA | Authored `spells_known`; availability of each spell per section 8. |
| Disciple of Life (healing spells heal an extra 2 + spell level) | 1 | NEW | Heal-spell bonus rider on the caster (B3). |
| Channel Divinity (1/rest at L2, 2/rest at L6) | 2 | NEW | Pool/uses of an active feature with non-heal effects (B4). |
| Channel Divinity: Turn Undead | 2 | NEW | WIS save or turned (frightened-like, must flee, cannot take reactions) for 1 minute; area 30 ft (B4, C1, C3). |
| Channel Divinity: Preserve Life | 2 | NEW | Distribute a 5 x level HP pool among creatures within 30 ft, up to half max HP each (B4, C3, C9). |
| Destroy Undead (CR 1/2 at L5) | 5 | NEW | Turn Undead outcome threshold (B4). |
| Ability Score Improvement | 4 | DATA | Authored. |

### 6.3 Wizard (full; School of Evocation)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit die d6, proficiencies, saves (Int/Wis) | 1 | DATA | Baked. |
| Spellcasting, spellbook (known list), prepared spells, cantrips | 1 | DATA (G1) | `spells_known`; spellbook copying/prep is OOS (downtime). |
| Arcane Recovery (once per day, short rest, recover slots <= half wizard level) | 1 | NEW | Rest-time feature with a choice of slot levels; `recover_slots_short_rest` only restores Pact slots (C10). |
| Arcane Tradition: Evocation; Evocation Savant | 2 | OOS | Downtime spell copying cost. |
| Sculpt Spells (allies auto-succeed and take no damage from your evocation area) | 2 | NEW | Needs area spells (C3) plus a per-cast ally choice (C8). |
| Ability Score Improvement | 4 | DATA | Authored. |
| Cantrip damage scaling at 5 | 5 | DATA | `scaling.cantrip.levels [5,11,17]` (shipped on Fire Bolt). |
| 3rd-level slots at 5 | 5 | DATA | `spell_slots` table baked on the character. |

### 6.4 Lower tier

| Class / Feature | Lvl | Status | Notes |
|---|---|---|---|
| **Barbarian** hit die d12, proficiencies | 1 | DATA | Baked. |
| Rage (+damage, resist B/P/S, adv on Str checks/saves; bonus action; uses/long rest) | 1 | NEW | Timed self-effect with resistances and damage bonus (B2, B4). |
| Unarmored Defense (10 + Dex + Con) | 1 | DATA | Baked AC. |
| Reckless Attack | 2 | NEW | Per-attack choice: advantage for you and against you until next turn (B6/B2). |
| Danger Sense (adv on Dex saves vs visible effects) | 2 | NEW | Conditional save advantage (B7 style trait). |
| Primal Path: Berserker, Frenzy (extra attack as bonus action, exhaustion after) | 3 | NEW | B5, conditions. |
| Extra Attack | 5 | NEW (wiring) | A1. |
| Fast Movement (+10 ft) | 5 | DATA | Baked speed (`speed.walk`). Condition "no heavy armor" is the author's. |
| **Bard** spellcasting (full caster) | 1 | DATA (G1) | |
| Bardic Inspiration (bonus action, d6/d8; ally adds to a roll within 10 min) | 1 | NEW | Roll-modifier token with a pool of uses (B4, B6). |
| Jack of All Trades (+half proficiency to checks) | 2 | NEW | Check modifier (B6/small). |
| Song of Rest | 2 | OOS | Out of combat healing. |
| Expertise | 3 | DATA | Authored skills. |
| College of Lore: Cutting Words (reaction, subtract inspiration die from enemy roll) | 3 | NEW | Reaction roll modifier (B6). |
| Font of Inspiration (short rest recharge) | 5 | NEW | Recharge change on the same pool (B4). |
| **Druid** spellcasting (full caster) | 1 | DATA (G1) | |
| Wild Shape (CR 1/4 at L2, no fly/swim; CR 1/2 at L4; CR 1 at L8) | 2 | NEW | Replace stat block temporarily; HP overflow carries over (C5). |
| Circle of the Land: Natural Recovery | 2 | NEW | Same family as Arcane Recovery (C10). |
| Druidic, Land bonus cantrip | 1-2 | OOS | Language / one extra cantrip (authored). |
| **Monk** Unarmored Defense (10 + Dex + Wis) | 1 | DATA | Baked AC. |
| Martial Arts (d4 die, Dex for unarmed, unarmed bonus action attack) | 1 | NEW | Bonus-action attack (B5). Die is baked. |
| Ki (pool = monk level, short rest) | 2 | NEW | Pool (B4). |
| Flurry of Blows, Patient Defense, Step of the Wind | 2 | NEW | Ki-cost bonus actions (B4, B5). Patient Defense and Step of the Wind could use `bonus_action_grant` without a ki cost only. |
| Unarmored Movement (+10 ft) | 2 | DATA | Baked speed. |
| Deflect Missiles (reaction: reduce ranged damage by 1d10 + Dex + level) | 3 | NEW | Reaction feature (B6). |
| Way of the Open Hand: Open Hand Technique (rider on Flurry: prone / push / no reactions) | 3 | NEW | Rider on hit (A3, C2). |
| Slow Fall | 4 | OOS | Falling damage not simulated. |
| Extra Attack | 5 | NEW (wiring) | A1. |
| Stunning Strike (ki; CON save or stunned until next turn) | 5 | NEW | Rider (A3) + ki pool (B4). |
| Ki-Empowered Strikes (magic for resistance, L6) | 6 | NEW | A4 (beyond L5, listed for completeness). |
| **Paladin** Divine Sense | 1 | OOS | Detection. |
| Lay on Hands (pool = 5 x level; also cures disease/poison for 5 points) | 1 | NEW | Pool and variable heal amount (B4). `self_heal` heals only the user. |
| Divine Smite (expend slot after a hit; 2d8 + 1d8 per slot above 1st, +1d8 vs undead/fiend) | 2 | NEW | Post-hit rider that spends a spell slot and doubles on crit (B3). |
| Fighting Style, Spellcasting (half caster) | 2 | NEW / DATA | Styles as in 6.1; slots as a half caster are DATA (G1). |
| Divine Health | 3 | OOS | Disease immunity. |
| Oath of Devotion: Channel Divinity Sacred Weapon (+Cha to attacks, light) | 3 | NEW | Timed buff (B2, B4). |
| Oath of Devotion: Turn the Unholy | 3 | NEW | B4/C1. |
| Oath spells (Protection from Evil and Good, Sanctuary, ...) | 3 | NEW | See section 8. |
| Extra Attack | 5 | NEW (wiring) | A1. |
| **Ranger** Favored Enemy / Natural Explorer | 1 | OOS | Tracking, languages, travel. |
| Fighting Style | 2 | NEW / DATA | As 6.1 (Archery, Defense, Dueling DATA). |
| Spellcasting (half caster) | 2 | DATA (G1) | |
| Ranger Archetype: Hunter, Colossus Slayer (+1d8 once per turn vs a creature below max HP) | 3 | NEW | Once-per-turn rider (B3). |
| Hunter: Giant Killer, Horde Breaker (alternatives) | 3 | NEW | Reaction/extra attack rider (B6, B3). |
| Primeval Awareness | 3 | OOS | Detection. |
| Extra Attack | 5 | NEW (wiring) | A1. |
| **Rogue** Expertise | 1 | DATA | Authored skills. |
| Sneak Attack (xd6 once per turn with advantage or an ally within 5 ft of the target; finesse/ranged only) | 1 | NEW | Conditional once-per-turn damage with sim-supplied "ally adjacent to target" fact (B3). |
| Thieves' Cant | 1 | OOS | Language. |
| Cunning Action (bonus Dash / Disengage / Hide) | 2 | DATA | `bonus_action_grant {actions:[dash,disengage,hide]}`. |
| Roguish Archetype: Thief, Fast Hands (bonus-action Use an Object) | 3 | OOS | Use an Object is not simulated. |
| Thief: Second-Story Work | 3 | OOS | Climb/jump. |
| Uncanny Dodge (reaction: halve one attack's damage) | 5 | NEW | Reaction feature (B6). |
| **Sorcerer** spellcasting (full caster, spells known) | 1 | DATA (G1) | |
| Draconic Bloodline: Draconic Resilience (HP +1/level, AC 13 + Dex unarmored) | 1 | DATA | Baked HP and AC. |
| Draconic Bloodline: Dragon Ancestor | 1 | OOS | Language and Cha checks. |
| Font of Magic / Sorcery Points (convert slots and points) | 2 | NEW | Pool + conversions (B4). |
| Metamagic (Careful, Distant, Empowered, Extended, Heightened, Quickened, Subtle, Twinned) | 3 | NEW | Per-cast modifiers on `ActionRequest` (C8). |
| **Warlock** Pact Magic (slots recharge on short rest, all same level) | 1 | DATA (G1) | `pact_slot_level`, `pact_slots`, short-rest recovery implemented. |
| Otherworldly Patron: The Fiend, Dark One's Blessing (temp HP on kill) | 1 | NEW | On-kill trigger granting temp HP (B7/B2). |
| Eldritch Invocations: Agonizing Blast (+Cha per beam) | 2 | NEW | Per-beam damage bonus on a spell (B3). |
| Invocations: Armor of Shadows, Fiendish Vigor, Repelling Blast (push 10 ft), Devil's Sight | 2 | NEW | Mage Armor / False Life at will (B2), forced movement (C2), darkvision (C7). |
| Invocations: Eldritch Sight, Beast Speech, Mask of Many Faces, etc. | 2 | OOS | Utility. |
| Pact Boon: Tome (3 cantrips) | 3 | DATA | `spells_known`. |
| Pact Boon: Blade (pact weapon, proficiency, magic) | 3 | NEW | Summoned/bonded weapon (C5, A4). |
| Pact Boon: Chain (find familiar variants) | 3 | NEW | Familiar (C5). |

### 6.5 Races (party members' combat traits)

| Trait | Status | Notes |
|---|---|---|
| Darkvision | NEW | C7. |
| Dwarven Resilience (poison resistance; advantage vs poison) | NEW | Resistance DATA after wiring G1; save advantage vs source type is B7 style. |
| Fey Ancestry (advantage vs charm) / Brave (halfling, vs frightened) | NEW | Conditional save advantage (B7). |
| Halfling Lucky (reroll natural 1) | NEW | B6. |
| Relentless Endurance (drop to 1 HP once per day) / Savage Attacks (extra crit die) | NEW | B7 / B6. |
| Dragonborn breath weapon and resistance | NEW | Area save + limited use (C3, B7). |
| Tiefling Hellish Resistance / Infernal Legacy (Thaumaturgy, Hellish Rebuke) | NEW | Resistance after G1; Hellish Rebuke trigger (B6). |
| Gnome Cunning (advantage on Int/Wis/Cha saves vs magic) | NEW | B7. |
| Speed, size | DATA | Baked (`speed.walk`); size field not loaded (C6). |

## 7. Core spell mechanics

| Element | Status | Notes |
|---|---|---|
| Spell slots by level, Pact slots, recovery | DATA | `Combatant::spell_slots`, `spend_slot`, rest recovery. |
| Save DC = 8 + prof + mod; spell attack bonus | DATA | `spell_save_dc`, `spell_attack_bonus`. |
| Components V/S/M, material cost/consumed | DATA | Flags in `SpellDef`; `hooks.can_provide_components` (unwired in sim, G1). |
| Concentration (CON save on damage DC max(10, half), ends on incapacitation / new concentration) | DATA | `damage_creature`, `concentration_spell`. |
| Cantrip tiers (5/11/17), upcast scaling dice/darts/targets/beams | DATA | `scaling.cantrip`, `scaling.upcast`. |
| Reaction spells with triggers (being hit, targeted by named spell, spell cast with sight) | DATA | `casting_time.trigger` {being_hit, spell_cast, or_targeted_by, requires_sight}. |
| Spell with one effect | DATA | Single `effects[0]`. |
| Spell with several effects (damage + rider) | NEW | B1 (S6 deferred). |
| Targeting: single, N targets, creature_type filter, max spread | DATA | `targets {count, creature_type, max_spread_ft}`; `hooks.spell_target_ok`, `targets_within` (unwired in sim). |
| Area of effect templates | NEW | C3. |
| Casting time minute/hour (non-combat) | OOS | Stored; rejected during encounters. |
| Duration expiry for rounds/minutes/hours | NEW | C9. |
| Counterspell (auto up to slot level, else ability check DC 10 + level) | DATA | `counter` effect (shipped). |
| Ritual casting | OOS | See above. |
| Spell cast request/validation, slot spend, events, replay | DATA | `ActionKind::CastSpell`, events 30-38, golden replay. |

## 8. Spells, cantrips through 3rd level (combat-relevant)

Legend for "DATA" rows: kind used in `spell@1`. "Shipped" = already in `srd51.core` (6 spells: Fire Bolt, Magic Missile, Cure Wounds, Shield, Hold Person, Counterspell). Everything else is authoring work (Tools & Content) unless marked NEW.

### 8.1 Cantrips

| Spell | Status | Notes |
|---|---|---|
| Acid Splash | DATA | `save` DEX, 1d6 acid, `on_success none`, `targets {count 2, max_spread_ft 5}`. |
| Chill Touch | NEW | Attack damage is DATA; "can't regain HP, undead disadvantage" rider needs B1/B2. |
| Dancing Lights, Light, Mage Hand, Mending, Message, Minor Illusion, Prestidigitation, Thaumaturgy, Druidcraft, Guidance, Resistance | OOS | Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW B6 if wanted in combat. |
| Eldritch Blast | DATA | `spell_attack` with `scaling.cantrip {adds: beams}`; range 120. Agonizing Blast: B3. |
| Fire Bolt | DATA | Shipped. Ignites objects: OOS. |
| Poison Spray | DATA | `save` CON, 1d12 poison, range 10, `on_success none`. |
| Produce Flame | DATA | Thrown `spell_attack` 1d8 fire (the light effect is OOS). |
| Ray of Frost | NEW | Damage DATA; speed -10 until next turn is B2. |
| Sacred Flame | DATA | `save` DEX, 1d8 radiant, none on success. "Target gains no benefit from cover" is C7. |
| Shillelagh | NEW | Weapon-damage buff (B2). |
| Shocking Grasp | NEW | Melee spell attack DATA; advantage vs metal armor and "no reactions" rider B1/B2. |
| Spare the Dying | DATA | `ActionKind::SpareTheDying` (hook `spare_the_dying`). |
| Thorn Whip | NEW | Pull 10 ft (C2). |
| True Strike | NEW | Advantage on next attack (B2). |
| Vicious Mockery | NEW | Damage DATA; disadvantage on next attack rider B1/B2. |

### 8.2 Level 1

| Spell | Status | Notes |
|---|---|---|
| Alarm, Animal Friendship, Comprehend Languages, Create or Destroy Water, Detect Evil and Good, Detect Magic, Detect Poison and Disease, Disguise Self, Feather Fall, Goodberry, Identify, Illusory Script, Jump, Longstrider, Purify Food and Drink, Silent Image, Speak with Animals, Unseen Servant | OOS | Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). |
| Armor of Agathys | NEW | Temp HP plus retaliation (B2, B7). |
| Arms of Hadar | NEW | Area (C3) + no reactions rider. |
| Bane | NEW | Penalty dice on attacks and saves (B2). |
| Bless | NEW | Bonus dice on attacks and saves (B2). |
| Burning Hands | NEW | Cone (C3); dice/save/upcast are DATA. |
| Charm Person | NEW | Charmed with source (C1). |
| Color Spray | NEW | HP-pool targeting (C9) + area. |
| Command | NEW | Forced action next turn (C1). |
| Cure Wounds | DATA | Shipped (`heal`, excludes undead/construct). |
| Dissonant Whispers | NEW | Damage + forced move reaction (C2). |
| Divine Favor | NEW | Weapon damage buff (B2/B3). |
| Entangle | NEW | Area zone + restrained (C3, C4). |
| Expeditious Retreat | NEW | Bonus-action Dash for duration (B2). |
| Find Familiar | NEW | Familiar (C5). |
| Fog Cloud | NEW | Obscurement zone (C7, C4). |
| Faerie Fire | NEW | Area + advantage against (C3, B2). |
| False Life | NEW | Temp HP effect kind (B2). |
| Grease | NEW | Zone + prone (C4). |
| Guiding Bolt | NEW | Attack damage DATA; advantage on next attack vs target rider (B1/B2). |
| Healing Word | DATA | `heal`, `casting_time bonus_action`, range 60, `upcast.dice`. |
| Hellish Rebuke | NEW | Needs a "damaged by creature" reaction trigger (B6). |
| Heroism | NEW | Temp HP each turn, frightened immunity (B2). |
| Hex | NEW | Bonus action rider (B3, B2). |
| Hideous Laughter | NEW | Prone + incapacitated (two conditions) and save-on-damage repeat (B1). |
| Hunter's Mark | NEW | B3. |
| Inflict Wounds | DATA | Melee `spell_attack`, 3d10 necrotic, `upcast.dice`. |
| Mage Armor | NEW | AC set (B2). |
| Magic Missile | DATA | Shipped (`auto_hit`). |
| Protection from Evil and Good | NEW | B2. |
| Ray of Sickness | NEW | Attack damage + CON save poisoned rider (B1/A3). |
| Sanctuary | NEW | Attackers must save to target (B2/B6). |
| Searing Smite, Thunderous Smite, Wrathful Smite, Branding Smite, Ensnaring Strike, Hail of Thorns | NEW | Weapon-attack riders (B3, A3, C3). |
| Shield | DATA | Shipped (`ac_bonus`). |
| Shield of Faith | NEW | Non-reaction AC bonus buff (B2). `ac_bonus` is reaction-only. |
| Sleep | NEW | HP-pool (C9). |
| Thunderwave | NEW | Cube (C3) + push (C2); dice/save DATA. |
| Witch Bolt | NEW | Sustained damage each turn (B2). |

### 8.3 Level 2

| Spell | Status | Notes |
|---|---|---|
| Aid | NEW | Max HP and HP increase (B2). |
| Alter Self, Animal Messenger, Arcane Lock, Augury, Continual Flame, Darkvision, Gentle Repose, Knock, Levitate, Locate Animals or Plants, Locate Object, Magic Mouth, Pass without Trace, Prayer of Healing (10 min), Rope Trick, See Invisibility, Spider Climb, Zone of Truth, Find Traps | OOS | Utility / out of encounter casting time. |
| Barkskin | NEW | AC floor (B2). |
| Blindness/Deafness | NEW | `save` with condition is DATA, but the caster chooses blinded or deafened per cast (parameterized effect, small B1). |
| Blur | NEW | Disadvantage against you (B2). |
| Branding Smite | NEW | B3. |
| Calm Emotions | NEW | Area + suppress condition (C3, B2). |
| Crown of Madness | NEW | Forced attack (C1). |
| Darkness | NEW | Obscurement (C7). |
| Enhance Ability | NEW | Check advantage and bonus per mode (B2). |
| Enlarge/Reduce | NEW | Size/damage change (B2, C6). |
| Flame Blade | NEW | Summoned weapon (C5). |
| Flaming Sphere | NEW | Object/zone (C4/C5). |
| Gust of Wind | NEW | Line + push (C2, C3). |
| Heat Metal | NEW | Sustained damage (B2). |
| Hold Person | DATA | Shipped (`save` WIS, paralyzed, repeat end_of_turn, humanoid, upcast targets). |
| Invisibility | NEW | Condition applied with no save, ends on attack/cast; `save` kind requires a save (B1/B2). |
| Lesser Restoration | NEW | End a condition (C9). |
| Magic Weapon | NEW | Magic bonus (A4/B2). |
| Mirror Image | NEW | Duplicates that absorb attacks (B2). |
| Misty Step | NEW | Teleport (C2). |
| Moonbeam | NEW | Zone (C4). |
| Protection from Poison | NEW | B2. |
| Ray of Enfeeblement | NEW | Attack + damage halving rider (B2). |
| Scorching Ray | DATA | `spell_attack` with `beams 3`, 2d6 fire, `upcast.beams 1`. |
| Shatter | NEW | Sphere (C3). |
| Silence | NEW | Zone preventing verbal components (C4). |
| Spike Growth | NEW | Zone (C4). |
| Spiritual Weapon | NEW | Summoned attacker (bonus action each turn) (C5). |
| Suggestion | NEW | Forced behavior (C1). |
| Warding Bond | NEW | Shared damage (B2). |
| Web | NEW | Area zone + restrained (C3, C4). |
| Acid Arrow | NEW | Delayed second damage (C9). |

### 8.4 Level 3

| Spell | Status | Notes |
|---|---|---|
| Clairvoyance, Create Food and Water, Major Image, Meld into Stone, Nondetection, Phantom Steed, Plant Growth, Sending, Speak with Dead, Speak with Plants, Tongues, Water Breathing, Water Walk, Gaseous Form, Glyph of Warding (trap setup), Remove Curse | OOS | Utility / out of encounter casting time. |
| Animate Dead | NEW | Raise controlled creatures (C5). |
| Beacon of Hope | NEW | Save advantage, max healing (B2). |
| Bestow Curse | NEW | Touch + chosen effect (B1/B2). |
| Blink | NEW | Phasing (C2). |
| Call Lightning | NEW | Zone + repeating action (C4). |
| Conjure Animals | NEW | Summons (C5). |
| Counterspell | DATA | Shipped (`counter`). |
| Dispel Magic | NEW | End effects, check by level (C9). |
| Daylight | NEW | Light/obscurement dispelling (C7). |
| Magic Circle | NEW | Zone with creature-type restrictions (C4). |
| Fear | NEW | Cone, frightened, drop items, forced Dash (C3, C1). |
| Fireball | NEW | Sphere (C3); 8d6 DEX half, `upcast.dice 1` are DATA. |
| Fly | NEW | Movement mode (C6). |
| Haste | NEW | Speed, AC, extra action (B2, B4). |
| Hypnotic Pattern | NEW | Area, charmed + incapacitated (C3, C1). |
| Lightning Bolt | NEW | Line (C3); dice/save DATA. |
| Mass Healing Word | DATA | `heal` with `targets {count 6}`, bonus action, range 60. |
| Protection from Energy | NEW | Resistance buff (B2). |
| Revivify | NEW | Raise a creature dead within 1 minute (C9). |
| Sleet Storm | NEW | Zone (C4). |
| Slow | NEW | Speed/AC/attacks (B2). |
| Spirit Guardians | NEW | Aura/zone around caster (C4). |
| Stinking Cloud | NEW | Zone (C4). |
| Vampiric Touch | NEW | Attack damage DATA; heal half and repeat action need B1/B2. |
| Wind Wall | NEW | Zone (C4). |

## 9. Monsters: stat blocks, traits and actions (CR 0 - ~5)

Stat-block data: `monster@3` carries abilities, AC, HP average, speed (walk, fly, swim, ...), saving throws, skills, damage and condition resistances/immunities/vulnerabilities, senses, languages, CR, traits (`trait_active` with an `effect`), actions, bonus actions, reactions, legendary actions, `behavior.ai` Luau script. Only the subset in G1 reaches sim.

| Trait / action | Examples (SRD, mostly CR 0-5) | Status | Notes |
|---|---|---|---|
| Single weapon attack (melee or ranged) | goblin, kobold, skeleton, zombie, orc, bandit | DATA | One `*_weapon_attack` action; ranged normal/long range. |
| Multiattack (several attacks, possibly different) | owlbear, veteran, knight, gladiator, troll, ettin, manticore, wight | NEW | `monster@3 actions` has no multiattack entry; `attacks_per_action` is uniform and not loaded (A1). |
| Several damage components in one attack | giant spider, giant scorpion, ankheg | NEW | A2. |
| On-hit saving throw rider (poison, paralysis, prone, grapple, restrain) | ghoul (paralysis), wolf (prone), giant constrictor snake, crocodile (grapple), giant toad (swallow) | NEW | A3, C1. |
| Action: Web (ranged restrain, escape DC) | giant spider | NEW | A3, C1. |
| Pack Tactics (advantage if an ally is within 5 ft of the target) | wolf, dire wolf, jackal, kobold, blood hawk | NEW | Conditional advantage fed by a sim adjacency fact (B7). |
| Keen Senses / Smell / Hearing (Perception advantage) | wolf, hawk, owl | OOS | Perception checks are not simulated beyond Hide vs passive Perception. |
| Darkvision, Blindsight, Tremorsense, Truesight | most monsters, bat, ankheg | NEW | Lighting and vision (C7). |
| Damage resistances / immunities / vulnerabilities | skeleton (poison immune, bludgeoning vulnerable), zombie, fire elementals | NEW (wiring) | G1. |
| Nonmagical-attack resistance | werewolf, gargoyle, specter, ghost, wight | NEW | A4. |
| Condition immunities | skeleton, zombie, ghoul, golems, elementals | NEW (wiring) | G1. |
| Regeneration (loses it to certain damage) | troll, vampire spawn | NEW | Start-of-turn heal with suppression by damage type (B7). |
| Recharge (5-6) breath weapon | dragon wyrmlings, hell hound | NEW | Recharge roll on an action (B7) + cone/line (C3). |
| Limited-use actions (N/day) | innate spellcasters and various | NEW | Uses/recharge machinery exists only for `self_heal` features (B4/B7). |
| Spellcasting (slots, prepared list) | acolyte, priest, druid (CR 2), mage (CR 6) | DATA (G1) | Slots/known spells are `Combatant` fields; spell list per section 8. |
| Innate Spellcasting (at will, N/day) | imp? (no), pixie, dryad, couatl | NEW | At-will and per-day lists (B7). |
| Legendary actions, lair actions, Legendary Resistance | SRD creatures of CR 10+ (aboleth, mummy lord, adult dragons) | OOS | Not reachable within CR 0-5; deferred (C11). |
| Charge / Pounce / Trampling Charge (move then bonus damage / knock prone) | boar, lion, panther, tiger, warhorse | NEW | Needs movement history this turn (B7, A3). |
| Aggressive (bonus action move toward a hostile) | orc | NEW | `bonus_action_grant` has no Move (B7). |
| Reckless | minotaur, berserker | NEW | B6. |
| Rampage (bonus attack after reducing a creature to 0 HP) | gnoll | NEW | On-kill trigger (B7). |
| Nimble Escape | goblin | DATA | `bonus_action_grant [disengage, hide]` (shipped). |
| Parry (reaction, +AC) | gladiator, knight, bandit captain | NEW | Reaction feature (B6). |
| Flyby (no opportunity attacks) | owl | NEW | Flight (C6) + OA exception. |
| Fly / Swim / Climb / Burrow speeds | bat, eagle, gargoyle, crocodile, giant octopus, ankheg | NEW | C6; sim reads `speed.walk` only. |
| Large and bigger creatures (multi-tile space, grapple size limits) | ogre, ankheg, owlbear, troll, ettin | NEW | C6. |
| Swarm of Tiny creatures (shares space, half damage at half HP, resistances) | swarms of rats, ravens, insects | NEW | C6, G1. |
| Undead Fortitude (CON save to drop to 1 HP unless radiant or crit) | zombie | NEW | Drop-to-0 trigger (B7). |
| Death Burst | magmin | NEW | Death trigger (B7). |
| Magic Resistance (advantage on saves vs spells) | dryad, satyr, imp | NEW | Save advantage by source (B7). |
| Magic Weapons (attacks count as magical) | couatl and many fiends/celestials | NEW | A4. |
| Sunlight Sensitivity | kobold, drow | NEW | Lighting (C7). |
| Life Drain / max HP or ability drain | specter, wraith, shadow, vampire spawn | NEW | B7. |
| Incorporeal Movement | specter, ghost, will-o'-wisp | NEW | Movement through creatures/objects (C6). |
| Frightful aura or gaze (save or frightened) | ghost (Horrifying Visage), mummy (Dreadful Glare), knight (Leadership aura) | NEW | Aura and conditions (C4, C1). |
| Engulf / Swallow | gelatinous cube, giant toad | NEW | Contained state (C1). |
| Petrifying Gaze / Touch (delayed condition progression) | basilisk, cockatrice | NEW | A3, B2. |
| Teleport bonus action | blink dog | NEW | C2. |
| Shapechanger | werewolf, doppelganger | NEW | Stat block swap (C5). |
| Rust Metal / Corrode / Stench | rust monster, ghast | NEW | A3 and aura (C4). Disease itself OOS. |
| Blood Frenzy (advantage vs damaged creatures) | sharks | NEW | Conditional advantage (B7). |
| False Appearance, ambush setups | mimic | OOS | Only surprise (C10) matters. |
| Amphibious, Hold Breath, Spider Climb, Web Walker, Web Sense | crocodile, giant spider | OOS | Non-combat movement/sense. |

(Representative lists name examples to size the mechanic, not a complete CR 0-5 roster. Per-monster counts are the Tools & Content task.)

## 10. Summary counts

Row counts by status are computed over the tables above (sections 1-9). Spell rows that group several OOS utility spells in one cell count as one row.

| Category | DATA | NEW | OOS | Rows |
|---|---|---|---|---|
| 1. Core rules engine (what is implemented in rules5e today) | 16 | 10 | 5 | 31 |
| 2. Weapons (properties) | 11 | 5 | 0 | 16 |
| 3. Armor and shields | 2 | 2 | 2 | 6 |
| 4. Conditions (SRD Appendix A) | 12 | 6 | 0 | 18 |
| 5. Damage types and damage handling | 4 | 5 | 0 | 9 |
| 6.1 Fighter (full) | 4 | 8 | 0 | 12 |
| 6.2 Cleric (full; Life domain) | 5 | 5 | 1 | 11 |
| 6.3 Wizard (full; School of Evocation) | 5 | 2 | 1 | 8 |
| 6.4 Lower tier | 15 | 39 | 12 | 66 |
| 6.5 Races (party members' combat traits) | 1 | 8 | 0 | 9 |
| 7. Core spell mechanics | 10 | 3 | 2 | 15 |
| 8.1 Cantrips | 7 | 7 | 1 | 15 |
| 8.2 Level 1 | 5 | 32 | 1 | 38 |
| 8.3 Level 2 | 2 | 30 | 1 | 33 |
| 8.4 Level 3 | 2 | 23 | 1 | 26 |
| 9. Monsters: stat blocks, traits and actions (CR 0 - ~5) | 3 | 36 | 4 | 43 |
| **Total** | **104** | **221** | **31** | **356** |

"NEW (wiring)" rows (rules5e models it, content-to-sim does not carry it) are counted under NEW.

## 11. Missing mechanics, prioritized

Sizes are rough (S = days, M = about a week, L = multi-week/cross-module). Owner is who carries the main work; dependencies in notes.

| # | Id | Mechanic | Unlocks | Owner | Size |
|---|---|---|---|---|---|
| 1 | G1 | **Content-to-sim wiring.** `ContentLibrary` loads resistances/immunities/vulnerabilities and condition immunities (and a condition-immunity mask on `Combatant`), creature type, size, `attacks_per_action`, spellcasting block (slots, caster level, spell list), casts via `Combat` (`CastSpell` command, hooks `spells`, `spell_target_ok`, `targets_within`, `can_see`, `can_provide_components`, area target list), and spell events in the viewer | Everything marked "(wiring)"/"DATA (G1)": every caster, resistant monster, skeleton/zombie immunities | Engine Core (sim) with Rules S; Tools & Content for schema fields (spellcasting on stat blocks) | L |
| 2 | A1 | **Multiattack and extra attack.** Action sequence (`multiattack` entry naming actions and counts) or `attacks_per_action`+attack list; an `extra_attack` effect kind; loading property (one shot per action) | Fighter/Barbarian/Monk/Paladin/Ranger L5; owlbear, gladiator, bugbear-class monsters (most CR 2-5) | Rules (resolve sequence) M; Tools & Content (schema + validator) S | M |
| 3 | A2 | **Multiple damage components per attack** (`damage[]` beyond the first) in `Attack` and resolution; crit doubles each | giant spider, flaming weapons, elemental damage, smites | Rules S; Engine Core S (loader); T&C S | S |
| 4 | B1 | **Effects array (S6) with N effects per spell**, plus a no-save apply-condition effect and per-cast option (Blindness/Deafness) | Guiding Bolt, Ray of Sickness, Vicious Mockery, Hideous Laughter, Thunderwave (damage+push), Chill Touch, Shocking Grasp | Rules S-M (loader and `resolve_spell` iterate effects); Tools & Content S (schema/validator) | M |
| 5 | A3 | **Attack riders**: on a hit, a save-or-condition / extra damage / knock prone / grapple / restrain, for weapon attacks, monster actions and spell attacks | wolf trip, ghoul paralysis, giant spider poison, Stunning Strike, Open Hand, net, Ray of Sickness | Rules M; Tools & Content M (action schema `on_hit`) | M |
| 6 | B2 | **Timed modifier effects (buffs/debuffs)**: generic `ActiveEffect` with duration (rounds), source, concentration link and modifiers: attack/save/check bonus dice or flat, advantage/disadvantage grants, AC set/bonus, speed modifier, temp HP, damage bonus, resistance grants; also expiry for timed durations | Bless, Bane, Shield of Faith, Mage Armor, Haste, Slow, Faerie Fire, Rage, Hex, Blur, Barkskin, Protection from Energy, Heroism, True Strike, Ray of Frost | Rules L; Tools & Content M (effect kinds, spell effect `apply_modifier`) | L |
| 7 | C3 | **Area templates on the grid** (cone, cube, cylinder, line, sphere): compute affected creatures, LOS/cover for Dex saves, feed `ActionRequest::targets` | Burning Hands, Fireball, Lightning Bolt, Thunderwave, Shatter, dragon breath, Fear | Engine Core M; Rules S (resolve with per-target save, already done) | M |
| 8 | A4 | **Nonmagical/silvered/adamantine damage qualifiers and magic-weapon flag** | werewolf, gargoyle, specter, golems, Magic Weapon, Ki-Empowered | Rules S; Tools & Content S | S |
| 9 | B3 | **On-hit / once-per-turn damage riders**: Sneak Attack (needs "ally adjacent to target" from sim), Divine Smite (post-hit, spend slot), Hunter's Mark, Colossus Slayer, Hex, Agonizing Blast, Disciple of Life | Rogue, Paladin, Ranger, Warlock, Cleric healing | Rules M; Engine Core S (adjacency fact) | M |
| 10 | B4 | **Feature resource pools and more active feature kinds**: ki, rage uses, Channel Divinity, Lay on Hands pool, Bardic Inspiration, sorcery points, Action Surge (extra action), static `attack_bonus`, `ac_bonus`, `crit_range` kinds | Fighter 1-5 (Archery, Defense, Action Surge, Improved Critical), Barbarian, Monk, Paladin, Bard, Cleric, Sorcerer | Rules M (pools in `EncounterState`, like `FeatureUses`); Tools & Content M (effect kinds in `common@1`) | M |
| 11 | B5 | **Bonus-action attacks**: two-weapon fighting (off-hand without ability mod), Martial Arts strike, Flurry of Blows, Frenzy | Monk, Barbarian Berserker, dual wielders, light property | Rules M | M |
| 12 | B6 | **Reaction features and roll modifiers**: allow `cost: reaction` features (Uncanny Dodge, Parry, Deflect Missiles, Protection, Redirect Attack, Hellish Rebuke with a new "damaged by" trigger, Cutting Words), d20 modify/reroll windows (Bardic Inspiration, Halfling Lucky, Great Weapon Fighting damage reroll) | Rogue 5, Monk 3, Bard 3, knights/gladiators/goblin bosses, Fighting Styles | Rules M; Tools & Content S (unreserve `reaction`) | M |
| 13 | B7 | **Monster trait hooks**: recharge (5-6) and limited-use actions, Regeneration, start/end-of-turn triggers, death/drop-to-0 triggers (Undead Fortitude, Death Burst, Relentless Endurance), conditional advantage traits (Pack Tactics, Blood Frenzy, Sunlight Sensitivity), save-advantage-by-source (Magic Resistance, Gnome Cunning, Fey Ancestry), Charge/Pounce, Aggressive, max-HP drain | Wolves, trolls, zombies, wyrmlings, mephits, orcs | Rules M; Engine Core S (adjacency/movement facts); Tools & Content S | M |
| 14 | C1 | **Conditions with a source and grapple/shove**: charmed, frightened (move-closer restriction, LOS), grappled (source, escape check, drag), restrained escape, crawl cost; Grapple/Shove actions; swallow/engulf states | Charm Person, Command, Fear, Turn Undead, grapple monsters, Shove tactics | Rules M; Engine Core S | M |
| 15 | C2 | **Forced movement and teleport** (push/pull, Misty Step, Dissonant Whispers), with OA rules | Thunderwave, Thorn Whip, Repelling Blast, Open Hand, Misty Step | Engine Core M; Rules S | M |
| 16 | C7 | **Cover from the grid and lighting/vision/obscurement** (darkvision, Darkness, Fog Cloud, heavy obscurement, sunlight sensitivity) | Cover bonuses (rules already accept `cover_ac_bonus`), Sacred Flame, Darkness | Engine Core M | M |
| 17 | C6 | **Movement modes and creature size/space** (fly, swim, climb, burrow; Large+ multi-tile; incorporeal) | Bats, harpies, gargoyles, crocodiles, ogres, owlbears, ghosts, Fly | Engine Core M; Rules S (speed per mode); Tools & Content S | M-L |
| 18 | C4 | **Persistent zones and auras** (Spirit Guardians, Moonbeam, Spike Growth, Web, Entangle, Stinking Cloud, Cloud damage, Aura of Protection later) with enter/start-of-turn triggers | Level 1-3 control spells | Rules L; Engine Core L | L |
| 19 | C5 | **Summons, familiars, bonded weapons, transforms (Wild Shape, Shapechanger, Animate Dead, Conjure Animals, Spiritual Weapon)**: add combatants mid-encounter with a controller and lifetime; swap stat blocks with HP overflow | Druid, Warlock Chain, Cleric 2, Wizard familiars, werewolf | Rules L; Engine Core L; Tools & Content M | L |
| 20 | C9 | **Spell kinds**: HP-pool (Sleep, Color Spray), dispel/cleanse (Dispel Magic, Lesser Restoration), delayed/sustained damage (Acid Arrow, Heat Metal, Witch Bolt), resurrection (Revivify), duration expiry for non-concentration spells | Sleep (very common at L1), Lesser Restoration, Revivify | Rules S-M (each) | M total |
| 21 | C8 | **Per-cast options**: Metamagic flags (Twinned, Quickened, Careful, ...), Sculpt Spells, chosen condition/mode | Sorcerer 3, Evocation Wizard 2 | Rules M | M |
| 22 | C10 | **Surprise, Hit Dice, Arcane/Natural Recovery** | Round 1 ambush, between-fight attrition | Rules S | S |
| 23 | C11 | **Legendary actions, lair actions, Legendary Resistance** | CR >= 10 only; out of the level 1-5 goal | Rules L (deferred) | L |

Recommended order for the stated goal (L1-5 party vs CR 0-5 monsters): 1, 2, 3, 8 first (they unblock most monster stat blocks and the Fighter at level 5 without new combat rules), then 4, 5, 9, 10, 12 (class features), then 7, 6, 14 (spells with areas/buffs/conditions), then movement and environment (15-17), then zones and summons (18-19) last.

## 12. Source notes and caveats

- SRD 5.1 content checked against the published CC-BY-4.0 SRD (classes, spells, monsters, conditions, equipment). Class tables and spell texts are summarized, not reproduced.
- "Current code" statements were verified on `main` at `6012567`: `engine/rules` (types, encounter, spells), `engine/rules/content` (spell loader), `engine/core/sim` (content loader, features, combat), `schemas/{common,character,monster,spell}`, and `tools/validate`.
- Where an example monster list is inexact, treat it as illustrative; Tools & Content owns the authoritative per-monster classification.
