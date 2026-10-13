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
every type.

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
