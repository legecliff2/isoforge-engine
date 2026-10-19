# Content conventions the sim compiles (timed modifiers, bonus-attack options)

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
