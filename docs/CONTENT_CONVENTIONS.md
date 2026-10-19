# Content conventions the sim compiles (timed modifiers, bonus-attack options, R-G7 reactions and feature kinds)

Owner: Tools & Content. Source of truth is the C++ loader; this file records what it reads and what the validator checks.
All numbers are integers.

## 1. Timed modifier properties (`common@2 $defs/modifier`, spell@1 `apply_modifier`)

Compiled by `rules5e_content::parse_modifier` (`engine/rules/content/src/spell_loader.cpp`, sim #96, proposal section 6.2b) for
spell `apply_modifier` effects and for feature `apply_modifier` effects (`modifier` / `modifiers[]`, min schema 4).

| property | shape | allowed on `stat` | meaning |
|---|---|---|---|
| `consume` | `next_attack` \| `next_attack_against` \| `until_damaged` | any | spent early (`ModifierSpec::consume` 1 / 2 / 3), combined with any `duration` |
| `speed_sub` | integer 1..255 | `speed` | feet subtracted after `speed_scale` (a speed modifier still needs `speed_scale`; `{"num":1,"den":1}` for a pure subtraction) |
| `ac_floor` | integer 1..40 | `ac` | AC is at least this before flat bonuses; an `ac` modifier needs `bonus` **or** `ac_floor` |
| `ac_floor_dex` | boolean | `ac`, needs `ac_floor` | add the target's Dex modifier to the floor |
| `per_turn` | boolean | `temp_hp` | re-grant `bonus` at the start of each of the target's turns |
| `when` | array of `predicate` (row 9 vocabulary) | `attack_roll`, `ac`, `damage`, `attacks_against` | every predicate must hold |

Spells: `apply_modifier` is a spell@1 effect kind with `modifier` (one, no `modifiers[]`, no `bonus_table`: check 18),
optional `applies_to: all|affected` after the first effect (Guiding Bolt rider).

Versioning: these are **optional additive properties** on the already-live `common@2 modifier` def and an additive spell@1
effect kind. No tag change, no migration, no api_level gate: `docs/CONTENT_COMPAT.md` section 1 gates only fields that were
descriptive or change meaning, and the old schemas rejected every one of these properties, so no existing file can carry
them (`tests/compat` and both shipped packs validate unchanged). Spell@1 gets the kind additively as proposal 1.1 decided.

## 2. Bonus-action attack options (sim #100, proposal 7.2b)

No schema change: the options compile from existing fields, only for documents declared @4 (or undeclared), never for older
files. Conventions (ids are matched literally against `features[]` / `traits[]` ids):

| option | content |
|---|---|
| two-weapon fighting | a **character** with >= 2 `melee_weapon_attack` actions whose `properties` list `light`: one off-hand option per light weapon (a different light weapon than the Attack action used). Off-hand dice drop the ability modifier unless a feature `two_weapon_fighting` exists |
| `martial_arts` | feature; needs a `melee_weapon_attack` action with property `unarmed`. Prerequisite attack: any `unarmed` or `monk` action |
| `flurry_of_blows` | feature; same unarmed action; 2 strikes spending 1 from the pool whose `resources[].id` is **`ki`** |
| `frenzy` | feature; needs a feature with id `rage` (load error otherwise); available while the Rage modifier `<archetype>#rage#m0` is in force, i.e. the `rage` feature's first modifier (`modifier`, or `modifiers[0]`) |

Validator (`tools/validate/checks_v4.py`, @4 monsters and characters):

- error: `martial_arts` / `flurry_of_blows` without an unarmed melee action; `frenzy` without a `rage` feature (the sim refuses to load these).
- warning: `flurry_of_blows` with no `ki` resource (loads, but the option can never be paid; the sim does not check this itself); `frenzy` whose `rage` feature has no live `apply_modifier` effect (the option is never available).
- the `ki` pool counts as referenced for check 12 when `flurry_of_blows` is present.
- nothing is required for two-weapon fighting, `light`, `monk`, or the `rage` / `ki` resource shapes beyond the above.

## 3. R-G7: Cutting Words (`roll_attack` / `subtract_roll`), Lucky, Great Weapon Fighting, Evasion (sim #114)

Compiled by `compile_reactions` (`engine/core/sim/src/content.cpp`) and the feature hooks in `engine/core/sim/src/features.cpp`
(sim #114; rules side #103, proposal section 7.2e).

### 3.1 Reactions: window `roll_attack`, effect `subtract_roll`

```json
{ "id": "cutting_words", "name": "Cutting Words",
  "window": { "kind": "roll_attack" },
  "effect": { "kind": "subtract_roll", "dice": "1d6" },
  "usage":  { "kind": "pool", "pool": "bardic_inspiration", "cost": 1 } }
```

Compiles to `rules5e::Reaction{window AttackRolled, kind SubtractRoll, dice, pool, pool_cost}`. The sim opens the window for an
enemy of the attacker within 60 ft (footprint-aware) that is not Blinded and has line of sight to the attacker; the default decider
takes option 0. Check 9 (`roll_attack` / `subtract_roll` reserved, Q6) is lifted.

| part | loader rule | schema / validator |
|---|---|---|
| `window.kind` | `damaged`, `ally_attacked`, `roll_attack` compile; any other window is listed in `unsupported_features` and skipped | schema enum unchanged (`roll_attack` was already in it); warning when `subtract_roll` sits in a window that is not compiled |
| `effect` on `roll_attack` | only `subtract_roll`: any other kind is a **load error** (`the roll_attack window only takes subtract_roll`) | error |
| `subtract_roll` elsewhere | in `damaged` / `ally_attacked`: **load error** (`subtract_roll needs the roll_attack window`) | error |
| `subtract_roll.dice` | required, `NdS[+/-B]` with N >= 1 (`subtract_roll needs dice`) | schema (`dice` required, `$defs/dice` pattern starts at 1; arm closed to `kind`, `dice`) |
| `usage` | optional; omitted = ungated. Compiled only on `roll_attack` + `subtract_roll` and only as `kind: pool`; any other usage kind (or a usage on any other reaction) makes the reaction listed as unsupported | schema `$defs/usage` unchanged; warning for a non-pool usage on `roll_attack`, or a usage on another `roll_attack` / `subtract_roll` reaction |
| `usage.pool` | must name a `resources[].id` (or a level-unlimited pool, which then gates nothing): **load error** `usage names unknown pool` | check 2 (already covers reaction usages); the pool counts as referenced for check 12 |
| `usage.cost` | integer 1..65535, default 1: **load error** `bad usage cost` | schema `minimum: 1` + integer + float rejection; validator error above 65535 (`(R-G7 reactions)`); check 2 also rejects a cost above the pool's constant `max` (stricter than the loader, which never checks it) |
| `window.source` / `visible` | `source` other than `self` or `visible: false`: listed as unsupported | warning on `roll_attack` / `subtract_roll` reactions |
| `melee_only` + `ranged_only` | both true: **load error** | error on `roll_attack` reactions |

### 3.2 Feature / trait effect kinds `reroll_ones`, `great_weapon_fighting`, `evasion`

| kind | fields | compiles to |
|---|---|---|
| `reroll_ones` | `rolls` (optional): non-empty array of `attack`, `check`, `save` (repeats allowed); omitted = all three | `Combatant::reroll_ones` bits 1 / 2 / 4 (Halfling Lucky = 7); several declarations OR together. Empty array or another entry: **load error** |
| `great_weapon_fighting` | none | `Combatant::great_weapon_fighting` |
| `evasion` | none | `Combatant::evasion` |

They are **open kinds** (registered with no minimum version, like the sim's R-G5 kinds): the sim applies them in every document
whose effects are active (`character@3`/`@4`, `monster@3`/`@4`; `@1`/`@2` effects are inert through the 2 -> 3 migration) and they
are not in the frozen 3 -> 4 list, so the migration never stamps them. Schema: three appended arms of `common@1 $defs/effect` (the
open, append-only effect contract that `@3` `trait_active` and `@4` `effect_live` both use, so the rule is the same at `@3` and
`@4`); the object stays open (the loader ignores fields it does not read). Validator: `checks_v4.OPEN_EFFECT_KINDS_RG7` joins every
row of the tag table (no unknown-kind warning; shape violations are errors at `@3`/`@4`, warnings at `@1`/`@2`).

### 3.3 Grip for Great Weapon Fighting (v0 wield model, no schema change)

Read from existing fields: an action's `properties` `two_handed` (or `two-handed`) and `versatile` are carried onto the attack.
A versatile **melee** weapon counts as held in two hands when the stat block carries no shield and the creature has no two-weapon
bonus option. A shield is an `equipment[]` entry whose id is `shield` or a namespaced id whose last `.`-segment is `shield`: a bare
string, or the character@4 item object `{"item": "srd51.item.shield"}` (the sim does not load item@1 definitions, so it matches the
id, not the item's `category`); or an `armor_class.note` containing `shield` (case-insensitive). Name a custom shield item id so
that it ends in `.shield`, or mention the shield in `armor_class.note`.

Versioning: no tag change, no migration. The reaction parts were already in the `@4` schemas (only check 9 rejected them), and the
feature arms only constrain kinds that no file can have relied on (they were unknown kinds; no shipped pack or `tests/compat` file
uses them, and both validate unchanged).

