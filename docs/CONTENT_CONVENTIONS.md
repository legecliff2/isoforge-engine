# Content conventions the sim compiles (timed modifiers, bonus-attack options, R-G7 reactions and feature kinds, R-G11 zones, R-G8 grapple rider, R-G15 spell kinds, priced materials, R-G16 encounter surprise, slot recovery)

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

### 3.2 Feature / trait effect kinds `reroll_ones`, `great_weapon_fighting`, `evasion`, `pack_tactics`, `aggressive`, `reckless_attack`, `metamagic`, `sculpt_spells`, `font_of_magic`

| kind | fields | compiles to |
|---|---|---|
| `reroll_ones` | `rolls` (optional): non-empty array of `attack`, `check`, `save` (repeats allowed); omitted = all three | `Combatant::reroll_ones` bits 1 / 2 / 4 (Halfling Lucky = 7); several declarations OR together. Empty array or another entry: **load error** |
| `great_weapon_fighting` | none | `Combatant::great_weapon_fighting` |
| `evasion` | none | `Combatant::evasion` |
| `pack_tactics` | none | rules5e `RollTrait {Attack, Advantage, kWhenAllyNearTarget}` (sim #127; the sim supplies `ally_near_target`). A second declaration adds nothing: validator **warning** |
| `aggressive` | none | rules5e `Combatant::aggressive` (sim #129; bonus-action Dash toward a visible hostile). Duplicate: validator **warning** |
| `reckless_attack` | none | rules5e `Combatant::reckless_attack` (sim #129; the caller declares `ActionRequest::reckless`, the built-in AI does not). Duplicate: validator **warning** |
| `metamagic` | `options` (required): non-empty array of `careful`, `distant`, `empowered`, `extended`, `heightened`, `quickened`, `subtle`, `twinned`; `pool` (required): a `resources[].id` | rules5e `Combatant::metamagic_known` (kMeta* bits) and `sorcery_pool` (sim #143; the caller sets `ActionRequest::metamagic`, one option per cast, log_hash 0xB1). `pool` must be a **limited** `resources[]` pool of the document (absent, or a `max.table` entry that is `unlimited` at the document's level: **load error** `<id>: metamagic pool '<pool>' is not a limited resources[] pool`; so a `@3` document, which has no `resources[]`, can never declare it). Several declarations OR their options and must name the same pool (else **load error** `metamagic declarations name different pools`). Empty `options` or an unknown name: **load error** (schema). A repeated option adds nothing: validator **warning**. The pool counts as referenced (check 12) |
| `sculpt_spells` | none | rules5e `Combatant::sculpt_spells` (sim #143; Evocation Wizard: the caller lists `ActionRequest::spared` for its evocation spells, spell@1 `school: "evocation"`). Fields are ignored by the loader: validator **warning**. Duplicate: validator **warning** |
| `font_of_magic` | `pool` (required): a `resources[].id` (VERSION_BUMP_BATCH 7.2o) | sim #152: the pool becomes max = start = sorcerer level (`classes[]` sorcerer entries), recharge `long_rest` (registered when `resources[]` lacks it), and `sorcery_pool` (Flexible Casting, log_hash 0xBA). **Load errors**: `<id>: font_of_magic needs sorcerer level 2+ (sorcerer level N)`, `font_of_magic pool '<pool>' is unlimited`, `font_of_magic and metamagic name different pools` (either order; metamagic after it: `metamagic declarations name different pools`). A metamagic after it may name the pool even when `resources[]` lacks it (the loader registered it). Validator **warnings**: max not the sorcerer level, recharge not `long_rest`, start not the max, pool not in `resources[]`. The pool counts as referenced (check 12) |

They are **open kinds** (registered with no minimum version, like the sim's R-G5 kinds): the sim applies them in every document
whose effects are active (`character@3`/`@4`, `monster@3`/`@4`; `@1`/`@2` effects are inert through the 2 -> 3 migration) and they
are not in the frozen 3 -> 4 list, so the migration never stamps them. Schema: nine appended arms of `common@1 $defs/effect` (the
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
`docs/proposals/ZONES_AURAS.md`. **The loader parses zones** since R-G11 rules PR 2: `rules5e_content` (`spell_loader.cpp`)
maps kind `zone` onto `SpellDef::zone` (`needs_conscious_owner` onto the D7 field of the same name) and gives the zone an
expiry timer from the spell's duration. This section is the **contract for the loader author (Engine Core / Rules)**: the schema
(`schemas/spell/v1.schema.json` `$defs/zone`, `zone_trigger`, `zone_effect`) and `tools/validate` `check_spell_zone` define
the keys, values, ranges and errors below, and the loader must map them exactly and refuse the same documents with the same
wording (the validator's messages are quoted). Schema, validator and loader now agree; change all three together.

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
Entangle's specimen is `tests/validate/fixtures/zones/spells/entangle.json` (differences from Web in that directory's README).
T&C authors `srd51.spell.entangle` from it.

### 4.1 `zone` (`ZoneSpec`)

Closed object, discriminated by `shape`; each arm is a propertyNames allow-list (`tests/validate/test_union_arms.py`).

| key | shape | maps to | notes |
|---|---|---|---|
| `shape` | `sphere` \| `cylinder` \| `cube` | `ZoneSpec::shape` (`AreaShape::Sphere` / `Cylinder` / `Cube`) | required. `line` (walls, Wall of Fire): appended by rules PR 2, see 4.6 |
| `size_ft` | integer 5..65530, multiple of 5 | `size_ft` | required (forbidden on `line`, 4.6). Sphere / cylinder radius, cube side |
| `height_ft` | integer 5..65530, multiple of 5 | `height_ft` | required on `cylinder` and `line` (4.6), refused on the other shapes |
| `anchor` | `point` \| `creature` | `ZoneAnchor::Point` / `Creature` | required. `creature` = an aura centred on the caster |
| `flags` | non-empty array of unique names | `flags` bitmask, OR of `kZone*` | `difficult` 1, `light_obscured` 2, `heavy_obscured` 4, `magical_darkness` 8, `silence` 16, `halve_speed` 32, `opaque` 64. Named strings, never a raw integer. Omitted = 0 |
| `move` | `{cost, feet}`, both required | `move_cost`, `move_ft` | `cost`: `action` = 1 (Moonbeam, D3), `bonus_action` = 2. `feet`: integer 5..65530, multiple of 5. Omitted = `move_cost` 0 (cannot move) |
| `chooses_excluded` | boolean | `chooses_excluded` | Spirit Guardians. Default false |
| `needs_conscious_owner` | boolean | `needs_conscious_owner` | the zone / aura only functions while its owner is conscious (D7; rules R-G11 PR 2). Default false. The Spirit Guardians fixture sets it |
| `triggers` | non-empty array of trigger rows (4.2), in order | `triggers` | omitted = no rows (Fog Cloud, Darkness, Silence) |

`length_ft` arrived with `line` (append-only); the PR 2 keys are in 4.6.

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
only for the prefixes `effect`, `scaling.`, `'`, `zone`, ...; the loader (R-G11 rules PR 2) uses these messages verbatim, so
the cross-field `tests/validate/cases/zone_*` cases compare their first error there.
Every rule in the table below has a `tests/validate/cases/zone_*` case (loader-checked by `spell_loader_tests`) as well as a
`tools/validate/test_validate_packs.py` row.

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

### 4.5 Rules PR 2 keys: specified (formerly pending)

The keys this section used to list as pending (`ZoneSpec` `upcast_size_ft`, `ring_size_ft`, `sided`, `side_ft`, the `line` shape
with `length_ft`, the D7 aura fields `aura_modifier` / `aura_bonus_ability` / `aura_immunity` / `aura_allies_only`; row `region`,
`loses_action`, `auto_success_damage` / `_conditions` / `_no_breath`, `shapechanger_disadvantage`, `evil_damage_type`) are now
specified in 4.6 and accepted by `schemas/spell/v1.schema.json` (`$defs/zone`, `$defs/zone_aura_modifier`, `$defs/zone_trigger`;
the `line` arm is its own propertyNames allow-list) and `check_spell_zone`, together with the loader (`parse_zone`) in the same PR.
Optional additive keys: no tag change (docs/CONTENT_COMPAT.md). Type, range, enum and shape-arm errors are the schema's (the
validator prints jsonschema wording; the loader its own 4.6 wording); the cross-field rules print the 4.6 loader messages verbatim
and each has a `tests/validate/cases/zone_*` case. Fixtures: `tests/validate/fixtures/zones/spells` `fog_cloud` (upcast_size_ft),
`moonbeam` (shapechanger_disadvantage), `spirit_guardians` (evil_damage_type) (still the loader's 8-spell book), and
`tests/validate/fixtures/zones/spells_pr2` `stinking_cloud`, `wall_of_fire`, `warding_aura` (test-only D7 aura). The creature
facts `no_breath`, `shapechanger`, `evil` (booleans, default false) are accepted on `monster@4` / `character@4` but are
**reserved**: their reader is the sim archetype loader (`engine/core/sim/src/content.cpp`, Engine Core), not wired yet, so
the validator warns (`/<key>: <key> is reserved: the sim archetype loader does not read it yet ...`, not an error) and they have
no effect until Engine Core lands it. Silence needs none of these: `flags: ["silence"]`.

Formerly not expressible, now expressed by those keys: Fog Cloud's +20 ft radius per slot level, Spirit Guardians' necrotic
for an evil caster, Moonbeam's shapechanger disadvantage, Stinking Cloud's lost action (D9), walls (`line`, Wall of Fire) and
the D7 aura fields (the paladin aura content itself stays deferred). Silence's deafened / thunder immunity come from the
`silence` flag alone. Still not expressible: see the end of 4.6.

Versioning: an additive spell@1 effect kind with new closed `$defs`. No tag change, no migration, no api_level gate: the old
schema rejected kind `zone`, so no existing file can carry it (`tests/compat` and both shipped packs validate unchanged).

### 4.6 R-G11 rules PR 2 keys (loader `parse_zone`, branch `feat/rules-zone-loader-keys`; supersedes the 4.5 pending list)

All keys optional; absent = the default. Feet are integers 5..65530, multiples of 5 (floats rejected). Enum values are the
existing common@1 names (`damage_type`, `condition`, `ability`). The loader checks in table order; the first failure is the error.

**`zone` (`ZoneSpec`)**

| key | JSON type / range | default | maps to | loader error |
|---|---|---|---|---|
| `shape` | adds `"line"` (sphere, cylinder, cube, line) | — | `AreaShape::Line` | `zone.shape must be sphere, cylinder, cube or line` |
| `size_ft` | feet; **required except** for `line`, **forbidden** on `line` | — | `size_ft` | `zone.size_ft invalid (5..65530, a multiple of 5)` / `zone.size_ft not allowed on a line (length_ft / height_ft give a wall's geometry)` |
| `length_ft` | feet; required for (and only for) `line` | 0 | `length_ft` | `zone.length_ft is required for (and only for) lines` / `zone.length_ft invalid (5..65530, a multiple of 5)` |
| `height_ft` | feet; now required for `cylinder` **and** `line` | 0 | `height_ft` | `zone.height_ft is required for (and only for) cylinders and lines` |
| `upcast_size_ft` | feet; not on `line` | 0 | `upcast_size_ft` (+ per slot level above) | `zone.upcast_size_ft invalid (5..65530, a multiple of 5)` / `zone: upcast_size_ft not allowed on a line` |
| `ring_size_ft` | feet; only on `line` | 0 | `ring_size_ft` | `zone.ring_size_ft invalid (5..65530, a multiple of 5)` / `zone: ring_size_ft needs shape line` |
| `sided` | boolean; only on `line` | false | `sided` | `zone.sided must be a boolean` / `zone: sided needs shape line` |
| `side_ft` | feet; required iff `sided: true` | 0 | `side_ft` | `zone.side_ft invalid (5..65530, a multiple of 5)` / `zone: side_ft is required for (and only for) sided zones` |
| `aura_modifier` | closed object `{stat, bonus, ability}`: `stat` must be `"save"`; `bonus` integer -30..30 (default 0); `ability` an ability (scope; absent = all saves). No duration (the aura lasts while in the zone) | absent | `aura_modifier` (stat Save, id = `spell_id_of(content_id + "#aura")`) | `zone.aura_modifier must be {stat, bonus, ability}` / `zone.aura_modifier stat must be save` / `zone.aura_modifier bonus invalid (-30..30)` / `zone.aura_modifier ability invalid` |
| `aura_bonus_ability` | ability enum | absent | `has_aura_bonus_ability` + `aura_bonus_ability` | `zone.aura_bonus_ability invalid` / `zone: aura_bonus_ability needs aura_modifier` |
| `aura_immunity` | non-empty array of unique conditions | [] | `aura_immunity` (bit mask) | `zone.aura_immunity must be a non-empty array of unique conditions` |
| `aura_allies_only` | boolean | false | `aura_allies_only` | `zone.aura_allies_only must be a boolean` |
| (cross) | `aura_modifier` / `aura_immunity` / `aura_allies_only` need `anchor: creature` | | | `zone: aura fields need anchor creature` |

`zone: needs triggers or flags (it would do nothing)` also accepts a zone with `aura_modifier` or `aura_immunity` (D7 auras).

**`zone.triggers[N]` (`ZoneTriggerRow`)**

| key | JSON type / range | default | maps to | loader error (`where` = `zone.triggers[N]`) |
|---|---|---|---|---|
| `region` | `"zone"` \| `"zone_or_side"` | `"zone"` | `region` 0 / 1 | `where.region must be zone or zone_or_side` |
| (cross) | `zone_or_side` needs a `sided` zone and `on` in zone_appear / zone_start_turn / zone_end_turn | | | `where: region zone_or_side needs a sided zone` / `where: region zone_or_side is only for zone_appear / zone_start_turn / zone_end_turn` |
| `loses_action` | boolean | false | `loses_action` | `where: flags must be booleans` |
| `auto_success_damage` | non-empty array of unique damage types; row effect `save` only | [] | `auto_success_damage` (mask) | `where.auto_success_damage must be a non-empty array of unique damage types` |
| `auto_success_conditions` | non-empty array of unique conditions; `save` only | [] | `auto_success_conditions` (mask) | `where.auto_success_conditions must be a non-empty array of unique conditions` |
| `auto_success_no_breath` | boolean; `save` only | false | `auto_success_no_breath` | `where: flags must be booleans` |
| `shapechanger_disadvantage` | boolean; `save` only | false | `shapechanger_disadvantage` | `where: flags must be booleans` |
| (cross) | the four above on a non-`save` row | | | `where: auto_success / shapechanger_disadvantage need effect kind save` |
| `evil_damage_type` | damage type; the row effect needs `damage` | absent | `has_evil_damage_type` + `evil_damage_type` | `where.evil_damage_type invalid` / `where: evil_damage_type needs effect damage` |

**Creature facts** (`monster@4` / `character@4` top level): `no_breath`, `shapechanger`, `evil`, booleans, default false, map to
`Combatant::no_breath / shapechanger / evil`. They are read by the sim archetype loader (`engine/core/sim/src/content.cpp`, Engine
Core's), not by this PR: until EC wires them the schema should accept them but they have no effect (tracked for EC).

Still not expressible: Darkness on an object, Moonbeam's form revert, the Paladin auras' content (fields exist, content deferred, D7).

## 5. R-G8 grapple hit rider: `common@2 $defs/rider` kind `grapple` (sim #104)

Read by the sim loader (`engine/core/sim/src/content.cpp`, the `on_hit[]` loop over the archetype's attack actions) since
sim #104; rules side `rules5e::grapple(state, attacker, target, escape_dc, ev)` (proposal section 7.2c). Until this schema change
the kind was a reserved name that the schema rejected (VERSION_BUMP_BATCH `$defs/rider`).

```json
{ "id": "claw", "name": "Claw", "kind": "melee_weapon_attack", "to_hit": 3, "reach": 5, "targets": 1,
  "damage": [{ "dice": "1d6+1", "type": "bludgeoning" }],
  "on_hit": [{ "kind": "grapple", "escape_dc": 11 }] }
```

A hit by the creature's own Attack action (not a reaction or bonus attack) with that attack calls `grapple(...)`: the target gains Grappled with the
attacker as its source and the given escape DC (rules refuses a dead, immune or self target). The grapple rider compiles to
`Archetype::grapple_on_hit[attack index]`, **not** to a trigger: it gets no derived trigger id (check 8 skips it) and does not
count toward the 32-trigger limit. Other riders in the same `on_hit[]` keep their positional ids (`<action id>#<index>`).

| part | loader rule | schema / validator |
|---|---|---|
| where | only `on_hit[]` of `actions[]` entries of kind `melee_weapon_attack` / `ranged_weapon_attack` in `monster@4` / `character@4` (`declared >= 4`) | schema: `on_hit` is allowed only on those kinds (existing); `common@2` riders exist only in `@4` documents and `item@1` |
| `escape_dc` | optional; absent = 0 = escape by a contest against the grappler's Athletics; otherwise an integer 1..30. Not an integer (a float `11.0`, a string) or outside 0..30: **load error** `on_hit: bad grapple escape_dc` | schema `integer`, `minimum` 0, `maximum` 30 + the float rejection; validator error `grapple escape_dc must be an integer 0..30` (`(R-G8 grapple)`) |
| other fields | ignored by the loader | schema: arm closed to `kind`, `escape_dc` (no `condition`, `save`, ...); `escape_dc` is forbidden on every other rider kind |
| two `grapple` riders on one attack | the last one wins (map assignment), no error | warning `the sim keeps only the last one` |
| `bonus_actions[]` weapon attacks | their `on_hit[]` is never read | warning `this grapple rider never applies` |
| `on_event` `effects[]` | reaches `compile_rider`: **load error** `unknown rider kind 'grapple'` | error (`grapple is an on_hit[] rider of a weapon attack only`) |
| `item@1` `on_hit[]` | not read by the sim | error (check 17, like `bonus_attack`): items carry no attack of their own; put the rider on the wielder's action |

Not expressible (content keeps it in `description`): "until this grapple ends, the target is restrained" (crocodile, constrictor
snake: no condition tied to a grapple), "can't bite another target" / "can grapple only one target per claw", and Restrained with
an escape (giant spider Web, R-G8 restrain source).

Versioning: an additive kind with a new closed arm in `common@2 $defs/rider` and one new base property (`escape_dc`). No tag change,
no migration, no api_level gate: the old schema rejected kind `grapple`, so no existing file can carry it (`tests/compat` and the
shipped packs validate unchanged). Fixture: `tests/validate/fixtures/v4/extra/giant_crab_grappler.json`; cases: `test_v4_validator.py`
`GRAPPLE_CASES`.

## 6. R-G15 spell@1 kinds wired by sim #120: `hp_pool`, `dispel`, `revive`, `end_condition`, `delayed_damage`, `sustained_damage`

Compiled by `rules5e_content::spell_from_json` (`engine/rules/content/src/spell_loader.cpp`,
#120, 66012c7; rules #105 / #106, proposal sections 7.2f / 7.2g). Schema: six closed arms of spell@1 `$defs/effect` (each also a
propertyNames allow-list, `tests/validate/test_union_arms.py` `SpellEffectArmsR120`) and the closed `$defs/delayed_expiry` union.
Fixtures: `tests/validate/fixtures/spell_kinds/spells/` (Sleep, Color Spray, Dispel Magic, Revivify, Lesser Restoration, Acid
Arrow, Heat Metal; test namespace, not in `srd51.core`).

| kind | keys (required **bold**) | compiles to | loader errors |
|---|---|---|---|
| `hp_pool` | **`dice`** (`NdS[+/-B]`), **`condition`**, `ends_on_damage` (boolean), `excludes_creature_type` (non-empty creature types, as on `heal`) | `HpPool`: roll `dice` (+ `scaling.upcast.dice`), cover targets by lowest current HP with `condition` for the spell's duration (Sleep, Color Spray) | `effect dice invalid`, `effect condition invalid`, `effect ends_on_damage must be a boolean`, `effect excludes_creature_type invalid` |
| `dispel` | `check` = `{ability: "spellcasting", dc_base: 1..99, plus: "spell_level"}` (all three required; omitted = dc_base 10) | `Dispel`: spells of the slot level or lower end; higher ones on a check, DC `dc_base` + their level (Dispel Magic) | see note |
| `revive` | `within_rounds` 1..255 (default 10 = 1 minute) | `Revive`: a creature dead at most that long returns with 1 HP (Revivify) | `effect within_rounds invalid` |
| `end_condition` | **`conditions`**: non-empty array of common@1 conditions (unique in the schema) | `EndCondition`: ends the lowest `Condition` value present (Lesser Restoration) | `effect conditions invalid` |
| `delayed_damage` | **`damage`** `{dice, type}`, **`expiry`**: `{kind: "rounds", n: 1..65535}` or `{kind: "until_end_of_target_next_turn"}`; `applies_to` | `DelayedDamage` with its own timer (Acid Arrow's 2d4 rides the spell attack with `applies_to: affected`) | `effect damage invalid`, `effect expiry must be {kind: rounds, n} or {kind: until_end_of_target_next_turn}`, `effect expiry n invalid` |
| `sustained_damage` | **`damage`** `{dice, type}`; no `expiry` | `SustainedDamage`: damage now and on each bonus-action sustain while the spell lasts (Heat Metal) | `effect damage invalid`, `effect expiry is only for delayed_damage (sustained_damage lasts the spell's duration)` |

Every arm also takes `description`; only `delayed_damage` takes `applies_to`. Cross-field rules (validator `check_spell`, same
wording as the loader):

- `effects[N]: <kind> is only allowed as the first effect` for `hp_pool`, `dispel`, `revive`, `end_condition`,
  `sustained_damage` (only `delayed_damage` rides another effect).
- `hp_pool needs a rounds, minutes or hours duration` / `sustained_damage needs ...`: their timer is the spell's duration in rounds
  (minutes x 10, hours x 600; Color Spray's "until the end of your next turn" is written as 1 round).
- `scaling.upcast.dice` is allowed on `hp_pool`, `delayed_damage`, `sustained_damage` (first effect); `scaling.upcast.dice not
  allowed on a dispel effect` (and on `revive` / `end_condition`). `targets` is not allowed on any of the six.

**Dispel DC (note).** The #120 loader reads a bare integer `effect.dc_base`; Engine Core is switching it (follow-up PR) to the
`check` object above, the same shape as `counter`'s. The schema follows the check-object form and intentionally does **not**
accept the bare `dc_base` (it was never valid content). Until the follow-up lands the loader ignores `check`, so authored DCs other
than 10 do not take effect yet; SRD Dispel Magic uses 10.

Shapes the #120 description gave that the loader does not use (loader wins): `hp_pool` takes a bare `dice` string, not
`damage {dice, type}` (the pool is hit points, not damage); `damage {dice, type}` is used by `delayed_damage` /
`sustained_damage` only.

Versioning: additive spell@1 kinds and a new `$defs`; no tag change, no migration, no api_level gate (the old schema rejected
these kinds, so no existing file carries them; `tests/compat` and both shipped packs validate unchanged).

### 6.1 R-G9 spell@1 kinds `forced_move` and `teleport` (sim #144)

Compiled by `rules5e_content::spell_from_json` (`spell_loader.cpp`, R-G9 / VERSION_BUMP_BATCH 7.2k; sim wiring #144). Schema: two
closed arms of spell@1 `$defs/effect` (propertyNames allow-lists, `tests/validate/test_union_arms.py` `SPELL_EFFECT_RG9`).
Fixtures: `tests/validate/fixtures/spell_forced/spells/` (Misty Step, a save + push test spell; test namespace).

| kind | keys (required **bold**) | compiles to | loader errors |
|---|---|---|---|
| `forced_move` | **`feet`** integer, positive multiple of 5 (5..65530); `mode` `push` (default) \| `pull`; `up_to` boolean (default false); `max_size` tiny..gargantuan (omitted = any); `provokes_oa` `false` only; `applies_to` (effects after the first) | `ForcedMove` (`SpellDef::forced` / `SpellEffect::forced`): the sim walks each target tile by tile, farthest first, stopping at the last free tile | `effect mode must be push or pull` (`line`, Gust of Wind, is data only in R-G9), `effect feet must be a positive multiple of 5`, `effect max_size invalid`, `effect provokes_oa: true is not supported (R-G9: forced moves never provoke)` |
| `teleport` | **`max_feet`** integer, positive multiple of 5; `needs_sight` boolean (default true) | `Teleport` (`SpellDef::teleport`): the caster moves to the request's destination, re-validated by the sim (Misty Step) | `effect max_feet must be a positive multiple of 5`; `effects[N]: teleport is only allowed as the first effect` |

Both arms also take `description`. Cross-field rules (validator `check_spell`, loader wording): `teleport` is first-effect only
(`FIRST_EFFECT_ONLY`); `targets` is not allowed on either (`targets not allowed on a forced_move effect`); `scaling.upcast` has no
key for them. Shape errors (feet, mode, max_size, provokes_oa) are the schema's, so the negative cases
(`tests/validate/cases/forced_*`, `teleport_*`) expect the schema wording, which does not start with a loader-compared prefix.

Versioning: additive spell@1 kinds; no tag change, no migration (the old schema rejected these kinds).

## 7. Priced material components (Director ruling; contract agreed with Engine Core)

Content keys (all integers; additive, no tag change, no migration):

| where | key | shape | meaning |
|---|---|---|---|
| spell@1 `components.m` | `cost_cp` | integer >= 1, optional | the material's price in copper (`SpellDef::material_cost_cp`, loaded today) |
| spell@1 `components.m` | `consumed` | boolean, needs `cost_cp` | the cast uses the material up (`SpellDef::material_consumed`, loaded today) |
| spell@1 `components.m` | **`item`** | content id string (common@1 `content_id`: dotted lowercase, e.g. `srd51.item.diamond`), optional | the equipment id that provides the priced material. Read by the spell loader into `SpellDef::material_item` (EC, priced materials PR) |
| character@4 `equipment[]` item object | **`value_gp`** | integer >= 0, optional, **default 0** | the entry's value in gold pieces |

Example (Revivify, fixture `tests/validate/fixtures/spell_kinds/spells/revivify.json`) and a caster that can pay for it:

```json
"components": { "v": true, "s": true,
                "m": { "text": "diamonds worth 300 gp, which the spell consumes", "item": "srd51.item.diamond",
                       "cost_cp": 30000, "consumed": true } }

"equipment": [ { "item": "srd51.item.diamond", "value_gp": 300 } ]
```

Sim semantics (agreed; implemented by Engine Core: `Archetype::equipment`, `Combat`'s combat-local copy, `can_provide_components`, consumption at cast completion, `Combat::hash` tag 0xAD once something was consumed):

1. A material with no cost (`cost_cp` absent) needs nothing.
2. A priced material (`cost_cp` > 0) is satisfied by the **first** `equipment[]` entry, in array order, whose id equals
   `components.m.item` **exactly** (the full content id; not the last-segment suffix match used for shields / armor) and whose
   `value_gp * 100 >= cost_cp`. The entry id is read like `equipment_entry_id`: a bare string, `{"id": ...}` or `{"item": ...}`.
   An entry without `value_gp` counts as 0 gp, so bare strings (which cannot carry `value_gp`) never satisfy a priced material.
   Values are **not summed** across entries: one entry has to cover the cost on its own.
3. No `item` key on a priced material: it is unsatisfiable and the cast is refused (today's behaviour for every priced material).
4. `consumed: true` removes that one entry (the one that satisfied the cast) from the caster's combat-local equipment state; one
   entry per cast. Nothing is written back to the content document.
5. No item@1 document is needed for the id: the match is on the id string and `value_gp`, never on an item@1 definition (the sim
   does not load item@1 documents). `srd51.item.diamond` now also exists as an item@1 `valuable` (section 7.1), so a reference to it
   resolves, but an entry with `value_gp` naming an id that has no document stays valid.

Validator:

- `components.m.item`: schema `content_id` pattern; **no existence check** against item@1 documents (the sim requires none).
- warning: `cost_cp` > 0 without `item` (`/components/m: cost_cp N without item: no equipment entry can provide this material,
  so the sim refuses the cast (priced materials)`): the spell loads, but nobody can ever cast it.
- warning: `item` without `cost_cp` (`... is ignored without cost_cp (a material with no cost needs nothing; priced
  materials)`).
- There is no "not loaded yet" warning for `item`: like the zone keys (section 4), it is a contract the loader is written
  against, and valid content should validate clean.
- character@4 `value_gp`: integer >= 0 (floats and negatives are schema errors). An equipment entry with `value_gp` is exempt from
  check 6/11 (its `item` need not resolve to an item@1); an entry without `value_gp` must still resolve.

### 7.1 item@1 category `valuable` (gems)

An additive arm of the closed item@1 union (Director-approved): `category: "valuable"` takes **only the common item fields**
(`$schema`, `id`, `name`, `category` required; `source`, `cost_cp`, `weight_oz`, `description` optional). It has no value field of its
own: a valuable's worth is per instance, so it lives on the character@4 `equipment[]` entry as `value_gp` (section 7 table), not
on the item. `cost_cp` stays available for a valuable with a fixed list price; `value_gp` on an item is a schema error.

```json
{ "$schema": "item@1", "id": "srd51.item.diamond", "name": "Diamond", "category": "valuable",
  "source": { "ref": "SRD 5.1", "license": "CC-BY-4.0" }, "description": "..." }
```

`srd51.item.diamond` (`content/packs/srd51.core/items/diamond.json`): the SRD equipment list (5e-bits/5e-database `a6212be`,
`5e-SRD-Equipment.json`: categories weapon, armor, adventuring-gear, tools, mounts-and-vehicles) has **no Diamond and no gems**, so
the item is based on the SRD spell material text (Revivify "Diamonds worth 300gp, which the spell consumes", Raise Dead,
Resurrection, Greater Restoration, Stoneskin, ...) and has no `cost_cp` or `weight_oz` (the SRD gives neither). Pinned by
`tests/validate/test_srd51_equipment.py` (`VALUABLES`).

Validator: the schema arm (closed, `test_union_arms.py` row); `check_item` (checks 15 / 17) applies to weapons only, so nothing else
is checked. Check 6/11: an `equipment[]` entry `{"item": "srd51.item.diamond"}` resolves to the document; with `value_gp` it is exempt
as before. Loader: the C++ side registers `item@1` by type and version only (`content_migrations.cpp`, `pack_set.cpp` indexes the
id); nothing reads `category`, so a `valuable` document loads like any item. No tag change, no migration: the old schema rejected
the category, and `tests/compat` has no items.

### Untimed `save_condition` riders (validator mirror of the sim loader)

A `save_condition` rider (an attack's `on_hit[]` or an `on_event` `effects[]` entry) whose `apply[]` entry has neither
`duration` nor `repeat_save` fails to load in the sim ("save_condition uses an unsupported duration"); the validator reports the
same error. `checks_v4.UNTIMED_ON_HIT_CONDITIONS` lists the exceptions, mirroring the loader's `kEndsByOwnRules` (#133): `prone`
only, a condition that ends by its own rules (the target stands up). The sim applies it untracked, the condition bit only with no
timed entry, as Shove does; a timed prone rider loads as before. Every other untimed condition, including one beside an untimed
prone in the same `apply[]`, is still the load error. The exception covers riders only: a `save_effect` action with an untimed
condition (prone included) is still listed unsupported by the loader (it loads, the action is not compiled). Example, the SRD wolf
bite: `"on_hit": [{"kind": "save_condition", "save": {"ability": "str", "dc": 11}, "apply": [{"condition": "prone"}]}]`.

## 8. R-G16 encounter surprise: chunk@1 top-level `surprise` (reserved; contract for the loader)

**Status: reserved.** The key is in the schema and the validator checks its references, but **no loader reads it yet**: the
validator warns `surprise is reserved: no loader reads it yet ... so it has no effect until that loader lands` on every chunk that
carries it (the same "declared, validated, not applied" pattern as check 12's declaration-only pools and `forward_compat`).
Engine Core's #123 added the runtime entry point `sim::Combat::apply_surprise(ids)`; this section is the contract for the code that
will call it from content.

```json
"surprise": { "sides": ["party"] }
"surprise": { "spawns": [0, 3] }
```

| part | rule | schema / validator |
|---|---|---|
| shape | closed object, **exactly one** of `sides` / `spawns` | schema `oneOf` required + `additionalProperties: false` |
| `sides` | non-empty, unique team names `^[a-z][a-z0-9_]*$`. A spawn's team is `sim::spawn_team_name`: `overrides.team`, else `party` for a character archetype, `monsters` for a monster archetype | schema; validator error when no combatant spawn **of this chunk** is on the team (`surprise.sides[k]: no combatant spawn in this chunk is on team ...`) |
| `spawns` | non-empty, unique indices into this chunk's `spawns[]` (`EntitySpawn.stable_id` is derived from the index and is forbidden in files) | schema integer 0..2^32-1 + the float rejection; validator errors for an index past the end and for a spawn whose archetype is not a monster / character (no combatant: `apply_surprise` would refuse its id) |
| hashing | not part of `content_hash`: `chunk_codec.cpp` ignores unknown top-level keys, and the Python mirror hashes the decoded chunk | `test_surprise_is_not_hashed` |

**Loader contract** (Engine Core, when it lands):

1. Resolve per chunk: `sides` -> every combatant spawn of that chunk whose `spawn_team_name` is listed; `spawns` -> `spawns[i].stable_id`.
   A chunk's `surprise` only ever names that chunk's own spawns (a multi-chunk combat takes the union over its chunks). Spawns whose
   archetype is not loaded are skipped like in `Combat::create` (non-combatants).
2. Call `Combat::apply_surprise(ids)` once, after `Combat::create` and before the first command (the call is refused after a command,
   with a pending decision, or for an unknown or dead id). Replays pass the same set: `surprised_at_start()` -> `replay(..., surprised)`.
3. Semantics are the rules' (#108): a surprised creature only ends its turn in round 1 and cannot react until that turn ends; the
   0xA8 hash fold applies only while one is surprised. Listing every side is legal (everyone loses round 1).
4. Rolled surprise (Dexterity (Stealth) vs passive Perception, `Combat::surprised_by_stealth`) is **not** part of this key; a later
   additive key (for example a `stealth` list of hiders) would be a third, exclusive arm.

Versioning: an additive optional top-level key of chunk@1 with a new closed `$defs/surprise`. No tag change, no migration: the old
schema rejected the key (chunk@1 is closed), and no shipped or `tests/compat` chunk carries it (pinned by
`test_no_shipped_or_compat_chunk_carries_surprise`), so nothing existing changes meaning when the loader lands. A chunk that carries it
today is authored with that intent (the warning says so). Fixtures: `tests/validate/fixtures/surprise/`; cases:
`tools/validate/test_validate_packs.py` `SURPRISE_CASES`.

## 9. R-G12 transforms: spell@1 `polymorph`, common@2 `wild_shape` / `shapechanger`, `requires_humanoid_form`

Agreed with Rules (PR #156, `docs/proposals/VERSION_BUMP_BATCH.md` 7.2q). Additive optional schema keys and new closed arms only,
so no version bump (`docs/CONTENT_COMPAT.md`). Integers only: CRs are monster@4 `challenge.rating` strings compared as CR*8
(0, 1/8 = 1, 1/4 = 2, 1/2 = 4, n = 8n). `form_archetype` (ActionRequest) = 1 + index into `forms[]` (at most 255 forms).
Form ids resolve across the pack set, dependency packs included; every form must be a beast.

**Version gate (sim, EC fix/reach0-gates).** `wild_shape` and `shapechanger` are live at `character@4` / `monster@4` only. Like `extra_attack` and `gaze` (section 11) they are not in the frozen 3 -> 4 kind list, so the sim gates them in the loader hook: a file that declares `@3` or older lists the feature in `Archetype::inert_features` (`<fid> (wild_shape)` / `<fid> (shapechanger)`) and never parses it (no key errors, no FeatureSpec, no form swap); `@1` / `@2` effects are already inert through the 2 -> 3 migration. This is the CONTENT_COMPAT convention (a version below a kind's minimum is inert, never a load error). The `shapechanger` creature fact is read as before. Pinned by `transforms_content_tests.cpp` ("version gate").

**spell@1 `polymorph`** `{kind, forms (required, 1..255), max_cr ("target_level" default, or a CR string), save {ability} (default wis)}`.
Loader: rules spell loader key `polymorph` -> `SpellEffectKind::Polymorph`, concentration from the spell; Rules parses it with
these exact strings.

| finding | severity |
|---|---|
| `polymorph: primary effect only` | error (polymorph in effects[1..]) |
| `polymorph: one target` | error (`targets.count` != 1 or an `area`) |
| `polymorph: needs concentration` | error |
| `polymorph: unknown form '<id>'` / `polymorph: form '<id>' is not a beast` / `polymorph: duplicate form '<id>'` | error |
| `polymorph: form '<id>' above max_cr` | warning (fixed `max_cr` only; `target_level` is sim's cast-time check) |

**common@2 `wild_shape`** (character feature) `{kind, pool (required), cost (action), class (druid), forms (required),
max_cr_by_level (required), no_fly_below (8), no_swim_below (4)}`; no duration key (the engine computes it). Loader: Engine Core
`build_form`.

| finding | severity |
|---|---|
| `wild_shape: unknown pool '<id>'` / `wild_shape: pool '<id>' must recharge on short_rest` | error |
| `wild_shape: max_cr_by_level levels must be strictly ascending` | error |
| `wild_shape: unknown form '<id>'` / `wild_shape: form '<id>' is not a beast` / `wild_shape: duplicate form '<id>'` | error |
| `wild_shape: one declaration per document` | error |
| `wild_shape: pool '<id>' max should be 2` | warning |
| `wild_shape: form '<id>' above every max_cr_by_level row` | warning |
| `wild_shape: class '<c>' not in classes[]` | warning |

**common@2 `shapechanger`** (monster or character trait) `{kind, cost (action), forms: [{id, name, size?, armor_class?, speed?
(replaces the whole object), actions? (own actions[] ids, unique), creature_type?}] (1..255)}`. Loader: Engine Core `build_form`.

| finding | severity |
|---|---|
| `shapechanger trait requires shapechanger: true` (the #141 creature fact) | error |
| `shapechanger: duplicate form id '<id>'` / `shapechanger: form '<f>' names unknown action '<a>'` | error |
| `shapechanger: one declaration per document` | error |
| `shapechanger: form '<f>' changes nothing` / `shapechanger: form '<f>' allows no actions` (empty list) | warning |

With the trait present, the #141 `shapechanger is reserved` warning is dropped (the trait reads the boolean); the boolean without
the trait stays legal and keeps that warning.

**`requires_humanoid_form`** (common@2 `$defs/trait`, optional boolean, default false): error `requires_humanoid_form on the
wild_shape feature itself`; warning `requires_humanoid_form has no effect without wild_shape`.

**Loaded kinds (EC #176).** The sim loaders for `wild_shape` / `shapechanger` read the common@2 keys above (#176), so both are
known kinds of the `@4` rows of the tag table (`checks_v4.COMPILED_EFFECT_KINDS_AT_4`) and no longer get the unknown-kind
warning there; below `@4` they keep it. The schema arms match the loader's key lists: shapechanger effect `{kind, cost, forms}`,
form `{id, name, size, armor_class, speed, actions, creature_type}` (anything else, e.g. `speed_walk`, is refused: use
`speed {walk}`), `requires_humanoid_form` on the feature object. `polymorph` is parsed by the rules spell loader since #171
(no reserved warning; the unknown-form, not-a-beast and above-max_cr checks stay).

**Cast-time refusals (rules only; documented, not validated):** `polymorph: target is a shapechanger`, `target has 0 hit points`,
`target already transformed`, `feature not usable in this form`.

Tests: `tests/validate/test_v4_validator.py` `RG12Transforms`, `tests/validate/cases/polymorph_*`,
`tests/validate/effect_cases/v4_wild_shape_*` / `v4_shapechanger_*`, `tests/validate/test_union_arms.py` `TransformArmsRG12`.

## 10. Feature kind `slot_recovery` (open kind, VERSION_BUMP_BATCH 7.2n, sim #148)

**Status: loaded.** sim #148 (`features.cpp` `apply_slot_recovery`) reads it, so it is an open kind like the R-G7 / R-G14 kinds
(`checks_v4.OPEN_EFFECT_KINDS_RG7`; no minimum version, active at @3/@4, inert at @1/@2). The former reserved warning is gone.
The schema arm checks the shape; `checks_v4.check_slot_recovery` mirrors the loader errors the arm cannot express.

```json
"effect": { "kind": "slot_recovery", "class": "wizard", "max_slot_level": 5, "uses": 1 }
```

| part | rule | schema / validator |
|---|---|---|
| `class` | `wizard` \| `druid`, required | schema enum (loader: `slot_recovery class must be wizard or druid`); validator error `<id>: slot_recovery class '...' is not in classes[]` when no `classes[]` entry has that class (always on a monster), `slot_recovery class level out of range 1..20` when the summed class level is outside 1..20, and `slot_recovery declared twice for class '...'` for a second declaration of one class (all loader wording) |
| `max_slot_level` | integer 1..9, default 5 | schema + float rejection |
| `uses` | integer 1..255, default 1 | schema + float rejection |
| other keys | ignored (the effect object is open, append-only) | none |

**Loader** (sim #148): `SlotRecoverySpec { feature_id, class_level = summed level of the classes[] entries named by class,
max_slot_level, uses }`; after a successful `sim::short_rest` the caller's choice goes to `rules5e::apply_slot_recovery`. The budget,
ceil(class level / 2), is derived, not authored.

Content: `srd51.character.wizard_level_1..5` carry `arcane_recovery`; `srd51.character.druid_level_2..5` carry `natural_recovery`
(`class druid`). Cases: `tests/validate/effect_cases/*slot_recovery*`.

## 11. Feature kind `gaze` (Petrifying Gaze, VERSION_BUMP_BATCH 7.2p, sim #153)

`monster@4` / `character@4` trait or feature effect, compiled by sim `content.cpp` `compile_gaze` (#153) into a synthetic Save
`SpellDef` and a `Combatant::Gaze{spell_id, range_ft}`. Not a registry kind and not in the frozen 3 -> 4 list: the sim gates it at
@4 itself, so a file that declares `@3` or older lists it inert (validator: unknown-kind warning there). Schema: the closed inline
`gaze` arm of `common@2 $defs/effect` (no new `$defs` entry: the VERSION_BUMP_BATCH 1.1 def list is unchanged) (`effect_live` excludes `gaze`, so only the closed arm accepts it).

```json
"effect": { "kind": "gaze", "range_ft": 30, "save": { "ability": "con", "dc": 12 }, "condition": "restrained",
            "repeat_save": "end_of_turn", "escalates": true, "escalate_to": "petrified" }
```

| key | rule | loader default / error (`<id>: gaze '<fid>': ...`) |
|---|---|---|
| `range_ft` | required, integer multiple of 5 in 5..65530 | `bad range_ft` |
| `save` | required, `{ability, dc 1..30}` (common@2 `$defs/save`) | `missing save` / `bad save` |
| `condition` | common@1 condition | default `restrained`; `bad condition` |
| `repeat_save` | `end_of_turn` \| `start_of_turn` \| `none` | default `end_of_turn`; `bad repeat_save` |
| `escalates` | boolean | default false: a failed repeat save turns `condition` into `escalate_to` |
| `escalate_to` | common@1 condition | default `petrified`; `bad escalate_to` |
| `escalate_margin` | integer 0..30 | default 0; failing the first save by this much or more gives `escalate_to` at once (medusa 5); `bad escalate_margin` |

Validator (`checks_v4.check_gaze`, @4 rows only): the loader's refusals above as errors in its wording (the closed arm also rejects
them, but its `anyOf` message names no field), keys outside the arm as errors, and a **warning** when `condition`, `repeat_save` or
`escalate_to` is omitted (the loader's defaults restrained / end_of_turn / petrified then apply silently). The averting choice, the
sight check (`gaze_seen`) and the turn-start `GazeResponse` are the sim's (7.2p hand-off), not content. Cases:
`tests/validate/effect_cases/*gaze*`.

## 12. Character senses and racial Darkvision (EC #236, validator `checks_v4.check_race_senses`)

PC senses use the stat block's existing `senses` object (`common@1 $defs/senses`, required on character@2 / @3 / @4 since those versions): `senses.darkvision` / `blindsight` / `tremorsense` / `truesight` are integer feet, `passive_perception` stays 10 + Perception. No schema change, no version bump: EC #236's loader reads the same block for characters and monsters into `Combatant::darkvision_ft` ... `truesight_ft` (0 when absent; never hashed).

The validator ties the race string to Darkvision (character@2+, errors):

| race strings | rule |
|---|---|
| `dwarf`, `hill_dwarf`, `elf`, `high_elf`, `gnome`, `rock_gnome`, `half_elf`, `half_orc`, `tiefling` | `senses.darkvision` must be 60: `race '<race>': senses.darkvision must be 60 (SRD racial Darkvision), got <value>` |
| `human`, `halfling`, `lightfoot_halfling`, `dragonborn` | no `senses.darkvision`: `race '<race>': senses.darkvision <value> not allowed (the race has no Darkvision in SRD 5.1)` |

Other race strings are not checked. The level-1 hit point check adds SRD Dwarven Toughness (+1 per level) for `hill_dwarf`. Sample PCs: `srd51.character.sample_hill_dwarf_cleric_1`, `sample_high_elf_wizard_1`, `sample_tiefling_warlock_1` (`characters/sample_*.json`).


## 13. Light gear (item@2 category `gear`, docs/proposals/EQUIPMENT.md 6.2)

srd51.core carries the SRD 5.1 light sources as item@2 `gear` (5e-database a6212be, costs in cp, weights in oz, descriptions verbatim):

| Item | `light` | Notes |
|---|---|---|
| `srd51.item.torch` | 20 / 20 radius, 600 rounds, no fuel (consumed) | `torch_attack {damage [{flat 1, fire}], requires_lit}` |
| `srd51.item.candle` | 5 / 5 radius, 600 rounds, no fuel | |
| `srd51.item.lamp` | 15 / 30 radius, 3600 rounds, fuel `srd51.item.oil_flask` | |
| `srd51.item.lantern_hooded` | 30 / 30 radius, 3600 rounds, oil | state `hooded` (bright 0, dim 5), `toggle: action` |
| `srd51.item.lantern_bullseye` | 60 / 60 cone (length = bright_ft), 3600 rounds, oil | |
| `srd51.item.oil_flask` | none | the fuel; splash / thrown / pooled-fire effects are not modelled yet |
| `srd51.item.tinderbox` | none | |

- `dim_ft` is additional beyond `bright_ft`. 1 hour = 600 rounds, and 6 hours per flask = 3600.
- Every light source has `lit_by_default: false`, `hands: 1` and `light.preset` `srd51.core.light.<name>` (`torch`, `candle`, `lamp`, `lantern_hooded`, `lantern_bullseye`), the keys Render's #233 / #255 glow palette uses. No `visual` is authored, so the built-in styles apply. **Lighting takes an action** (Tinderbox: "Using it to light a torch--or anything else with abundant, exposed fuel--takes an action").
- **Inert until equipment produces light (after #244).** The sim does not load item documents. Listing these items in `equipment[]` changes no fight, and Session `carried_lights` / `pc_torches` remain the light producers. Tests: `tests/validate/test_srd51_equipment.py` (`LIGHT_GEAR`).

## 14. srd51.core monsters at monster@5 with `derive: true` (docs/proposals/EQUIPMENT.md 5)

All 33 srd51.core monster@4 files are now monster@5 with `derive: true`. `goblin` stays at monster@3 for fl1 compatibility. The printed `armor_class.value`, `to_hit` and `damage[0].dice` are kept and become assertions. The validator (`checks_v4.check_monster_derive`, a mirror of `rules5e::derive_monster`) uses the 5.6 strings.

- **Armor and shield** are `equipment[]` entries with `equipped: true`, taken from the printed AC note.
- **Weapons:** every weapon attack names its `weapon` (an item id, or `#local`), and the item is listed in `equipment[]`. The melee weapon is equipped; ranged backups and javelins are carried.
- **`use`** (added to the monster@5 action): `two_hands` for a versatile die or a two-handed melee weapon, `thrown` for a thrown use. The default is `one_hand`. It is explicit, never inferred from the action id.
- **Natural attacks:** `natural {dice, ability}`. Gelatinous Cube Pseudopod has `damage_ability: "none"`.
- **Local weapons:** lizardfolk Heavy Club 1d6 and Spiked Shield 1d6, grimlock Spiked Bone Club 1d4. The 1d4 rider stays as `damage[1]`, unchecked.
- **Structure:**
  - `natural_armor`: animated armor 18, basilisk 13, black bear 11, black dragon wyrmling 15, boar 11, dire wolf 12, gargoyle 15, giant spider 11, lizardfolk 13, medusa 13, wolf 11.
  - Ogre: `weapon_dice_multiplier: 2`.
  - Bugbear Brute: `{kind: weapon_extra_dice, dice: 1, melee_only: true}`. It is read only by the derivation, so the sim still uses the printed dice.
- **`hands`** is explicit: 0 for beasts, the dragon, the cube, the gargoyle, the swarm, the toad and the spider; 2 otherwise.
- **The 4 approved adjustments**, which the validator lists as warnings:
  - skeleton `ac_adjust +1` ("armor scraps");
  - black bear bite and claws `attack_bonus 3`;
  - ghoul bite `attack_bonus 2`;
  - mule hooves `attack_bonus 2` (bestiary v0.1 batch 2: SRD prints +2, derived +4; the printed value wins wherever printed and derived differ, with a test DEVIATIONS entry).
- **The loader:** until Engine Core wires `derive`, it uses the printed numbers exactly as written, so no fight changes. `rules5e::derive_monster` reproduces all 33 with no errors (cross-checked locally).

## 15. srd51.core PCs at character@5 with `derive: true` (docs/proposals/EQUIPMENT.md 1, 2, 3; PR C)

All 63 srd51.core PCs are now character@5 with `derive: true`. The sample pack's character@3 stays as it is: @3 -> @5 is not an identity migration, and the vertical slice goldens read it.

- **Equipment:**
  - The equipped weapon has `hand: "main"` and the equipped shield `hand: "off"`. Backup weapons are carried.
  - Every weapon attack names its `weapon` (an `equipment[]` item) and `use` (`two_hands` for two-handed and versatile grips, `thrown` for thrown uses). These are character@5 action keys, appended like monster@5's. The monk's `unarmed_strike` stays underived (an engine built-in).
- **Ammunition:**
  - Ammunition weapons are item@2 with `ammo_kind`.
  - Starting ammunition is a stack: 20 `srd51.item.arrow` / `crossbow_bolt` / `sling_bullet`, or 50 `blowgun_needle`, replacing the descriptive `arrows_20` / `crossbow_bolts_20` strings.
  - The ammunition items are per piece. `weight_oz` is omitted, and so is the sling bullet's `cost_cp` (not whole numbers).
- **Proficiencies:**
  - `weapon_proficiencies` / `armor_proficiencies` are the SRD class plus race grants. The mirror is `checks_v4.SRD_CLASS_PROFICIENCIES` / `SRD_RACE_WEAPON_PROFICIENCIES`, pinned to `tests/validate/data/srd51_proficiencies_5e_database_a6212be.json`.
  - A named weapon covered by `simple` / `martial` is not listed again.
  - A missing grant is an error: `<id>: <key> missing '<entry>' (SRD 5.1 class / race)`. An extra one is a warning: `<id>: <key> '<entry>' is not granted by the SRD 5.1 class / race`.
- **Derivation** (`checks_v4.check_character_derive`, rules5e `compute_ac` / `derive_weapon_attack` semantics):
  - AC comes from the equipped armor and shield, plus feature bases: barbarian and monk `unarmored_defense` (the monk's is lost with a shield), `draconic_resilience` 13 + Dex, and `armor_of_shadows` (Mage Armor counted as in force).
  - To-hit adds PB only with proficiency. Monk Martial Arts uses the higher of Str / Dex with monk weapons.
  - Feature rows (Defense, Archery, Dueling) are applied by the engine on top, so they are never part of the printed numbers.
  - The strings follow the 5.6 forms (`<id>: derived AC ...`, `<id>.<action>: derived to-hit ...`, `... derived damage ...`).
  - All 63 PCs reproduce their printed AC and attacks (also cross-checked locally against the rules5e functions).

## 16. Ammunition containers (item@2 gear `container`, EQUIPMENT 2.5)

These are SRD 5.1 containers from 5e-database a6212be, with exact costs, weights and descriptions:

| Item | Cost / weight | `container.holds` |
|---|---|---|
| `srd51.item.quiver` | 1 gp / 1 lb | arrow 20 |
| `srd51.item.case_crossbow_bolt` | 1 gp / 1 lb | bolt 20 |
| `srd51.item.pouch` | 5 sp / 1 lb | sling_bullet 20 or needle 50 (one kind at a time) |

- The SRD has no blowgun needle case: the Pouch is the needle container.
- `container` is `{holds: [{ammo_kind, capacity}]}`, gear only, with unique kinds. A list was needed because the Pouch's capacity differs per kind.
- **Ammunition pieces** (`srd51.item.arrow`, `crossbow_bolt`, `sling_bullet`, `blowgun_needle`):
  - Arrows and bolts cost 5 cp each, needles 2 cp each.
  - The sling bullet (0.2 cp) has no `cost_cp`, and no piece has `weight_oz` (none is a whole number of ounces).
- **Ready for the ammunition recovery rule** (EQUIPMENT 2.5: spend one per attack; recover `floor(spent / 2)` after combat). Stacks are `equipment[]` counts (§15), and these containers give the capacity a stack fits in.
- **Inert today:** the sim loads no item documents. PCs do not list containers yet; adding them changes no number.

## 17. Burrow speed (`speed.burrow`)

`common@1 $defs/speed` already accepts optional `burrow` (the same `feet` def as `fly`, `swim` and `climb`: integer 0..255, multiple of 5). The loader (`ContentLibrary::add`) reads it for monsters and characters into `rules5e::Combatant::burrow_ft` (`uint8_t`, 0 when the key is absent, not an integer, or negative; values above 255 clamp to 255), the same way it reads `speed.climb`. `FormStats::burrow_ft` is copied with the other speeds when a form is saved, applied or built, so a transform replaces it and a revert restores it. A shapechanger form's `speed` object still replaces only `walk`, the same as it does for `climb`.

It is static content and is not folded into `summons_digest` or `Combat::hash`, the same way `fly_ft`, `swim_ft` and `climb_ft` are not. Since altitude PR2 (feat/altitude-move) burrow **is a movement mode**: `ActionRequest::move_mode` 4 spends the burrow speed through `move_allowance`, budgeted like fly / swim / climb (the turn's budget is shared, Dash grants it again), and a creature without a burrow speed cannot burrow (`no such speed`; the sim refuses going below the ground). A burrowed creature (`alt_ft < 0` on walkable, non-water ground) moves under difficult terrain, ledges and surface creatures at 5 ft per tile. **Tunnelling** (leaving a passable tunnel, burrowing through solid rock or walls) is still out of scope unless the stat block says otherwise. No content-schema version bump and no new sim-event tag: the key was already optional, and mode 4 is a new value of the existing 0xC3 `move_mode` fold.

## 18. Spellcasting focus family (item@2 gear `focus`)

`focus: "arcane" | "druidic" | "holy"` is an optional, closed, additive key on item@2 `gear` (no version bump; gear arm only, `test_union_arms.py` row). It marks the SRD spellcasting foci:

| `focus` | srd51.core items |
|---|---|
| `arcane` | `arcane_focus_crystal`, `_orb`, `_rod`, `_staff`, `_wand` |
| `druidic` | `druidic_focus_sprig_of_mistletoe`, `_totem`, `_wooden_staff`, `_yew_wand` |
| `holy` | `holy_symbol_amulet`, `_emblem`, `_reliquary` |

- The component pouch carries no `focus`: it holds materials, it is not a focus.
- **Inert until rules reads it.** No C++ reads the key yet. The sim loads gear as `ItemKind::Other`, not `ItemKind::Focus` (EQUIPMENT 3.2: focus hand, worn holy symbol, emblem on a shield). Adding it changes no fight and no hash.
- Tests: `tests/validate/test_srd51_equipment.py` (`FOCUS_FAMILY`, `test_focus_field_schema`).
- Deferred equipment mechanics are listed in `docs/SRD_EQUIPMENT_DEFERRED.md`.

## 19. Flat action damage (monster@5)

SRD stat blocks such as rat, cat, owl, crab, and scorpion print a fixed amount: "Hit: 1 piercing damage." A monster@5 action
`damage[]` component (including `extra_damage`) may be written as `{"flat": N, "type": "<damage_type>"}`, with N between 1 and 255.

- `$defs/monster_damage` in `schemas/monster/v5.schema.json` is a `oneOf` of the common@2 `damage` component (`dice`) and the
  flat arm. A component has `flat` or `dice`, never both. The flat arm is closed: no `bonus_vs`, and no separate modifier field.
  A modifier belongs in the dice string (`"1d4+2"`), and `flat` is already the whole amount.
- The loader maps `flat: N` to `DiceExpr{0, 0, N}`. With count 0, `roll_dice` draws no RNG, and a critical hit does not double the
  amount (only dice double).
- Use `flat` only for amounts printed without dice. Do not write `"dice": "1d1"`, because a crit doubles it.
- Only monster@5 accepts it. monster@4, character, and common@2 are unchanged. Archetype data is static, so there is no hash
  format change and no SIM_EVENTS tag.
- Mixed components are fine: `[{"flat": 1, "type": "piercing"}, {"dice": "3d4", "type": "poison"}]`.
- **Natural weapon source.** monster@5 attacks name their source with `weapon` or `natural`, and that rule is unchanged. For a flat
  attack, `natural` takes `flat` instead of `dice`: `"natural": {"flat": 1, "ability": "dex"}`. `natural.flat` (1..255) and
  `natural.dice` (NdM) are exclusive (`oneOf`). Never write `"dice": "1"`. `flat` is the whole amount, so `damage_ability` is not
  allowed with it. The validator requires `natural.flat` to equal `damage[0].flat`
  (`/actions/<i>: natural.flat N != damage[0] flat M`). With `derive: true`, the derived damage is the flat amount and the to-hit
  is derived from `ability` as usual. Use `attack_bonus` with a reason when the printed to-hit differs (the SRD Cat prints +0).
  Example: `tests/validate/fixtures/monster_flat/monsters/cat.json`:

  ```json
  {"id": "claws", "name": "Claws", "kind": "melee_weapon_attack",
   "natural": {"flat": 1, "ability": "dex"},
   "attack_bonus": {"value": 0, "reason": "SRD Cat Claws prints +0; Dex 15 + PB 2 = +4"},
   "to_hit": 0, "reach": 5, "targets": 1,
   "damage": [{"flat": 1, "type": "slashing"}],
   "description": "Melee Weapon Attack: +0 to hit, reach 5 ft., one target. Hit: 1 slashing damage."}
  ```

## 21. Printed-damage override and size-gated save riders (monster@5, Gladiator)

- **`damage_override {dice, reason}`** is an optional monster@5 action key, the damage counterpart of `attack_bonus`.
  - It replaces the derived `damage[0]` (it is not a delta) when the derivation can't reach the stat block's printed damage. `reason` is required and must cite the SRD 5.1 stat block.
  - It is an error when the reason is blank, or when the derived damage already matches ("damage_override is not needed"). The validator lists every override as a warning, as it does for `attack_bonus`.
  - Brute (`weapon_extra_dice`, `melee_only: true`) adds its die on melee use only. The SRD Bugbear Javelin prints "9 (2d6 + 2) piercing damage in melee or 5 (1d6 + 2) piercing damage at range", so the bugbear stays as printed and needs no override.
  - The SRD Gladiator Spear prints a single line for both uses: "Melee or Ranged Weapon Attack: +7 to hit, reach 5 ft. or range 20/60 ft., one target. Hit: 11 (2d6 + 4) piercing damage". Its `use: thrown` action therefore carries `damage_override {dice: "2d6+4", reason: ...}`. There is no thrown variant of Brute.
- **Size gate on a `save_condition` rider.** The first `apply[]` entry may carry `max_size` without `contain`.
  - SRD Gladiator Shield Bash: "If the target is a Medium or smaller creature, it must succeed on a DC 15 Strength saving throw or be knocked prone."
  - A target larger than `max_size` makes no save and gets no condition. The hit's damage still applies.
  - On a later `apply[]` entry, or on a `save_effect` action without `contain`, `max_size` is a load error (`checks_v4.check_save_max_size` mirrors the sim loader).
  - There is no new state, so no hash change.

## 22. Item `material` (item@1 / item@2 weapon, armor, shield)

Optional on the weapon, armor, and shield arms of `item@1` and `item@2`. Gear, ammunition, and valuable do not take it (the arm `propertyNames` lists omit it; `tests/validate/test_union_arms.py`).

| value | meaning |
|---|---|
| `metal` | manufactured metal |
| `wood` | wood |
| `leather` | leather |
| `hide` | hide |
| `cloth` | cloth |
| `rope` | rope |

**When `material` is absent, `manufactured_metal()` falls back to the old category rule.** Omitting the key is `ItemMaterial::Absent`. The predicate (`engine/rules/src/encounter.cpp`) then uses the category rule that used to sit at encounter.cpp ~3078: any weapon, armor, or shield counts as manufactured metal. Only an explicit non-metal value (`wood`, `leather`, `hide`, `cloth`, `rope`) removes a Heat Metal or rust target. An unknown string is not one of those values. `material_of` (`engine/core/sim/src/equipment.cpp`) leaves it `ItemMaterial::Absent` and emits a loader warning on `ContentLibrary::warnings()` (the same channel as a summon `max_count` that clips a CR band). It does not map the string to a non-metal material. No `item@1` / `item@2` bump: an old file that omits the key keeps the meaning it had.

Heat Metal and rust-type effects key off `manufactured_metal()`. The predicate is true when the kind is weapon, armor, or shield and the material is `metal` or absent. It is false for an explicit non-metal value. `legal_actions` and `cast_spell_impl` share it, so a listed Heat Metal cast is one `equip_item` per object the cast would accept: a hand instance whose def is manufactured metal, or worn armor whose def id is manufactured metal. A hand whose instance equals the armor def id wins, so a non-metal object in that hand does not fall through to the armor. Worn armor cannot be dropped. Ammunition, a light, a focus, and a weapon, armor, or shield with an explicit non-metal material are refused with `heat_metal: no metal object`. Rust Metal is not authored yet; when it is, it uses this predicate rather than item category.

A weapon with a metal head or blade counts as a manufactured metal object. The spear and the javelin are `metal`, matching the pike, the halberd, the glaive, the lance, and the crossbows. All-wood or otherwise non-metal gear stays non-metal: club, greatclub, quarterstaff, shortbow, longbow, sling, blowgun, a dart when it is all wood (otherwise `metal`; the srd51 dart is metal), whip (`leather`) and net (`rope`), and padded, leather, studded leather, and hide armor. The generic shield is `metal` (the SRD allows wood or metal; metal is the default, so the shield is a Heat Metal target). srd51.core sets the key on every weapon, armor, and shield (`tests/validate/test_srd51_equipment.py` `MATERIAL`).

Versioning: appended, no tag change, no migration, no `api_level` gate. The field is not hashed, so goldens are unchanged. The justification and the Elf (tools/content) sign-off are in `docs/proposals/VERSION_BUMP_BATCH.md` (7.2zh follow-up).

<!-- Code, schema and tests cite this section by its title ("Attack ability"), not its number. -->
## 23. Attack ability (monster@5 action `ability`, Strength Drain ruling)

An optional monster@5 weapon-attack key: `"ability": "str" | "dex" | "none"`. It names the ability the printed `to_hit` and damage
use; `none` means no ability modifier. It is read only when it matters: when a score is reduced (Strength Drain recomputes the
attack at use time by the modifier change) and for the two-weapon off-hand.

- **Present: it wins** (over a finesse property and over inference).
- **Absent: still valid, no version bump.** The loader falls back in this order (Lich ruling):
  1. a ranged weapon attack (`ranged_weapon_attack`) that is not thrown (no `thrown` on the action, the item, or the `weapons[]`
     entry it names) uses Dex;
  2. a finesse weapon (action `properties`, or the item / `weapons[]` entry it names) uses the better of Str / Dex;
  3. otherwise it is inferred from the number: Str if `to_hit` = Str mod + PB, else Dex if = Dex mod + PB, else none. When both
     match (an ambiguous Str/Dex tie) it picks Str and emits a loader warning (`ContentLibrary::warnings()`); set `ability` to
     silence it.
- Any other value is a load error; on a monster@4 or older file the key is a load error; on a non-weapon action the validator
  refuses it (`checks_v4.BATCH3_WEAPON_ONLY_KEYS`).
- Two-weapon fighting (SRD 5.1): the off-hand bonus attack adds the attack's effective ability modifier to damage only when it is
  negative (the Two-Weapon Fighting style still adds it in full).
