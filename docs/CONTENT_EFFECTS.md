# Content contract: feature effects, damage, range, tile flags

Part of the content compatibility contract (ARCHITECTURE §3). Machine-checked by
`schemas/common/v1.schema.json` (`$defs/effect`, `effect_cost`, `effect_condition`) and
`tools/validate/validate_packs.py`. The engine side is `eng::sim::FeatureRegistry` (`sim/features.hpp`).

## 0. Version gating: when effects are applied

| document | effects | validator |
|---|---|---|
| `character@3`, `monster@3` | **applied by the engine** | kind-contract violations are errors; `cost: reaction` is an error (reserved) |
| `character@1` / `@2`, `monster@1` / `@2` | **inert descriptions**: the engine's @2->@3 migrations mark each effect `"inert": true` and never apply it, because those stat blocks may bake the numbers in (fl1's fighter writes `1d8+5` with Dueling included) | kind-contract findings are warnings only |

Effects are applied by the engine in @3 files, so the numbers an effect adds must **not** be
baked into the stat block. Dueling's +2 is not in the fighter's longsword damage (write `1d8+3`,
not `1d8+5`), and a monster `damage_bonus` is not folded into its action damage. To activate
effects on an old file, move it to @3 and de-bake its numbers in the same change. The sample
goblin is `monster@3`: Nimble Escape (`bonus_action_grant`) has no number to bake.

`inert` is reserved for the engine migrations. Authoring it is an error in every version of
every type, on effects and (at `@4`) on `reactions[]` / `bonus_actions[]` entries. The `@4`
schemas accept it so an in-memory migration result validates; a file on disk that carries it fails.

### 0.1 `@4` (docs/proposals/VERSION_BUMP_BATCH.md, row 2: schemas and validator only)

| document | effects | validator |
|---|---|---|
| `character@4`, `monster@4` | the `@3` kinds unchanged, plus `on_event`, `apply_modifier`, `grant_action`, `heal_pool`, `gaze` (closed arms of `common@2 $defs/effect`; `gaze`: VERSION_BUMP_BATCH 7.2p, `docs/CONTENT_CONVENTIONS.md` section 11); `cost: reaction` is legal | `checks_v4.TAG_TABLE` row; cross-reference checks 1-18 of the proposal's section 8.3 |
| a `3 -> 4` migrated file | effects of the frozen new-kind list (`on_event`, `apply_modifier`, `grant_action`, `heal_pool`, `save_effect`, `reaction`) carry `"inert": true`; parameters are **not** validated | accepted in memory; an authored `inert` is an error |
| `monster@5` (batch 3) | monster-local trait kinds `weapon_extra_dice`, `save_advantage`, `aura` (ghast Stench: `{radius_ft, on: start_turn, save {ability, dc}, condition, duration: until_start_of_next_turn, success_immunity_hours?}`, a creature-anchored aura from the start of combat; sim features.cpp `apply_aura`), and `keen_senses` (`{senses: [sight\|hearing\|smell]}`; CONTENT_CONVENTIONS §30; sim features.cpp `apply_keen_senses`) | `checks_v4.check_aura` and `check_keen_senses` mirror the loader refusals |

The engine does not apply any `@4`-only construct yet (loader: row 3); until then these documents are
validated, not played. `@4` packs and `item@1` documents need `api_level >= 2` in the manifest.

## 1. Feature effects

Character `features[]` and monster `traits[]` entries may carry a machine-readable `effect`
object tagged by `kind`. The engine maps `kind` to a registered hook and never matches feature
names or ids, so renaming a feature does not change what it does.

- **Known kinds** (listed below) have their fields validated against `common@1 $defs/effect`
  (`character@3` and `monster@3` use it directly via `$defs/trait_active`). A malformed known effect is an
  **error** in @3 documents, and the engine also rejects it at load. In @1/@2 documents it is
  only a **warning** (see §0).
- **Unknown kinds** are allowed so content can run ahead of the engine. The validator prints a
  **warning** and the engine lists them in `Archetype::unsupported_features` and ignores them.
- The effect object is **open**: extra fields are ignored. This keeps `common@1` append-only;
  adding an optional field or a new kind is additive and needs no schema bump.

Common field, valid on any kind:

| field | type | meaning |
|---|---|---|
| `kind` | string `^[a-z0-9_]+$` | hook id (required) |
| `cost` | `"action"` \| `"bonus_action"` | action-economy cost of an *active* effect; omitted = `action`. `"reaction"` is **reserved**: the schema enum lists it, but it is an error in @3 documents and the engine rejects it. |

### `damage_bonus` (static)

Applied once when the archetype is built: adds `amount` to the damage bonus of every attack
action that matches `condition`.

| field | type | required | meaning |
|---|---|---|---|
| `amount` | integer (int16) | yes | flat bonus added to the damage dice's bonus |
| `condition` | condition id (§1.1) | no | which attacks qualify; omitted = all attacks |

Example (Fighting Style: Dueling): `{ "kind": "damage_bonus", "amount": 2, "condition": "one_handed_melee_no_other_weapon" }`

### `bonus_action_grant` (static)

| field | type | required | meaning |
|---|---|---|---|
| `actions` | array of `"dash"` \| `"disengage"` \| `"hide"` \| `"dodge"` | yes | actions the creature may take as a bonus action |

Example (Nimble Escape): `{ "kind": "bonus_action_grant", "actions": ["disengage", "hide"] }`

### `self_heal` (active, rules5e `UseFeature`)

| field | type | required | meaning |
|---|---|---|---|
| `dice` | dice string `NdS[+/-B]` | yes | healing roll |
| `plus` | integer, `"level"` or `"<class>_level"` | no | flat bonus: the integer itself, total level, or that class's level (e.g. `fighter_level`) |
| `uses` | integer 0..255 | no | uses per recharge; 0 or omitted = unlimited |
| `recharge` | `"short_rest"` \| `"long_rest"` | no | when uses refill; omitted = never (any other value is an error; the engine also rejects it) |
| `cost` | see above | no | default `action` |

Example (Second Wind): `{ "kind": "self_heal", "dice": "1d10", "plus": "fighter_level", "uses": 1, "recharge": "short_rest", "cost": "bonus_action" }`

### `damage_mitigation` (static)

Qualified resistances and immunities, for SRD text like "bludgeoning, piercing, and slashing from nonmagical attacks". Unqualified defenses stay in `damage_resistances` / `damage_immunities`. Applied to `Combatant::resist_qualified` / `immune_qualified` when the archetype is built; matched against `Attack::qualifiers` (rules5e #47).

| field | type | required | meaning |
|---|---|---|---|
| `resistances` | object | one of the two | qualifier -> damage types resisted |
| `immunities` | object | one of the two | qualifier -> damage types immune |

Qualifier keys: `nonmagical` (any nonmagical source), `nonmagical_not_silvered`, `nonmagical_not_adamantine`. Each value is a unique array of damage types. Spell damage is always magical.

Example (werewolf): `{ "kind": "damage_mitigation", "immunities": { "nonmagical_not_silvered": ["bludgeoning", "piercing", "slashing"] } }`

### `magic_weapons` (static)

No fields. Every attack of the creature counts as magical (`kAttackMagical`), so qualified resistance / immunity does not apply to it (SRD Magic Weapons, Ki-Empowered Strikes). A single weapon is marked with the action's `qualifiers` array (`magical`, `silvered`, `adamantine`, monster@3 / character@3; inert before @3, marker `attack_qualifiers`).

Example: `{ "kind": "magic_weapons" }`

### `multiattack` (static)

SRD Multiattack as declared sequences (rules5e #48: `Combatant::multiattacks`). Each option is a closed object (`id`, `attacks` only; unknown keys fail validation) holding an ordered list of the creature's own attack action ids (`actions[].id` of attack kinds) made with one Attack action. The Attack request starts one with `ActionRequest::multiattack = 1 + option index` (`Combat::multiattack_option(id, option_id)` finds it) and `attack_index` = the sequence's first attack; each further attack follows the declared order, with reactions / Ready / movement allowed in between.

| field | type | required | meaning |
|---|---|---|---|
| `options` | array of `{ id, attacks[] }` | one of the two | option id (unique per creature) and ordered action ids (1..255, repeats allowed) |
| `attacks` | array of action ids | one of the two | shorthand for a single option named `multiattack` |

The engine rejects an action id that names no attack action and duplicate option ids. Option order is declaration order. The sim AI (`next_command`) opens the first option made only of melee attacks and finishes it; options with a ranged attack are available to Commands but the AI never picks them.

Example (owlbear): `{ "kind": "multiattack", "options": [ { "id": "claw_beak", "attacks": ["beak", "claws"] } ] }`

### `reroll_ones` (static, R-G7)

Halfling Lucky: a natural 1 on the listed d20 rolls is rerolled once and kept (`Combatant::reroll_ones`).

| field | type | required | meaning |
|---|---|---|---|
| `rolls` | non-empty array of `attack` / `check` / `save` | no | which rolls; omitted = all three. Several declarations OR together |

Example: `{ "kind": "reroll_ones" }`, `{ "kind": "reroll_ones", "rolls": ["save"] }`

### `great_weapon_fighting` (static, R-G7)

No fields. Weapon damage dice of a `two_handed` melee attack, or a `versatile` melee attack held in two hands (v0 wield model:
`docs/CONTENT_CONVENTIONS.md` section 3.3), showing 1 or 2 are rerolled once.

### `evasion` (static, R-G7)

No fields. A Dex save against an effect that deals half on a success deals none on a success and half on a failure.

### `pack_tactics` (static, T&C-G7)

No fields. SRD Pack Tactics: advantage on the creature's attack rolls against a creature when at least one of its allies that
is not incapacitated is within 5 ft of it (sim #127: rules5e `RollTrait {Attack, Advantage, kWhenAllyNearTarget}`). A second
declaration adds nothing (the validator warns).

Example: `{ "kind": "pack_tactics" }`

### `keen_senses` (static, monster@5)

`senses` (required): a non-empty unique list of `sight`, `hearing`, `smell`. SRD Keen Hearing and Smell and the other named-sense traits (CONTENT_CONVENTIONS §30). The loader ORs the bits into `Combatant::keen_senses`. Advantage on a Wisdom (Perception) check that relies on a listed sense; the printed passive is not increased. A second declaration that adds no new sense does nothing (the validator warns). Refused below monster@5 and on a character. No `SIM_EVENTS` tag.

Example: `{ "kind": "keen_senses", "senses": ["hearing", "smell"] }`

### `aggressive` (static, T&C-G7)

No fields. SRD orc Aggressive (sim #129: rules5e `Combatant::aggressive`): a bonus-action Dash whose extra movement must take the
creature toward a hostile it can see (`Combat::aggressive_step_ok`); the built-in AI uses it when the Dash lets it reach a target.
A second declaration adds nothing (the validator warns).

### `reckless_attack` (static, T&C-G7)

No fields. Barbarian Reckless Attack / SRD berserker Reckless (sim #129: rules5e `Combatant::reckless_attack`): declared by the
caller with `ActionRequest::reckless` on the turn's first attack (advantage on Strength melee weapon attacks that turn; attacks
against the creature have advantage until its next turn). The built-in AI never declares it. A second declaration adds nothing
(the validator warns).

### `metamagic` (static, R-G14)

Fields: `options` (required, non-empty array of `careful`, `distant`, `empowered`, `extended`, `heightened`, `quickened`,
`subtle`, `twinned`) and `pool` (required, the `resources[].id` of the sorcery-point pool). Sorcerer Metamagic (sim #143: rules5e
`Combatant::metamagic_known` / `sorcery_pool`): the caller picks at most one known option per cast with
`ActionRequest::metamagic` and its sorcery-point cost is spent from `pool` (Distant also widens an aimed area's range). `pool` must
be a limited `resources[]` pool of the document (load error otherwise, so only `@4` documents can declare it). Several declarations
add options and must name the same pool (load error otherwise); a repeated option adds nothing (the validator warns).

Example: `{ "kind": "metamagic", "options": ["quickened", "twinned"], "pool": "sorcery_points" }`

### `sculpt_spells` (static, R-G14)

No fields. Evocation Wizard Sculpt Spells (sim #143: rules5e `Combatant::sculpt_spells`): when the creature casts an evocation
spell (spell@1 `school: "evocation"`), the caller may name creatures spared from its area with `ActionRequest::spared`. The
built-in AI does not. A second declaration, or any field, adds nothing (the validator warns).

Example: `{ "kind": "sculpt_spells" }`

### `font_of_magic` (static, VERSION_BUMP_BATCH 7.2o)

Field: `pool` (required, the `resources[].id` of the sorcery-point pool, the same pool as `metamagic.pool`). Sorcerer Font of Magic
(sim #152 `apply_font_of_magic`): the sim resolves `pool` to max = start = the sorcerer level (sum of the `classes[]` entries with
`class: sorcerer`), recharge `long_rest`, registering it when `resources[]` lacks it, and points `Combatant::sorcery_pool` at it even
without Metamagic; the creature may then use Flexible Casting (rules5e `FlexibleCasting`, a bonus action). Load errors (validator
errors, loader wording): sorcerer level below 2 (`font_of_magic needs sorcerer level 2+ (sorcerer level N)`), an unlimited pool
(`font_of_magic pool '<pool>' is unlimited`), and a metamagic declaration naming a different pool, in either order
(`font_of_magic and metamagic name different pools` / `metamagic declarations name different pools`). Validator warnings: the
pool's `max` is not the sorcerer level, its `recharge` is not `long_rest` or its `start` differs (the sim overrides all three), or
`resources[]` lacks it (declare it so the document shows the pool).

Example: `{ "kind": "font_of_magic", "pool": "sorcery_points" }` with `resources: [{ "id": "sorcery_points", "max": 3, "recharge": "long_rest" }]` at sorcerer level 3.

These nine are open kinds (live at `@3` and `@4`); see `docs/CONTENT_CONVENTIONS.md` section 3.

### `wild_shape` (character feature, common@2, R-G12; loaded since EC #176)

Closed common@2 arm `$defs/wild_shape_effect` (`@4` only). Fields: `pool` (required, a `resources[]` pool recharging on
`short_rest`, max 2), `cost` (default `action`), `class` (default `druid`), `forms` (required, 1..255 unique monster content ids,
each a beast, resolved across the pack set including dependency packs; `form_archetype` = 1 + index), `max_cr_by_level` (required,
`[{level 1..20, max_cr}]`, levels strictly ascending; CRs are monster@4 `challenge.rating` strings compared as CR*8),
`no_fly_below` (1..20, default 8), `no_swim_below` (1..20, default 4). No duration key: the engine computes it (600 * (class
level / 2) rounds). One declaration per document. Loader: Engine Core `build_form` / `feature_spec` (VERSION_BUMP_BATCH 7.2q).

Example: `{ "kind": "wild_shape", "pool": "wild_shape", "forms": ["srd51.monster.wolf"], "max_cr_by_level": [{"level": 2, "max_cr": "1/4"}] }`

### `shapechanger` (monster or character trait, common@2, R-G12; loaded since EC #176)

Closed common@2 arm `$defs/shapechanger_effect`. Fields: `cost` (default `action`), `forms` (required, 1..255 of `{id, name,
size?, armor_class?, speed? (replaces the whole object), actions? (unique ids of this document's own actions[]; [] = none),
creature_type?}`). Requires the R-G11 creature fact `shapechanger: true` (#141). One HP pool; one declaration per document.
Loader: Engine Core `build_form`.

Trait flag `requires_humanoid_form` (common@2 `$defs/trait`, optional boolean, default false): the feature is refused in a Wild
Shape form. Error on the `wild_shape` feature itself; warning when the document has no `wild_shape`.

Both kinds are loaded at `@4` since EC #176 (the sim's `apply_wild_shape` / `apply_shapechanger` read exactly these keys:
an unknown effect key, an unknown form key such as `speed_walk` (use `speed {walk}`), or `requires_humanoid_form` on the
`wild_shape` feature itself are load errors; `requires_humanoid_form` is read from the feature object, not the effect), so the
validator no longer gives them the unknown-kind warning at `@4`; below `@4` they keep it. Details:
`docs/CONTENT_CONVENTIONS.md` section 9.

### `extra_attack` (character feature, common@2, EC #179)

Closed common@2 arm `$defs/extra_attack_effect` (`@4` only): `{ "kind": "extra_attack", "attacks_per_action": 2 }`.
`attacks_per_action` is required (integer 2..4, no default): the Attack action makes that many weapon attacks
(`Combatant::attacks_per_action`; grapple / shove may replace one; moving between them is allowed). Several declarations:
the highest wins, they do not stack (validator warning). Characters only. Loader errors (sim `features.cpp`
`apply_extra_attack`, prefixed with the document id; the validator prints the same text):

* `<id>: extra_attack: unknown key '<k>'`
* `<id>: extra_attack: only valid on characters`
* `<id>: extra_attack: attacks_per_action must be an integer 2..4`

In a `character@3` (or older) file the kind is inert: the hook's `@4` gate lists it in `inert_features`, and the frozen
3 -> 4 migration kind list cannot carry it. The validator gives the unknown-kind warning there with that hint; declare
`character@4` to use it. SRD fighter 5 (`srd51.character.fighter_level_5`) uses it with `attacks_per_action: 2`.

### `slot_recovery` (open kind, VERSION_BUMP_BATCH 7.2n, sim #148)

Arcane Recovery (wizard) and Natural Recovery (druid, Circle of the Land). Loaded by sim `features.cpp` `apply_slot_recovery`
(#148) into a rules5e `SlotRecoverySpec`; an open kind (live at `@3` and `@4`, `checks_v4.OPEN_EFFECT_KINDS_RG7`). The
validator mirrors the loader's errors: see `docs/CONTENT_CONVENTIONS.md` section 10.

| field | type | meaning |
|---|---|---|
| `class` | `"wizard"` \| `"druid"` (required) | whose level sets the budget; must name a `classes[]` entry (load error `slot_recovery class '...' is not in classes[]`); one declaration per class |
| `max_slot_level` | integer 1..9, default 5 | no recovered slot above this level (SRD: none 6th or higher) |
| `uses` | integer 1..255, default 1 | uses per long rest; a short rest does not reset it ("once per day") |

The budget, combined slot levels <= ceil(class level / 2), is derived from `classes[]` by the rules (`slot_recovery_budget`); it is
not a field. Which slots come back is the caller's choice at short-rest time (sim AI / UI), not content.

### 1.1 Condition ids

Named predicates over an attack action, registered in the FeatureRegistry:

| id | true when |
|---|---|
| `one_handed_melee_no_other_weapon` | `kind` is `melee_weapon_attack` and `properties` lacks `two_handed` (v0 wield model: sim has no equipment or wield state yet) |
| `melee_weapon` | `kind` is `melee_weapon_attack` |
| `ranged_weapon` | `kind` is `ranged_weapon_attack` |

Adding a kind or condition: register the hook or predicate in the engine, add a branch or enum
value to `common@1` (additive), add it to `KNOWN_EFFECT_KINDS` in the validator, and document it
here. Negative cases live in `tests/validate/effect_cases/`.

## 2. Weapon damage is base damage

`actions[].damage[].dice` is the **base** damage: weapon die + ability modifier (+ magic item
bonus). Never fold feature bonuses into it. The engine applies them from effects. Example: a
STR 16 fighter with a longsword and Dueling writes `1d8+3`, and the engine adds Dueling's +2,
which gives 1d8+5 in play. `to_hit` is the full attack bonus (ability modifier + proficiency + magic).

## 3. Weapon range

`actions[].range` is `[normal, long]` in feet, both multiples of 5 (SRD "range 80/320" is
`[80, 320]`). Attacks beyond normal and up to long range roll with disadvantage; beyond long
range they are illegal. Melee attacks use `reach` (feet) instead.

## 4. Tile flags are independent

Chunk palette flags (u8): bit0 `blocks_move`, bit1 `blocks_sight`, bit2 `difficult`, bit3
`water`; bit 4 `climbable` (climb terrain, enterable by climbing even with `blocks_move`), bit 5 `open_air` (no footing: only flyers enter); bits 6-7 are reserved and must be 0. Any combination is valid. In particular,
`blocks_sight` without `blocks_move` is a valid, intended tile (curtains, fog, tall grass),
as is `blocks_move` without `blocks_sight` (glass, low fences). Line of sight checks only
`blocks_sight`, and pathing checks only `blocks_move`.

See `docs/CONTENT_CONVENTIONS.md` for the timed-modifier properties the sim compiles (`consume`, `speed_sub`, `ac_floor` / `ac_floor_dex`, `per_turn`, `when`) and the bonus-attack feature-id conventions, and for the R-G7 reactions (`roll_attack` / `subtract_roll`) and kinds.

## 5. spell@1 effect kinds

`schemas/spell/v1.schema.json` `$defs/effect` (closed arms; cross-field rules in `tools/validate` `check_spell`, mirrored by
`rules5e_content`). Each maps to a `rules5e::SpellEffectKind`.

| kind | rules5e | first effect only | notes |
|---|---|---|---|
| `spell_attack`, `save`, `heal`, `auto_hit`, `apply_condition`, `apply_modifier` | SpellAttack, Save, Heal, AutoHit, ApplyCondition, ApplyModifier | no (riders take `applies_to`) | |
| `ac_bonus`, `counter` | AcBonus, Counter | must be the only effect | reactions |
| `hp_pool` | HpPool | yes | `dice`, `condition`; needs a rounds / minutes / hours duration |
| `dispel` | Dispel | yes | optional `check {ability: spellcasting, dc_base, plus: spell_level}` |
| `revive` | Revive | yes | optional `within_rounds` |
| `end_condition` | EndCondition | yes | `conditions[]` |
| `delayed_damage` | DelayedDamage | no (Acid Arrow rider) | `damage`, `expiry` |
| `sustained_damage` | SustainedDamage | yes | `damage`; needs a rounds / minutes / hours duration; no `expiry` |
| `forced_move` | ForcedMove | no (Thunderwave / Repelling Blast rider, `applies_to`) | **`feet`** (positive multiple of 5), `mode` push \| pull, `up_to`, `max_size`, `provokes_oa` (false only); R-G9, sim #144 |
| `teleport` | Teleport | yes | **`max_feet`** (positive multiple of 5), `needs_sight` (default true); self (Misty Step); R-G9, sim #144 |
| `polymorph` | Polymorph (R-G12, 7.2q; parsed by the rules spell loader since #171) | yes (`polymorph: primary effect only`) | `forms[]` (beast content ids), `max_cr` (`target_level` or a CR string), `save {ability}` (default wis); one target, concentration; `docs/CONTENT_CONVENTIONS.md` section 9 |
| `zone` | Zone (`SpellDef::zone`, `ZoneSpec`) | yes (never an extra effect) | `zone {shape (sphere, cylinder, cube, line), size_ft, height_ft, length_ft, anchor, flags[], move, chooses_excluded, needs_conscious_owner, upcast_size_ft, ring_size_ft, sided, side_ft, aura_modifier, aura_bonus_ability, aura_immunity[], aura_allies_only, triggers[]}` (rows add `region`, `loses_action`, `auto_success_damage[]` / `_conditions[]` / `_no_breath`, `shapechanger_disadvantage`, `evil_damage_type`; rules PR 2 keys, section 4.6); needs a lasting duration (the zone's expiry timer); parsed by the loader since R-G11 rules PR 2 (#122); `docs/CONTENT_CONVENTIONS.md` section 4 |
| `descriptive` | Descriptive (`SpellEffectKind::Descriptive`) | must be the only effect | exactly `{"kind":"descriptive"}` (no other keys). The spell loads and stays on the spell list. `legal_actions` never offers it and `cast_spell` refuses it. No sim event. `docs/CONTENT_CONVENTIONS.md` section 28 |

Details and loader messages: `docs/CONTENT_CONVENTIONS.md` section 4 (R-G11 zones), section 6 (R-G15 kinds, sim #120), section 6.1 (R-G9 forced_move / teleport, sim #144), section 7 (priced material
components, character@4 `equipment[].value_gp`) and section 28 (text-only `descriptive`).

## on_event predicates and usage (mirrors sim #158)

| field | values | loader behaviour |
|---|---|---|
| `when[].kind` | `target_surprised`, `ally_near_target`, `weapon_attack` (no other keys) | compiled |
| `when[].kind` | `damage_at_most` with `amount` 1..32767 | `bad damage_at_most amount` otherwise |
| `usage.kind` | `at_will`; `per_rest` (`uses` 1..255, `rest` short/long); `per_day` (`uses` 1..255) | `bad uses` / `bad rest` |
| `usage.kind` | `pool` (`pool` must be a declared resource, `cost` 1..255) | `bad usage pool` (validator: cost > 255 is an on_event error; the schema keeps the u16 reaction cost) |
| `usage.kind` | `recharge` (`min` 2..6) | compiled since sim #170 (a d6 rolled at the owner's turn start); before #170 it was listed unsupported |

`weapon_attack` holds for any weapon attack; `weapon {is}` stays for melee/ranged-specific tests.

## sim #170 additions (EC deferred monsters)

| key | shape | loader behaviour |
|---|---|---|
| `when[].kind` | `target_tag` / `not_target_tag` with `tag` (non-empty) | `bad tag` otherwise. Content tags (monster `tags[]`, character `race`) are case-folded and the predicate tag is not, so the validator warns on a tag that isn't lower case. A race ending in `elf` adds `elf`; `half_elf` does not |
| `on_hit[].when` | array of predicates on any on_hit rider of an attack action | every predicate must hold. A rider `when` inside on_event `effects[]` is ignored (warning) |
| action `kind: attack_effect` | `id`, `attack` ranged/melee (default ranged), `to_hit` -20..30 and non-zero, `range_ft` (>= 5, default 5), `apply[]` with `escape {ability: str, dc 1..30}`, `usage` | an apply[] entry without `escape` makes the loader list the action as unsupported (warning). There is no damage key. Shares the check-4 id space with `save_effect` |
| monster `tags[]` | strings | read by `target_tag` (already in `monster@4`) |

## sim #172 / #174 additions (swarm of rats, giant spider follow-up)

Additive optional keys in `common@2` (no version bump). Validator: `checks_v4.check_sim172_174` mirrors the loader refusals the schema cannot express.

| key | shape | loader behaviour |
|---|---|---|
| monster `tags[]` value `swarm` | string | SRD Swarm trait (#172): shares spaces both ways, cannot regain HP or gain temp HP. The trait itself stays descriptive |
| weapon action `half_hp_damage` | dice with at least one die | replaces `damage[0]`'s dice while the attacker has half its HP or fewer (base max HP); `<id>: bad half_hp_damage in action <aid>` otherwise. Not allowed on `save_effect` / `attack_effect` |
| `attack_effect` `long_range_ft` | integer > `range_ft`, ranged only | legal out to long range, disadvantage beyond `range_ft`; `bad long_range_ft (ranged, > range_ft)` otherwise |
| `attack_effect` `apply[].object` | `{ac 1..30, hp 1..1000, vulnerable[], immune[]}` (damage types) | the restraint is a destructible object (`ActionRequest::special` 6); at 0 hp the condition ends |
| `save_effect` / `save_condition` `zero_hp` | `{condition, while, duration {kind: rounds, n 1..65535}}` | when the damage drops the target to 0 HP: stable, `condition` for n rounds, `while` as long as that lasts. `zero_hp needs damage`; `bad zero_hp condition / while` when they are equal; `bad zero_hp duration (rounds 1..65535)` |

## Rules #183 additions (7.2u, next-roll spell riders)

Additive optional values in `common@2` (no version bump; `docs/proposals/VERSION_BUMP_BATCH.md` §7.2u). Loader: `rules5e_content` `parse_modifier` / `parse_predicate`. Shipped by `srd51.core` Guiding Bolt, Vicious Mockery, Shocking Grasp and Chill Touch.

| key | value | loader / engine behaviour |
|---|---|---|
| modifier `duration.kind` | `until_end_of_caster_next_turn` | `ModifierSpec::expiry` 3 (`Expiry::EndOfCasterNextTurn`): ends at the end of the caster's next turn. Applied during the caster's own turn, that turn does not count (Guiding Bolt, Chill Touch) |
| modifier `duration.kind` | `until_start_of_target_turn` | expiry 4 (`Expiry::StartOfTargetNextTurn`): ends when the affected creature's next turn starts (Shocking Grasp) |
| modifier `stat` | `no_reactions` | `ModStat::NoReactions` marker: `may_react` refuses, so no reaction window opens and no reaction is spent |
| modifier `stat` | `no_healing` | `ModStat::NoHealing` marker: blocks every hit-point regain (heal spells, Heal request, regeneration, `heal_self` riders, pool / feature heal dice, a natural-20 death save). Temporary HP is unaffected |
| `no_reactions` / `no_healing` arm | closed | required `stat`, `duration`; optional `applies_to`, `stacks`, `consume`. Any other key (`bonus`, `mode`, `when`, ...) is a schema error |
| predicate `kind` | `subject_is_caster` | kind only: the roll's subject (the attack's target) is the modifier's caster |
| predicate `kind` | `target_below_max_hp` | kind only (7.2y, same arm as `target_surprised`): the creature hit is below its hit point maximum, read before the hit's damage. For once-per-turn weapon-hit extra damage (Colossus Slayer: `on_event` hit, `limit: once_per_turn`, `when: [weapon_attack, target_below_max_hp]`) |
| predicate `kind` | `owner_creature_type` with `creature_types[]` | the modifier's owner (the creature carrying it) has one of the types; `bad creature_types` otherwise |

`modifiers_digest` folds all of it under the existing tag 0xA2; no new tag, no log field. Not modelled: Shocking Grasp's advantage against metal armor (no armor material exists) and Vicious Mockery's "if the target can hear you".

## Engine Core loader durations and temp HP (fix/loader-duration-temphp)

Additive optional values in `common@2` (no version bump; older content loads and hashes unchanged). Loader: sim `content.cpp` (`compile_save_action`, `apply_condition`, `compile_rider`) and `rules5e_content` `parse_predicate`. Engine: flagged rules5e edits in `encounter.cpp`.

| key | value | loader / engine behaviour |
|---|---|---|
| condition `apply[].duration.kind` (save_effect, save_condition, area_save, apply_condition) | `until_end_of_caster_next_turn` | `SpellDef::expiry` 3: the condition ends at the end of the applier's next turn (its own current turn does not count). Was refused as unsupported |
| condition `apply[].duration.kind` | `until_start_of_target_turn` | expiry 4: the condition ends as the affected creature's next turn starts. Was refused as unsupported |
| condition `apply[].duration.kind` | `until_cured` | batch 3 (death dog, monster docs only): no timer, no repeat save; applied untracked and ended only by a cure (Lesser Restoration EndCondition). Only blinded, deafened, paralyzed, poisoned; anything else, a repeat_save or an escape is a load error |
| multiattack option `count_dice` | `"1d4"` | batch 3 (violet fungus, monster@5 only): the option names exactly one attack, made a rolled number of times; rolled from the encounter RNG when the multiattack starts (no event; the count shows as the attacks made). Bonus, zero or a second attack id is a load error |
| monster@5 action kind | `teleport` `{feet, usage?, with_attack?}` | batch 3 (blink dog): a synthetic Teleport SpellDef (not a spell, magical, self; sim destination checks, Recharge via usage). `with_attack` (an attack action id) is made after the teleport without another action ("Before or after teleporting, the dog can make one bite attack": teleport-then-bite only; bite-then-teleport deferred). The AI does not pick it (destination not guessed) |
| rider `amount_from` (heal_self / temp_hp only) | `{ability, class?, min?}` | amount = `ability` modifier + level in `class` (omitted: no level term), at least `min` (default 1), computed at load. One of `dice` / `amount` / `amount_from`. Dark One's Blessing: `{"kind": "on_event", "on": "reduce_to_zero", "when": [{"kind": "subject_hostile"}], "effects": [{"kind": "temp_hp", "amount_from": {"ability": "cha", "class": "warlock"}}]}` |
| predicate `kind` | `subject_hostile` | kind only: the event's subject is on another team than the owner |

`heal_self` / `temp_hp` riders of `on_event` triggers fire on every event but `hit` / `crit` (rules5e `run_event_triggers`); temporary HP never stacks: the larger pool is kept. Still refused for conditions (no rules5e expiry): `until_start_of_caster_turn`, `until_end_of_target_turn`, `concentration`, `until_rest`. Validator: `checks_v4.check_refused_condition_durations` uses the loader's wording. **Error** on riders, where the sim refuses to load the whole document: `save_condition uses an unsupported duration`, `unsupported apply_condition duration`, `area_save uses an unsupported usage or condition duration`. **Warning** on `save_effect` / `attack_effect` actions, which still load with that one action listed and never used: `<id> (save_effect: unsupported usage or condition duration)`. No new hashed state, no new tag.

