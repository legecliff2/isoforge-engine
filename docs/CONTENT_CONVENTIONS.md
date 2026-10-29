# Content conventions the sim compiles (timed modifiers, bonus-attack options, R-G7 reactions and feature kinds, R-G11 zones, R-G8 grapple rider, R-G15 spell kinds, priced materials, R-G16 encounter surprise, slot recovery, headless scenario@1)

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

Druid gap G (monster@5) carries the clauses this section used to leave in `description`: `restrains` ("until this grapple ends, the target is restrained"), `exclusive` ("can't bite another target"), and `max_grapples` ("each claw can grapple only one target"). Giant Crab's Claw is `max_grapples: 2` with `escape_dc` 11 and no `restrains` (the SRD grapples only). Restrained with an escape on a non-grapple attack (giant spider Web) stays an `attack_effect` apply, not this rider.

`max_size` (`tiny` .. `gargantuan`, monster@5 only) is the largest creature the hit grapples. A larger target takes the attack's damage and is not grappled. Omit it for no cap beyond the existing one-size-larger rule. Chuul's pincer is `large`; an otyugh's tentacle is `medium`. It is not an extended rider field: a size-only grapple leaves `via_attack` unset, so `Combat::hash` is unchanged. A bad string is `on_hit: bad grapple max_size`. Below monster@5 the loader says `grapple max_size needs a monster@5 file`. The one-size-larger rule still applies on top of this cap.

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

Elf Keen Senses is proficiency in Perception, already carried by `skills.perception`. It is not the monster trait in §30.


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

Burrowed creatures pass under the surface per the approved altitude engine conventions, docs/SRD_COMBAT_COVERAGE.md §11.8 "Altitude engine conventions (approved)".

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

## 20. SRD tools (item@2 gear `tool_kind`)

(Sections 17 to 19 are taken: 17 burrow (#290), 18 the gear `focus` key (#292), 19 flat damage.) SRD 5.1 tools are item@2 `gear` with an optional, closed, additive `tool_kind`. It takes one of four values, which mirror the 5e-database `tool_category`:

| `tool_kind` | SRD category |
|---|---|
| `artisans_tools` | Artisan's Tools |
| `gaming_set` | Gaming Sets |
| `musical_instrument` | Musical Instrument |
| `other_tools` | Other Tools (navigator's, thieves') |

- **Why a field, not a new category:** a key on the gear arm keeps every gear key (`hands`, `description`, ...) and needs no union arm. It follows the same pattern as `container` and `focus`, and needs no version bump.
- **Content:** ids are the snake_case of the 5e-database index; names, costs, weights and descriptions are verbatim.
- **SRD 5.1 has no** disguise, forgery, herbalism or poisoner's kit, so none is authored.
- **Inert:** no tool-proficiency checks exist. No validator check, rules code or sim reads `tool_kind`, and characters' tool proficiencies stay descriptive.
- **Healer's kit** is SRD adventuring gear (kits), not a tool, so it is plain gear with no `tool_kind`.
- **Tests:** `tests/validate/test_srd51_equipment.py` (`TOOLS`, `test_tools`, `test_tool_kind_schema`).

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

§20-21 are reserved for in-flight branches.

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

## 24. Headless encounter scenarios (`scenario@1`)

Not a pack document and not a migrated content type. The C++ `SchemaRegistry` does not register it. `tools/validate/validate_packs.py` loads `schemas/scenario/v1.schema.json` on the default `content_validate` run (no pack arguments) and checks each `scenarios/<name>/scenario.json`. `tools/encounter_runner` parses the file itself. See `tools/encounter_runner/README.md`.

| field | rule |
|---|---|
| `id` | dotted id, unique under `scenarios/` |
| `seed` | integer 0 .. 2^64-1. The encounter RNG seed |
| `pack` | pack id under `content/packs`. Archetypes resolve through that pack and its depends |
| `board` | exactly one of `chunk` (pack-relative `chunk@` file, no `..`) or `map` (open 32×32 floor, optional `blocked` tiles that block move and sight) |
| `party`, `monsters` | at least one each. `id`, `archetype`, `tile` `{x,y}` in 0..31, optional `team` (default `party` / `monsters`). Party archetypes are characters; monster archetypes are monsters. Ids and tiles are unique. At least two teams |
| `surprise` | roster ids, or `{hiders: [{who, stealth}]}` (`stealth` is a Dexterity (Stealth) total, -32768..32767). An empty array surprises nobody |
| `end_condition` | `teams` (a team with nobody conscious), `no_pc_conscious`, or `no_pc_or_no_opponent` |
| `round_cap` | integer 1..10000 |

Roster order is party, then monsters. Stable ids are `spawn_stable_id(chunk coord, index)` in that order. A chunk file's own spawns are replaced. An open map is chunk coord `(0,0,0)`.

The runner applies surprise before the first command, then `sim::set_scenario_end`. The built-in policy runs first. Round cap: when `round_cap != 0` and `state.round > round_cap`, and the built-in policy returned nothing, the outcome is `EndReason::Custom` (a cap of N fires at the start of round N+1). `ScenarioEnd` is replay input (`replay(..., surprised_at_start(), scenario_end())`). It is not an input to `Combat::hash`. The `EncounterEnded` event it emits (`a` = `EndReason`) is folded with the rest of the event stream, so a custom end changes the digest only through that event.

Both sides use `sim::NearestEnemyPolicy`. The same seed and pack contents print the same stdout. `golden.txt` is that stdout without `--log`, LF. `encounter_scenario_<name>` runs it twice against the golden.

Directory name: `^[a-z][a-z0-9_]*$`. Integers only. `golden.txt` must be non-empty. Explicit pack arguments to the validator skip this check, so the pack summary line is unchanged.

Parked monsters are soft-blocked from these playable encounters. A roster `archetype` on `party` or `monsters` whose id is a Deferred row in `docs/SRD_MONSTERS_DEFERRED.md` prints `warning <scenario>: <slot>: archetype '<id>' is Parked ... and is soft-blocked from this playable encounter`. The warning does not fail the scenario. Authored rows, including **partial** rows, are not parked. `srd51.monster.goblin` is not in that ledger. The check reads the ledger; it does not add a stat block and does not approximate the blocking mechanic. `encounter_runner` still loads whatever the pack contains. A Deferred id with no monster file fails the existing archetype lookup, and the soft-block warning is printed as well.

## 25. Printed passive Perception (monster@5 `passive_perception_reason`, Tier-A)

- `senses.passive_perception` must equal 10 + Perception (the `skills.perception` value, else the Wis modifier). On a
  monster@5 file a mismatch is allowed only with top-level `passive_perception_reason: "srd_printed"` (the SRD 5.1 printed
  value; validator warning instead of an error). The reason on a matching value, any other value, or on a non-monster@5 file
  is refused.
- The sim stores the printed value in `Combatant::passive_perception_override` (static content, never hashed) and
  `rules5e::passive_perception` uses it instead of 10 + Perception (Hide, hidden-from, surprise). It shifts by the same
  effective-Wis modifier delta as the derived value (a Wis reduction moves both), and caller bonuses (dim light -5) still apply.
- Printed SRD passives never include Keen Senses (wolf, hell hound: 10 + Perception exactly). The keen hook (§30) adds
  +5 only on a Wisdom (Perception) check that relies on a listed sense, and on the Hide / surprise passives described there.
  It never changes `passive_perception` or the printed number.
- SRD 5.1 data blocks whose printed passive differs from 10 + Perception (5e-database copy): black bear, blink dog,
  half-red dragon veteran, spider, swarm of ravens. Blink dog carries the reason (`srd_printed`, Perception +3, passive 10); the others are not authored.

## 26. Save-effect fail-by margin, success immunity, must see (monster@5, Tier-A)

- `also_on_fail_by: {margin, condition}` (save_effect, monster@5): failing the save by `margin` (1..30) or more ALSO applies
  `condition` on the same effect entry, so it has the primary's duration and ending the entry (expiry or a successful repeat
  save) ends both. It differs from gaze `escalate_margin`, which replaces. It needs a tracked primary condition in apply[]
  (not prone untracked, not until_cured) that differs from it; no escalates / control / area. When the target is immune to
  the primary condition, the margin still applies `condition` alone (Lich ruling).
- `immune_on_success: {hours, scope}`: a success makes the target immune to this action for `hours` (1..8760):
  `same_action` = every creature using this archetype's action (SRD "all mummies"; keyed by the action's spell_id, so a
  different archetype's glare is a different action), `same_source` = that creature only. State: `EncounterState::action_immunities`,
  hashed under 0xE1 only when non-empty (separate from the ghast aura 0xDE). An immune target makes no roll.
- `target_must_see_source: true`: a target that cannot see the user (blinded, or the sight hook) makes no roll and gains no
  immunity; FeatureTriggered (actor user, target, a = spell_id, c = 3).
- Exemptions (undead, elves) are authored only where the SRD text says so; Dreadful Glare has none.
- Mummy Multiattack (glare + fist) is deferred: the glare is its own action (SRD_COMBAT_COVERAGE).

## 27. Grapple instead of damage (monster@5 grapple rider `instead_of_damage`, Tier-A)

SRD 5.1 vampire spawn Claws: "Instead of dealing damage, the vampire can grapple the target (escape DC 13)." Author it as the
attack's `on_hit` grapple rider with `"instead_of_damage": true` (a bool; the loader and validator refuse it together with
`restrains`, and the validator warns when the attack has other on_hit riders). It is a per-attack choice:
`ActionRequest::grapple_instead` (log_hash 0xE2). With the choice, a hit (a crit too) grapples and deals no damage: no damage
riders, no concentration save; non-damage on-hit riders still fire; AttackResolved c bit 8 marks it. Without it the attack
deals damage and does not grapple. The request is refused on an attack without such a rider. The driver AI picks the grapple
only when a Bite (grappled_by_self gated attack) is still left in the multiattack and in reach, and the target is not already
grappled by the attacker, incapacitated or restrained, and the claw's average damage would not drop it.

## 28. Text-only spell effect `descriptive` (spell@1)

`{"kind":"descriptive"}` is a spell@1 effect. The object has no other keys, and it is the spell's only effect. An empty `effects` array is still an error (schema `minItems: 1`; the loader says `effects missing`).

The spell loads and stays on a creature's spell list. `legal_actions` never offers it. `cast_spell` refuses it (`descriptive spells cannot be cast`), including an innate cast. The innate AI skips it. Effect resolution does nothing: no roll, no state, no event. There is no `SIM_EVENTS` tag and goldens are unchanged.

`schemas/spell/v1.schema.json` `$defs/effect` adds `descriptive` to the kind enum and a `oneOf` arm that allows only `kind`. `tools/validate/checks_v4.py` `check_spell_descriptive` and `tools/validate/validate_packs.py` `check_spell` use the loader's wording and order (`engine/rules/content/src/spell_loader.cpp`):

| case | error |
|---|---|
| any key besides `kind` on the first effect | `descriptive effect: unknown key '<key>'` (the schema also rejects it: additional properties are not allowed) |
| mixed with another effect, either order | `descriptive effect must be the spell's only effect` |
| `effects` empty or absent | schema error (`[] should be non-empty`, or `'effects' is a required property`). The loader's string for an empty list is `effects missing` |

**No version bump.** `docs/CONTENT_COMPAT.md` §1 bumps a schema when a field that used to be descriptive becomes mechanical, or an existing field changes meaning. `descriptive` is a new effect-kind value. The previous `spell@1` kind enum did not include it, and the arm is closed, so no existing file can carry it. No tag change, no migration, no `api_level` gate. The decision is recorded in `docs/proposals/VERSION_BUMP_BATCH.md`.

## 29. Monster area action (`area_effect`, monster@5)

A monster area ability is an action of kind `area_effect`. The loader (`compile_area_action`) builds one synthetic `SpellDef` (`is_spell` false, kind `Save`, fixed `save.dc`) and a `SaveAction` row, the same path a `save_effect` with an `area` already uses. Targets come from `Combat::area_targets`. No new recharge roll and no `SIM_EVENTS` tag.

- **Shapes.** `area.shape` is `cone`, `line`, `cube`, `sphere`, or `cylinder`. `size_ft` is the cone or line length, the cube side, or the sphere or cylinder radius, a multiple of 5. A line needs `width_ft`. A cylinder needs `height_ft`; that height is stored on the spell and still ignored by membership (the cylinder-height gap). Other shapes must not carry the other shape's extra field.
- **Origin.** `self` (the default) starts at the caster. `range_ft` is forbidden then. `point` needs `range_ft` and centres a cube or a sphere on the aim tile.
- **Save and damage.** `save` `{ability, dc}` is required. `damage[]` needs `on_success` `half` (`OnSave::Half`, integer division) or `none` (`OnSave::None`). A breath that deals no damage (blinding, sleep, slow) uses `apply[]` and no `on_success`.
- **Uses.** `usage` is the existing object: `recharge` with `min` 2..6 (`ActionUse`, the turn-start d6), or `per_day` / `per_rest` (`FeatureUses`). One action is one use. Two shapes that share a recharge (young brass, young copper) are not two `area_effect` actions; that choice is not this key.
- **Not this kind.** `save_effect` is unchanged, including breaths already authored that way. Whirlwind is its own action (§40), not an area template. Whelm and ghast Stench stay on the zones work. Cover on area saves, the square sphere, and cylinder height are not changed here.

monster@4 and character files cannot carry `area_effect`. No schema version bump: the kind is a new monster@5 enum value, and no existing file uses it.

## 30. Keen Senses (monster@5 trait `keen_senses`)

SRD 5.1 wolf: "The wolf has advantage on Wisdom (Perception) checks that rely on hearing or smell." The same sentence, with a different sense list, is Keen Sight, Keen Smell, Keen Hearing, Keen Hearing and Sight, Keen Sight and Smell, and the pseudodragon's Keen Senses (sight, hearing, or smell). The list is the mechanic. The trait name is not.

This is not the elf racial trait of the same name (§12: proficiency in Perception). Death dog Two-Headed is advantage on every Wisdom (Perception) check, not on a named sense; that blanket advantage stays deferred (the save half is already `save_advantage`).

Author it on a monster@5 trait:

```json
"effect": { "kind": "keen_senses", "senses": ["hearing", "smell"] }
```

`senses` is a non-empty set of `sight`, `hearing`, and `smell` (unique). Several traits on one creature union the bits. A second trait that adds no new sense does nothing (validator warning). The loader refuses the kind on a character and on a file below monster@5. Wolf is stamped (`hearing`, `smell`). Other stat blocks that still carry the sentence as description only do not gain the bonus until an author adds the effect. Hell hound and winter wolf are in the pack with that sentence and no effect (the monster tests pin it); their printed passives, like the wolf's, are 10 + Perception and stay that.

The loader stores `Combatant::keen_senses` (bits `kKeenSight` 1, `kKeenHearing` 2, `kKeenSmell` 4). Static content, not hashed. No `SIM_EVENTS` tag. `perception_senses` on an ability check is not folded into `log_hash` (same as `skill`, `dc`, and `check_mode`). Replay stores the request. A fight that never sets the mask hashes as before. When the mask changes who is hidden, that is the existing 0xC5 fold.

`rules5e::passive_perception` is unchanged: 10 + Perception, or the §25 printed override, shifted by a Wisdom reduction. Keen Senses never adds 5 to that number. SRD passive checks add 5 only for a check that has advantage, and only on the paths below. Two listed senses on one check are still one advantage (+5), never +10.

**Named sense.** Advantage applies only when the Wisdom (Perception) check relies on a sense the creature has. A smell-only check does not gain Keen Hearing. A sight-only check does not gain Keen Smell.

**Sight, and the vision hook.** Dim light, other light obscurement, and Sunlight Sensitivity still impose the existing −5 (`passive_sight_penalty`); those two sources do not stack. Keen Sight's advantage applies only when the viewer perceives the subject with sight (`Sight::sense == Sight`). On that same sight check, advantage and the −5 cancel (passive +0, a normal roll). While the subject is unseen, Keen Sight does not raise the opposing passive: the viewer is not seeing them. The engine still does not auto-fail a sight check for the blinded condition, or a hearing check for the deafened condition. Deafened only removes the hearing path (no Keen Hearing, and Hide will not treat the subject as heard).

**Hide / unseen.** The creature is still hidden only from a hostile that cannot see it. The Stealth total is compared with the better of:

- the sight passive: `passive_perception` plus the existing sight penalty (0 or −5). Keen Sight does not add +5 on this unseen path;
- the keen non-sight passive: `passive_perception` + 5, when a listed non-sight sense can be used.

Hearing can be used when the subject is audible and the viewer is not Deafened. On this path the viewer cannot see the subject, so `Sight::perceives` is that fact. Smell can be used on this opposing check whenever the creature has Keen Smell. SRD Keen Smell states no range, and the vision model has no smell fact, so range, wind, and cover for smell are deferred rather than invented. The two passives are never summed. A creature with no keen senses is unchanged (the sight passive only, including the −5).

**Surprise.** `surprised_by_stealth` has no per-pair light or audible fact. Keen Hearing (unless Deafened) and Keen Smell add +5 to the passive it compares, still not stacked, and still not written into the printed passive. Keen Sight does not: there is no light fact, and dim light might have cancelled it. Audible gating is the Hide hook only.

**Active Perception.** `ActionKind::AbilityCheck` with `skill` Perception and `perception_senses` naming the senses that check relies on (the same bits). Zero means the check names no sense, and Keen Senses does not apply; the caller may still pass `check_mode`. With a target, light, sunlight, and whether sight works come from `sight(actor, target)` (no sight hook: everyone sees everyone, as today). With no target there is no light penalty, and a named sight check is treated as sight working. The field is refused on any other action or skill, and if any other bit is set. Active Search as its own action stays unmodelled; this is the roll.

**Forms.** Wild Shape and Polymorph copy the beast's `keen_senses` through `FormStats` and restore the creature's own on revert. A druid in wolf form uses the wolf's hearing and smell. The druid's Wisdom is still the one Wild Shape keeps.

**Not this.** Magic Resistance, elf Perception proficiency, death dog's blanket Perception advantage, revealing a hidden creature by noise, and a smell range.

## 31. Regeneration (monster@5 trait `regen`)

SRD 5.1 troll: "The troll regains 10 hit points at the start of its turn. If the troll takes acid or fire damage, this trait doesn't function at the start of the troll's next turn. The troll dies only if it starts its turn with 0 hit points and doesn't regenerate."

That sentence is the mechanic. The trait name is not. Author it on a monster@5 trait:

```json
"effect": { "kind": "regen", "amount": 10, "suppressed_by": ["acid", "fire"] }
```

`amount` is an integer 1..32767, the hit points regained, capped at the creature's hit point maximum. `suppressed_by` is a non-empty unique list of damage types (the common@1 enum). The loader stores one `Combatant::regeneration`: `amount`, and `suppressed_by` as a bitmask over `DamageType` in that enum's order. A second trait with the same amount and the same set adds nothing (validator warning). A second trait that differs is a load error (`a creature has one regeneration`). The loader refuses the kind on a character and on a file below monster@5.

At the start of the creature's turn, a listed damage type taken since its previous turn started suppresses the heal. Any other damage does not. A resistance or immunity that reduces the hit points dealt to 0 leaves that type out of `DamageApplied::types_dealt`, so it does not suppress. Otherwise the creature regains `amount`. Dropping to 0 hit points does not kill it. It stays in the fight until a later turn of its own starts. Suppressed at that start, it dies. Clear at that start, it regenerates and stands. Massive damage still kills at once. A creature with no `regen` effect dies at 0 hit points as usual. The rules tests are `engine/rules/tests/regeneration_tests.cpp`. The content path is `engine/core/sim/tests/regeneration_content_tests.cpp` (`features.cpp` `apply_regen`, dispatched from `content.cpp`).

`amount` and `suppressed_by` are static content, copied onto the combatant at spawn. They are not hashed. `regen_suppressed` and `regen_down` are the existing rules flags for "took a suppressing type since its last turn started" and "at 0 hit points, waiting for its turn." This kind does not fold them and does not claim a `SIM_EVENTS` tag. The next free hash tag stays **0xE3**. A fight whose creatures have no regeneration hashes as before. A completed heal or a death is already in hit points and `dead`. Replay of a loaded regenerator reproduces the heal from a logged hit. The 0 HP death is the same rules path; the sim test sets hit points for that case the way the rules tests do, because a content file sets only the starting hit points, so it cannot start a creature at 0.

**Not this kind.** Oni and the shield guardian regain hit points only while they already have at least 1, with no damage-type list and with an ordinary death at 0. Loading `regen` on them would keep them up at 0. Radiant damage does suppress vampire spawn. The spawn still stays off `regen`: it heals only while it has at least 1 hit point, sunlight and running water also stop it, and it dies at 0. Rejuvenation (lich, ghost) is a different trait. Wild Shape and Polymorph do not copy `regeneration`: `FormStats` does not carry it, and `build_form` does not write it. The stat block is `content/packs/srd51.core/monsters/troll.json` (`srd51.monster.troll`): this effect, amount 10, `suppressed_by` acid and fire, and Keen Smell as `keen_senses` with `smell` (§30). The deferred troll row is unparked. Oni, the shield guardian, and vampire spawn still do not carry `regen`.

## 32. Presentation visual and clip registry (character@6 / monster@6, Engine Core M0)

`visual` is optional on `character@6` and `monster@6`. Migration 5 -> 6 is identity: older files do not gain a `visual` key. The loader still resolves a presentation for every stat block it accepts.

| Authored | Resolved |
|---|---|
| `visual` absent, `type` `humanoid` | `kind` paperdoll, `kit` `paperdoll`, empty `standin_bin` |
| `visual` absent, any other type, size tiny / small / medium / large | `kind` standin, `standin_bin` from size, `kit` `standin.<standin_bin>` |
| `visual` absent, size huge or gargantuan | `kind` unbinned, empty `kit` (no M0 art bin) |
| `visual.kind` / `visual.standin_bin` | override the default. `standin_bin` is `tiny`, `small`, `medium`, or `large` |
| `visual.kit` | kit id. Wins over `presentation.sprite` |
| `presentation.sprite` | kit id when `visual.kit` is absent |
| `visual.art_root` | pack-relative directory under `art/`. When `ContentLibrary::add` is given the pack directory, the directory must exist. `..` is refused |
| `visual.size_short` | stored on `CreatureVisual`. `kSizeShortRuntime` is false, so M0 does not swap `_short` plates and does not scale |

`CreatureVisual` sits on `Archetype`. `ActorView` copies `visual_kind` (`"paperdoll"` / `"standin"` / `"unbinned"`), `kit`, `standin_bin`, `art_root`, and `size_short`. `ActorView::clip` is the clip-clock request (Render & Platform). There is no `hard_land` field; that name is only a registry alias. None of these fields are written back into sim, and none are folded into `Combat::hash` or `log_hash`. No new `SIM_EVENTS` tag. A humanoid with no visual block, including the sample goblin, resolves to paperdoll, not `standin.<size>`.

The clip registry is `content/packs/srd51.core/art/clips.json` (not a manifest content document; `validate_packs` skips `art/`). `ContentLibrary::load` reads `<pack>/art/clips.json` when the file is present. Every alias target must be a clip id in that kit. `ClipRegistry::resolve` maps `-` to `_` and is the only alias path. The paperdoll kit resolves `hard_land` / `hard-land` to `prone_idle`, `dodge` to `guard`, `knockback` to `react`, and swim / fly idle and move to `idle` / `walk`. The stand-in kits (`standin.tiny` through `standin.large`) resolve `climb` and `crawl` to `walk`, `hide` to `idle`, `melee` / `melee_near` / `melee_far` to `attack`, `hard_land` / `hard-land` to `fall_impact` (not `prone_idle`), and an unknown name to `idle`.

Facing stays the four iso values `SouthEast`, `SouthWest`, `NorthEast`, `NorthWest`. Art still authors eight. A committed `TileMoved` or a later aim / attack sets `ActorView::facing` from the tile step (`+x` SouthEast, `+y` SouthWest). That state lives on the presentation feed, not on the combatant. `SouthWest` and `NorthWest` are the west mirror. Huge and gargantuan bins, `size_short` runtime, and an 8-value facing enum are not M0.

## 33. Save grappled by the user, and grapple `max_size` (monster@5)

Chuul Tentacles and Otyugh Tentacle Slam name creatures the monster has grappled. A monster@5 `save_effect` may set `target_requires_any: ["grappled_by_self"]` and nothing else from that enum. Each creature the cast names must be grappled by the user (`condition_source` of Grappled is the user). Any other target refuses the cast before the action is spent (`target must be grappled by the user`). `legal_actions` does not list those targets. The built-in AI skips the save when its current target fails the gate, and uses it when that target passes.

`targets` on that save is an integer 1..8 (default 1): how many creatures one cast may name. Tentacle Slam sets it high enough to name each creature the tentacles are holding. The engine does not add a grappled creature the caller left out, and one creature outside the gate refuses the whole cast. Other `target_requires_any` values stay weapon-attack-only. An area save cannot carry the key (`target_requires_any takes no area`). Below monster@5 the loader says `target_requires_any on a save_effect needs a monster@5 file`. The bit is `SpellDef::target_requires_any` (`kTargetGateGrappledBySelf`). Static content, not hashed. No new `SIM_EVENTS` tag.

Grapple `max_size` is §5. Chuul's pincer is Large or smaller; an otyugh's tentacle is Medium or smaller.

**Not this key.** A negative gate ("one target not grappled by the user", giant crocodile tail, ankheg Acid Spray) is not `grappled_by_self`. `attack_effect` still has no such gate (giant toad / giant frog Swallow). Otyugh Tentacle Slam is that standalone save on `srd51.monster.otyugh` (`targets` 2, `range_ft` 10, half damage, stunned until the end of the otyugh's next turn). It is not a free rider.

**Multiattack free rider (B3b, chuul Tentacles).** A monster@5 multiattack option may set `free_rider` to the id of a `rider_only: true` save_effect on the same creature. The option is still two (or more) attack indices. When those attacks finish, `TurnBudget::free_rider` is that save's spell id until it is used or another action is spent. The cast does not spend an action. The save's `grappled_by_self` gate is checked at that moment, so a pincer in this same Multiattack that just established the grapple counts, and so does a grapple already held. One use; one named target (`targets` stays 1 for chuul). It is not listed in `legal_actions` and the built-in AI does not cast it as its own action. Outside that window the cast is refused (`only a multiattack rider`) and the action is not spent. A refused target does not consume the use. `count_dice` cannot be combined with `free_rider`. A `rider_only` save that no option names, or a `free_rider` that does not name one, is a load error. Below monster@5 the loader says `multiattack free_rider needs a monster@5 file` or `rider_only needs a monster@5 file`. Not hashed: the cast is an ordinary `CastSpell` in the log, and replay re-arms the window. No new `SIM_EVENTS` tag.

**Paralyzed while poisoned.** The first `apply[]` entry of that save may set `while` to a different condition (`poisoned`, `while: paralyzed`). The primary condition's effect holds both. A successful repeat save, or the timer (1 minute is 10 rounds), ends both. Immunity to the primary condition applies neither (unlike `also_on_fail_by`). `while` needs a tracked primary condition, takes no area, and takes no `also_on_fail_by` or `control`. On a later `apply[]` entry the loader says `while goes on the first apply[] entry`.

Chuul is authored (`srd51.monster.chuul`): pincer `max_size: large` and `max_grapples: 2`, Multiattack `pincer` ×2 with `free_rider: tentacles`. Amphibious and Sense Magic stay descriptive.

Otyugh is authored (`srd51.monster.otyugh`): tentacle `max_size: medium`, `max_grapples: 2`, and `restrains` (escape DC 13). Tentacle Slam is its own action. `range_ft` 10 is the tentacle reach; the slam text names no separate distance, and the cast does not add a grappled creature the caller left out. Limited Telepathy stays descriptive.

## 34. Area-save petrify escalation (`escalates` on `save_effect`, monster@5)

SRD 5.1 Gorgon Petrifying Breath is the basilisk sentence on a cone, not a gaze. A failed save restrains. The restrained target repeats the save at the end of its next turn. Success ends the effect. Failure replaces restrained with petrified. That is `SpellDef::escalates` / `escalate_to` / `escalate_margin`, which the save resolver already applies. The keys now compile on a monster@5 `save_effect`, including one with an `area`. Gaze (§11) is unchanged.

```json
"kind": "save_effect", "save": { "ability": "con", "dc": 13 }, "origin": "self",
"area": { "shape": "cone", "size_ft": 30 },
"apply": [ { "condition": "restrained", "repeat_save": "end_of_turn" } ],
"escalates": true, "escalate_to": "petrified", "escalate_margin": 0,
"usage": { "kind": "recharge", "min": 5 }
}
```

| key | rule | loader error (`<id>: save_effect '<aid>': ...`) |
|---|---|---|
| `escalates` | boolean, required true to turn the path on | `escalates must be a boolean`; `escalate_to / escalate_margin need escalates` |
| `escalate_to` | common@1 condition; omitted means `petrified` | `bad escalate_to`; `escalate_to must differ from the primary condition` |
| `escalate_margin` | integer 0..30; omitted means 0 | `bad escalate_margin`. Above 0, failing the first save by that much applies `escalate_to` at once (medusa). Gorgon is 0 |

The primary `apply[]` entry must be a tracked condition with a repeat save (`end_of_turn`, `start_of_turn`, `on_damage`, or `until_save`). Further `apply[]` entries, `while`, `control`, and `also_on_fail_by` are refused. A `save_condition` rider is refused (`escalates is a save_effect action, not a rider`). Below monster@5: `escalates / escalate_to / escalate_margin need a monster@5 file`. `area_effect` does not gain the keys; breaths that already use `save_effect` with an `area` do.

No new hash and no new `SIM_EVENTS` tag. `ActiveSpellEffect::escalates` is already folded under 0xAA while the repeat is pending, and cleared when the effect petrifies or ends.

`srd51.monster.gorgon` authors the breath (cone 30 ft, Con DC 13, recharge 5–6, margin 0). "Until freed by the greater restoration spell" stays description only, as basilisk. Trampling Charge is the gore's `on_hit` (`moved_at_least_ft` 20, Str DC 16, prone untimed; elk / boar, so it does not also fire on hooves). The bonus-action hooves against the prone target stays a partial, the same gap as lion and tiger Pounce's bonus bite. "Straight toward" is not checked.

## 35. Damageless restraining grapple, and pull (monster@5, B5)

SRD 5.1 roper Tendril: "Hit: The target is grappled (escape DC 15). Until the grapple ends, the target is restrained". The hit never deals damage. That is not §27. §27 is a choice (`instead_of_damage`, `ActionRequest::grapple_instead`, log_hash 0xE2), and the loader still refuses that choice together with `restrains`.

Author the tendril as a weapon attack with no `damage` array and an `on_hit` grapple rider `"no_damage": true` (a bool). A hit grapples and deals no damage: no damage roll, no concentration save from this attack. It is not a request bit. `no_damage` may combine with `restrains`, `exclusive`, `max_grapples`, and `max_size`. It cannot combine with `instead_of_damage`. Listing `damage` or `half_hp_damage` on that attack is a load error (`grapple no_damage can't list damage`). Below monster@5 the loader says `grapple no_damage needs a monster@5 file`. A bad bool is `bad grapple no_damage`.

`no_damage` is an extended rider, so the grapple records `via_attack`. The lasting state folds under the existing 0xDB when `via_attack` is set. There is no new flag bit and no new `SIM_EVENTS` tag. The grapple ends when the two creatures are farther apart than that attack's reach (a plain Grapple action, `via_attack` 0xFF, stays 5 feet). A 50-foot tendril still holds a creature a 25-foot pull left at 25 feet.

**Pull.** SRD Reel: "The roper pulls each creature grappled by it up to 25 feet straight toward it." A monster@5 action:

```json
{ "id": "reel", "name": "Reel", "kind": "pull", "feet": 25, "targets": 6,
  "target_requires_any": ["grappled_by_self"] }
```

`feet` is 5..60, a multiple of 5. `targets` is 1..8 (default 1): how many creatures one use may name. `target_requires_any` is `["grappled_by_self"]` and nothing else. Each named creature must be grappled by the user. One that is not refuses the use before the action is spent (`target must be grappled by the user`). The engine does not add a grappled creature the caller left out. There is no save and no damage. The pull is toward the user (`ForcedMoveSpec` mode 2) and `up_to`. Omitting `ActionRequest::forced_feet` pulls the full distance. A multiple of 5 up to `feet` pulls that far and folds under the existing 0xB4. There is no second range and no sight check: being grappled is the constraint. Below monster@5 the loader says `pull needs a monster@5 file`.

This is not a `free_rider`. A free rider runs after every attack in the option, and only for a `rider_only` save. Reel inside the roper's Multiattack sits between the tendril attacks and the bite. That order is not this action. `srd51.monster.roper` stays deferred (SRD_MONSTERS_DEFERRED).

Not hashed beyond 0xB4 and 0xDB. No new tag. B5 left **0xE3** free; B6 claims it (§36). Next free is **0xE4**.

## 36. Bite disease, 24-hour hit point maximum (monster@5 `save_condition` `disease`)

SRD 5.1 otyugh Bite (the death dog is the same schedule at DC 12): "it must succeed on a DC 15 Constitution saving throw against disease or become poisoned until the disease is cured. Every 24 hours that elapse, the target must repeat the saving throw, reducing its hit point maximum by 5 (1d10) on a failure. The disease is cured on a success. The target dies if the disease reduces its hit point maximum to 0. This reduction to the target's hit point maximum lasts until the disease is cured." The 5 is the average. The roll is 1d10.

Author it on the bite's `on_hit` `save_condition`, not as a second save:

```json
"on_hit": [{
  "kind": "save_condition",
  "save": {"ability": "con", "dc": 15},
  "apply": [{"condition": "poisoned", "duration": {"kind": "until_cured"}}],
  "disease": {"every_hours": 24, "hp_max": "1d10"}
}]
```

`every_hours` is 24. `hp_max` is dice (`1d10` for both printed bites). The save is Constitution. `apply` is exactly one `until_cured` poisoned entry. The rider lists no `damage`. It is an `on_hit` rider, not an `on_event` and not a `save_effect`. Below monster@5 the loader says `disease needs a monster@5 file`.

A failed save applies poisoned (the existing untracked `until_cured` bit) and contracts one disease for that rider's spell id. A second failed save from the same rider does not add another row and does not reset the clock. A successful bite save does not cure a disease already contracted. Immunity to poisoned skips the condition and still contracts the schedule: the hit point maximum loss is the disease, not the condition.

The clock is the encounter round. 24 hours is 14400 rounds (`hours × 600`, the same hour as ghast Stench and Dreadful Glare). On the diseased creature's turn, once `round` has reached `next_round`, it repeats that Constitution save. Failure rolls `hp_max`, adds it to that disease's reduction, and schedules another 24 hours. Current hit points above the new maximum are lost. A maximum of 0 kills. Success removes that disease's reduction and, when no other disease and no tracked effect still applies poisoned, removes poisoned. Current hit points do not rise when the maximum does.

`effective_hp_max` subtracts Life Drain and this reduction. A long rest clears Life Drain only. This reduction stays until that disease is cured.

`Combat::hash` tag **0xE3** folds the rows, then any `disease_hp_max_reduction`, only while one of them exists. Empty state hashes as before. Next free is **0xE4**.

Not this key. Mummy rot lowers the maximum by 3d6 every 24 hours with no repeat save and no cure-on-success; `curse` is still only `no_regain_hp`. Lesser Restoration ends the poisoned condition and does not cure this disease. Lay on Hands' disease spend and Divine Health are not wired. Greater Restoration's hit point maximum option clears Life Drain only. Two `until_cured` poisons without a disease row share one condition bit; curing a disease clears that bit unless another disease row or a tracked effect still applies it. `srd51.monster.otyugh` authors the bite with this object (Con DC 15). Death dog stays on its poisoned-only rider until T&C adds `disease`.

## 37. Both-hits Engulf (monster@5 multiattack `engulf_both_hits`)

SRD 5.1 Shambling Mound Multiattack: "The shambling mound makes two slam attacks. If both attacks hit a Medium or smaller target, the target is grappled (escape DC 14), and the shambling mound uses its Engulf on it." Engulf: "The shambling mound engulfs a Medium or smaller creature grappled by it. The engulfed target is blinded, restrained, and unable to breathe, and it must succeed on a DC 14 Constitution saving throw at the start of each of the mound's turns or take 13 (2d8 + 4) bludgeoning damage. If the mound moves, the engulfed target moves with it. The mound can have only one creature engulfed at a time."

A monster@5 multiattack option may set `engulf_both_hits`. The option lists at least two attacks. When every attack in that option hits the same creature, the engine applies a gelatinous-cube contain (kind `engulf`) to that creature. No save is rolled to avoid it. It is not a `free_rider` and not an action. One attack missing, or the attacks hitting different creatures, does not engulf. A target larger than `max_size` takes the hits' damage and is not engulfed. Ending the turn before the last attack forfeits it.

```json
"options": [{
  "id": "slams",
  "attacks": ["slam", "slam"],
  "engulf_both_hits": {
    "max_size": "medium",
    "escape_dc": 14,
    "while": "blinded",
    "damage": {"dice": "2d8+4", "type": "bludgeoning"}
  }
}]
```

| key | rule | loader error |
|---|---|---|
| `max_size` | `tiny` .. `gargantuan` | `bad engulf_both_hits max_size` |
| `escape_dc` | integer 1..30. A Strength check, the cube's escape (special 4) | `bad engulf_both_hits escape_dc` |
| `while` | `blinded` only | `engulf_both_hits while must be blinded` |
| `damage` | `{dice, type}` dealt at the start of each of the user's turns | `engulf_both_hits needs damage {dice, type}` / `bad engulf_both_hits damage` |

The condition is restrained. Blinded lasts as long as that effect (`while`, the B3b one-entry rule). Immunity to restrained applies neither. Immunity to blinded still restrains and contains. `contain_one` refuses a second engulf while this user already holds any contained creature. The first one stays. Escape (special 4, Strength vs `escape_dc`) ends restrained, blinded, and the contain. The user's death frees the creature and does not knock it prone (that is swallow).

`count_dice` and `free_rider` cannot combine with this key. Below monster@5 the loader says `multiattack engulf_both_hits needs a monster@5 file`. The synthetic spell id is `<archetype>#<option>#engulf`. It is not in `spells_known` and it is not a `save_action`.

No new `Combat::hash` tag and no new `SIM_EVENTS` tag. The contain effect's turn-start damage is `damage_when` 3 under the existing 0xAC. Blinded is the effect's `while`, derived from the SpellDef. The both-hits window is `TurnBudget` and is not hashed; replay re-runs the attacks. Next free stays **0xE4**.

Product calls, not a second contain model:

* The printed "grappled (escape DC 14)" is the Strength escape check on the contain. A sourced Grappled condition is not added. Two escapes would be a second action the stat block does not print. Drag-by-grapple is not this sentence.
* Turn-start damage is automatic, the gelatinous cube's contain damage. The printed "DC 14 Constitution saving throw or take 13 (2d8 + 4)" is not rolled. The dice are the printed `2d8+4`.
* "Unable to breathe" is the existing suffocation gap (coverage row 11). Not modelled.
* "If the mound moves, the engulfed target moves with it" is the existing contain carry. `container_of` is set. Sim movement of contained creatures stays pending.
* Lightning Absorption (lightning damage heals instead) has no peer in tree. It is not this key.

This section does not author `srd51.monster.shambling_mound`. The row stays deferred.

## 38. Deadly Leap (monster@5 action `deadly_leap`)

SRD 5.1 Bulette Deadly Leap: "If the bulette jumps at least 15 feet as part of its movement, it can then use this action to land on its feet in a space that contains one or more other creatures. Each of those creatures must succeed on a DC 16 Strength or Dexterity saving throw (target's choice) or be knocked prone and take 14 (3d8 + 1) bludgeoning damage plus 14 (3d8 + 1) piercing damage. On a successful save, the creature takes only half the damage, isn't knocked prone, and is pushed 5 feet out of the bulette's space into an unoccupied space of the creature's choice. If no unoccupied space is within range, the creature instead falls prone in the bulette's space."

It is its own action. It is not a Multiattack rider and not a `free_rider`. A monster@5 action:

```json
{
  "id": "deadly_leap",
  "name": "Deadly Leap",
  "kind": "deadly_leap",
  "jump_ft": 15,
  "save": { "ability": "str", "dc": 16 },
  "save_alt": "dex",
  "damage": [
    { "dice": "3d8+1", "type": "bludgeoning" },
    { "dice": "3d8+1", "type": "piercing" }
  ],
  "push_ft": 5
}
```

| key | rule | loader error |
|---|---|---|
| `jump_ft` | 5..60, a multiple of 5. The turn's `jumped_ft` must already be at least this | `deadly_leap: jump_ft must be 5..60, a multiple of 5` |
| `save` | `{ability, dc}`. `dc` is 1..30. This ability is used on a tie | `deadly_leap: missing save` / `deadly_leap: bad save` |
| `save_alt` | the other ability. Must differ from `save.ability` | `deadly_leap: save_alt must be a different ability` |
| `damage` | exactly two `{dice, type}` packets. One save covers both | `deadly_leap: damage needs two components` / `deadly_leap: bad damage` |
| `push_ft` | 5..30, a multiple of 5. The success push | `deadly_leap: push_ft must be 5..30, a multiple of 5` |

`on_success`, `apply`, and `usage` are not keys. Half damage, prone, and at-will are the sentence. Below monster@5 the loader says `deadly_leap needs a monster@5 file`. The synthetic spell id is `<archetype>#<action id>`. It is a `save_action` and it is in `spells_known`. The cast is `ActionKind::CastSpell` of that id. It spends the action. A cast that has not jumped far enough, or that names the user, is refused with the action unspent.

The two packets are one roll each, shared by every target (`also_damage`, the Ice Storm peer: one save, not two). Half is integer division. A creature killed by the first packet does not take the second. Resistance, immunity, and vulnerability are the existing per-type mitigation. The damage is not magical. Prone on a failure is untracked, the Shove peer. Immunity to prone skips the condition. The damage still applies.

`log_hash` tag **0xE4** folds `ActionRequest::jump_ft` on a Move, only when it is non-zero. `TurnBudget::jumped_ft` is not hashed. Replay re-runs the moves. Next free is **0xE5**.

Product calls:

* **Jump distance.** A completed own-turn Move may set `jump_ft`: a multiple of 5, at most `5 * move_tiles` (the distance of the step, not its terrain cost). Those feet add to `jumped_ft`. They are not a second movement spend (`move_ft` is the cost). An interrupted move does not add them, the same place `moved_ft` is added. A prone creature cannot jump. A walk with `jump_ft` 0 does not qualify, including a walk of 15 feet or more. This is not the Charge / Pounce `moved_ft` proxy. Strength, a 10-foot running start, and high jump are not checked. The built-in pathing does not set `jump_ft`. A caller does.
* **Landing.** A Step with `jump_ft` greater than 0 may enter and end in another creature's space, including a hostile fewer than two sizes away. The occupied tile still costs as difficult terrain (the existing through-creature cost). The action does not move the token. Sim requires every named target's footprint to overlap the leaper's current footprint (`target is not in the landing space`). Rules does not see tiles: the named targets are the creatures in the space. The engine does not add a creature the caller left out. At most 8, the existing multi-target save cap. There is no size gate. Overlap is the space, so a Large leaper's footprint is the space. An ally may be named. `legal_actions` lists hostiles only, the existing save-action list, and only once `jumped_ft` meets the gate.
* **Str or Dex.** At the roll, each target uses whichever of the two abilities has the higher `save_modifier` (ability modifier, plus proficiency when proficient in that save). A tie uses `save.ability` (Strength for the printed leap). The choice is not a request field and is not hashed. Cover's bonus and Evasion apply only when Dexterity is the one rolled (the existing Dexterity save rules). Legendary Resistance can turn a failure into a success (the existing rule). A caller cannot pick the lower bonus.
* **Push.** A success queues the existing push (mode 1): straight away from the leaper's centre, coincident centres break toward +x, exactly `push_ft`, not "up to". That walk is the creature's choice of unoccupied space. There is no per-creature destination. If the hook moves 0 feet, the creature falls prone in the space unless it is immune to prone, and it still has the half damage. With no hook the push is reported as the full distance (the existing no-hook rule), so the blocked case is the sim.
* **End of turn.** The existing refusal to end a turn in another creature's space still applies to the leaper. A failed save, or a push that cannot move, leaves that creature in the space, so EndTurn is refused while a step out exists. The leap does not grant a lasting exception. A creature that already has Air Form or Water Form (§39) may keep a hostile's space. The leap's push still needs an unoccupied tile, and that form does not make its own tile unoccupied.
* **AI.** The built-in AI does not declare a jump and does not use this action.

This section does not author `srd51.monster.bulette`. Burrow is not this key. The row stays deferred.

## 39. Air Form / Water Form (monster trait ids `air_form`, `water_form`)

SRD 5.1 Air Form, as the deferred ledger states it: the elemental can enter a hostile creature's space and stop there. It can move through a space as narrow as 1 inch wide without squeezing. SRD 5.1 Water Form is the same two sentences. The ledger's water row parks Whelm and Freeze and does not restate the movement sentence; the movement sentence is the Air Form sentence.

A descriptive trait is enough. No effect object. The Flyby peer (`traits[].id`), not a new effect kind.

```json
{ "id": "air_form", "name": "Air Form", "description": "The elemental can enter a hostile creature's space and stop there. It can move through a space as narrow as 1 inch wide without squeezing." }
```

`water_form` is the same object with that id and the name Water Form. Either id sets `Combatant::enter_hostile_space`. Both on one creature are still one flag. Any other id, including the authored `fire_form`, does not.

The flag is static content. It is not hashed. No `SIM_EVENTS` tag. Next free stays **0xE5**. Replay reloads the trait with the archetype.

Product calls:

* **Who may overlap whom.** Only the mover with the flag. It may enter a hostile creature's space at any size difference, including fewer than two sizes apart, and it may stop there. It may end its turn there. The other creature does not gain the exception: a same-size hostile still cannot enter the elemental's space, and a creature two or more sizes apart may pass through and still may not stop. A swarm is unchanged (both ways, `occupied_except`).
* **Hostile only.** `hostile` is a different team, the existing test. A nonhostile space is the existing rule: the elemental may move through an ally, and it may not willingly stop there. A footprint that overlaps a hostile and a nonhostile may not stop. Two allied elementals therefore may not both stop in one hostile's space, because each would also be stopping in its ally's space. Two elementals that are hostile to each other may both stop in the same hostile's space.
* **Difficult terrain.** The occupied tile still costs as difficult terrain (the existing through-creature cost, 10 ft on open ground). The form does not waive it. An airborne or burrowed mover still ignores that cost (the existing altitude rule). A vertical span that does not overlap is not in the way.
* **Deadly Leap.** A jump step (`jump_ft` > 0) may still end in any creature's space, and EndTurn still refuses that overlap for a leaper that lacks this flag while a step out exists (§38). The form is not granted by the leap. A success push still walks to an unoccupied tile (`occupied_except`). The form does not make the elemental's tile unoccupied, and it does not let the push end inside another creature. A push that moves 0 feet still knocks the target prone in the space.
* **Pathing.** `occupied_except` still lists the other creature's tiles. The built-in pathing, readied moves, teleports, summons, and forced movement do not route through them. That is the size-space audit's existing deferral of AI movement through spaces. A caller Steps. The built-in AI does not seek the overlap.
* **Forms.** The flag is not a `FormStats` field, the Flyby peer. A transform does not add it or clear it.
* **1 inch.** Deferred. The grid has no sub-tile openings (the swarm Tiny-openings peer, and the authored ooze / shadow / Fire Form sentences that only describe the gap). Squeezing stays the existing Large+ opt-in. This flag is not squeezing and does not open a 1-inch gap.

`fire_form` on the authored fire elemental includes the enter-and-stop clause and also the 1d10 contact fire. This flag does not apply that damage, so `fire_form` is not this id. The fire row stays partial. Whirlwind is §40. Whelm and Freeze are not this key.

This section does not author `srd51.monster.air_elemental` or `srd51.monster.water_elemental`. Counts stay 195 authored and 15 deferred.

## 40. Whirlwind (monster@5 action `whirlwind`)

SRD 5.1 Air Elemental Whirlwind, as dnd5eapi states the action: "Whirlwind (Recharge 4–6). Each creature in the elemental's space must make a DC 13 Strength saving throw. On a failure, a target takes 15 (3d8 + 2) bludgeoning damage and is flung up 20 feet away from the elemental in a random direction and knocked prone. If a thrown target strikes an object, such as a wall or floor, the target takes 3 (1d6) bludgeoning damage for every 10 feet it was thrown. If the target is thrown at another creature, that creature must succeed on a DC 13 Dexterity saving throw or take the same damage and be knocked prone. If the saving throw is successful, the target takes half the bludgeoning damage and isn't flung away or knocked prone."

The success sentence is the Strength save. The Dexterity clause stays "or take the same damage and be knocked prone."

It is its own action. It is not a Multiattack rider, not an `area_effect`, and not Whelm.

```json
{
  "id": "whirlwind",
  "name": "Whirlwind",
  "kind": "whirlwind",
  "save": { "ability": "str", "dc": 13 },
  "damage": [ { "dice": "3d8+2", "type": "bludgeoning" } ],
  "fling_ft": 20,
  "impact": { "dice": "1d6", "per_ft": 10, "type": "bludgeoning" },
  "impact_save": { "ability": "dex", "dc": 13 },
  "usage": { "kind": "recharge", "min": 4 }
}
```

| key | rule | loader error |
|---|---|---|
| `save` | `{ability, dc}`. `dc` is 1..30. The Strength save | `whirlwind: missing save` / `whirlwind: bad save` |
| `damage` | one `{dice, type}` or `{flat, type}` packet | `whirlwind: damage needs one component` / `whirlwind: bad damage` |
| `fling_ft` | 5..60, a multiple of 5. Up to this far | `whirlwind: fling_ft must be 5..60, a multiple of 5` |
| `impact` | `{dice` or `flat`, `per_ft`, `type}`. `per_ft` is 5..30, a multiple of 5 | `whirlwind: missing impact` / `whirlwind: bad impact` |
| `impact_save` | `{ability, dc}` for the creature thrown into | `whirlwind: missing impact_save` / `whirlwind: bad impact_save` |
| `usage` | `recharge` with `min` 2..6. At-will and pool are refused | `whirlwind: recharge is the sentence` |

`on_success` and `apply` are not keys. Half on the Strength success, prone, and the fling are the sentence. Below monster@5 the loader says `whirlwind needs a monster@5 file`. The synthetic spell id is `<archetype>#<action id>`. It is a `save_action` and it is in `spells_known`. The cast is `ActionKind::CastSpell` of that id. It spends the action and the recharge. A cast that names the user, or a creature that is not in the space, is refused with the action unspent.

One `3d8+2` roll is shared by every named target (the existing save). A success takes half of that roll, integer division (`OnSave::Half`). A failure takes the full roll. A creature killed by that packet is not flung (a dead creature is not moved). Resistance, immunity, and vulnerability are the existing per-type mitigation, applied after the half. The damage is not magical. Prone on a failure is untracked, the Shove peer. Immunity to prone skips the condition. The damage and the fling still apply on a failure. The Dexterity impact save is not this half.

No new `Combat::hash` tag and no new `SIM_EVENTS` tag. The direction is an RNG draw, not a request field. Replay re-runs the save, the d8, and the walk. Next free stays **0xE5**.

Product calls:

* **Strength success.** "If the saving throw is successful, the target takes half the bludgeoning damage and isn't flung away or knocked prone." Half is integer division of the shared roll. `on_save` is `Half`. No fling and no prone. The Dexterity impact save is a different sentence and stays failure-only.
* **In the space.** Footprint overlap, the Deadly Leap landing test (§38). B9 `enter_hostile_space` is how a same-size hostile comes to be there. The action does not require `air_form`. The caller names the creatures. The engine does not add one the caller left out. At most 8. An ally may be named. `legal_actions` lists overlapping hostiles only. Nobody overlapping: one cast with no target. The action and the recharge are spent, and no save and no dice are rolled. A named creature that does not overlap is refused (`target is not in the elemental's space`). The user is not a target.
* **Random direction.** One d8 per creature that fails, in named-target order, from the encounter RNG, after that save and only then. Face 1 is +x and the faces go clockwise: E, SE, S, SW, W, NW, N, NE. That replaces the push's away-from-centre direction. "Away" is the fling along that ray, up to `fling_ft`. A ray that first crosses the elemental's own tiles still counts. Those tiles are not an object and not a creature. The caller cannot choose a shorter fling (`forced_feet` is not this action).
* **Object.** The next step's footprint is not walkable (a wall, or off the map). The thrown creature takes `impact` once per `per_ft` of feet it actually moved before the stop (`feet_moved / per_ft`, integer, one roll of that many dice, or a flat bonus times that count and no roll). Under `per_ft` feet: the strike happens and the dice are 0. A clear path of the full distance strikes nothing.
* **Creature.** The next step holds another creature, not the elemental. The lowest id wins if several share the step. The thrown creature does not take the object packet. That creature makes `impact_save`. A failure takes the same impact packet (the feet the first creature moved, not the `3d8+2`) and is knocked prone. A success takes no damage and is not prone. Not half. Evasion does not apply (it is not a half-on-success Dexterity effect). Cover is not added. Legendary Resistance can turn the failure into a success (the existing save). An object and a creature on the same step: the object wins, and there is no Dexterity save.
* **Floor.** A fling that walks onto a ledge uses the existing fall (1d6 bludgeoning per 10 feet fallen, prone when that damage lands). Whirlwind does not add a second packet for the horizontal feet. A flat landing is not a floor strike.
* **No grid.** With no forced-move hook the fling is reported as the full distance and strikes nothing (the existing no-hook rule). Impact is what the hook reports.
* **Recharge.** Available at the start of combat, the breath-weapon peer. The built-in AI does not use the action.
* **Not this action.** Whelm, Freeze, and the 1-inch gap. This section does not author `srd51.monster.air_elemental`. The row stays deferred until T&C writes the stat block. Counts stay 195 authored and 15 deferred.
