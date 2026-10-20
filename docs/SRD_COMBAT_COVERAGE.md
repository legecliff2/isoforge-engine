# SRD 5.1 combat coverage audit (levels 1-5 party vs SRD monsters)

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Goal: a complete level 1-5 party (Fighter, Cleric, Wizard in full; the other nine classes as a lower tier) fighting SRD monsters, deterministically, from content data. This audit lists every combat-relevant SRD element and says what the code does **today** (first written at `6012567`, after #23 spellcasting, #34 follow-ups, #41 PackSet; statuses refreshed for the version-bump batch after #91, with row 15 `compute_ac` in #93, see 11.5). SRD text was cross-checked against the 5e-database mirror at `a6212be` (see 12). It is not a plan of record; section 11 is a proposed priority list that includes the Director's decisions.

## 0. How to read this

Status (first word of the status cell):

- **DATA** = expressible as data with what exists now: a `spell@1` effect (`spell_attack`, `save`, `heal`, `auto_hit`, `ac_bonus`, `counter`), a `common@1` feature effect kind (`damage_bonus`, `bonus_action_grant`, `self_heal`), a stat-block field, a rules5e `ActionKind`, or a rules5e hook.
- **NEW** = needs a new mechanic. The note gives the gap id from section 11. "(wiring)" means rules5e already models it but nothing carries it from content into a running `sim::Combat`.
- **OOS** = out of scope for combat simulation, with the reason.

**Gap ids (one numbering).** `T&C-G1`..`T&C-G19` are Tools & Content's schema/content gaps (`srd_content_inventory.md` section 3). `R-G1`..`R-G17` are rules-only mechanics with no T&C counterpart (R-G1 is the content-to-sim wiring item). Section 11.2 maps every id used in the first draft of this document (A1.., B1.., C1.., and the old "G1") to the unified ids. Director decisions (recorded 2026-10-17) are folded into section 11.3.

Things that apply to many rows (so they are not repeated):

1. **R-G1, content-to-sim wiring (status on main after #45, #47, #48, #49).** `sim::ContentLibrary::add` (engine/core/sim/src/content.cpp) now reads: abilities, AC value, average HP, walk speed, proficiency bonus, saving-throw proficiencies, PC vs monster, `melee_weapon_attack`/`ranged_weapon_attack` actions with **every** `damage[]` entry (#45; `Attack::extra_damage`), `finesse` still always false, feature effects, `damage_resistances` / `damage_immunities` / `damage_vulnerabilities` and `condition_immunities` (#45, monster@3 / character@3; pre-@3 files stay inert via the migration gate), `type` -> creature type, `size` (stored in `Archetype::size`, no behaviour), and the `spellcasting` block (ability, caster level, slots, pact slots, known spells) (#49). `sim::Combat` now has `CastSpell` plumbing: it installs `hooks.spells` (a SpellBook built from the pack set's `spell@1` files), `spell_target_ok`, `targets_within`, `can_see`, `can_provide_components` (V/S always; a material cost is refused, no inventory) and answers BeingHit / SpellCast windows in `in_range`; sim event schema is v5 (`SpellTarget`, hashed only when present). **Still not read:** skills / passive Perception (T&C-G16), `attacks_per_action` and any multiattack declaration (T&C-G1 content side), qualified resistances and `Attack::qualifiers` from data (T&C-G2 / R-G17 content side), alignment/type normalization (T&C-G15), `finesse` and other weapon properties (T&C-G18), multi-tile footprint (T&C-G14; the `size` value is stored only, footprint work is in flight), monster innate spellcasting (T&C-G10, `monster@4`). Area templates are still not computed: callers list the creatures in `targets`. Spell rows marked DATA are DATA in `spell@1` + rules5e and are now castable from content in a sim combat.
2. **Several effects per spell (S6 / R-G2, rules + loader + validator done).** `spell@1` `effects` may hold N effects: the first carries targeting and scaling, later effects resolve in order over the same targets (optionally only for the targets the previous effect affected, `applies_to: affected`), a no-save `apply_condition` kind exists, and reaction effects stand alone. Extra effects do not scale. Still not expressible: effects that need new mechanics (push, speed change, advantage on the next attack, "can't regain HP", forced movement) and per-cast options (Blindness/Deafness choice).
3. **Area spells.** `spell@1` stores `area` (cone/cube/cylinder/line/sphere + size) and rules5e resolves a caller-supplied `ActionRequest::targets` list with a save each. Nothing computes who is inside a template on the grid, so area spells were NEW (R-G10) even though their dice/save/scaling are data. **Update (#78):** sim now computes the targets of cone / line / cube / sphere / cylinder templates over footprint tiles with LOS from the origin (`Command::aim/to`), so area spells and area actions are DATA; cover for Dex saves and lighting stay R-G13.
4. **Spell durations.** Concentration, repeat saves and `UntilStartOfCasterTurn` (Shield) end effects. Timed `rounds/minutes/hours` durations are stored but never expire on their own (no round counter on `ActiveSpellEffect`). Fine for concentration spells (which end with concentration), a gap for non-concentration timed buffs (R-G3, R-G15). **Update:** `rounds` and "end of the target's next turn" durations now expire for condition effects (#76, #83) and for modifiers (#89); `minutes`/`hours` are still stored only and there is no rest model.
5. **Authored numbers.** Today characters are authored per level as `character@3` stat blocks. Static numbers such as AC from armor, Unarmored Defense, Unarmored Movement, +1 magic weapon bonuses, ability modifiers and proficiency are baked by the author; that is DATA **today**. Director approved `item@1` + `character@4` with an equipment model and AC recompute (T&C-G17), after which armor/weapons drive AC and attacks and the equipment rows in sections 2-3 become NEW. Level-up / ASI / multiclassing remain OOS (Tools & Content authoring). **Update:** `item@1` exists with the SRD weapons, armor and shield authored (#67, #72), and `rules5e::compute_ac` is written and tested (#93); applying it at archetype build is Engine Core's (T&C-G17), so equipment-driven AC is still NEW (wiring).
6. **Version-bump batch (rows 1-13 and 15), status after #91 / #93.** Landed end to end (rules + content + sim): save actions, recharge and pool usage (rows 6, 7), area templates (row 7), innate casting (row 8), triggers / riders 9a-9c, timed condition durations. Landed in rules only, waiting for Engine Core / T&C wiring (shown as `NEW (wiring)`): reactions (row 11), timed modifiers (row 12), pool-backed features, `grant_action`, `heal_pool`, Legendary Resistance (row 13), `compute_ac` (row 15). Wiring needed: `hooks.feature_modifiers`, `hooks.feature_spec`, the reaction option hooks, `resources[]` registration and `Combatant::legendary_pool` in `Combat::create`, the 0xA2 modifiers hash fold, `spell@1` `apply_modifier`, applying `compute_ac`. Hand-off APIs are in `docs/proposals/VERSION_BUMP_BATCH.md` (6.2a, 7.2a, 10.1).
7. SRD 5.1 has one subclass per class (Fighter: Champion, Cleric: Life, Wizard: Evocation, Druid: Land, Barbarian: Berserker, Bard: Lore, Monk: Open Hand, Paladin: Devotion, Ranger: Hunter, Rogue: Thief, Sorcerer: Draconic, Warlock: Fiend). Only those are listed. The only feat in the SRD (Grappler) is not audited; it depends on R-G8.

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
| Healing, temp HP (absorbs first) | DATA | `apply_healing`, `apply_damage`. Temp HP can be set on a combatant but no spell/feature grants it (R-G3). Update: rules can now grant temp HP (`ModStat::TempHp` through `apply_modifier`, #89; riders `temp_hp`, #85); no authored `spell@1` kind produces it yet. |
| Ability checks, saving throws, proficiency, advantage/disadvantage cancel | DATA | `ActionKind::AbilityCheck`, `saves.hpp`, `combine_modes`. |
| Standing up from prone (half movement) | DATA | `ActionKind::StandUp`. The built-in AI stands up at the start of its turn when not grappled, speed > 0 and at least half its speed is left. |
| Movement on a grid, difficult terrain (2x), LOS | DATA | sim `Grid`, `step_cost_ft`, supercover LOS. |
| Crawling while prone (extra movement cost) | NEW (wiring) | Rules done: a Move while Prone costs double (already in `Move`); sim's own movement budget is not wired (R-G8). |
| Opportunity-attack exceptions (Disengage, teleport, forced movement) | DATA | Disengage implemented; forced moves and teleports never open a `LeavingReach` window and spend no movement (R-G9, VERSION_BUMP_BATCH 7.2k). |
| Surprise (skip first turn, no reactions) | NEW (wiring) | rules5e `Combatant::surprised` (only EndTurn on the first turn, no reactions until it ends), `surprised_by_stealth` (Stealth vs passive Perception), `apply_surprise` (R-G16, VERSION_BUMP_BATCH 7.2h). Sim: `Combat::apply_surprise` / `surprised_by_stealth`, 0xA8 fold, replay input, driver ends the surprised turn. Pending: an ambush flag in chunk / encounter content (no schema field yet). |
| Short / long rest: spell slots, Pact slots, feature uses | DATA | `recover_slots_short_rest/long_rest`, `recover_features`. |
| Rests: Hit Dice spending, HP regained | NEW (wiring) | rules5e `Combatant::hit_die` / `hit_dice` / `hit_dice_max`, `spend_hit_dice`, `short_rest`, `long_rest` (rest.hpp, R-G16, 7.2h). Sim: loader sets `hit_die` / `hit_dice_max` from `character@N classes[]`; `sim/rest.hpp` short / long rest over a kept roster on a dedicated rest PRNG stream. Pending: the campaign / party layer that carries a roster into the next `Combat` (create rebuilds combatants from content). |
| Falling damage, suffocation, starvation, extreme heat | OOS | Hazard/exploration; not part of a fight. |
| Mounted combat | OOS | No mounts in the SRD party we target. |
| Underwater combat | OOS | Environment variant; defer. |
| Cover (+2 / +5 AC **and Dexterity saves**, total cover) | NEW | `AttackContext::cover_ac_bonus` exists and is honored for attack rolls only; there is no cover bonus on Dexterity saves, and sim never computes cover (R-G13). SRD: half cover +2, three-quarters +5, to AC and Dex saves. |
| Lighting, darkness, obscurement, darkvision/blindsight/truesight | NEW | Not modelled; sim has LOS only (R-G13). |
| Flanking | OOS | Optional rule, not SRD core. |
| Two-weapon fighting | NEW (wiring) | Rules done (R-G6): `Combatant::bonus_attacks` option with `requires_attacks` (light weapons), `different_weapon` and off-hand `damage` dice; `ActionRequest::bonus_option`. Loader / AI pending. |
| Grapple and shove (contested Athletics/Acrobatics) | NEW (wiring) | Rules done (R-G8): `ActionRequest::special` 1-3, Athletics vs Athletics/Acrobatics, one-size limit, replaces one attack. The push goes through the R-G9 `forced_move` hook; `Moved.c` = 1 from rules via `EncounterHooks::shove_forced_flag`, which sim sets (R-G9 sim PR; same events as EC #104's own flagging) (R-G9, VERSION_BUMP_BATCH 7.2k). Sim / AI wiring pending. |
| Flying, swimming, climbing, burrowing movement | NEW | One `speed_ft`; sim reads `speed.walk` only (T&C-G12, T&C-G14). |
| Creature size and space (large creatures occupy 2x2) | NEW | Size is loaded (`Archetype::size`, #49) but only stored; the grid is still one tile per creature (T&C-G12, T&C-G14; footprint model in flight). |
| Dispel / end effects on a creature | NEW (wiring) | Rules done (R-G15): `SpellEffectKind::Dispel` ends spell effects and spell modifiers on a target (check vs 10 + level above the slot). Loader done (EC #120, check form in #126): spell@1 effect `dispel {check: {ability: spellcasting, dc_base, plus: spell_level}}` (same object as `counter`; `dc_base` required, 1..99). No pack ships it yet; schema / validator pending (T&C). |

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
| Heavy (Small creatures disadvantage) | NEW (wiring) | Rules done: `kWeaponHeavy` + `Combatant::size` (Small/Tiny attacker = disadvantage). Sim loader sets `Combatant::size` from the stat block `size` (R-G12 sim PR 1, #165; inert size stays Medium); the adapter still sets the Heavy bit. |
| Special: net (restrains; Large or smaller, DC 10 Str escape or 5 slashing vs AC 10; one attack per action), lance (disadvantage within 5 ft; two hands unless mounted) | NEW | Net = restrain rider (T&C-G5); lance: `kWeaponLance` gives disadvantage with the target within 5 ft (rules done; two-hands/mounted rule is not modelled). Low priority. |
| Silvered / adamantine weapons, magic weapons vs nonmagical resistance | NEW (wiring) | Rules done (#47): `Attack::qualifiers` (magic / silvered / adamantine) and `Combatant::resist_qualified` / `immune_qualified`. Nothing loads them from content yet, and no weapon data sets the flags (R-G17, T&C-G2, T&C-G17). Magic bonuses themselves are baked: DATA. |
| Improvised weapons, unarmed strike (1 + Str) | DATA | Authored as an action. |
| Weapon proficiency | DATA | Baked into `to_hit`. |

## 3. Armor and shields

| Element | Status | Notes |
|---|---|---|
| Armor AC (light/medium/heavy, shield, Dex cap) | DATA (baked) | Baked into `armor_class.value` today; equipment-derived AC is T&C-G17. Update: `rules5e::compute_ac` (armor, shield, Dex cap, unarmored / Unarmored Defense / natural AC) written and tested (#93); nothing applies it at archetype build yet (T&C-G17 wiring). |
| Equipment model (item@1, equipped armor/weapons driving AC and attacks) | NEW (wiring) | `item@1` schema and the SRD weapons/armor/shield content exist (#67, #72, #75); `compute_ac` exists in rules (#93). Not wired: the loader does not read `character@4.equipment` objects into `Combatant::ac` (Engine Core, T&C-G17). |
| Donning/doffing time, inventory in the middle of a fight | OOS | Not simulated. |
| Strength requirement (speed -10) | NEW (wiring) | `compute_ac` returns `speed_ft` reduced by 10 for a Str shortfall (#93); applying it at archetype build is Engine Core's (T&C-G17). |
| Stealth disadvantage (padded, scale mail, half plate, ring mail, chain mail, splint, plate) | NEW | SRD marks exactly these 7 of 13 armor entries. `Hide` has no armor input (T&C-G17 item property, R-G1 skills). Corrected: the first draft omitted padded and ring/chain/splint/plate. Update: `compute_ac` reports `stealth_disadvantage` for worn armor (#93); `Hide` does not read it yet. |
| Spell: Mage Armor, Shield of Faith, Barkskin (AC changes) | NEW (wiring) | Rules: `ModStat::Ac` bonus modifiers (#89, `bonus`-only per Q15, so Barkskin's AC floor and Mage Armor's base-13 are approximations to settle) through `SpellEffectKind::ApplyModifier`; no `spell@1` kind or authored spell yet (R-G3, T&C). |
| Shield spell (+5 AC reaction) | DATA | `ac_bonus` effect (shipped `srd51.spell.shield`). |

## 4. Conditions (SRD Appendix A)

| Condition | Status | Notes |
|---|---|---|
| Blinded | DATA | Attack disadvantage on self, advantage against. Sight-based checks are not modelled. |
| Charmed | NEW (wiring) | Rules done (R-G8): the spell-applied source is recorded (`EncounterState::sourced`) and the charmed creature cannot attack its charmer. Social advantage is a sim fact. |
| Deafened | DATA | No combat effect beyond the flag. |
| Frightened | NEW (wiring) | Disadvantage on attacks/checks is implemented; the spell-applied source is recorded (`condition_source`). The movement limit needs positions (sim). |
| Grappled | NEW (wiring) | Rules done (R-G8): speed 0, source and escape DC recorded, `special` 4 Escape (DC or contest), ends if the grappler is incapacitated or dies, drag halves the grappler's Move cost. Sim moves the held creature (`grappled_by`). |
| Incapacitated | DATA | No actions/reactions. |
| Invisible | DATA | Attack advantage / disadvantage against. No "heard/ noticed" rules. |
| Paralyzed | DATA | Incapacitated, auto-fail Str/Dex saves, advantage against, crit within 5 ft. |
| Petrified | DATA | Incapacitated, speed 0, resistance to all damage, auto-fail Str/Dex. Aging/weight not modelled. SRD also grants immunity to poison and disease, which is not modelled (condition-granted immunity). |
| Poisoned | DATA | Attack and check disadvantage. |
| Prone | DATA | Attack disadvantage on self; advantage within 5 ft against, disadvantage beyond. `StandUp` action. Built-in AI: at the start of its turn a prone creature stands up (`StandUp`, `StoodUp`) unless it is grappled, its speed is 0 or less than half its speed is left; otherwise it stays prone and acts as before. |
| Restrained | DATA | Speed 0, attack disadvantage, advantage against, Dex save disadvantage. |
| Stunned | DATA | Incapacitated, auto-fail Str/Dex, advantage against. |
| Unconscious | DATA | Prone-like effects, crit within 5 ft, death saves. |
| Exhaustion levels 1-6 | DATA | `exhaustion_level`: check disadvantage, speed half, attack/save disadvantage, HP max half, speed 0, death. |
| Condition immunities on a stat block | DATA | `Combatant::condition_immunities` (#45): `add_condition` refuses an immune condition, and save-spell conditions skip it. Loaded from `condition_immunities` for monster@3 / character@3. |
| Conditions with a duration or end trigger (end of next turn, until damaged) | DATA | Timed condition durations: rules `Expiry {Rounds, EndOfTargetNextTurn}` (#76) compiled from `save_effect apply[].duration` and trigger riders (#83, #90); `until_save` repeat saves. Timed non-condition modifiers are rules-only (row 12, #89). |
| Apply a condition from a monster action (ghoul claws, wolf trip, spider poison) | DATA | `save_effect` actions (#77), `on_hit[]` riders `save_condition` / `apply_condition` (#84, #90, rules #82/#85/#86). Grapple, swallow and restrain-with-escape still need R-G8. |

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
| Evasion (no damage on success, half on fail) | NEW (wiring) | Rules done (R-G7): `Combatant::evasion` (Dex save vs half: none on success, half on failure). Loader done (EC #114): feature / trait kind `evasion`. |
| Maximum HP reduction (wraith, vampire, specter life drain) | NEW (wiring) | rules5e `Attack::drain_max_hp` / `drain_save_dc`, `Combatant::hp_max_reduction` (death at 0 maximum, `long_rest` clears) (small leftovers, VERSION_BUMP_BATCH 7.2j). |

## 6. Class features, levels 1-5

Existing feature machinery: `damage_bonus` (static), `bonus_action_grant` (static), `self_heal` (active `UseFeature`, with uses and short/long rest recharge). Spells use `Combatant` slots/pact slots/caster level (rules) and R-G1 for wiring.

### 6.1 Fighter (full)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit Die d10, proficiencies, saves (Str/Con), armor/weapons | 1 | DATA | `character@3 classes[].hit_die`, `saving_throws`, baked AC/to_hit. |
| Fighting Style: Dueling | 1 | DATA | `damage_bonus {amount:2, condition: one_handed_melee_no_other_weapon}` (shipped, `fighter_1`). |
| Fighting Style: Archery (+2 ranged attack rolls) | 1 | NEW (wiring) | Rules done (R-G5): `Combatant::attack_bonus_ranged` (weapon attacks only). Loader kind `attack_bonus` pending. |
| Fighting Style: Defense (+1 AC in armor) | 1 | NEW (wiring) | Rules done (R-G5): `AcInput::armored_bonus` in `compute_ac` (only while armor is worn). Loader kind `ac_bonus` pending. |
| Fighting Style: Great Weapon Fighting (reroll 1-2 on damage dice) | 1 | NEW (wiring) | Rules done (R-G7): `Combatant::great_weapon_fighting`, two-handed or versatile with `ActionRequest::two_handed_grip`; weapon dice showing 1-2 rerolled once. Loader done (EC #114): feature kind `great_weapon_fighting`, `two_handed` / `versatile` property bits; the AI sets `two_handed_grip` for a versatile melee weapon of a stat block without a shield (v0 wield model). |
| Fighting Style: Protection (reaction, impose disadvantage on attack vs adjacent ally) | 1 | NEW (wiring) | Rules done (#87): `AllyAttacked` window + `ImposeDisadvantage` reaction (R-G7); no content/hook compile yet (Engine Core). |
| Fighting Style: Two-Weapon Fighting (add mod to off-hand) | 1 | NEW (wiring) | Rules done (R-G6): omit the off-hand `damage` override so the baked ability modifier stays. Loader pending. |
| Second Wind | 1 | DATA | `self_heal {dice 1d10, plus fighter_level, uses 1, recharge short_rest, cost bonus_action}` (shipped). |
| Action Surge | 2 | NEW (wiring) | Rules done (#91): `FeatureSpec::grant_actions` -> `TurnBudget::extra_actions` (R-G5). Needs the `grant_action` feature compile and a `feature_spec` hook (Engine Core). |
| Martial Archetype: Champion, Improved Critical (crit on 19-20) | 3 | NEW (wiring) | Rules done (R-G5): `Combatant::crit_min` (19 / 18); a non-20 crit must still hit. Loader kind `crit_range` pending. |
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
| Channel Divinity (1/rest at L2, 2/rest at L6) | 2 | NEW (wiring) | Pool + `FeatureSpec` pool spend done in rules (#91, R-G5); feature compile and per-effect content pending. |
| Channel Divinity: Turn Undead | 2 | NEW | WIS save or turned (frightened-like, must flee, cannot take reactions) for 1 minute; area 30 ft (R-G5, R-G8, R-G10). |
| Channel Divinity: Preserve Life | 2 | NEW | Distribute a 5 x level HP pool among creatures within 30 ft, up to half max HP each (R-G5, R-G10, R-G15). |
| Destroy Undead (CR 1/2 at L5) | 5 | NEW | Turn Undead outcome threshold (R-G5). |
| Ability Score Improvement | 4 | DATA | Authored. |

### 6.3 Wizard (full; School of Evocation)

| Feature | Lvl | Status | Notes |
|---|---|---|---|
| Hit die d6, proficiencies, saves (Int/Wis) | 1 | DATA | Baked. |
| Spellcasting, spellbook (known list), prepared spells, cantrips | 1 | DATA | `spells_known`; spellbook copying/prep is OOS (downtime). |
| Arcane Recovery (once per day, short rest, recover slots <= half wizard level) | 1 | NEW (wiring) | rules5e `recover_spell_slots` + `recovery_budget` (R-G16, 7.2h); feature kind `SlotRecoverySpec` + `apply_slot_recovery` (validated choice, spends the once-per-day `FeatureUses` row, long-rest reset) (7.2n). Sim: `slot_recovery {class wizard, max_slot_level 1..9 = 5, uses 1..255 = 1}` feature kind (class level from `classes[]`) -> `Archetype::slot_recovery`; `sim::register_slot_recovery` adds the row to a kept roster, `sim::short_rest_with_recovery` / `sim::apply_slot_recovery` return the rules result (refusals change nothing); long rest resets via `RestTables`. Outside `Combat`, no hash tags. Pending: schema (T&C); slot choice is the caller's (AI / UI). |
| Arcane Tradition: Evocation; Evocation Savant | 2 | OOS | Downtime spell copying cost. |
| Sculpt Spells (allies auto-succeed and take no damage from your evocation area) | 2 | NEW (wiring) | rules5e `Combatant::sculpt_spells` + `SpellDef::evocation` + `ActionRequest::spared` (R-G14, VERSION_BUMP_BATCH 7.2i). Sim wiring done: open feature kind `sculpt_spells {}`; spell@1 `school: evocation` sets `evocation` (loader). Validator: common@1 `sculpt_spells` arm (#145). Authored on `srd51.character.wizard_level_2..5` (`features[].effect {"kind": "sculpt_spells"}`). |
| Ability Score Improvement | 4 | DATA | Authored. |
| Cantrip damage scaling at 5 | 5 | DATA | `scaling.cantrip.levels [5,11,17]` (shipped on Fire Bolt). |
| 3rd-level slots at 5 | 5 | DATA | `spell_slots` table baked on the character. |

### 6.4 Lower tier

| Class / Feature | Lvl | Status | Notes |
|---|---|---|---|
| **Barbarian** hit die d12, proficiencies | 1 | DATA | Baked. |
| Rage (+damage, resist B/P/S, adv on Str checks/saves; bonus action; uses/long rest) | 1 | NEW (wiring) | Rules done: `apply_modifier` Damage (melee `scope`) + per-type Resistance + Str Save/AbilityCheck advantage (#89) with a `rage` pool (#91). Needs the `character@4` effect compile, `feature_modifiers` / `feature_spec` hooks and the 0xA2 hash fold (Engine Core). |
| Unarmored Defense (10 + Dex + Con) | 1 | DATA | Baked AC. |
| Reckless Attack | 2 | NEW (wiring) | rules5e `Combatant::reckless_attack` + `ActionRequest::reckless` on the first attack: advantage on Str melee weapon attacks this turn, attacks against it have advantage until its next turn (small leftovers, VERSION_BUMP_BATCH 7.2j). Loader done (EC #129): feature / trait kind `reckless_attack`; the caller declares `reckless`; the built-in AI (`sim::next_command`, Director-approved policy) declares it on the first melee attack of each of the creature's turns, never on a ranged attack. |
| Danger Sense (adv on Dex saves vs visible effects) | 2 | NEW | Conditional save advantage (T&C-G7 style trait). |
| Primal Path: Berserker, Frenzy (extra attack as bonus action, exhaustion after) | 3 | NEW (wiring) | Rules done (R-G6): `any_melee` bonus attack with `requires_modifier` = the Rage modifier; the exhaustion at the end of Rage is not modelled (conditions). |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| Fast Movement (+10 ft) | 5 | DATA | Baked speed (`speed.walk`). Condition "no heavy armor" is the author's. |
| **Bard** spellcasting (full caster) | 1 | DATA | |
| Bardic Inspiration (bonus action, d6/d8; ally adds to a roll within 10 min) | 1 | NEW | Roll-modifier token with a pool of uses (R-G5, R-G7). |
| Jack of All Trades (+half proficiency to checks) | 2 | NEW | Check modifier (R-G7/small). |
| Song of Rest | 2 | OOS | Out of combat healing. |
| Expertise | 3 | DATA | Authored skills. |
| College of Lore: Cutting Words (reaction, subtract inspiration die from enemy roll) | 3 | NEW (wiring) | Rules done for attack rolls (R-G7): `AttackRolled` window, `Reaction{SubtractRoll, dice, pool}`; checks and damage rolls not covered. Loader and sim done (EC #114): `reactions[]` window `roll_attack` + `subtract_roll {dice}` + `usage {kind: pool}` (resources[] pool); `in_range` = within 60 ft of the attacker and sees it. The Python validator still lists `roll_attack` / `subtract_roll` as reserved. |
| Font of Inspiration (short rest recharge) | 5 | NEW | Recharge change on the same pool (R-G5). |
| **Druid** spellcasting (full caster) | 1 | DATA | |
| Wild Shape (CR 1/4 at L2, no fly/swim; CR 1/2 at L4; CR 1 at L8) | 2 | NEW (wiring) | R-G12 rules PR 1 (7.2q): `UseFeature` of a `FeatureSpec{form = WildShape}` with `form_archetype`, stats via `build_form`; keeps Int / Wis / Cha, proficiencies, features (D7), temp HP (D8), concentration; can't cast; bonus-action `Revert`; HP overflow and D12 massive damage on revert; duration in rounds; uses 2 / short rest. Sim done (R-G12 sim PR 1): loader kind `wild_shape {pool, cost, class = druid, forms[], max_cr_by_level: [{level, max_cr}], no_fly_below = 8, no_swim_below = 4}` (common@2 keys exactly; uses / recharge from the resources[] pool, 1 per use; fix/wild-shape-keys), `build_form` (CR / fly / swim limits, beasts only, unknown ids refused, `requires_humanoid_form` blocked, Large forms must fit), Footprint updates, 0xAF / 0xBD folds, v10 ending order, AI Wild Shape off for content (no common@2 key; `FormFeature::ai_prefer`). T&C schema / validator: #163; the validator still lists the kind as reserved (warning). |
| Circle of the Land: Natural Recovery | 2 | NEW (wiring) | Same feature kind as Arcane Recovery: `SlotRecoverySpec` (druid level) + `apply_slot_recovery` (7.2n, on R-G16 `recover_spell_slots`). Sim: same `slot_recovery` kind with `class: druid` (druid level from `classes[]`) and the same `sim/rest.hpp` short-rest call. Pending: schema (T&C); Circle of the Land subclass gating is content's. |
| Druidic, Land bonus cantrip | 1-2 | OOS | Language / one extra cantrip (authored). |
| **Monk** Unarmored Defense (10 + Dex + Wis) | 1 | DATA | Baked AC. |
| Martial Arts (d4 die, Dex for unarmed, unarmed bonus action attack) | 1 | NEW (wiring) | Rules done (R-G6): unarmed bonus attack after the Attack action with an unarmed strike or monk weapon (`requires_attacks`). The martial-arts die is baked. Loader pending. |
| Ki (pool = monk level, short rest) | 2 | NEW (wiring) | Pool, spend and `ResourceSpent` done in rules (#91, R-G5); `resources[]` compile at `Combat::create` pending. |
| Flurry of Blows, Patient Defense, Step of the Wind | 2 | NEW (wiring) | Flurry of Blows rules done (R-G6): `count` 2 with the ki pool (`ResourceSpent`, spent once); Patient Defense / Step of the Wind need pool-backed Dodge / Disengage / Dash features (row 13 `FeatureSpec` covers the pool, the action grant is a `bonus_action_grant` use). |
| Unarmored Movement (+10 ft) | 2 | DATA | Baked speed. |
| Deflect Missiles (reaction: reduce ranged damage by 1d10 + Dex + level) | 3 | NEW (wiring) | Rules done (#87): `ReduceDamage` with `dice` (1d10 + bonus) in the `Damaged` window; the catch-and-throw option is not modelled. Compile pending. |
| Way of the Open Hand: Open Hand Technique (rider on Flurry: prone / push / no reactions) | 3 | NEW | Rider on hit (T&C-G5, R-G9). |
| Slow Fall | 4 | OOS | Falling damage not simulated. |
| Extra Attack | 5 | NEW (wiring) | T&C-G1. |
| Stunning Strike (ki; CON save or stunned until next turn) | 5 | NEW | Rider (T&C-G5) + ki pool (R-G5). |
| Ki-Empowered Strikes (magic for resistance, L6) | 6 | NEW | R-G17, T&C-G2 (beyond L5, listed for completeness). |
| **Paladin** Divine Sense | 1 | OOS | Detection. |
| Lay on Hands (pool = 5 x level; also cures disease/poison for 5 points) | 1 | NEW (wiring) | Rules done (#91): `FeatureSpec::heal_pool` spends `ActionRequest::amount` and heals the target. The cure disease/poison option is not modelled. Content compile pending. |
| Divine Smite (expend slot after a hit; 2d8 + 1d8 per slot above 1st, +1d8 vs undead/fiend) | 2 | DATA | `on_hit[]` rider with `choice: ask`, `spend` (slot, `per_slot_level` dice, crit doubling) and the `HitRider` window: rules #82/#86, sim #84/#90 (HitRider declined by default AI; ask via a policy). |
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
| Sneak Attack (xd6 once per turn with advantage or an ally within 5 ft of the target; finesse/ranged only) | 1 | DATA | `on_hit[]` extra_damage rider with once-per-turn limit, advantage/ally-adjacent predicates (`ActionRequest::ally_near_target`) and `scale.per_levels`: rules #82, sim #84. |
| Thieves' Cant | 1 | OOS | Language. |
| Cunning Action (bonus Dash / Disengage / Hide) | 2 | DATA | `bonus_action_grant {actions:[dash,disengage,hide]}`. |
| Roguish Archetype: Thief, Fast Hands (bonus-action Use an Object) | 3 | OOS | Use an Object is not simulated. |
| Thief: Second-Story Work | 3 | OOS | Climb/jump. |
| Uncanny Dodge (reaction: halve one attack's damage) | 5 | NEW (wiring) | Rules done (#87): `Damaged` window + `ReduceDamage {half}` reaction (R-G7). No hook/content compile yet. |
| **Sorcerer** spellcasting (full caster, spells known) | 1 | DATA | |
| Draconic Bloodline: Draconic Resilience (HP +1/level, AC 13 + Dex unarmored) | 1 | DATA | Baked HP and AC. |
| Draconic Bloodline: Dragon Ancestor | 1 | OOS | Language and Cha checks. |
| Font of Magic / Sorcery Points (convert slots and points) | 2 | NEW (wiring) | rules5e `font_of_magic.hpp`: `sorcery_points_max`, `flexible_casting_cost` (2/3/5/6/7), `flexible_casting` (out of encounter) and `ActionKind::FlexibleCasting` (bonus action, `ActionRequest::flexible` / `flexible_level`, log_hash 0xBA), `legal_actions` (7.2o). Points are the Metamagic `sorcery_pool`; created slots vanish on a long rest. Sim: feature kind `font_of_magic {pool}` sets the pool (registered when missing from `resources[]`) to `max = start = sorcery_points_max(sorcerer level)`, recharge long rest, and sets `sorcery_pool` without Metamagic (a different Metamagic pool is a load error); `Combat::log_hash` folds 0xBA + `flexible` + `flexible_level` when `flexible != 0`; `sim::flexible_casting(roster, id, ...)` (sim/font_of_magic.hpp) out of combat. Pending: AI conversions (none by default), validator / schema (T&C). |
| Metamagic (Careful, Distant, Empowered, Extended, Heightened, Quickened, Subtle, Twinned) | 3 | NEW (wiring) | rules5e `ActionRequest::metamagic` (+ `spared`, `heighten_target`), `Combatant::metamagic_known` / `sorcery_pool`, all eight options (R-G14, 7.2i). Sim wiring done: open feature kind `metamagic {options[], pool}` (sorcery points = a `resources[]` pool, folded under 0xA3); the request's options reach rules5e, fold under log_hash 0xB1, and Distant also widens sim's aim range for area spells; the AI casts without options. Validator arm pending (T&C); Font of Magic conversions: see the row above. |
| **Warlock** Pact Magic (slots recharge on short rest, all same level) | 1 | DATA | `pact_slot_level`, `pact_slots`, short-rest recovery implemented. |
| Otherworldly Patron: The Fiend, Dark One's Blessing (temp HP on kill) | 1 | NEW | On-kill trigger granting temp HP (T&C-G7/R-G3). |
| Eldritch Invocations: Agonizing Blast (+Cha per beam) | 2 | NEW | Per-beam damage bonus on a spell (R-G4). |
| Invocations: Armor of Shadows, Fiendish Vigor, Repelling Blast (push 10 ft), Devil's Sight | 2 | NEW | Mage Armor / False Life at will (R-G3), darkvision (R-G13). Repelling Blast's push is done in rules: a `ForcedMove` rider `up_to` 10 ft per beam hit, `ActionRequest::forced_feet` (R-G9, VERSION_BUMP_BATCH 7.2k); the invocation-to-Eldritch-Blast wiring is loader work. |
| Invocation: Eldritch Spear (Eldritch Blast range 300 ft) | 2 | NEW | Per-spell range modifier from a feature (R-G14). |
| Invocations: Eldritch Sight, Beast Speech, Mask of Many Faces, Beguiling Influence, Book of Ancient Secrets, Misty Visions, etc. | 2 | OOS | Utility. |
| Invocation: Thirsting Blade (Blade pact, attack twice with pact weapon) | 5 | NEW | Extra attack with a specific weapon (T&C-G1). |
| Invocations: Mire the Mind, One with Shadows, Sign of Ill Omen | 5 | NEW | Slot-free cast of Slow, Invisibility, Bestow Curse once per rest (R-G5 limited-use cast; needs the spells per section 8). |
| Pact Boon: Tome (3 cantrips) | 3 | DATA | `spells_known`. |
| Pact Boon: Blade (pact weapon, proficiency, magic) | 3 | NEW | Summoned/bonded weapon (R-G12, R-G17, T&C-G2). |
| Pact Boon: Chain (find familiar variants) | 3 | NEW (wiring) | R-G12 rules PR 3 (7.2s, D6): `Combatant::pact_of_chain`; an `Attack` with `familiar_id` trades one of the warlock's attacks for the familiar's reaction attack (familiar = its Telepathic summon). Familiar via Find Familiar (PR 2); imp / pseudodragon / quasit / sprite forms are content. Sim: 0xBE. |

### 6.5 Races (party members' combat traits)

| Trait | Status | Notes |
|---|---|---|
| Darkvision | NEW | R-G13. |
| Dwarven Resilience (poison resistance; advantage vs poison) | NEW | Resistance is DATA (loaded by #45); save advantage vs source type is T&C-G7 style. |
| Fey Ancestry (advantage vs charm) / Brave (halfling, vs frightened) | NEW (wiring) | Rules done: `{Save, Advantage, save_vs_conditions}` (Charmed / Frightened). Content loader is T&C's. |
| Halfling Lucky (reroll natural 1) | NEW (wiring) | Rules done (R-G7): `Combatant::reroll_ones` (attack / check / save; not death saves). Loader done (EC #114): feature kind `reroll_ones` (default all three = 7, optional `rolls`). |
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
| Components V/S/M, material cost/consumed | DATA | Flags in `SpellDef`; `hooks.can_provide_components`, installed by sim (#49): V/S always; a priced material needs the first combat-local `equipment[]` entry with the exact `components.m.item` id and `value_gp * 100 >= cost_cp` (no `item`: refused); `consumed` removes that entry (CONTENT_CONVENTIONS §7, `Combat::hash` 0xAD). |
| Concentration (CON save on damage DC max(10, half), ends on incapacitation / new concentration) | DATA | `damage_creature`, `concentration_spell`. |
| Cantrip tiers (5/11/17), upcast scaling dice/darts/targets/beams | DATA | `scaling.cantrip`, `scaling.upcast`. |
| Reaction spells with triggers (being hit, targeted by named spell, spell cast with sight) | DATA | `casting_time.trigger` {being_hit, spell_cast, or_targeted_by, requires_sight}. |
| Spell with one effect | DATA | Single `effects[0]`. |
| Spell with several effects (damage + rider) | DATA | `effects[]` with N effects, `applies_to: affected` riders, no-save `apply_condition` (R-G2 done). The rider mechanics themselves (push, disadvantage on next attack, etc.) are still NEW. |
| Targeting: single, N targets, creature_type filter, max spread | DATA | `targets {count, creature_type, max_spread_ft}`; `hooks.spell_target_ok`, `targets_within` (installed by sim, #49: range + line of sight; no area templates). |
| Area of effect templates | DATA | Done in sim (#78, R-G10): cone / line / cube / sphere / cylinder over footprint tiles, LOS from the origin, `Command::aim/to` for spells and the area-action AI. Cover for Dex saves and lighting remain R-G13. |
| Casting time minute/hour (non-combat) | OOS | Stored; rejected during encounters. |
| Duration expiry for rounds/minutes/hours | DATA (partial) | `rounds` and "end of the target's next turn" expire for condition effects (#76, #83) and for modifiers (#89); `minutes`/`hours` are stored and end with concentration only; no rest model (`until_rest` does not expire in an encounter). |
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
| Ray of Frost | NEW (wiring) | Rules done (follow-up): `Speed` modifier with `speed_sub` 10 and `until_end_of_target_next_turn` as an `ApplyModifier` rider on the spell attack; damage is DATA. Spell kind / loader pending (R-G3). |
| Sacred Flame | DATA | `save` DEX, 1d8 radiant, none on success. "Target gains no benefit from cover" is R-G13. |
| Shillelagh | NEW | Weapon-damage buff (R-G3). |
| Shocking Grasp | NEW | Melee spell attack DATA; advantage vs metal armor and "no reactions" rider R-G2/R-G3. |
| Spare the Dying | DATA | `ActionKind::SpareTheDying` (hook `spare_the_dying`). |
| True Strike | NEW (wiring) | Rules done (follow-up): AttackRoll advantage modifier with `consume` 1 (spent by the next attack roll). Spell kind pending (R-G3). |
| Vicious Mockery | NEW (wiring) | Rules done (follow-up): AttackRoll disadvantage modifier, `consume` 1, as a rider on the save effect. Spell kind pending (R-G3). |

### 8.2 Level 1

| Spell | Status | Notes |
|---|---|---|
| Alarm, Animal Friendship, Comprehend Languages, Create or Destroy Water, Detect Evil and Good, Detect Magic, Detect Poison and Disease, Disguise Self, Feather Fall, Goodberry, Identify, Illusory Script, Jump, Longstrider, Purify Food and Drink, Silent Image, Speak with Animals, Unseen Servant, Floating Disk | OOS | Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). |
| Bane | NEW (wiring) | Rules done (#89): negative die (`die_mult` -1) on AttackRoll and Save, concentration. Needs a `spell@1` `apply_modifier` kind / loader (R-G3, T&C adds the kind). |
| Bless | NEW (wiring) | Rules done (#89): 1d4 die on AttackRoll and Save, concentration, up to 3 targets. Needs a `spell@1` `apply_modifier` kind / loader (R-G3). |
| Burning Hands | DATA | Cone template computed by sim (#78); dice/save/upcast are data. |
| Charm Person | NEW | Charmed with source (R-G8). |
| Color Spray | NEW (wiring) | Rules done (R-G15): `SpellEffectKind::HpPool` with Blinded; end of the caster's next turn approximated by 1 round. Loader and sim done (EC #120): `hp_pool {dice, condition}`, the timer from the spell's `duration` (`rounds 1` = 1 round); area targets from an aimed `Command` (self cone). No pack ships it yet; schema / validator pending (T&C). |
| Command | NEW | Forced action next turn (R-G8). |
| Cure Wounds | DATA | Shipped (`heal`, excludes undead/construct). |
| Divine Favor | NEW (wiring) | Rules done (#89): Damage modifier with `damage_type` radiant and a 1d4 die, concentration (weapon attacks only). Spell kind pending (R-G3). |
| Entangle | NEW | Area zone + restrained (R-G10, R-G11). |
| Expeditious Retreat | NEW | Bonus-action Dash for duration (R-G3). |
| Find Familiar | NEW (wiring) | R-G12 rules PR 2 (7.2r): `Summon` (Own initiative, Telepathic: acts on its own, vanishes at 0 HP). Casting time (1 hour) means sim casts it before the encounter or content marks it; Chain variants are PR 3. |
| Fog Cloud | NEW | `Zone` (point sphere, 20 ft + 20 ft per slot level above 1 via `upcast_size_ft`, `kZoneHeavyObscured` flag) in rules (R-G11 PR 2, 7.2m); the obscurement itself waits for R-G13 (Q6). |
| Faerie Fire | NEW (wiring) | Rules done (#89): `AttacksAgainst` advantage modifier applied to each target in the area (sim computes the area, #78). Spell kind pending; the Dex save gate rides the existing Save effect (R-G3). |
| False Life | NEW (wiring) | Rules done (#89): `ModStat::TempHp` (the larger pool is kept; no record); the 1d4 + 4 roll is fixed by the loader as `bonus` today. Spell kind pending (R-G3). |
| Grease | NEW (wiring) | `Zone` (point, 10 ft cube, difficult terrain, 1 min timer, no concentration): Appear / Enter / EndTurn rows, Dex save or prone (R-G11, VERSION_BUMP_BATCH 7.2l). Sim: geometry, membership, terrain overlay. Sim done (zones sim PR): geometry / `zone_contains`, membership lists, `legal_zone_tiles` destination checks, terrain overlays, 0xAB / 0xB7. |
| Guiding Bolt | NEW (wiring) | Rules done (follow-up): `ApplyModifier` rider (`affected_only`) with AttacksAgainst advantage, `consume` 2 and `until_caster_turn`; covered by a test. Spell kind pending (R-G3). |
| Healing Word | DATA | `heal`, `casting_time bonus_action`, range 60, `upcast.dice`. |
| Hellish Rebuke | NEW (wiring) | Rules done (#87): `Damaged` window + `Retaliate` reaction resolving a synthetic Save SpellDef against the damager (R-G7). Content/hook compile pending. |
| Heroism | NEW (wiring) | Rules done (follow-up): `TempHp` modifier with `per_turn`; the frightened immunity is not modelled (R-G3). |
| Hideous Laughter | NEW | Prone + incapacitated (two conditions) and save-on-damage repeat (R-G2). |
| Hunter's Mark | NEW (wiring) | Rules done (#89, follow-up): Damage modifier (1d6, concentration, `applies_to` self) with the general `when` scope for conditions on the target; the "marked target only" scope and the move-the-mark option are not modelled (R-G4). |
| Inflict Wounds | DATA | Melee `spell_attack`, 3d10 necrotic, `upcast.dice`. |
| Mage Armor | NEW (wiring) | Rules done (follow-up): `Ac` modifier with `ac_floor` 13 + Dex (`ac_floor_dex`); the loader gates "not wearing armor". Spell kind pending (R-G3). |
| Magic Missile | DATA | Shipped (`auto_hit`). |
| Protection from Evil and Good | NEW | R-G3. |
| Sanctuary | NEW | Attackers must save to target (R-G3/R-G7). |
| Shield | DATA | Shipped (`ac_bonus`). |
| Shield of Faith | NEW (wiring) | Rules done (#89): +2 `Ac` modifier, concentration. Spell kind pending (R-G3). |
| Sleep | NEW (wiring) | Rules done (R-G15): `SpellEffectKind::HpPool` (lowest HP first, upcast dice, undead excluded, ends on damage). Loader and sim done (EC #120): `hp_pool {dice, condition, ends_on_damage, excludes_creature_type}`, `scaling.upcast.dice`, timer from `duration` (1 minute = 10 rounds), area targets from an aimed `Command`; the AI wakes a sleeping ally within 5 ft (Wake, `special` 5). No pack ships it yet; schema / validator pending (T&C). |
| Thunderwave | NEW (wiring) | Cube (R-G10) + push: `ForcedMove` rider (10 ft, `affected_only`: failed saves only) (R-G9, VERSION_BUMP_BATCH 7.2k); dice/save DATA. Sim done (R-G9 sim PR): spell@1 `forced_move {mode, feet, up_to, max_size}` rider; sim's `forced_move` hook walks the push tile by tile (8-way, stops at the last free tile, farthest first) and ends grapples out of reach. No pack ships Thunderwave yet; schema / validator arm pending (T&C). |

### 8.3 Level 2

| Spell | Status | Notes |
|---|---|---|
| Aid | NEW | Max HP and HP increase (R-G3). |
| Alter Self, Animal Messenger, Arcane Lock, Augury, Continual Flame, Darkvision, Gentle Repose, Knock, Levitate, Locate Animals or Plants, Locate Object, Magic Mouth, Pass without Trace, Prayer of Healing (10 min), Rope Trick, See Invisibility, Spider Climb, Zone of Truth, Find Traps, Arcanist's Magic Aura, Detect Thoughts, Enthrall, Find Steed (10 minutes) | OOS | Utility / out of encounter casting time. |
| Barkskin | NEW (wiring) | Rules done (follow-up): `Ac` modifier with `ac_floor` 16, concentration. Spell kind pending (R-G3). |
| Blindness/Deafness | NEW (wiring) | rules5e `SpellDef::condition_choices` + `ActionRequest::condition_choice` (R-G14, 7.2i). |
| Blur | NEW (wiring) | Rules done (#89): `AttacksAgainst` disadvantage modifier on self, concentration (the blindsight/truesight exception is not modelled). |
| Branding Smite | NEW | R-G4. |
| Branding Smite | NEW | Weapon-attack rider: extra radiant damage, reveals invisible, concentration (R-G4, T&C-G5). SRD level 2 (the earlier draft also listed it at level 1). |
| Calm Emotions | NEW | Area + suppress condition (R-G10, R-G3). |
| Darkness | NEW | `Zone` (point sphere 15 ft, `kZoneMagicalDarkness` flag) in rules (R-G11 PR 2, 7.2m); vision, dispelling light (R-G13) and cast on an object (R-G12) still open. |
| Enhance Ability | NEW (wiring) | Rules done (#89): AbilityCheck advantage scoped to one ability; the temp-HP / carrying options are not modelled (R-G3). |
| Enlarge/Reduce | NEW | Size/damage change (R-G3, T&C-G12, T&C-G14). |
| Flame Blade | NEW | Summoned weapon (R-G12). |
| Flaming Sphere | NEW | Object/zone (R-G11/R-G12). |
| Gust of Wind | NEW | Line + push (R-G9, R-G10). |
| Heat Metal | NEW (wiring) | rules5e `SustainedDamage` + `CastSpell sustain` bonus action (R-G15, VERSION_BUMP_BATCH 7.2g); object rider not modelled. Loader and sim done (EC #120): `sustained_damage {damage}` (+ `scaling.upcast.dice`, `duration.concentration`, timer from `duration`); the AI sustains its own Heat Metal with the bonus action from the next turn; `log_hash` folds `sustain` (0xB8). No pack ships it yet; schema / validator pending (T&C). |
| Hold Person | DATA | Shipped (`save` WIS, paralyzed, repeat end_of_turn, humanoid, upcast targets). |
| Invisibility | NEW | Condition applied with no save, ends on attack/cast; `save` kind requires a save (R-G2/R-G3). |
| Lesser Restoration | NEW (wiring) | rules5e `EndCondition` + `ends_conditions` (R-G15, 7.2g); the caster chooses which listed condition ends (`ActionRequest::condition_choice` = 1 + index into the spell's `conditions` in JSON list order; not listed / not on the target = refused, no state change; no choice and the AI = the lowest listed condition present); no disease model. Loader done (EC #120): `end_condition {conditions[]}` (one def; no per-condition defs). No pack ships it yet; schema / validator pending (T&C). |
| Magic Weapon | NEW | Magic bonus (R-G17, T&C-G2/R-G3). |
| Mirror Image | NEW | Duplicates that absorb attacks (R-G3). |
| Misty Step | NEW (wiring) | `Teleport` effect (`max_feet` 30), bonus action, self; destination from the request, validated by sim (R-G9, VERSION_BUMP_BATCH 7.2k). Sim done (R-G9 sim PR): spell@1 `teleport {max_feet, needs_sight}`; `Combat::legal_teleport_tiles`; submit re-validates (range, free footprint, sight, Frightened not closer) and fills `teleport_feet`; log_hash 0xB2 / 0xB4. The built-in AI does not cast it; schema / validator arm pending (T&C). |
| Polymorph (L4) | NEW (wiring) | R-G12 rules PR 1 (7.2q): `SpellEffectKind::Polymorph`, unwilling Wis save, beast form via `build_form` (all stats replaced, fresh HP, can't cast), reverts at 0 HP with overflow carried (D12), on death or when concentration ends; refused on a shapechanger or a 0-HP creature (D13). Sim: `build_form` for Polymorph done (R-G12 sim PR 1: choices = rated beasts in id order, CR <= the target's CR or character level, footprint fit). Pending: T&C `polymorph` effect kind / rules loader key, AI. |
| Moonbeam | NEW (wiring) | `Zone` (point cylinder): Enter / StartTurn once per turn, Con save, 2d10 radiant half, upcast; the action move up to 60 ft on later turns (`zone_move`, D3) (R-G11, 7.2l). Shapechanger disadvantage done (`shapechanger_disadvantage`, PR 2); reverting the form is not modelled (R-G12). Sim: geometry, `legal_zone_tiles`. Sim done (zones sim PR): geometry / `zone_contains`, membership lists, `legal_zone_tiles` destination checks, terrain overlays, 0xAB / 0xB7. |
| Protection from Poison | NEW | R-G3. |
| Ray of Enfeeblement | NEW | Attack + damage halving rider (R-G3). |
| Scorching Ray | DATA | `spell_attack` with `beams 3`, 2d6 fire, `upcast.beams 1`. |
| Shatter | DATA | Sphere template computed by sim (#78); dice/save are data (the inorganic-material disadvantage is not modelled). |
| Silence | NEW (wiring) | `Zone` with `kZoneSilence` (R-G11 PR 2, 7.2m): a creature completely inside can't cast spells with a V component (casts, `legal_actions`, reaction spells), is immune to thunder damage, and is deafened while it stays inside (zone-bound effect). Sim: `zone_contains`. |
| Spike Growth | NEW (wiring) | `Zone` (point, 20 ft radius, difficult terrain): MoveWithin 2d4 piercing per 5 ft, no save, no per-turn limit, forced moves included (D6) (R-G11, 7.2l). Camouflage OOS (D6). Sim: membership per tile / forced move. Sim done (zones sim PR): geometry / `zone_contains`, membership lists, `legal_zone_tiles` destination checks, terrain overlays, 0xAB / 0xB7. |
| Spiritual Weapon | NEW (wiring) | R-G12 rules PR 3 (7.2s, D3): `SummonWeapon` kind, `ActiveSummonWeapon` token (no HP, no turn, id from the summon counter), melee spell attack on the cast and by `SummonWeaponAttack` (bonus action), 1d8 + mod force, +1d8 per two slot levels, 1 minute, no concentration, teardown with summons. Sim: token position / 20 ft move, 0xAF. |
| Suggestion | NEW | Forced behavior (R-G8). |
| Warding Bond | NEW | Shared damage (R-G3). |
| Web | NEW (wiring) | `Zone` (point, 20 ft cube, difficult terrain, lightly obscured flag): StartTurn / own-turn Enter, Dex save or restrained while in the webs, not re-saved; Strength check (action, `special` 4) to break free; ends on leaving or with the zone (R-G11, 7.2l). Flammability / anchoring OOS (D8). Sim done (zones sim PR): geometry / `zone_contains`, membership lists, `legal_zone_tiles` destination checks, terrain overlays, 0xAB / 0xB7. |
| Acid Arrow | NEW (wiring) | rules5e `DelayedDamage` rider at end of target's next turn (R-G15, 7.2g). Loader done (EC #120): `delayed_damage {damage, expiry}` as an `applies_to: affected` rider (or primary); both the initial and the later damage gain `scaling.upcast.dice` per slot above 2nd (SRD; the loader copies it to the rider's `SpellEffect::upcast_dice`, applied at cast; the effect stores the scaled dice and slot level, folded under 0xAC). No pack ships it yet; schema / validator pending (T&C). |

### 8.4 Level 3

| Spell | Status | Notes |
|---|---|---|
| Clairvoyance, Create Food and Water, Major Image, Meld into Stone, Nondetection, Phantom Steed, Plant Growth, Sending, Speak with Dead, Speak with Plants, Tongues, Water Breathing, Water Walk, Gaseous Form, Glyph of Warding (trap setup), Remove Curse, Tiny Hut (1 minute), Animate Dead (1 minute; control in an encounter: see its row), Magic Circle (1 minute) | OOS | Utility / out of encounter casting time (1 minute or longer). |
| Beacon of Hope | NEW | Save advantage, max healing (R-G3). |
| Bestow Curse | NEW | Touch + chosen effect (R-G2/R-G3). |
| Blink | NEW | Phasing (R-G9). |
| Call Lightning | NEW | Zone + repeating action (R-G11). |
| Animate Dead (controlling the undead in an encounter) | NEW (wiring) | R-G12 rules PR 2 (7.2r): the 1-minute cast happens before the encounter (sim); `Summon` (Group initiative per cast, D2; bonus-action command for any or all; Dodge when uncommanded; does not vanish: drops as a monster). Lasting control past the encounter is out of scope (Q15). |
| Conjure Animals | NEW (wiring) | R-G12 rules PR 2 (7.2r): `Summon`, the caster picks the creatures (D1, `summon_choice_ok` CR band), one initiative group (D2), verbal commands (free), Dodge when uncommanded (D4), vanish at 0 HP, end with concentration (D10) and at encounter end (D15). Sim: placement, 0xAF / 0xBC. |
| Counterspell | DATA | Shipped (`counter`). |
| Dispel Magic | NEW (wiring) | Rules done (R-G15): `SpellEffectKind::Dispel` (auto at or below the slot level, else spellcasting check vs 10 + level). Loader done (EC #120, check form in #126): `dispel {check: {ability: spellcasting, dc_base, plus: spell_level}}` (same object as `counter`; `dc_base` required). No pack ships it yet; schema / validator pending (T&C). |
| Daylight | NEW | Light/obscurement dispelling (R-G13). |
| Fear | NEW | Cone, frightened, drop items, forced Dash (R-G10, R-G8). |
| Fireball | DATA | Sphere template computed by sim (#78); 8d6 DEX half, `upcast.dice 1` are data. |
| Fly | NEW | Movement mode (T&C-G12, T&C-G14). |
| Haste | NEW (wiring) | Marker `ModStat::Haste` + existing AC / Dex-save advantage / Speed x2 modifiers; extra action `TurnBudget::haste_action` / `ActionRequest::haste_action` (one weapon attack, Dash, Disengage, Hide); lethargy when it ends (small leftovers, VERSION_BUMP_BATCH 7.2j). Use an Object not modelled. |
| Hypnotic Pattern | NEW | Area, charmed + incapacitated (R-G10, R-G8). |
| Lightning Bolt | DATA | Line template computed by sim (#78); dice/save are data. |
| Mass Healing Word | DATA | `heal` with `targets {count 6}`, bonus action, range 60. |
| Protection from Energy | NEW (wiring) | Rules done (#89): `Resistance` modifier for one chosen damage type, concentration (R-G3). |
| Revivify | NEW (wiring) | Rules done (R-G15): `SpellEffectKind::Revive` within 10 rounds of death (`Combatant::died_round`), 1 HP. Loader and sim done (EC #120): `revive {within_rounds?}`; sim refuses a revival into an occupied space. Material done (EC, priced materials): a caster with `{"item": "srd51.item.diamond", "value_gp": 300}` (or more) in `equipment[]` casts it and the diamond is consumed from combat-local state. |
| Sleet Storm | NEW | Zone (R-G11). |
| Slow | NEW (wiring) | Marker `ModStat::Slow` + AC / Dex-save -2 and half-speed modifiers; no reactions, action or bonus action, one attack; end-of-turn repeat save (`ModifierSpec::repeat_save`) (small leftovers, VERSION_BUMP_BATCH 7.2j). The somatic-spell delay (d20 >= 11 or next turn) is not modelled. |
| Spirit Guardians | NEW (wiring) | `Zone` anchored on the caster (aura, 15 ft, halved-speed flag): Enter / StartTurn once per turn (D2), Wis save, 3d8 radiant half, +1d8 per slot above 3, excluded creatures from the cast (`zone_excluded`), never the caster (R-G11, 7.2l). Evil caster's necrotic: `has_evil_damage_type` + `Combatant::evil` (PR 2). Sim: aura geometry, Q8 changes, halved-speed cost. Sim done (zones sim PR): geometry / `zone_contains`, membership lists, `legal_zone_tiles` destination checks, terrain overlays, 0xAB / 0xB7. |
| Stinking Cloud | NEW (wiring) | `Zone` (R-G11 PR 2, 7.2m): StartTurn row, completely within, Con save or lose the action that turn (`loses_action`, `TurnBudget::action_lost`; movement and bonus action remain, D9); poison-immune / poisoned-immune / `no_breath` creatures succeed automatically. Wind OOS (D8). |
| Vampiric Touch | NEW | Attack damage DATA; heal half and repeat action need R-G2/R-G3. |
| Wind Wall | NEW | Zone (R-G11). |

## 9. Monsters: stat blocks, traits and actions (CR 0 - ~5)

Stat-block data: `monster@3` carries abilities, AC, HP average, speed (walk, fly, swim, ...), saving throws, skills, damage and condition resistances/immunities/vulnerabilities, senses, languages, CR, traits (`trait_active` with an `effect`), actions, bonus actions, reactions, legendary actions, `behavior.ai` Luau script. Only the subset in T&C-G2 reaches sim.

| Trait / action | Examples (SRD, mostly CR 0-5) | Status | Notes |
|---|---|---|---|
| Single weapon attack (melee or ranged) | goblin, kobold, skeleton, zombie, orc, bandit | DATA | One `*_weapon_attack` action; ranged normal/long range. |
| Multiattack (several attacks, possibly different) | owlbear, veteran, knight, gladiator, troll, ettin, manticore, wight | NEW (wiring) | Rules done (#48): `Combatant::multiattacks` (declared, id-addressable sequences of attack indices), `ActionRequest::multiattack`, per-attack steps so reactions/Ready/movement happen between attacks. No content `multiattack` effect, loader, or sim AI option choice yet (T&C-G1 content side). |
| Several damage components in one attack | ankheg (slashing + acid), basilisk (piercing + poison) | DATA | T&C-G6, done in #45. Corrected: giant spider (poison is a Constitution-save rider with save-for-half damage, T&C-G5) and giant scorpion (multiattack + save poison rider) are not multi-component attacks. |
| On-hit saving throw rider (poison, paralysis, prone, grapple, restrain) | ghoul (claws: paralysis, not vs elf/undead), wolf (prone), giant spider (bite: CON save, 2d8 poison, half on save), giant scorpion (sting: CON save, 4d10 poison), crocodile (grapple, restrained), giant toad (swallow) | DATA (partial) | `on_hit[]` riders with `save` / `save_condition` / `apply_condition` (rules #82/#85/#86, sim #84/#88/#90) cover poison (save for half), paralysis and prone riders. Grapple/restrain-with-escape and swallow still need R-G8; the ghoul's elf/undead exemption is a predicate. |
| Action: Web (ranged restrain, escape DC) | giant spider | NEW | T&C-G5, R-G8. |
| Pack Tactics (advantage if an ally is within 5 ft of the target) | wolf, dire wolf, jackal, kobold, blood hawk | DATA (sim) | Rules done: `Combatant::roll_traits` `{Attack, Advantage, kWhenAllyNearTarget}`; sim supplies `ActionRequest::ally_near_target`; sim loader kind `pack_tactics` (trait `effect`, no fields, open kind at `@3` / `@4`) fills the trait (#127, `engine/core/sim/tests/pack_tactics_tests.cpp`). T&C still registers the kind in `common@1` / the validator and authors the monsters. |
| Keen Senses / Smell / Hearing (Perception advantage) | wolf, hawk, owl | OOS | Perception checks are not simulated at all (Hide stores a Stealth total but nothing opposes it). |
| Darkvision, Blindsight, Tremorsense, Truesight | most monsters, bat, ankheg | NEW | Lighting and vision (R-G13). |
| Damage resistances / immunities / vulnerabilities | skeleton (poison immune, bludgeoning vulnerable), zombie, fire elementals | DATA | Loaded by #45 (T&C-G2). |
| Nonmagical-attack resistance | werewolf, gargoyle, specter, ghost, wight | NEW (wiring) | Rules done (#47); content lists and `Attack::qualifiers` not loaded (R-G17, T&C-G2). |
| Condition immunities | skeleton, zombie, ghoul, golems, elementals | DATA | Loaded by #45 (T&C-G3). |
| Regeneration (loses it to certain damage) | troll, vampire spawn | NEW (wiring) | Rules done: `Combatant::regeneration {amount, suppressed_by}`; start-of-turn heal, suppressed by a listed damage type taken since its last turn, and a regenerator at 0 HP dies only at its next turn start if suppressed. Content `regen` effect + loader and the vampire-spawn sunlight/running-water clause (needs a lighting/terrain fact) remain. |
| Recharge (5-6) breath weapon | dragon wyrmlings, hell hound (CR 3, in the mirror; T&C's name list missed it) | DATA | `save_effect` action with `usage: recharge` (rules #70, sim #77), area from #78; the AI picks area actions (#78). |
| Limited-use actions (N/day) | knight Leadership, innate spellcasters and various | NEW (wiring) | Rules: at-will / recharge / pool usage rows exist for `save_effect` actions (#70, #77) and per-day rows for innate spells (#73, #79); non-spell `per_day` / `per_rest` actions and Leadership await compile (T&C-G4, R-G5). |
| Spellcasting (slots, prepared list) | acolyte (CR 1/4), priest, druid (CR 2), mage (CR 6) | DATA | monster@3 now accepts the `spellcasting` block (#49: ability, caster level, slots, pact, known spells; an engine-minimal addition whose final shape T&C owns) and sim casts through `CastSpell`. Corrected from NEW. At-will / N-per-day Innate Spellcasting is separate (below). |
| Innate Spellcasting (at will, N/day) | couatl, dryad, drow, green hag, lamia, unicorn | DATA | Rules #73, sim #79: `spellcasting.innate` (at-will, `per_day`, `recharge`), DC / attack overrides, 0xA4 fold. The monster still needs authored spells (T&C). |
| Legendary actions, lair actions, Legendary Resistance | aboleth (CR 10), mummy lord, adult/ancient dragons, **unicorn (CR 5)** | OOS | Deferred by Director decision (T&C-G11). 32 SRD monsters have legendary actions: 31 are CR 10 or higher, the unicorn is CR 5; none is CR 4 or lower. Corrected from "CR 10+". Update: Legendary Resistance is no longer deferred (Q14); it is a plain pool: rules done (#91, `Combatant::legendary_pool`, auto-spend on a failed save); `Combat::create` must register the pool and set the field. Legendary and lair actions stay deferred. |
| Charge / Pounce / Trampling Charge (move then bonus damage / knock prone) | boar, lion, panther, tiger, warhorse | DATA | `on_hit[]` rider with the `moved_at_least_ft` predicate (rules `TurnBudget::moved_ft` fed by `ActionRequest::move_tiles`; sim #61, #84) for the damage; the prone-on-failed-save rider is `save_condition` (#90). |
| Surprise Attack / Martial Advantage / Relentless (conditional extra damage; drop to 1 HP once per rest) | bugbear, hobgoblin, boar (wereboar) | DATA (sim) | `on_event` predicates `target_surprised` (round 1 and the target in the encounter's surprised set, `Combatant::surprised_at_start`, R-G16; holds after the target's own first turn too), `weapon_attack` (melee or ranged weapon attack; false for spell attacks / no attack), `ally_near_target` (sim fact `ActionRequest::ally_near_target`; Martial Advantage: `when` [weapon_attack, ally_near_target], `limit: once_per_turn`), `damage_at_most {amount}` (drop_to_zero damage after resistances) -> rules5e `PredicateKind::TargetSurprised` / `AllyNearTarget` / `DamageAtMost` / `WeaponAttack`; on_event `usage` per_day / per_rest / pool -> a pool Spend rider (trigger pool `<trait id>#usage`, recharged by rests, 0xA3 pools). Schema arms for the four predicate kinds pending (T&C). |
| Ghoul claws (paralysis, elves/undead immune); giant spider Web (attack -> restrained, escape DC, Recharge 5-6); on_event recharge | ghoul, giant spider (swarm of rats: next row) | DATA (sim) | on_hit rider `when[]` (appended after the AttackIs gate) with `not_creature_type [undead]` + new `not_target_tag {tag}` / `target_tag {tag}` -> rules5e `PredicateKind::TargetTag` / `NotTargetTag` over `Combatant::tags` (monster `tags[]`, character `race`; a race ending in `elf` except `half_elf` also tags `elf`). Action kind `attack_effect {attack ranged/melee, to_hit, range_ft, apply[{condition, escape{ability str, dc}}], usage}` -> non-spell SpellAttack + `affected_only` ApplyCondition with `escape_dc` (rules: escape action `special 4` now also breaks `escape_check` effects). on_event `usage: recharge {min}` -> `Trigger::recharge_min` + an ActionUse row (0xA1 fold, only when present), rolled at turn start. Spider follow-up: `long_range_ft` (Web 30/60; beyond normal range or a hostile within 5 ft = disadvantage, sim fills the ranged facts for non-spell ranged attack_effects); apply[] `object {ac, hp, vulnerable[], immune[]}` -> the restraint is a destructible web (`ActionRequest::special` 6 attacks it, any creature in reach / range including the restrained one; at 0 HP the restraint ends; Combat::hash 0xBF); save_effect / save_condition `zero_hp {condition, while, duration rounds n}` -> rules5e `SpellDef::zero_hp_*` (bite: stable, poisoned 600 rounds = 1 hour as one timed effect that outlasts healing, paralyzed for as long as that effect lasts). Pending: schema arms (T&C). |
| Swarm (shares spaces, Tiny openings, no healing / temp HP, half-HP bite) | swarm of rats (and other SRD swarms) | DATA (sim) | Monster tag `swarm` -> `Archetype::swarm` (`Combat::occupied_except`: a swarm ignores others' tiles and its own tiles block no one, so moves, paths, readied moves, forced moves and teleports share spaces both ways and may end there; non-swarms still block each other) + rules5e `Combatant::no_regain_hp` (apply_healing heals 0; temp HP grants from riders / modifiers skipped). Weapon action `half_hp_damage: "1d6"` -> rules5e `Attack::half_hp_damage` (replaces component 1 when hp * 2 <= hp_max). Immunities / BPS resistances: existing `condition_immunities` / `damage_resistances`. Tiny openings: the grid has no sub-tile openings, nothing to model. No hash tags. Pending: schema arms (T&C). |
| Aggressive (bonus action move toward a hostile) | orc | NEW (wiring) | rules5e `Combatant::aggressive`: bonus-action Dash (`ActionRequest::aggressive`, `TurnBudget::aggressive_dash`); sim keeps the movement toward a hostile (small leftovers, VERSION_BUMP_BATCH 7.2j). Sim done (EC #129): trait kind `aggressive`; the AI takes the Dash toward a visible target when it then reaches it (or cannot shoot), and a step on the Dash's movement must end nearer to a hostile the orc can see. |
| Reckless | minotaur, berserker | NEW (wiring) | Same as Reckless Attack (`reckless_attack`, `ActionRequest::reckless`) (small leftovers, VERSION_BUMP_BATCH 7.2j). Loader done (EC #129): trait kind `reckless_attack`; the built-in AI declares `reckless` on the first melee attack of each of its turns (never on a ranged attack). |
| Rampage (bonus attack after reducing a creature to 0 HP) | gnoll | DATA | `reduce_to_zero` trigger with a `bonus_attack` rider (rules #85, sim #88 incl. AI). |
| Nimble Escape | goblin | DATA | `bonus_action_grant [disengage, hide]` (shipped). |
| Parry (reaction, +AC) | gladiator, knight, bandit captain | NEW (wiring) | Rules done (#87): `BeingHit` window with an AC-bonus option plus `Attack` / `ReduceDamage` reactions. `reactions[]` compile and hooks pending (T&C-G9, Engine Core). |
| Flyby (no opportunity attacks) | owl | NEW | Flight (T&C-G12, T&C-G14) + OA exception. |
| Fly / Swim / Climb / Burrow speeds | bat, eagle, gargoyle, crocodile, giant octopus, ankheg | NEW | T&C-G12, T&C-G14; sim reads `speed.walk` only. |
| Large and bigger creatures (multi-tile space, grapple size limits) | ogre, ankheg, owlbear, troll, ettin | NEW | T&C-G12, T&C-G14. |
| Swarm of Tiny creatures (shares space, half damage at half HP, resistances) | swarms of rats, ravens, insects | NEW | T&C-G14, T&C-G2. |
| Undead Fortitude (CON save to drop to 1 HP unless radiant or crit) | zombie | DATA | `drop_to_zero` trigger with the `save` predicate (DC 5 + damage) and `set_hp` rider (rules #85, sim #88). |
| Death Burst | magmin | DATA | `death` trigger with an `area_save` rider (rules #85, sim #88, area from #78). |
| Magic Resistance (advantage on saves vs spells) | dryad, satyr, imp | NEW (wiring) | Rules done: `{Save, Advantage, kWhenSourceMagical}`; spell saves pass `SaveSource{magical}`. Content loader is T&C's. |
| Magic Weapons (attacks count as magical) | couatl and many fiends/celestials | NEW (wiring) | `Attack::qualifiers` exists (#47); no trait effect sets it (R-G17, T&C-G2). |
| Sunlight Sensitivity (disadvantage on attacks and sight Perception in sunlight) | kobold, drow | OOS | Descriptive for the slice: needs a sunlight/lighting model (R-G13) that is not planned, so the T&C data keeps it as a plain trait. Not filed as a lighting mechanic. If lighting ever lands it is a conditional-disadvantage trait (T&C-G7). |
| Life Drain / max HP or ability drain | specter, wraith, shadow, vampire spawn | NEW (wiring) | Max-HP drain: `Attack::drain_max_hp` (+ `drain_save_dc`) (small leftovers, VERSION_BUMP_BATCH 7.2j). Ability drain (shadow's Strength Drain) still open. |
| Incorporeal Movement | specter, ghost, will-o'-wisp | NEW | Movement through creatures/objects (T&C-G12, T&C-G14). |
| Frightful gaze/visage (save or frightened) | ghost (Horrifying Visage), mummy (Dreadful Glare) | NEW | Save action + condition with source (T&C-G4, R-G8). |
| Leadership (limited-use buff action: allies add 1d4 to attacks and saves for 1 minute) | knight | NEW (wiring) | Rules done (#89, #91): `AlliesWithin30` modifiers (1d4 on AttackRoll and Save, timed) through a `feature_modifiers` hook, with a per-day or pool use. Compile pending (T&C-G4, R-G3). |
| Engulf / Swallow | gelatinous cube, giant toad | NEW | Contained state (R-G8). |
| Petrifying Gaze / Touch (delayed condition progression) | basilisk, cockatrice | NEW (wiring) | `SpellDef::escalates` / `escalate_to` / `escalate_margin`: restrained, petrified on the failed repeat save (or at once when failing by the margin) (small leftovers, VERSION_BUMP_BATCH 7.2j). Gaze trigger (7.2p): `Combatant::gazes`, turn-start `GazeResponse` (`gaze_seen` sim fact, `avert_gaze` caller input, log_hash 0xBB; averted state Combat::hash 0xAE), averted eyes = gazer unseen (disadvantage against it, its attacks with advantage). Medusa (margin 5) and basilisk covered. Sim (7.2p EC): loader `gaze` feature kind (synthetic Save SpellDef + `Combatant::Gaze`), `Combat::gaze_seen` (line of sight, within `range_ft`, not Blinded, gazer not Invisible; one fact for both directions until R-G13) filled at submit, folds 0xBB / 0xAE, AI / headless driver answer without averting (surprised turn: `GazeResponse`, then `EndTurn`). Pending: schema (T&C). Cockatrice's touch is an on-hit rider, not a gaze. |
| Teleport bonus action | blink dog | NEW (wiring) | Innate `Teleport` effect (`max_feet` 40) (R-G9, VERSION_BUMP_BATCH 7.2k); content wiring pending. |
| Shapechanger | werewolf, doppelganger | NEW (wiring) | R-G12 rules PR 1 (7.2q): `FeatureSpec{form = Shapechanger}` form swap with one HP pool, action `Revert`, reverts on death; Polymorph refuses shapechangers (D13). Sim done (R-G12 sim PR 1): trait kind `shapechanger {cost, forms: [{id, name, size?, armor_class?, speed?, actions?, creature_type?}]}` (needs the fact `shapechanger: true`; `speed` keeps its walk, `actions` drops the other own actions' attacks), `build_form` from the creature's own block with the listed overrides, one HP pool; the AI keeps the spawn form. T&C schema: #163. |
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
| 4. Conditions (SRD Appendix A) | 15 | 3 | 0 | 18 |
| 5. Damage types and damage handling | 6 | 3 | 0 | 9 |
| 6.1 Fighter (full) | 4 | 8 | 0 | 12 |
| 6.2 Cleric (full; Life domain) | 5 | 5 | 1 | 11 |
| 6.3 Wizard (full; Evocation) | 5 | 2 | 1 | 8 |
| 6.4 Lower tier (nine classes) | 17 | 40 | 12 | 69 |
| 6.5 Races | 1 | 8 | 0 | 9 |
| 7. Core spell mechanics | 13 | 0 | 2 | 15 |
| 8.1 Cantrips | 7 | 6 | 1 | 14 |
| 8.2 Level 1 | 6 | 24 | 1 | 31 |
| 8.3 Level 2 | 3 | 29 | 1 | 33 |
| 8.4 Level 3 | 4 | 19 | 1 | 24 |
| 9. Monsters: stat blocks, traits, actions | 13 | 26 | 5 | 44 |
| **Total** | **129** | **192** | **31** | **352** |

"NEW (wiring)" rows (rules5e models it, content-to-sim does not carry it) are counted under NEW. Counts are as of main after #91 (and #93 for row 15); after #45, #47, #48 and #49 the totals were DATA 112 / NEW 209 / OOS 31 = 352, and before those PRs DATA 104 / NEW 217 / OOS 31. The version-bump batch moved 17 rows NEW -> DATA (conditions with a duration, condition riders from actions, area templates, duration expiry, Divine Smite, Sneak Attack, Burning Hands, Shatter, Fireball, Lightning Bolt, on-hit save riders, Recharge breath weapons, Innate Spellcasting, Charge / Pounce, Rampage, Undead Fortitude, Death Burst). A further 26 rows moved NEW -> `NEW (wiring)` (rules done, still counted NEW): see 11.5; the bonus-action attack PR (R-G6) moved 5 more (two-weapon fighting and its style, Martial Arts, Flurry of Blows, Frenzy), still counted NEW; the grapple / shove PR (R-G8) moved 5 more (grapple and shove, Charmed, Frightened, Grappled, crawling), still counted NEW; the static-feature-kinds PR (R-G5) moved 3 more (Archery, Defense, Improved Critical), still counted NEW; the R-G7 roll-window PR moved 4 more (Lucky, Cutting Words, Great Weapon Fighting, Evasion), still counted NEW; the R-G15 spell-kinds PR moved 5 more (dispel / end effects, Color Spray, Sleep, Dispel Magic, Revivify), still counted NEW; the R-G15 follow-up moved 3 more (Lesser Restoration, Acid Arrow, Heat Metal), still counted NEW; the R-G16 PR moved 4 more (surprise, Hit Dice / rests, Arcane Recovery, Natural Recovery), still counted NEW; the R-G14 PR moved 3 more (Sculpt Spells, Metamagic, Blindness/Deafness), still counted NEW; the small-leftovers PR (7.2j) moved 8 more (Maximum HP reduction, Reckless Attack, Haste, Slow, Aggressive, Reckless, Life Drain, Petrifying Gaze), still counted NEW; the R-G9 rules PR (7.2k) moved 3 more (Thunderwave, Misty Step, blink dog teleport), still counted NEW; the R-G11 rules PR 1 (7.2l) moved 5 more (Spirit Guardians, Moonbeam, Spike Growth, Grease, Web), still counted NEW; the R-G11 rules PR 2 (7.2m) moved 2 more (Silence, Stinking Cloud), still counted NEW (Fog Cloud and Darkness stay plain NEW until R-G13; Wall of Fire is level 4, outside the tables). This recount was produced by a script over the tables, which also reproduced the previous 112 / 209 / 31. "DATA (baked)" / "DATA (partial)" rows count as DATA.

### 10.2 Spells, per spell (SRD 5.1 levels 0-3)

The mirror has **169** SRD spells of level 0-3 (24 cantrips, 49 level 1, 54 level 2, 42 level 3). Every one is now named in section 8 (the first draft missed six and listed 13 that are not in SRD 5.1). Per spell: **DATA 20, NEW 77, OOS 72** (169). DATA: Burning Hands, Fireball, Lightning Bolt, Shatter (area templates, #78), Acid Splash, Eldritch Blast, Fire Bolt, Poison Spray, Produce Flame, Sacred Flame, Spare the Dying, Cure Wounds, Healing Word, Inflict Wounds, Magic Missile, Shield, Hold Person, Scorching Ray, Counterspell, Mass Healing Word.

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
| 7 | T&C-G8 | **Regeneration / Rejuvenation** (`regen` effect, suppression by damage type; depends on T&C-G7 events). **Regeneration rules side done** (`Combatant::regeneration`, `DamageApplied::types_dealt`); Rejuvenation (lich/ghost) is not. | troll, vampire spawn | Rules S; T&C S | S | N |
| 8 | R-G2 | **Effects array (S6)**: N effects per spell, a no-save apply-condition effect, per-cast option. **Done except the per-cast option** (rules + loader + validator; schema additive: new `apply_condition` kind and optional `applies_to`, no version bump). | Guiding Bolt, Vicious Mockery, Hideous Laughter, Thunderwave, Chill Touch, Shocking Grasp | Rules S-M; T&C S | M | N |
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

Update after #91 / #93 (version-bump batch):

| Item | Status | Remaining |
|---|---|---|
| T&C-G4 save actions, recharge, usage | **Done** end to end: rules #70, sim #77 (compile, recharge/pool registration, single-target AI), area targets #78, timed apply durations #83 | `per_day` / `per_rest` non-spell actions, Leadership-style usage, `until_save` variants beyond repeat saves |
| T&C-G5 on-hit riders / R-G4 | **Done** for hit/crit extra damage, event triggers, `bonus_attack`, `area_save`, `set_hp`, save and apply-condition riders, `ask` + `spend` + `per_slot_level`: rules #82, #85, #86; sim #84, #88, #90 | grapple / swallow / push riders (R-G8, R-G9); Colossus Slayer-style "below max HP" predicate check; AI that opts in to `ask` riders (default declines) |
| T&C-G9 / R-G7 reactions | **Rules done** (#87: `Damaged`, `AllyAttacked`, `ReduceDamage`, `Retaliate`, `Attack`, `ImposeDisadvantage`) | `reactions[]` compile, `damaged_options` / `being_hit_options` hooks, default decline policy for the new windows (Engine Core); d20 modify/reroll windows (Cutting Words, Lucky, Great Weapon Fighting) are still open |
| T&C-G10 monster spellcasting | **Done** for slots (#49) and innate (rules #73, sim #79) | authored monster spells (T&C) |
| R-G3 timed modifiers | **Rules done** (#89: attack, save, check, AC, speed, weapon damage, temp HP, resistance, attacks-against; replace/stack; rounds / target-turn / caster-turn / concentration expiry) | `spell@1` `apply_modifier` kind and loader, `feature_modifiers` hook, 0xA2 fold (Engine Core / T&C) |
| R-G5 pools and feature kinds | **Rules done** for pool-backed features, cost kinds, `grant_action`, `heal_pool`, Legendary Resistance (#91; pools and `ResourceSpent` since #68) | `resources[]` registration, `feature_spec` hook, `legendary_pool` (Engine Core); `attack_bonus`, `ac_bonus`, `crit_range` static kinds remain open |
| T&C-G17 equipment and AC | `item@1` + SRD items (#67, #72); **rules done** `compute_ac` (#93) | apply at archetype build, read `{item, equipped}` (Engine Core); Hide reading `stealth_disadvantage` |
| R-G10 area templates | **Done** in sim (#78) | cover for Dex saves, lighting (R-G13) |

### 11.4a TODO: campaign carry-over input (owned by Engine Core)

- **TODO (Engine Core):** a future optional per-creature carry-over input to `Combat::create` for the campaign layer: current HP,
  remaining Hit Dice, spell slots, feature / pool uses and consumed items (the combat-local equipment after priced materials were
  used up). Today every combat starts from content (full HP, full slots, the content `equipment[]`), and consumption is
  combat-local only (CONTENT_CONVENTIONS §7: nothing is written back). The input would be a replay input like the seed and the
  surprised set; absent, behaviour and every hash stay as today.
- **TODO (R-G12 carry-over between fights, campaign layer; `docs/proposals/SUMMONS_TRANSFORMS.md` §3.5, D15):** today Combat ends every summon and transform at
  the end of the encounter (Q15). Carrying long-lived summons (Find Familiar, Find Steed, Animate Dead control) and lasting
  transforms into the next fight belongs in this carry-over input: summon records with their ids from a campaign-level counter,
  hashed under 0xAF, and replayed like the surprised set.

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

### 11.5 Remaining rules-only gaps and proposed order (after #91 / #93)

Gaps that need new rules mechanics (not just Engine Core / T&C wiring, which is listed in 11.4), with the proposed order. Sizes follow 11 (S days, M about a week, L multi-week).

| Order | Id | Gap | Unlocks | Size |
|---|---|---|---|---|
| 1 | R-G3 follow-ups | **Done in rules** (this PR): one-shot / until-damaged expiry (`consume`), speed reduction (`speed_sub`), AC floor (`ac_floor`), per-turn temp HP (`per_turn`), general `when` scope (attack roll, attacks against, AC, damage), and `apply_modifier` as a rider effect. Haste's restricted extra action done in 7.2j. Still open: `when` on saves / checks, a "marked target" scope (Hunter's Mark), "frightened immunity" (Heroism) | Needs only wiring now: True Strike, Guiding Bolt, Vicious Mockery, Ray of Frost, Mage Armor, Barkskin, Heroism | done |
| 2 | R-G5 rest | **Done in rules**: static feature kinds `attack_bonus`, `ac_bonus`, `crit_range` (loader kinds pending). Dueling stays a baked `damage_bonus` | Archery, Defense, Champion Improved Critical | done |
| 3 | R-G8 | **Done in rules** (conditions with a source, grapple / shove / escape, drag, one-size limit). Still open: swallow / engulf, web-style restrain, sim wiring of the push / drag | grapple monsters, crocodile, Charm Person, Fear | done |
| 4 | R-G6 | **Done in rules**: bonus-action attack options (two-weapon fighting, Martial Arts, Flurry of Blows, Frenzy) with prerequisites, pool spend and an off-hand damage override. Still open: Open Hand Technique riders, Frenzy's exhaustion, a rules-side `legal_actions` listing | Monk, Berserker, dual wielders need wiring only | done |
| 5 | R-G7 rest | **Done in rules**: Lucky (`reroll_ones`), Cutting Words on attack rolls (`AttackRolled` + `SubtractRoll`), Great Weapon Fighting, Evasion. Still open: Cutting Words on checks / damage, Bardic Inspiration spending by the ally, Guidance / Resistance, roller-chosen rerolls (Lucky feat, Indomitable, Portent) | Bard, Halfling, Fighter style, Rogue / Monk | M (done) |
| 6 | R-G15 | **Done in rules**: HP-pool (Sleep, Color Spray), Dispel Magic, Revivify (7.2f); end-a-condition, delayed / sustained damage, Wake action (`special` 5) (7.2g). Loader / sim wiring done (EC #120); remaining: spell@1 schema / validator arms (T&C); Revivify's material done (priced materials, CONTENT_CONVENTIONS §7) | Sleep, Color Spray, Dispel Magic, Revivify, Lesser Restoration, Acid Arrow, Heat Metal done | - |
| 7 | R-G16 | **Done in rules** (7.2h): surprise (`surprised`, Stealth vs passive Perception helper), Hit Dice, `short_rest` / `long_rest` over combatant and table state, Arcane / Natural Recovery (`recover_spell_slots`). Sim wiring: surprise setup + 0xA8, Hit Dice loader, rest entry points (EC #123). Remaining: party layer carry-over, ambush content flag, Arcane / Natural Recovery content kind | round-1 ambush, attrition | done |
| 8 | R-G9 | **Done in rules** (7.2k): `ForcedMove` / `Teleport` effect kinds (per-effect `max_size`), the synchronous `forced_move` hook (+ `forced_move_order`), `Moved.c` flags with schema v8, grapples ending out of reach, shove through the hook. Spec: co-design proposal `docs/proposals/FORCED_MOVEMENT.md` (#110, merged). Still open: sim hook and paths (Engine Core), Open Hand Technique rider, Dimension Door companions and the 4d6 failure, Gust of Wind, falling / hazard damage (R-G11) | Thunderwave push, Repelling Blast, Misty Step, Open Hand | done (rules) |
| 9 | R-G14 | **Done in rules** (7.2i): Metamagic (all eight options, sorcery-point pool), Sculpt Spells, cast-time damage type / condition choices. Still open: Eldritch Spear (feature range modifier), Font of Magic slot / point conversion (rules 7.2o; sim wiring done), Empowered / Careful on non-Save effects beyond those listed | Sorcerer, Evocation Wizard, Blindness/Deafness, Chromatic Orb | done |
| 10 | Large / elevation follow-ups | Multi-tile footprints landed in sim (#50, #51, #55); remaining: Large reach and OA edge cases in rules, size limits for grapple / swallow (with R-G8), swarm (Tiny sharing space, half damage), and elevation / fly / swim / climb (T&C-G12, deferred; today all non-walk speeds act as walk) with the Flyby OA exception and falling damage | owlbear, troll, swarms, bat, eagle, owl | M-L, mostly Engine Core |
| 11 | R-G11 | **PR 1 done in rules** (7.2l): `SpellEffectKind::Zone` (`ZoneSpec`, `ActiveZone`), Appear / Enter / StartTurn / EndTurn / MoveWithin rows with once-per-turn groups, `counts_aura_move_as_enter` (D1), concentration and timer endings, Move membership lists, zone moves, `ForcedMoveResult` membership with recorded results (Q7), `zone_contains` hook, schema v9; Spirit Guardians, Moonbeam, Spike Growth, Grease, Web. Spec: `docs/proposals/ZONES_AURAS.md` (#113, merged). **PR 2 done in rules** (7.2m): Silence, Stinking Cloud, Wall of Fire (line / ring, chosen side, `zone_side_contains`), Fog Cloud / Darkness flags, Moonbeam's shapechanger disadvantage, the evil caster's necrotic, D7 feature-aura fields (`add_feature_aura`, `aura_modifier` saves, `aura_immunity`, `needs_conscious_owner`). Still open: the sim contract (Engine Core), loader keys (T&C), paladin aura content (D7) | Spirit Guardians, Moonbeam, Web, Spike Growth, Grease, Silence, Stinking Cloud, Wall of Fire done; Entangle, Rust Metal later | L (PR 1 + 2 done) |
| 12 | R-G12 | **PR 1 (transforms) done in rules** (7.2q): Wild Shape, Polymorph, shapechanger forms, overflow, D15 teardown, events 50 / 51 (schema v10), 0xAF / 0xBD. **PR 2 (summons)** (7.2r): Conjure Animals, Find Familiar, Animate Dead, events 48 / 49 / 52, 0xBC. **PR 3** (7.2s): Spiritual Weapon token, Pact of the Chain attack trade, 0xBE. Open: Conjure Elemental hostile turn (D5), sim + content wiring. | Druid, Warlock Chain, werewolf | L |
| deferred | T&C-G11 | Legendary actions and lair actions (Legendary Resistance is already a pool, #91) | CR 10+ monsters, unicorn | L |

Also open but small: Hide opposed by passive Perception (R-G1 / T&C-G16, sim; rules now has `passive_perception`). **Done in rules** (7.2j): Haste / Slow as full spells and the monster one-offs (Life Drain max-HP reduction, Reckless, Aggressive's bonus-action move, Petrifying Gaze progression); still open from that list: Slow's spell-casting delay, Haste's Use an Object, ability drain, and the gaze trigger (rules and sim wiring done in 7.2p; schema key pending).

## 12. Source notes and verification

- **SRD source**: SRD 5.1 (CC-BY-4.0) as mirrored by <https://github.com/5e-bits/5e-database> at commit `a6212be` (`src/2014/en`: Spells, Monsters, Features, Subclasses, Classes, Conditions, Equipment, Weapon-Properties, Rule-Sections, Traits). The published PDF was **not** re-read; the first draft's statement that it was checked against the PDF was inaccurate and is removed.
- **Verified against the mirror in this revision**: spell names, levels, casting times and the 0-3 spell list (all 169); class and subclass features through level 5, fighting styles per class, eldritch invocations; conditions (including Petrified's poison/disease immunity); weapon properties and special rules (lance, net); armor stealth disadvantage and Strength requirements; cover bonuses and crawling cost; Hit Dice and surprise rules; every monster named in section 9 (existence, CR, and the trait or action cited), plus population counts (monsters, CR bands, multiattack, legendary, weapons, armor).
- **UNVERIFIED** (not checked against SRD text): Cloud of Daggers is **not** in the mirror (so not listed; a PDF check could confirm whether SRD 5.1 omits it); the exact SRD wording of Hide/Search/Influence as actions (these are 2024-style action names, kept as engine concepts only); the per-row sizes and owners in section 11 (estimates, not SRD facts); gear (116) and tools (31) beyond the category counts.
- **Code claims** ("current code" statements) were verified on `main` at `6012567`: `engine/rules` (types, encounter, spells), `engine/rules/content` (spell loader), `engine/core/sim` (content loader, features, combat), `schemas/{common,character,monster,spell}`, and `tools/validate`. The `Hide` row was re-verified in this revision (no passive-Perception comparison exists).
- Where an example monster list is inexact, treat it as illustrative; Tools & Content owns the authoritative per-monster classification (`srd_content_inventory.md`, `gap_crosswalk.md`).
