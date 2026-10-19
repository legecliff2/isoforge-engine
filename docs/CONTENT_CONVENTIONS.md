# Content conventions the sim compiles (timed modifiers, bonus-attack options, R-G7 reactions and feature kinds, R-G11 zones)

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


## 4. R-G11 zones and auras: spell@1 effect kind `zone` (loader contract)

Rules side: #118 (merged 49d4fe2), `SpellEffectKind::Zone` with `SpellDef::zone` (`rules5e::ZoneSpec`,
`engine/rules/include/rules5e/spells.hpp`), proposal `docs/proposals/VERSION_BUMP_BATCH.md` section 7.2l and
`docs/proposals/ZONES_AURAS.md`. **No loader parses zones yet**: `rules5e_content` (`spell_loader.cpp`) refuses kind `zone`
with `effect kind invalid`. This section is the **contract for the loader author (Engine Core / Rules)**: the schema
(`schemas/spell/v1.schema.json` `$defs/zone`, `zone_trigger`, `zone_effect`) and `tools/validate` `check_spell_zone` define
the keys, values, ranges and errors below, and the loader must map them exactly and refuse the same documents with the same
wording (the validator's messages are quoted). Unlike sections 1-3, the source of truth here is this file until the loader lands.

```json
{ "kind": "zone",
  "zone": { "shape": "cylinder", "size_ft": 5, "height_ft": 40, "anchor": "point", "flags": ["light_obscured"],
            "move": { "cost": "action", "feet": 60 },
            "triggers": [
              { "on": "zone_enter",      "effect": { "kind": "save", "ability": "con", "on_success": "half",
                                                     "damage": { "dice": "2d10", "type": "radiant" } } },
              { "on": "zone_start_turn", "effect": { "kind": "save", "ability": "con", "on_success": "half",
                                                     "damage": { "dice": "2d10", "type": "radiant" } } } ] } }
```

(Moonbeam, with `scaling.upcast.dice: 1`. Fixtures for Moonbeam, Spirit Guardians, Fog Cloud, Darkness, Silence, Spike
Growth, Web and Grease: `tests/validate/fixtures/zones/spells/`. None are in `srd51.core` yet.)

### 4.1 `zone` (`ZoneSpec`)

Closed object, discriminated by `shape`; each arm is a propertyNames allow-list (`tests/validate/test_union_arms.py`).

| key | shape | maps to | notes |
|---|---|---|---|
| `shape` | `sphere` \| `cylinder` \| `cube` | `ZoneSpec::shape` (`AreaShape::Sphere` / `Cylinder` / `Cube`) | required. `line` (walls, rules PR 2) is appended later |
| `size_ft` | integer 5..65530, multiple of 5 | `size_ft` | required. Sphere / cylinder radius, cube side |
| `height_ft` | integer 5..65530, multiple of 5 | `height_ft` | required on `cylinder`, refused on the other shapes |
| `anchor` | `point` \| `creature` | `ZoneAnchor::Point` / `Creature` | required. `creature` = an aura centred on the caster |
| `flags` | non-empty array of unique names | `flags` bitmask, OR of `kZone*` | `difficult` 1, `light_obscured` 2, `heavy_obscured` 4, `magical_darkness` 8, `silence` 16, `halve_speed` 32, `opaque` 64. Named strings, never a raw integer. Omitted = 0 |
| `move` | `{cost, feet}`, both required | `move_cost`, `move_ft` | `cost`: `action` = 1 (Moonbeam, D3), `bonus_action` = 2. `feet`: integer 5..65530, multiple of 5. Omitted = `move_cost` 0 (cannot move) |
| `chooses_excluded` | boolean | `chooses_excluded` | Spirit Guardians. Default false |
| `triggers` | non-empty array of trigger rows (4.2), in order | `triggers` | omitted = no rows (Fog Cloud, Darkness, Silence) |

`needs_conscious_owner` (D7, feature auras) has no spell@1 key; `length_ft` arrives with `line`. Both are append-only additions.

### 4.2 `zone.triggers[]` rows (`ZoneTriggerRow`)

Closed object. Rows live only under `zone.triggers`; they are unrelated to spell@1 `casting_time.trigger` (a reaction window)
and to common@2 `on_event` `on` (row 9 `TriggerOn`), which is why the `on` values carry a `zone_` prefix.

| key | shape | maps to | default |
|---|---|---|---|
| `on` | `zone_appear` \| `zone_enter` \| `zone_start_turn` \| `zone_end_turn` \| `zone_move_within` | `ZoneTrigger::Appear` / `Enter` / `StartTurn` / `EndTurn` / `MoveWithin` | required |
| `group` | integer 0..255 | `group` | **1** (Enter + StartTurn share group 1, D2). 0 = no per-turn limit (Spike Growth, Grease) |
| `own_turn_only` | boolean | `own_turn_only` | false |
| `completely_within` | boolean | `completely_within` | false |
| `counts_aura_move_as_enter` | boolean | `counts_aura_move_as_enter` | false (D1) |
| `while_in_zone` | boolean | `while_in_zone` | false |
| `effect` | 4.3 | `effect` (`SpellEffect`) | required |

### 4.3 `effect` of a row (`SpellEffect`)

Closed object, discriminated by `kind`, propertyNames arms. Damage dice gain `scaling.upcast.dice` per slot level above the
spell's level (rules does this from `SpellDef::upcast_dice`).

| `kind` | keys | maps to |
|---|---|---|
| `save` | `ability` (required); `damage` + `on_success` (`half` \| `none`, each requires the other); `condition`; at least one of `damage` / `condition`; `description` | `SpellEffectKind::Save`, `save_ability`, `dice` / `damage_type`, `on_save` (`half` = `OnSave::Half`), `applies_condition` + `condition`. No `repeat_save`, no `applies_to` |
| `damage` | `damage` (required); `condition`; `description` | no save: `SpellEffectKind::ApplyCondition` (any non-Save kind; rules applies the damage and the condition without a save), `applies_condition` iff `condition` |
| `apply_condition` | `condition` (required); `description` | `SpellEffectKind::ApplyCondition`, `applies_condition` = true |

### 4.4 Cross-field rules (validator `check_spell_zone` / `check_spell`; the loader must refuse the same)

Messages start with `zone` on purpose: `spell_loader_tests` compares the loader's first error with a negative case's `.expect`
only for the prefixes `effect`, `scaling.`, `'`, ...; until the loader parses zones it answers `effect kind invalid`, so the
`tests/validate/cases/zone_*` cases are only required to fail there. Once the loader lands it should use these messages verbatim.

| rule | message |
|---|---|
| `zone` only as `effects[0]` (cast refuses an extra effect of kind Zone) | `zone must be the primary effect (effects[N] is a zone)` |
| no top-level `area` (the zone carries its geometry) | `zone: area not allowed (zone.shape / size_ft give the zone's geometry)` |
| no top-level `targets` | existing `targets not allowed on a zone effect (one target, per dart/beam, or self)` |
| `duration.kind` not `instantaneous` (the spell's duration is the zone's lifetime) | `zone: needs a lasting duration (not instantaneous)` |
| `anchor: creature` needs `range.kind: self` | `zone: anchor creature (an aura on the caster) needs range self` |
| `move` needs `anchor: point` | `zone: move needs anchor point` |
| `light_obscured` and `heavy_obscured` not together | `zone: flags light_obscured and heavy_obscured are exclusive` |
| at least one of `flags` / `triggers` | `zone: needs triggers or flags (it would do nothing)` |
| row `effect.kind` in `save`, `damage`, `apply_condition` (also schema) | `zone.triggers[N].effect.kind '<k>' is not a zone trigger effect (save, damage, apply_condition)` |
| `own_turn_only` only on `zone_enter` / `zone_move_within` | `zone.triggers[N]: own_turn_only is only for zone_enter / zone_move_within` |
| `counts_aura_move_as_enter` only on `zone_enter` | `zone.triggers[N]: counts_aura_move_as_enter is only for zone_enter` |
| ... and not on a point zone without `move` (it never moves) | `zone.triggers[N]: counts_aura_move_as_enter on a zone that never moves` |
| `while_in_zone` needs an effect `condition` | `zone.triggers[N]: while_in_zone needs an effect condition` |
| `scaling.upcast.dice` allowed on `zone`, needs a row with `damage` | existing `scaling.upcast.dice needs effect dice or damage` |
| other `scaling.upcast` keys, `scaling.cantrip` beams, reaction casting | existing messages (`scaling.upcast.targets not allowed on a zone effect`, ...) |

Ranges are integers only (floats are rejected: the content spec's integers-only rule, §4.3 in the schema descriptions). Loader mapping for the fixed parts:
`SpellDef::kind = Zone`; `targets` stays 1 (rules uses the caster); the spell's `duration` / `concentration` as for any spell.

### 4.5 Not expressible yet (no key; content keeps it in `description`)

Fog Cloud's +20 ft radius per slot level (no upcast size), Darkness on an object, Spirit Guardians' necrotic for an evil caster
(the fixture uses radiant), Moonbeam's shapechanger rules, Stinking Cloud's lost action (D9: no effect kind), Silence's
deafened / thunder immunity (the `silence` flag only, R-G13), walls (`line`, Wall of Fire), feature auras (D7).

Versioning: an additive spell@1 effect kind with new closed `$defs`. No tag change, no migration, no api_level gate: the old
schema rejected kind `zone`, so no existing file can carry it (`tests/compat` and both shipped packs validate unchanged).
