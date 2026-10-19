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
| `character@4`, `monster@4` | the `@3` kinds unchanged, plus `on_event`, `apply_modifier`, `grant_action`, `heal_pool` (closed arms of `common@2 $defs/effect`); `cost: reaction` is legal | `checks_v4.TAG_TABLE` row; cross-reference checks 1-18 of the proposal's section 8.3 |
| a `3 -> 4` migrated file | effects of the frozen new-kind list (`on_event`, `apply_modifier`, `grant_action`, `heal_pool`, `save_effect`, `reaction`) carry `"inert": true`; parameters are **not** validated | accepted in memory; an authored `inert` is an error |

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

These four are open kinds (live at `@3` and `@4`); see `docs/CONTENT_CONVENTIONS.md` section 3.

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
`water`; bits 4-7 are reserved and must be 0. Any combination is valid. In particular,
`blocks_sight` without `blocks_move` is a valid, intended tile (curtains, fog, tall grass),
as is `blocks_move` without `blocks_sight` (glass, low fences). Line of sight checks only
`blocks_sight`, and pathing checks only `blocks_move`.

See `docs/CONTENT_CONVENTIONS.md` for the timed-modifier properties the sim compiles (`consume`, `speed_sub`, `ac_floor` / `ac_floor_dex`, `per_turn`, `when`) and the bonus-attack feature-id conventions, and for the R-G7 reactions (`roll_attack` / `subtract_roll`) and kinds.
