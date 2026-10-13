# Content contract: feature effects, damage, range, tile flags

Part of the content compatibility contract (ARCHITECTURE §3). Machine-checked by
`schemas/common/v1.schema.json` (`$defs/effect`, `effect_cost`, `effect_condition`) and
`tools/validate/validate_packs.py`. The engine side is `eng::sim::FeatureRegistry` (`sim/features.hpp`).

## 1. Feature effects

Character `features[]` and monster `traits[]` entries may carry a machine-readable `effect`
object tagged by `kind`. The engine maps `kind` to a registered hook and never matches feature
names or ids, so renaming a feature does not change what it does.

- **Known kinds** (listed below) have their fields validated by the schema. A malformed known
  effect is a validation **error**, and the engine also rejects it at load.
- **Unknown kinds** are allowed so content can run ahead of the engine. The validator prints a
  **warning** and the engine lists them in `Archetype::unsupported_features` and ignores them.
- The effect object is **open**: extra fields are ignored. This keeps `common@1` append-only;
  adding an optional field or a new kind is additive and needs no schema bump.

Common field, valid on any kind:

| field | type | meaning |
|---|---|---|
| `kind` | string `^[a-z0-9_]+$` | hook id (required) |
| `cost` | `"action"` \| `"bonus_action"` \| `"reaction"` | action-economy cost of an *active* effect; omitted = `action`. Engine v0 implements `action` and `bonus_action`; `reaction` is reserved (engine v0 rejects it on `self_heal`). |

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
| `recharge` | `"short_rest"` \| `"long_rest"` | no | when uses refill; omitted = never |
| `cost` | see above | no | default `action` |

Example (Second Wind): `{ "kind": "self_heal", "dice": "1d10", "plus": "fighter_level", "uses": 1, "recharge": "short_rest", "cost": "bonus_action" }`

### 1.1 Condition ids

Named predicates over an attack action, registered in the FeatureRegistry:

| id | true when |
|---|---|
| `one_handed_melee_no_other_weapon` | `kind` is `melee_weapon_attack` and `properties` lacks `two_handed` (v0 wield model: sim has no equipment or wield state yet) |
| `melee_weapon` | `kind` is `melee_weapon_attack` |
| `ranged_weapon` | `kind` is `ranged_weapon_attack` |

Adding a kind or condition: register the hook or predicate in the engine, add a branch or enum
value to `common@1` (additive), add it to `KNOWN_EFFECT_KINDS` in the validator, and document it here.

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
