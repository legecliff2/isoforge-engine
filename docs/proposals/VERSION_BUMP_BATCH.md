# Proposal: the version-bump batch (common@2 / monster@4 / character@4 / item@1)

Status: Director decisions recorded (§13); T&C and Engine Core review notes folded in (rev 2). Reviewers: Tools & Content (schemas, validator, loaders,
migrations), Engine Core (sim hooks, event schema, digests). Author: Rules (rules5e).
Sequencing: lands **after #55** (footprint, event schema v6) and #58/#59 (content wiring); this batch is
event schema **v7** (§9).
Basis: `docs/SRD_COMBAT_COVERAGE.md` §11 (batch B rows R-G3..R-G5, R-G7, T&C-G4/G5/G9/G10/G17),
`docs/CONTENT_COMPAT.md`, `docs/SIM_EVENTS.md`, and the code on `main` after #57.

Scope: (1) save-based actions with area templates, (2) on-hit riders and `extra_damage` /
`on_event` triggers, (3) reactions as monster/character actions, (4) monster spellcasting (innate
and slot-based), (5) timed buffs and debuffs, (6) class resource pools. Out of scope, named so
nothing is promised by accident: legendary actions (T&C-G11), elevation (T&C-G12), armor/AC
recomputation from `item@1` equipment is *in* the bump but as its own late PR (§10, §12 row 15).

**Priority rule (Zach): old content must play identically.** Every section below states its
inert-marker behaviour and the compat test that proves it. The design goal for golden hashes is
*no change at all* (§9).

## 0. What is already on main and reused

| Piece | PR | Reused by |
|---|---|---|
| `SpellDef::extra_effects` (`SpellEffect`, `applies_to` riders), `SpellEffectKind::ApplyCondition`, `ActiveSpellEffect` (repeat save, concentration) | earlier (spells) | §2 save actions, §6 modifiers |
| Reaction windows (`BeingHit`, `LeavingReach`, `SpellCast`, `ReadiedTrigger`), `ReactionOption`, `ReactionDecision`, `EncounterHooks::{being_hit_options,spell_cast_options,in_range,allow_chain}`, per-trigger `ReactionPolicy` | earlier (reactions) | §4 |
| `RollTrait` / `SaveSource` (advantage, magic-resistance style traits) | #54 | §2 (breath weapon is *not* magical), §6 |
| `Combatant::regeneration`, `DamageApplied::types_dealt`, damage qualifiers, weapon `properties`, multiattack | #53/#56 | §3 predicates |
| `FeatureUses` (per `(actor, feature_id)`), `UseFeature`, `recover_features`, sim `FeatureRegistry` (static/active hooks, `effect.inert`) | earlier | §7 |
| `inert_mechanics` + `effect.inert` + sim `Archetype::inert_mechanics/inert_features` | earlier (compat) | §8 |

## 1. Versioning decision and the open-effect hazard

`common@1 $defs/effect` is an *open* object: unknown `kind`s are warned and ignored. That makes
several batch-B rows technically additive (a new effect kind in a `character@3` file). Two
consequences drive the structure of this proposal:

1. **The Director decision stands**: first-class save actions, riders, reactions, monster
   spellcasting and equipment ride *one* bump (no interim trait effects; T&C-G4/G5/G9/G10/G17).
2. **Ahead-of-engine hazard.** A pre-bump `@3` file may already carry an effect with a `kind`
   name the engine later learns (authored "ahead of the engine", descriptive today). The moment
   the registry learns the kind, that file would silently change behaviour. Shipped content has
   none (checked: `content/` and `tests/compat/` use only `self_heal`, `damage_bonus`,
   `bonus_action_grant`, spell kinds and the `region_enter` / `reaction` trigger descriptors), but
   third-party packs may. The `3→4` migrations therefore mark *every* effect whose `kind` is in the
   new-kind set (§8.2) with `"inert": true`, the same mechanism the `2→3` migration used. Engine
   Core's registry must also refuse to apply a new kind from a `@≤3` document even if the marker
   is missing (belt and braces; §8.3).

**Rows R-G3 (timed modifiers) and R-G5 (pools) are batch N in §11.** They *can* ship before the
bump on the spell side (`spell@1` gets additive effect kind `apply_modifier`, §6) because spells
have no old meaning to protect. The *trait/feature* surface (Rage, Bardic Inspiration, Ki) is
exactly the "interim trait effect" the Director ruled out, so it lands in `@4` (decided, Q2: one bump, no interim kinds).

### 1.1 Which schema carries what

| Schema | Change | Why it cannot be additive |
|---|---|---|
| `common@2` (new file) | **Only the defs that change**, re-declared locally (this is the complete local list, and the lint in §1.2 checks the doc's own snippets against it): `action` (v2), `legacy_action`, `legacy_nonspecial_action`, `legacy_special_action`, `effect`, `effect_inert`, `effect_live`, `apply_modifier_effect`, `grant_action_effect`, `heal_pool_effect`, `level_table`, `level_table_entry`, `pool_level_table`, `pool_level_table_entry`, `reaction`, `reaction_effect`, `on_event_effect`, `apply`, `item_damage` (`item@1`'s file), `rider`, `predicate`, `scaling`, `usage`, `area`, `save`, `resource`, `modifier`, `modifier_duration`, `damage` (new named def), `rider_damage_type`, `rider_damage`, `spellcasting` (v2, superset of v1), `trait` / `trait_active` (v2), `stat_block` (v2) | `common@1 $defs/action` is `additionalProperties:false`; `reactions`/`bonus_actions` exist in v1 but are descriptive; widening v1 would let `@3` files carry fields the `@3` contract says are inert |
| `monster@4` | `$ref common@2 stat_block`; **every `@3` top-level property is carried** (§1.3) | same, plus gating |
| `character@4` | same; adds `resources[]`, class-feature effect kinds | same |
| `item@1` | new type: weapons, armor, shields (§10) | new type: no old content |
| `spell@1` | **additive only**: effect kind `apply_modifier`; no tag change | open effect array, no old meaning |
| `manifest@1` | **additive**: `api_level` const `1` becomes `enum [1, 2]` (§8.5) | gating for scripts |

### 1.2 How `common@2` is built (standalone, minimal re-declaration)

`common@2` cannot be `allOf [v1 stat_block, overrides]`: `allOf` is an AND, so v1's
`actions.items: $ref action` would still reject new kinds. It is a standalone file that
**re-declares only what changes** and references every unchanged def *absolutely* into v1:

```json
"dice": { "$ref": "../common/v1.schema.json#/$defs/dice" }          // pattern for every unchanged def
```

Rules for the file (enforced, not a convention):

* Every `$ref` to an unchanged def is the literal `../common/v1.schema.json#/$defs/<name>`
  (`dice`, `ability`, `local_id`, `content_id`, `damage_type`, `condition`, `feet`, `range_ft`, ...);
  **all snippets in this document use the literal path** (it is the only reference form that survives
  copy-paste into the file). The **lint test** lands **with row 2** (`tests/validate/`, runs in
  `validate_packs`/ctest) and fails on any *local* `#/$defs/x` ref whose
  target is not in the §1.1 list and declared in the same file, and on any re-declared def that is byte-identical to its v1
  twin (it should be a ref instead). Without it a typo silently resolves to nothing or to a stale copy.
* New named def `damage` (`{dice, type}` object; v1 inlines it in `action`), referenced by `action`,
  `rider`, `reaction`, `item@1` and `spell@1`'s future use. Accepts `damage_type` from v1; riders use
  `rider_damage_type` (v1 enum plus `"same_as_weapon"`; valid only where a weapon is in scope) so the
  v1 enum is never widened.
* `area` mirrors `spell@1`'s inline `area` exactly, including `multipleOf: 5` on `size_ft`,
  `width_ft`, `height_ft` and the `width_ft` (line only) / `height_ft` (cylinder only) if/else rules.
  It adds nothing, so a later change to `spell@1` area stays in step by test (a shared-fixture test
  validates both against the same cases).
* The validator (`tools/validate`) keeps a **per-tag table** `{schema tag -> allowed action kinds,
  effect kinds, cost values, top-level keys}`; `@≥4` rows come from this proposal, and `@≤3` rows are
  frozen. No ad hoc `if version >= 4` branches.

### 1.3 `monster@4` / `character@4` carry every `@3` property

`unevaluatedProperties:false` means a `@4` schema that forgets a `@3` key rejects migrated files.
Both `@4` schemas therefore keep: `challenge` (monster), `behavior.ai`, `presentation`,
`legendary_actions` (**descriptive and ignored by the engine**, T&C-G11 is deferred; same `action`
list type, never loaded), `spellcasting` (v2), `traits`, `race/background/level/classes/xp/features/
equipment` (character), and top-level **`inert_mechanics`** (`array<string>`, unique; declared in
the `@4` schemas explicitly, since the `3→4` migration writes it). A schema test migrates every
`@3` fixture to `@4` and validates the result.

### 1.4 Forward-compat flag for elevation (T&C-G12, deferred)

Elevation is not in this batch, but `@4` documents must not need another bump to opt in later. Both
`@4` schemas accept an optional top-level `forward_compat: array<enum ["elevation"]>` with `uniqueItems: true`, **descriptive
and ignored by sim** (listed with the inert mechanics in the archetype report). `area` stays 2D until
the engine implements elevation; `cylinder.height_ft` is validated but unused by targeting. New enum values widen the `@4` schemas **additively** (no bump). When
elevation lands, sim honours the flag per document, so packs that declare it never change behaviour
for packs that do not.

## 2. Save-based actions with area templates (T&C-G4, R-G10)

### 2.1 Content shape (`common@2 $defs/action`, new kind `save_effect`)

`common@2 $defs/action.kind` is a **strict superset of v1**:
`melee_weapon_attack`, `ranged_weapon_attack`, `melee_spell_attack`, `ranged_spell_attack`,
`special` (all unchanged in shape, **including the v1 `qualifiers` array from #58**, and required-field rules: `_attack` kinds require `to_hit` and
`damage`, `melee_*` requires `reach`, `ranged_*` requires `range`), plus the new **`save_effect`**.
There is no `multiattack` action kind in v1 or v2, and **no new `multiattack` stat-block field**.
Multiattack's content shape is defined by **PR #59** (open, row 2's gate): a *`multiattack` feature
effect* in `traits[]`/`features[]`, `{ "kind": "multiattack", "options": [ { "id": "claw_beak",
"attacks": ["beak", "claws"] } ] }` (or the `attacks` shorthand for one option), each option an
**ordered list of the creature's own attack action ids, repeats allowed** (`["bite","claw","claw"]`),
loaded into `Combatant::multiattacks` and live from `@3`. `@4` **carries it unchanged** (it is in the live
`@3` set, so it is never in the `3→4` new-kind list, and its `@≤2` inertness is #59's). If #59 changes shape
before merge, this section follows it. Validator check 5 (§8.3) points at these `options[].attacks`.
`save_effect` adds:

| Field | Type | Req | Notes |
|---|---|---|---|
| `kind` | `"save_effect"` | y | breath weapons, Frightful Presence, Turn Undead, Stunning-Strike-style actions |
| `id`, `name`, `description` | as v1 | y/y/n | `id` is the `local_id` used by AI/UI and in the synthetic spell id (§2.3) |
| `save` | `$defs/save` = `{ability, dc}` | y | `dc` integer 1..30; no derived DCs for actions (SRD blocks print them) |
| `area` | `$defs/area` | n | absent = single target (`targets: 1`, `range_ft`) |
| `origin` | `"self"` \| `"point"` | n | **on the action, not in `area`** (default `self`); `point` requires `range_ft` |
| `range_ft` | `$defs/range_ft` (v1) | n | single-target range, or the reach of a `point` origin; forbidden iff `origin` is `self` and `area` is present (check 13) |
| `damage[]` | `$defs/damage` items | n | at least one of `damage[]`/`apply[]` required |
| `on_success` | `"half"` \| `"none"` | when `damage[]` | |
| `apply[]` | `[{condition, duration?, repeat_save?}]` | n | maps to `SpellEffectKind::ApplyCondition`; `duration`: `{kind:"rounds",n}` \| `{kind:"until_save"}` \| `{kind:"until_end_of_target_next_turn"}`; `repeat_save`: `"start_of_turn"` \| `"end_of_turn"` \| `"on_damage"` |
| `usage` | `$defs/usage` | n | default at-will (§2.2) |
| `magical` | boolean | n | default **false**. Breath weapons and Frightful Presence are not spells; this is what Magic Resistance keys on |

`$defs/area` is **`spell@1`'s inline `area` copied verbatim** (so the two cannot drift; a shared-fixture
test validates both against the same cases). `origin`/`range_ft` are deliberately *not* here: they
belong to the action (above).

```json
"area": { "type": "object", "additionalProperties": false, "required": ["shape", "size_ft"],
  "description": "Area of effect; sim decides which creatures are inside and passes them as targets.",
  "properties": {
    "shape":    { "enum": ["cone", "cube", "cylinder", "line", "sphere"] },
    "size_ft":  { "type": "integer", "minimum": 5, "multipleOf": 5, "description": "Cone/line length, cube side, sphere/cylinder radius." },
    "width_ft": { "type": "integer", "minimum": 5, "multipleOf": 5, "description": "line only." },
    "height_ft":{ "type": "integer", "minimum": 5, "multipleOf": 5, "description": "cylinder only." } },
  "allOf": [
    { "if": {"required":["shape"], "properties":{"shape":{"const":"line"}}},     "then": {"required":["width_ft"]},  "else": {"not":{"required":["width_ft"]}} },
    { "if": {"required":["shape"], "properties":{"shape":{"const":"cylinder"}}}, "then": {"required":["height_ft"]}, "else": {"not":{"required":["height_ft"]}} } ] }
```

**Schema-enforced `save_effect` rules** (not just prose; each has a validator negative case):

```json
"action.allOf (additions)": [
  { "if": {"required":["kind"], "properties":{"kind":{"const":"save_effect"}}},
    "then": { "required": ["save"], "dependentRequired": {"on_success":["damage"]},   // on_success only with damage[]
              "anyOf": [ {"required":["damage"]}, {"required":["apply"]} ],        // damage[] or apply[] required
              "not": { "anyOf": [ {"required":["to_hit"]}, {"required":["reach"]}, {"required":["range"]}, {"required":["properties"]}, {"required":["on_hit"]} ] } },
    "else": { "not": { "anyOf": [ {"required":["save"]}, {"required":["area"]}, {"required":["origin"]}, {"required":["range_ft"]},
                                  {"required":["apply"]}, {"required":["on_success"]}, {"required":["usage"]}, {"required":["magical"]} ] } } },
  { "if": {"required":["kind","damage"], "properties":{"kind":{"const":"save_effect"}}}, "then": {"required":["on_success"]} },   // on_success iff damage[]
  { "if": {"required":["origin"], "properties":{"origin":{"const":"point"}}}, "then": {"required":["range_ft"]} },
  { "if": {"required":["kind"], "properties":{"kind":{"enum":["melee_weapon_attack","ranged_weapon_attack"]}}}, "then": {"properties":{"on_hit":{"type":"array","items":{"$ref":"#/$defs/rider"}}}},
    "else": {"not":{"required":["on_hit"]}} } ]
```

Small shared defs used above and in §3/§4 (all local, all in the §1.1 list):

```json
"save":   { "type":"object","additionalProperties":false,"required":["ability","dc"],
            "properties":{"ability":{"$ref":"../common/v1.schema.json#/$defs/ability"},"dc":{"type":"integer","minimum":1,"maximum":30}} },
"damage": { "type":"object","additionalProperties":false,"required":["dice","type"],
            "properties":{"dice":{"$ref":"../common/v1.schema.json#/$defs/dice"},"type":{"$ref":"../common/v1.schema.json#/$defs/damage_type"}} },
"rider_damage_type": { "oneOf":[ {"$ref":"../common/v1.schema.json#/$defs/damage_type"}, {"const":"same_as_weapon"} ] },
"rider_damage": { "type":"object","additionalProperties":false,"required":["dice","type"],
                 "properties":{"dice":{"$ref":"../common/v1.schema.json#/$defs/dice"},"type":{"$ref":"#/$defs/rider_damage_type"}} },
"apply":  { "type":"object","additionalProperties":false,"required":["condition"],
            "properties":{"condition":{"$ref":"../common/v1.schema.json#/$defs/condition"},
                          "duration":{"$ref":"#/$defs/modifier_duration"},      // same closed def as §6.1
                          "repeat_save":{"enum":["start_of_turn","end_of_turn","on_damage"]}} }
```

(`rider.damage[]` items are `$defs/rider_damage` (type may be `same_as_weapon`); every other `damage` is `$defs/damage`. `on_hit[]` is allowed only on weapon-attack kinds; `damage` stays required for `_attack` kinds exactly as in v1.)

Example (adult red dragon, replaces today's descriptive `special`):

```json
{ "kind": "save_effect", "id": "fire_breath", "name": "Fire Breath",
  "save": { "ability": "dex", "dc": 21 },
  "area": { "shape": "cone", "size_ft": 60 },
  "damage": [ { "dice": "18d6", "type": "fire" } ], "on_success": "half",
  "usage": { "kind": "recharge", "min": 5 } }
```

### 2.2 `$defs/usage` (shared by §2, §3, §4, §7)

Every discriminated union in `common@2` uses the `spell@1` pattern: a closed base object plus
`allOf` of `if kind const … then` closed properties (never a bare `oneOf`, which accepts unknown
fields in the unselected arms).

```json
"usage": { "type":"object", "additionalProperties": false, "required":["kind"],
  "properties": { "kind": {"enum":["at_will","recharge","per_day","per_rest","pool"]},
                  "min": {"enum":[2,3,4,5,6]}, "uses": {"type":"integer","minimum":1,"maximum":255},
                  "rest": {"enum":["short","long"]}, "pool": {"$ref":"../common/v1.schema.json#/$defs/local_id"},
                  "cost": {"type":"integer","minimum":1} },
  "allOf": [
    { "if": {"required":["kind"], "properties":{"kind":{"const":"at_will"}}},   "then": {"propertyNames":{"enum":["kind"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"recharge"}}},  "then": {"required":["min"],  "propertyNames":{"enum":["kind","min"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"per_day"}}},   "then": {"required":["uses"], "propertyNames":{"enum":["kind","uses"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"per_rest"}}},  "then": {"required":["uses","rest"], "propertyNames":{"enum":["kind","uses","rest"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"pool"}}},      "then": {"required":["pool"], "propertyNames":{"enum":["kind","pool","cost"]}} } ] }
```

(`usage` is converted from per-arm forbid lists to the same `propertyNames` allow-list as every other union, so the
single "allowed == required ∪ optional" test covers it.)

`cost` is the **one name** for "how much a use takes from a pool" everywhere (`usage.cost`,
`rider spend.cost`, `resource` cost on features); it defaults to 1. `recharge` is "Recharge 5-6":
roll d6 at the start of the owner's turn while expended; `min..6` recharges. `per_day`/`per_rest` are
a `FeatureUses` row; `pool` spends from §7. (`per_day`/`per_rest` start full each combat: sim has no
cross-encounter state, §7.2.)

### 2.3 Rules mapping: a save action compiles to a synthetic `SpellDef` (Q1, decided)

All resolution code already exists for spells with `SpellEffectKind::Save` plus `extra_effects`
(damage, half on success, `ApplyCondition`, repeat saves). Sim compiles each `save_effect` into a
`SpellDef` and adds it to the shared `SpellBook`. Four requirements came out of Engine Core's review,
verified against `main`:

1. **Id derivation, not a reserved bit.** `spell_id_of` is plain 32-bit FNV-1a, so about half of
   all content ids already have the high bit set; a "high bit means synthetic" scheme is false.
   Derive `id = spell_id_of("<archetype id>#<action id>")` (`#` cannot appear in a content id, so the
   *string* namespaces are disjoint). The only collision guard is the one that exists:
   `SpellBook::add` returns false on a duplicate id, and the compile treats that as a **load error
   naming both strings** (a 32-bit hash collision between two strings is possible but is detected,
   never silent).
2. **Fixed DC.** `SpellDef` has no DC; rules derive `8 + prof + ability mod` (`spell_save_dc`). A
   dragon has different DCs for Fire Breath and Frightful Presence, so `SpellDef` gets appended
   **`int8_t fixed_dc = 0`** (0 = derive) and, for future attack-roll actions, `int8_t fixed_attack = 0`.
   `spell_save_dc(caster, def)` returns `fixed_dc` when non-zero. The per-creature
   `spell_dc_override` in §5 is for `spellcasting` blocks, a different concept.
3. **Archetype overrides and rollback.** `ContentLibrary::add` ends with `by_id_[a.id] = std::move(a)`,
   silently replacing an earlier pack's archetype. Synthetic spells are therefore compiled into a
   **per-archetype list** and registered into the shared book in `load()` once the final archetype
   set is known (or the replaced archetype's entries are erased on override). If registration fails,
   nothing is left half-registered (all-or-nothing per `load()`).
4. **Known spells without a `spellcasting` block.** Combat requires the caster to know the spell
   (`spells_known`) and installs spell hooks only when the book is non-empty. A monster with
   `save_effect` but no `spellcasting` block has `caster_level == 0` (and `content.cpp` rejects
   `caster_level` outside 1..20 for blocks that exist), so the compile path appends to
   `spells_known` itself and never requires a block. Side effect, noted: a pack that had no spells
   now yields a non-null book for those combats. Old content is unaffected (nothing in the installed
   hooks fires without a cast), and the compat goldens prove it.
5. **Direct `ContentLibrary::add()`** (not via `load()`): a `save_effect` registers its synthetic spells
   immediately, replacing that archetype's earlier entries. `SpellBook` has no erase today, so "erase on
   override" and "all-or-nothing" need either a rules5e `SpellBook::erase`/rebuild (listed in row 5) or
   **build-then-swap** of the whole book in `load()` (recommended: build a new book from the final archetype
   set, then swap the shared pointer). Direct `add()` uses the rebuild path for the replaced archetype's ids.

```cpp
// rules5e/spells.hpp (append to SpellDef; defaults preserve every existing spell)
struct SpellDef {
    // ... existing fields ...
    bool    is_spell = true;       // false: no SpellCast window (cannot be Counterspelled), no components
    bool    magical = true;        // SaveSource.magical (#54): breath weapons are NOT magical
    int8_t  fixed_dc = 0, fixed_attack = 0;   // 0 = derive from the caster
    uint8_t usage_kind = 0;        // 0 at-will, 1 recharge, 2 per-day/rest (FeatureUses), 3 pool
    uint8_t recharge_min = 0;      // usage_kind == 1
    uint32_t pool_id = 0; uint8_t pool_cost = 0;   // usage_kind == 3
};
```

`is_spell = false` is a **rules** change (suppress the `SpellCast` window, route `magical` into
`SaveSource`); sim cannot do it alone, hence Rules goes first in §12. The action is offered through
`legal_actions` as `CastSpell` with `spell_level = 0`, carrying `usage`. Area targets are
**caller-supplied** in `ActionRequest::targets` first (Q8, decided); `area` is validated and used
for UI hints only until the area-template PR.

#### 2.3.1 Row 6 hand-off API (Rules to Engine Core)

Rules (row 5 and the row 6 tests in `engine/rules/tests/save_action_tests.cpp`) provide everything below;
**Engine Core owns the compile, `Combat::create` registration, the AI and the `SpellTarget` change** (row 6 assignment).

1. **Compile.** For each `save_effect` build a `SpellDef` and add it to the `SpellBook` (new `SpellBook::erase(id)` supports
   the override/rebuild path):
   `id = spell_id_of("<archetype id>#<action id>")`, `content_id` = that string, `level = 0`, `casting_time = Action`,
   `range_ft` (single target) , `targets = 1`, `kind = Save`, `save_ability = save.ability`, **`fixed_dc = save.dc`**,
   `dice`/`damage_type` from `damage[0]` (further `damage[]` entries and `apply[]` become `extra_effects`: damage entries as
   further `Save` effects with `on_save` copied; each `apply` as an `ApplyCondition`/`Save`-with-condition effect with
   `affected_only` as the rules tests show), `on_save = on_success` (`half` -> `OnSave::Half`, `none` -> `OnSave::None`),
   **`is_spell = false`**, **`magical = action.magical` (default false)**, `area`/`area_size_ft`... from `area`, and `usage`:
   `at_will` -> `usage_kind 0`; `recharge` -> `1` + `recharge_min`; `per_day`/`per_rest` -> `2`; `pool` -> `3` + `pool_id`,
   `pool_cost`. `components = 0`, `concentration = false`.
2. **Registration** (`Combat::create`, only when the archetype has such an action, so old archetypes register nothing):
   append the synthetic id to the combatant's `spells_known`; `register_action_use(state, actor, spell_id)` for `recharge`;
   `register_feature(state, actor, spell_id, uses, recharge)` for `per_day`/`per_rest`; `register_pool(state, actor, pool_id,
   max, cur, recharge)` for pools. `Combat::hash` already folds the tables (0xA1 / 0xA3) when non-empty.
3. **Requests.** `ActionRequest{actor, CastSpell, target}` with `spell_id`, `spell_level = 0`, optional `targets` (area
   callers). `legal_actions` already offers the action per hostile target (level 0), and filters expended recharge, spent
   per-day and unaffordable pool actions. Rejections: `"action has not recharged"`, `"no uses left"`, `"not enough resource"`,
   `"too many targets"`.
4. **Events.** `ActionUsed` (depth 0) replaces `SpellCast` (no counter window), then `ResourceSpent` (pool), the save and
   damage events, then `SpellResolved`. Engine Core's `Combat::complete` change: also emit `SpellTarget` for a depth-0
   `ActionUsed` by the actor (and update the `SpellTarget` wording in `docs/SIM_EVENTS.md`).
5. **Timed conditions (rules, follow-up PR).** `SpellDef`/`SpellEffect` carry `expiry` (`0` none, `1` rounds, `2`
   end of the target's next turn; the `rules5e::Expiry` values) and `duration_rounds`. Map `apply[].duration`:
   `{kind:"rounds",n}` -> `expiry 1, duration_rounds n` (the condition ends at the start of round R+n, R = the round it was applied
   in); `{kind:"until_end_of_target_next_turn"}` -> `expiry 2` (ends at the end of the target's next turn; a turn the target is
   already acting in does not count); `{kind:"until_save"}` -> a `repeat_save`, no expiry. Expiry reuses the existing
   `SpellEffectEnded` + `ConditionRemoved` events (`ModifierExpired` stays reserved for row 12). `ActiveSpellEffect::timed`
   (`Timed{kind, rounds_left}`, helpers `make_timed` / `tick_round_start` / `tick_target_turn_end`) is the shared timer row 12 modifiers
   reuse. `Combat::hash` folds timed effects under tag `0xA5`, only when one exists.

### 2.4 Recharge

```cpp
// encounter.hpp
struct ActionUse { CombatantId actor; uint32_t spell_id; bool expended; };   // recharge actions
// EncounterState::action_uses  (appended, serialized/hashed with state; empty for old content)
// begin_turn(): for each expended recharge action of the actor: roll d6 (rng draw ONLY here),
//               recharge when roll >= recharge_min; emit RechargeRolled.
```

RNG-draw invariant: a combatant with no recharge action never draws. This is what keeps existing
goldens bit-identical (§9).

### 2.5 Inert behaviour

Old content has no `save_effect` kind (the `@3` schema rejects it). Old breath weapons are
`special` actions: they stay `special`, which sim ignores today and ignores at `@4`
(`special` keeps its meaning forever; migrating *to* `save_effect` is an authoring change, never
automatic). Nothing to mark. Test: the red-dragon-style fixture with a descriptive `special`
breath weapon digests identically at `@3` and migrated `@4`.

## 3. On-hit riders and triggers (T&C-G5, R-G4, part of R-G7)

### 3.1 Content shape

Two surfaces, one object model.

**(a) Attack riders**: `on_hit[]` on `melee_weapon_attack` / `ranged_weapon_attack` actions (and
on character `actions[]`): effects that happen *because this attack hit* (Grapple on hit, Poison
bite save, Pounce prone, Stunning Strike-style save_condition).

**(b) Event effects**: effect `kind: "on_event"` (renamed from "trigger" so it cannot be confused with
the reaction `window` of §4 or `ReadiedTrigger`) in `traits[]` / `features[]` (character `features[]`
carry the same effect object): effects that happen on an *event*, independent of one action
(Sneak Attack, Rampage, Undead Fortitude, Death Burst, Divine Smite).

```json
"$defs/on_event_effect": { "type":"object", "additionalProperties": false,
  "required": ["kind", "on", "effects"],
  "properties": {
    "kind": {"const": "on_event"},
    "on": { "enum": ["hit", "crit", "damage_dealt", "reduce_to_zero", "drop_to_zero", "death", "turn_start", "turn_end"] },
    "when": { "type":"array", "items": {"$ref":"#/$defs/predicate"} },
    "limit": { "enum": ["once_per_turn","once_per_round","once_per_attack","none"], "default": "none" },
    "choice": { "enum": ["auto","ask"], "default": "auto" },   // Divine Smite: "ask"
    "usage": { "$ref": "#/$defs/usage" },
    "effects": { "type":"array", "minItems":1, "items": {"$ref":"#/$defs/rider"} } } }   // no `inert` here: an inert on_event is arm (a) of `effect` (§8.2.1)
```

**Ids.** Content authors do not write trigger ids. The loader derives a stable one per trigger:
`"<owner id>#<index>"`, where the owner is the trait/feature `id` (index 0, since a trait carries one
effect) or, for `on_hit[]` riders, the attack action `id` with the rider's position in `on_hit[]`. The
numeric `Trigger::id` (and the `trigger_id` in `FeatureTriggered` and `ReactionOptionKind::Rider`) is
`spell_id_of("<archetype id>#<owner id>#<index>")`; a duplicate is a load error naming both strings.
`#` cannot appear in a `local_id`, so the string namespace cannot collide with spell or action ids.

`on` meanings (all evaluated by rules5e; `self` = the trait's owner):

| `on` | Fires when | Notes |
|---|---|---|
| `hit` / `crit` | owner's attack hits / crits | `limit: once_per_turn` for Sneak Attack. Extra dice are rolled with the attack's damage so a crit doubles them |
| `damage_dealt` | owner damages a target | after resistances; `types_dealt` available (#56) |
| `reduce_to_zero` | owner's damage drops a creature to 0 | Rampage |
| `drop_to_zero` | owner would drop to 0 | *before* it applies: Undead Fortitude, Relentless Endurance |
| `death` | owner dies | after `CombatantDied`: Death Burst |
| `turn_start`/`turn_end` | owner's turn | regeneration-style, recharge, aura ticks |

**Closed-union convention (used for every union below).** A closed base object lists *every*
property of every arm and has `additionalProperties:false`; the `kind` property is a closed enum;
and each arm is `if kind const … then { required: [...], propertyNames: { enum: [<exactly the arm's
allowed names>] } }`. The `propertyNames` allow-list is the explicit forbid list in its strongest form
(a field added to the base later is forbidden in every arm until an arm opts in). A test asserts for
each arm that `allowed == required ∪ optional` as documented here.

`$defs/predicate` (closed; unknown `kind` is a schema error at `@4`):

```json
"predicate": { "type":"object", "additionalProperties": false, "required": ["kind"],
  "properties": {
    "kind": {"enum": ["weapon","advantage_or_ally_adjacent","target_creature_type","not_creature_type",
                      "damage_type_is","damage_type_not","not_crit","moved_at_least_ft","self_hp_below","save"]},
    "is":    {"enum": ["melee","ranged","finesse","finesse_or_ranged"]},
    "types": {"type":"array","minItems":1,"uniqueItems":true,"items":{"$ref":"../common/v1.schema.json#/$defs/damage_type"}},
    "creature_types": {"type":"array","minItems":1,"uniqueItems":true,"items":{"$ref":"../common/v1.schema.json#/$defs/creature_type"}},
    "ft":  {"type":"integer","minimum":5,"multipleOf":5},
    "num": {"type":"integer","minimum":1}, "den": {"type":"integer","minimum":1},
    "ability": {"$ref":"../common/v1.schema.json#/$defs/ability"},
    "dc": {"type":"integer","minimum":1,"maximum":30},
    "dc_from_damage": {"const": true}, "base": {"type":"integer","minimum":0,"maximum":30} },
  "allOf": [
    { "if": {"required":["kind"], "properties":{"kind":{"const":"weapon"}}},                       "then": {"required":["is"], "propertyNames":{"enum":["kind","is"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"advantage_or_ally_adjacent"}}},   "then": {"propertyNames":{"enum":["kind"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"enum":["target_creature_type","not_creature_type"]}}}, "then": {"required":["creature_types"], "propertyNames":{"enum":["kind","creature_types"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"enum":["damage_type_is","damage_type_not"]}}},        "then": {"required":["types"], "propertyNames":{"enum":["kind","types"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"not_crit"}}},                     "then": {"propertyNames":{"enum":["kind"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"moved_at_least_ft"}}},            "then": {"required":["ft"], "propertyNames":{"enum":["kind","ft"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"self_hp_below"}}},                "then": {"required":["num","den"], "propertyNames":{"enum":["kind","num","den"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"save"}}},
      "then": {"required":["ability"], "propertyNames":{"enum":["kind","ability","dc","dc_from_damage","base"]},
               "oneOf": [ {"required":["dc"],             "not":{"anyOf":[{"required":["dc_from_damage"]},{"required":["base"]}]}},
                          {"required":["dc_from_damage"], "not":{"required":["dc"]}} ] } } ] }
```

| Predicate | Fields | Used by |
|---|---|---|
| `weapon` | `is`: `melee` \| `ranged` \| `finesse` \| `finesse_or_ranged` (weapon `properties`, #53) | Sneak Attack (`finesse_or_ranged`) |
| `advantage_or_ally_adjacent` | none (attack had advantage, or a non-incapacitated ally within 5 ft of the target and no disadvantage) | Sneak Attack |
| `target_creature_type` / `not_creature_type` | `creature_types[]` | smites vs undead/fiends |
| `damage_type_is` / `damage_type_not` | `types[]` | Undead Fortitude (not radiant) |
| `not_crit` | none | Undead Fortitude |
| `moved_at_least_ft` | `ft` (`TurnBudget::moved_ft` = 5 x `move_tiles` from sim, §3.2) | Charge, Pounce (20 ft). Approximation, see below (Q4 decided) |
| `self_hp_below` | `num`, `den` | Rampage-style gates |
| `save` | `ability`, then **either** `dc` **or** `dc_from_damage: true` with optional `base` (default 0; DC = `base` + damage taken) | Undead Fortitude: Con save, `dc_from_damage` with `base: 5` |

`$defs/rider` (closed set of kinds; `grapple`/`swallow` are **reserved names that are not in the enum**, so
they are a schema error until R-G8; `grapple` has since been appended, see `docs/CONTENT_CONVENTIONS.md` section 5):

```json
"rider": { "type":"object", "additionalProperties": false, "required": ["kind"],
  "properties": {
    "kind": {"enum": ["extra_damage","save_condition","apply_condition","heal_self","temp_hp","set_hp","bonus_attack","area_save","spend"]},
    "damage": {"type":"array","minItems":1,"items":{"$ref":"#/$defs/rider_damage"}},
    "scale":  {"$ref":"#/$defs/scaling"},
    "save":   {"$ref":"#/$defs/save"},
    "on_success": {"enum":["half","none"]},
    "apply":  {"type":"array","minItems":1,"items":{"$ref":"#/$defs/apply"}},
    "dice":   {"$ref":"../common/v1.schema.json#/$defs/dice"}, "amount": {"type":"integer","minimum":1},
    "value":  {"type":"integer","minimum":1},
    "attack_id": {"$ref":"../common/v1.schema.json#/$defs/local_id"},
    "area":   {"$ref":"#/$defs/area"},
    "pool":   {"$ref":"../common/v1.schema.json#/$defs/local_id"}, "cost": {"type":"integer","minimum":1},
    "slot":   {"oneOf":[{"const":"any"},{"type":"integer","minimum":1,"maximum":9}]} },
  "allOf": [
    { "if": {"required":["kind"], "properties":{"kind":{"const":"extra_damage"}}},    "then": {"required":["damage"], "propertyNames":{"enum":["kind","damage","scale"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"save_condition"}}},  "then": {"required":["save"], "anyOf":[{"required":["apply"]},{"required":["damage"]}],
                                                                           "dependentRequired": {"damage":["on_success"],"on_success":["damage"]},   // both directions
                                                                           "propertyNames":{"enum":["kind","save","apply","damage","on_success"]} } },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"apply_condition"}}}, "then": {"required":["apply"], "propertyNames":{"enum":["kind","apply"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"enum":["heal_self","temp_hp"]}}}, "then": {"oneOf":[{"required":["dice"]},{"required":["amount"]}], "propertyNames":{"enum":["kind","dice","amount"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"set_hp"}}},          "then": {"required":["value"], "propertyNames":{"enum":["kind","value"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"bonus_attack"}}},    "then": {"required":["attack_id"], "propertyNames":{"enum":["kind","attack_id"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"area_save"}}},       "then": {"required":["area","save","damage","on_success"], "propertyNames":{"enum":["kind","area","save","damage","on_success"]}} },
    { "if": {"required":["kind"], "properties":{"kind":{"const":"spend"}}},           "then": {"oneOf":[{"required":["pool"],"not":{"required":["slot"]}},
                                                                           {"required":["slot"],"not":{"anyOf":[{"required":["pool"]},{"required":["cost"]}]}}],   // pool XOR slot; cost only with pool
                                                                 "propertyNames":{"enum":["kind","pool","cost","slot"]}} } ] }
```

(`$defs/apply` is the shared `{condition, duration?, repeat_save?}` object of §2.1, also a local def, listed
in §1.1 with `save`; `damage` here is `$defs/rider_damage` (type may be `same_as_weapon`).)
`spend` + `extra_damage` with `scale.per_slot_level` is Divine Smite; `pool` + `cost` is a resource
spend. `set_hp` is Undead Fortitude's cancel; `bonus_attack` is Rampage; `area_save` is Death Burst.

Examples:

```json
{ "kind": "on_event", "on": "hit", "limit": "once_per_turn",
  "when": [ {"kind":"weapon","is":"finesse_or_ranged"}, {"kind":"advantage_or_ally_adjacent"} ],
  "effects": [ {"kind":"extra_damage","damage":[{"dice":"1d6","type":"same_as_weapon"}],
                "scale":{"per_levels":{"class":"rogue","every":2,"add_dice":"1d6"}}} ] }

{ "kind": "on_event", "on": "drop_to_zero", "when": [ {"kind":"damage_type_not","types":["radiant"]},
                                                       {"kind":"not_crit"},
                                                       {"kind":"save","ability":"con","dc_from_damage":true,"base":5} ],
  "effects": [ {"kind":"set_hp","value":1} ] }
```

**Charge/Pounce limitation (Q4, decided).** `moved_at_least_ft` means "the owner has moved at least
`ft` feet this turn (`TurnBudget::moved_ft`, tiles x 5, fed by sim's `ActionRequest::move_tiles`) before the attack". There is no path history, so the
SRD requirement that the movement be *in a straight line toward the target* is **not** enforced: a
creature that walks 20 ft sideways or away and then attacks still qualifies. This is slightly
generous to the attacker. If it matters later, a sim-supplied `charged_straight` fact can be added
as a new predicate without changing this one.

`$defs/scaling` (one shape for Sneak Attack dice, Divine Smite slots and any future scaling):

```json
"scaling": { "type":"object", "additionalProperties": false, "minProperties": 1, "properties": {
  "per_levels":     {"type":"object","additionalProperties":false,"required":["every","add_dice"],
                     "properties":{"class":{"$ref":"../common/v1.schema.json#/$defs/local_id"},"every":{"type":"integer","minimum":1},
                                   "add_dice":{"$ref":"../common/v1.schema.json#/$defs/dice"}}},
  "per_slot_level": {"type":"object","additionalProperties":false,"required":["base_level","add_dice"],
                     "properties":{"base_level":{"type":"integer","minimum":1,"maximum":9},
                                   "add_dice":{"$ref":"../common/v1.schema.json#/$defs/dice"}}} } }
```

**`per_levels` resolution (decided).** The loader resolves it to final dice when it builds the owner's `Trigger`
(rules5e only ever sees the resolved `RiderEffect::dice`): `extra_dice = floor((level - 1) / every)` copies of
`add_dice`, added to the base dice, where `level` is the owner's level in `class` (or the total character level when
`class` is omitted). Monsters have no class levels, so `per_levels` does not apply to them. Worked example, Sneak
Attack (base `1d6`, `every: 2`, `add_dice: 1d6`): levels 1, 2, 3, 4, 5, 19, 20 give 1, 1, 2, 2, 3, 10, 10 d6, i.e.
`ceil(level / 2)` d6.

**`AttackIs` (approved, rules-internal).** A predicate appended by the sim loader for `on_hit[]` riders, not part of the
content `predicate` schema above: `a` = index into the owner's `Combatant::attacks`; it holds only for that exact weapon
attack and is false for spell attacks. The loader binds each `on_hit[]` rider to its attack action with it, so a rider
fires only when that attack hits (the trigger is otherwise an ordinary `hit` trigger with the same limits and effects).

`type: "same_as_weapon"` is valid only through `rider_damage_type` (§1.2), never in v1's `damage_type`.

### 3.2 Rules sketch

```cpp
// rules5e/triggers.hpp (new)
enum class TriggerOn : uint8_t { Hit, Crit, DamageDealt, ReduceToZero, DropToZero, Death, TurnStart, TurnEnd };
enum class TriggerLimit : uint8_t { None, OncePerTurn, OncePerRound, OncePerAttack };
struct Predicate { uint8_t kind; int16_t a = 0, b = 0; uint32_t mask = 0; };
struct RiderEffect { uint8_t kind; DiceExpr dice{}; DamageType type{}; int16_t value = 0;
                     uint32_t spell_id = 0 /*synthetic save spell, §2.3*/; uint32_t pool = 0; };
struct Trigger { uint32_t id; TriggerOn on; TriggerLimit limit; ReactionPolicy choice;
                 std::vector<Predicate> when; std::vector<RiderEffect> effects; };
// Combatant (appended): std::vector<Trigger> triggers;
// TurnBudget (appended): uint32_t triggers_used_mask; uint16_t moved_ft; uint8_t extra_actions;
// moved_ft is a rules-side counter fed by a SIM-supplied fact. rules5e only ever sees ActionRequest::move_ft,
// which is the grid *cost* sim passes (grid_.move_cost_ft, doubled for difficult terrain) and which the
// Move handler doubles again for Prone (crawling), and `Moved` carries only that cost, so a tile count
// cannot be recovered in rules. Therefore: ActionRequest (appended, default 0) `uint8_t move_tiles`,
// set by sim in do_step (1 per Step) and in the readied-move path (path length). Rules add
// 5 * move_tiles to TurnBudget::moved_ft ONLY WHEN THE MOVE COMPLETES (where `Moved` is emitted), for the
// actor's OWN turn only. A move interrupted by an opportunity attack (MoveInterrupted: no movement spent,
// tile not moved) must not count; readied/out-of-turn moves are not counted. uint8_t is enough (a readied
// path is bounded by speed). move_tiles is logged with the request (replay reproduces it) and stays OUT of log_hash.
```

Hook points in `encounter.cpp` (all are existing internal functions; no new public step kind):

| Trigger | Where it is evaluated |
|---|---|
| `hit`/`crit`/`damage_dealt` | `resolve_attack` after the hit roll, *before* damage dice are rolled for `extra_damage`; conditions after damage |
| `reduce_to_zero` | `apply_damage` when the target crosses to 0, attacker's triggers |
| `drop_to_zero` | `apply_damage` before the 0-HP transition (runs before regeneration-down handling: Q5) |
| `death` | after `CombatantDied` is emitted, queued so reaction windows cannot nest (`allow_chain` unchanged) |
| `turn_start`/`turn_end` | `begin_turn`/`end_turn` after the existing regeneration/recharge steps |

Optional riders (`choice: "ask"`, Divine Smite) reuse the reaction-window machinery: new
`ReactionTrigger::HitRider` (appended; `kReactionTriggerCount` 4 → 7 with §3/§4 additions),
the "reactor" is the attacker, options are `ReactionOptionKind::Rider{trigger_id, slot_level}`,
answered with the existing `ReactionDecision`. The option is **free** (`ReactionOption::uses_reaction
= false`) so it does not consume the owner's reaction. `ReactionPolicy::Auto` makes AI/sim spend
without asking; default `Ask` with *no content* opens no window, so old encounters are unchanged.

Events: `FeatureTriggered` (§9.1). Damage from riders is folded into the existing `DamageRolled` /
`DamageApplied` payloads (`types_dealt` already carries per-type totals), so damage consumers need no change.

### 3.2a Row 9 sub-PR split and 9a as implemented

Row 9 lands as three rules PRs: **9a** (trigger model; `hit`/`crit` triggers with `extra_damage` riders; predicates; limits; the `FeatureTriggered` emit site), **9b** (`reduce_to_zero`/`drop_to_zero`/`death`/`turn_start`/`turn_end`/`damage_dealt` triggers; `bonus_attack`, `set_hp`, `area_save`, `heal_self`, `temp_hp` riders; the `save` predicate), **9c** (`choice: ask` through the `HitRider` reaction window, `spend`, `save_condition`/`apply_condition`, `scale.per_slot_level`).

9a: `Combatant::triggers` (`Trigger{id, on, limit, choice, when[], effects[]}`) is filled by `Combat::create`. Only `on: hit|crit`, `choice: auto`, and `extra_damage` effects fire; every other value is stored and inert until its sub-PR. On a hit, before damage is rolled, matching triggers emit `FeatureTriggered(actor=owner, target, a=trigger id, b=TriggerOn, c=1)` and their dice join the damage as extra pieces (dice doubled on a crit; type = the weapon's when `same_as_weapon`). A trigger with an empty `when` list always passes. `weapon` predicates are false for spell attacks. `advantage_or_ally_adjacent` is true when the attack rolled with advantage (net of disadvantage) or `ally_near_target`. `moved_at_least_ft` reads `TurnBudget::moved_ft` and only holds on the owner's own turn. Deviation from the sketch: limit bookkeeping (`trigger_turn_mask/stamp`, `trigger_round_mask/stamp`) lives on the `Combatant`, not on `TurnBudget`, so once-per-turn also covers off-turn reactions; stamps reset lazily. A combatant with no triggers draws no extra rng and emits no events, so goldens are unchanged. `triggers_digest` / `has_trigger_state` are rules-side; the fold into `Combat::hash` (tag `0xA6`, only when non-empty) is an Engine Core hand-off.

### 3.2b 9b as implemented

9b adds the non-attack-roll events. `damage_creature` now takes the damage source (`TrigSrc`: owner, attack facts, spell or not) so it can run triggers around `after_damage`:

| `on` | Evaluated | Subject (`FeatureTriggered.target`) | Effects that fire |
|---|---|---|---|
| `drop_to_zero` | before the HP is committed, when the damage would take the owner from above 0 to 0 (not for massive-damage instant death) | the damager if known, else the owner | `set_hp` (hp = `value`, clamped to 1..hp_max: the drop is cancelled and `DamageDealt.b` reports the new hp), `heal_self`, `temp_hp`, `area_save` |
| `damage_dealt` | after `DamageDealt`, owner is the damager, damage after mitigation > 0 | the creature damaged | `heal_self`, `temp_hp`, `bonus_attack`, `area_save` |
| `reduce_to_zero` | right after `damage_dealt`, when the damager took a creature from above 0 to 0 (dead or dying) | the creature | same as `damage_dealt` |
| `death` | after the step's events (and readies): `process_deaths` handles every creature with `dead && !death_triggers_done`, including ones killed by an earlier burst (bounded by the creature count); queued so no reaction window nests | the owner | `heal_self`/`temp_hp` do nothing (dead), `area_save` |
| `turn_start` | `start_turn`, after regeneration and recharge, before death saves; not for the dead | the owner | `heal_self`, `temp_hp`, `area_save` |
| `turn_end` | `EndTurn`, after repeat saves and timed-effect ticks, before `TurnEnded`; not for the dead | the owner | same |

* **Save predicate.** `Predicate{Save, a = Ability, b = DC or base, mask bit 0 = dc_from_damage}`: the *owner* rolls (`SavingThrowRolled`, c = success) when evaluation reaches it, so list it last. A failed save emits `FeatureTriggered` with c = 3 (failed save) and the trigger does not fire; an ordinary predicate failure is silent. DC from damage = `b + damage taken` (after mitigation).
* **`bonus_attack`** (Rampage) does not pick a target (rules have no positions): it sets `TurnBudget::bonus_attack_grant = value + 1` (`value` = index into `Combatant::attacks`, resolved by the loader from `attack_id`). The grant exists only on the owner's own turn with the bonus action unspent. The caller then sends `ActionRequest{Attack, attack_index, as_bonus_action = true}`: it spends the bonus action and the grant, and is refused otherwise (the old "attack is not a bonus action" error). The Rampage "move up to half speed" part is an ordinary `Move` request by the caller.
* **`area_save`** (Death Burst, aura ticks): `RiderEffect::spell_id` names a synthetic Save `SpellDef` (fixed DC, damage, `on_save`, `is_spell = false`) and the targets come from the new `EncounterHooks::area_targets(state, source, def)` (sim, positions; the source is ignored; dead creatures are dropped). No hook, no def or no `Save` kind: the trigger does not fire. It resolves with the existing Save resolution, so it emits the same `SavingThrowRolled`, `DamageDealt`, `ConditionAdded` events, and the damage counts as the owner's `damage_dealt` if the owner is alive.
* **`heal_self` / `temp_hp`**: `dice` if set, else `value`. `heal_self` emits `Healed`; `temp_hp` keeps the larger pool and has no event of its own (`FeatureTriggered` marks it; event schema stays v7).
* **Guard.** Trigger evaluation nests at most 4 levels (a `damage_dealt` area_save that damages again), so loops end.
* Limits work as in 9a. Still reserved for 9c: `choice: ask`, `spend`, `save_condition`, `apply_condition`, `scale.per_slot_level`.
* Goldens: unchanged. Creatures without triggers take none of these paths (no events, no rng).
* New state: `Combatant::death_triggers_done` (folded into `triggers_digest`) and `TurnBudget::bonus_attack_grant` (Engine Core: include it wherever `TurnBudget` is hashed, if it is).

### 3.2c 9c as implemented

* **`choice: ask`** (hit/crit triggers only; `ask` on any other event never fires). For each ask trigger whose `on`, limit, predicates and cost allow it, `do_attack` opens a `ReactionTrigger::HitRider` window right after the auto triggers and before damage is rolled, so a crit doubles the rider dice. The **reactor is the attacker** (`trigger_actor` = attacker, `target` = creature hit, `attack_total/natural` filled). One window per trigger. `ReactionWindowOpened` (a = `HitRider`), then `ReactionTaken`/`ReactionDeclined`, then `FeatureTriggered`: c = 1 applied, **c = 2 declined** (nothing is spent, no limit consumed). HitRider windows are not subject to chain gating (`allow_chain`): they belong to the attacker's own attack, so Smite works on an opportunity attack.
* **Options.** `ReactionOptionKind::Rider` (appended, value 4): `option_id` = trigger id, `slot_level` = slot spent (0 = no slot), `attack_index` = index of the trigger in `Combatant::triggers`, `uses_reaction = false` (free). One option per affordable slot level, ascending; **option 0 is what `ReactionPolicy::Auto` takes**. Policy `Never` for `HitRider` opens no window; default `Ask` with no ask trigger opens none, so old encounters are unchanged. With no decider, `step` returns `NeedsDecision` as for other windows and the sim default must decline (row 11 hand-off).
* **`spend`** (any hit/crit trigger, auto or ask): `RiderEffect::slot` 0 = pool `pool` with cost `value` (0 = 1; an unregistered pool is unlimited, as for pool-limited actions; `ResourceSpent` is emitted when paid), 1..9 = that slot level, 10 = any level 1..9. A trigger whose cost cannot be paid does not fire (silent, no window). Auto triggers pay the lowest affordable level.
* **`scale.per_slot_level`** is resolved by the loader into `RiderEffect::slot_dice` (`add_dice`) and `slot_base` (`base_level`); at slot level L the attack rolls the base dice plus `slot_dice.count * (L - slot_base)` extra dice of `slot_dice.sides` (same type, doubled on a crit). Divine Smite: `dice 2d8`, `slot_dice 1d8`, `slot_base 1`, `slot 10`.
* **`save_condition` / `apply_condition`** riders are resolved after the damage (and not on a dead target) with the existing Save / ApplyCondition resolution, so they emit the same `SavingThrowRolled`, `ConditionAdded` events and create the same timed effects. `RiderEffect::spell_id` names a **synthetic SpellDef** built by the loader (`Save` kind with `fixed_dc`, `save_ability`, `on_save`, `dice`/`damage_type`, `applies_condition`, `condition`, `expiry`/`duration_rounds`, `repeat_save`; or `ApplyCondition` kind with `condition`, `expiry`, `duration_rounds`; `is_spell = false`). The save damage of such a def has a fixed type: `same_as_weapon` is not supported in `save_condition.damage` (loader error). A missing def or one of the wrong kind means the trigger does not fire.
* Goldens unchanged: no ask triggers, no spend and no riders mean no windows, events or rng.

### 3.3 Inert behaviour

* Old traits whose effect `kind` is `on_event` do not exist (open effect, none shipped).
  `3→4` marks any pre-existing `on_event`-kind effect `"inert": true` (§8.2).
* `on_hit[]` is a new property of attack actions: absent in all `@≤3` files by schema.
* Descriptive Sneak Attack / Rampage / Undead Fortitude text remains description.

## 4. Reactions as actions (T&C-G9, version gate)

### 4.1 Content shape

`reactions[]` and `bonus_actions[]` exist in `common@1 stat_block` as lists of v1 `action`
and are descriptive today (sim ignores them). In `common@2` each entry is a **`oneOf` of exactly two
closed shapes**, so a migrated old entry still validates:

```json
"reactions": { "type":"array", "items": { "oneOf": [
    { "$ref": "#/$defs/reaction" },                    // mechanical (new at @4)
    { "$ref": "#/$defs/legacy_action" } ] } }          // v1 action shape, requires "inert": true
"bonus_actions": { "type":"array", "items": { "oneOf": [        // THREE arms, pairwise disjoint
    { "allOf": [ {"$ref":"#/$defs/action"}, {"properties":{"kind":{"not":{"const":"special"}}}} ] },   // (1) mechanical: every v2 kind EXCEPT special
    { "$ref": "#/$defs/legacy_nonspecial_action" },                                                    // (2) legacy, non-special, `inert: true` REQUIRED (stamped)
    { "$ref": "#/$defs/legacy_special_action" } ] } }                                                  // (3) legacy special, `inert` optional (never stamped)
```

Disjointness: (1) and (2) differ by `inert` (the v2 `action` forbids it); (2) and (3) differ by `kind`
(`legacy_nonspecial_action.kind` excludes `special`, `legacy_special_action.kind` is const `special`);
(1) never matches `special`. `reactions[]` keeps the two-arm `reaction | legacy_action` form (every
reaction entry is stamped, `special` included, so `legacy_action` there allows all five v1 kinds).
Required schema tests for `bonus_actions[]`: **special unstamped** (valid, arm 3), **special stamped**
(valid, arm 3), **mechanical** non-special v2 action (valid, arm 1), **stamped non-special** (valid, arm 2),
and the negatives **unstamped special read as mechanical** (a `special` carrying `save`/`usage` fields is
invalid) and **non-special with `inert:false`** (invalid).

`legacy_action` is **re-declared in full** (a plain `allOf` over v1's closed `action` would reject the
extra `inert` property): v1's property list verbatim, plus `inert`.

```json
"legacy_action": { "type":"object","additionalProperties":false,"required":["id","name","kind","inert"],
  "properties": {
    "id":{"$ref":"../common/v1.schema.json#/$defs/local_id"}, "name":{"type":"string"},
    "kind":{"enum":["melee_weapon_attack","ranged_weapon_attack","melee_spell_attack","ranged_spell_attack","special"]},
    "to_hit":{"type":"integer","minimum":-128,"maximum":127},
    "reach":{"$ref":"../common/v1.schema.json#/$defs/feet"},
    "range":{"type":"array","minItems":2,"maxItems":2,"prefixItems":[{"$ref":"../common/v1.schema.json#/$defs/range_ft"},{"$ref":"../common/v1.schema.json#/$defs/range_ft"}]},
    "targets":{"type":"integer","minimum":1},
    "properties":{"type":"array","uniqueItems":true,"items":{"type":"string"}},
    "qualifiers":{"type":"array","uniqueItems":true,"items":{"enum":["magical","silvered","adamantine"]}},   // live at @3 (#58); v1 field, carried
    "damage":{"type":"array","minItems":1,"items":{"type":"object","additionalProperties":false,"required":["dice","type"],
              "properties":{"dice":{"$ref":"../common/v1.schema.json#/$defs/dice"},"type":{"$ref":"../common/v1.schema.json#/$defs/damage_type"}}}},
    "description":{"type":"string"}, "inert":{"const":true} },
  "allOf": [ /* v1's three if/then rules (to_hit+damage for _attack, reach for melee_, range for ranged_) verbatim */ ] }
```

The two bonus-action legacy defs are written out in full (so the lint and the allowed = required + optional test
cover them; both share `legacy_action`'s property list):

```json
"legacy_nonspecial_action": { "type":"object","additionalProperties":false,"required":["id","name","kind","inert"],
  "properties": { /* legacy_action's properties verbatim (including `qualifiers`), except: */
    "kind":{"enum":["melee_weapon_attack","ranged_weapon_attack","melee_spell_attack","ranged_spell_attack"]},
    "inert":{"const":true} },
  "allOf": [ /* v1's three if/then rules (to_hit+damage for _attack, reach for melee_, range for ranged_) */ ] },
"legacy_special_action": { "type":"object","additionalProperties":false,"required":["id","name","kind"],
  "properties": { /* legacy_action's properties verbatim (including `qualifiers`), except: */
    "kind":{"const":"special"},
    "inert":{"const":true} } }                    // optional: a special bonus action is never stamped, but a stamped one is accepted
```

The `reactions[]` arms are mutually exclusive: `reaction` requires `window`/`effect` and forbids
`inert`; `legacy_action` requires `inert`. For `bonus_actions[]` there is a third arm,
`legacy_special_action` (= `legacy_action` with `kind` const `special` and `inert` *optional*; `legacy_nonspecial_action` is `legacy_action` minus `special`), because a
`special` bonus action is not loaded at `@4` and therefore is **not stamped**; the mechanical arm
excludes `special` (`kind: {not: {const: special}}`), so no `special` entry can be read as mechanical.

* **Migration stamps `inert: true` per entry** (not just a file-level marker): every `reactions[]`
  entry, and every `bonus_actions[]` entry **that `@4` would load**, meaning `kind` other than
  `special` (a `special` bonus action stays descriptive at `@4` exactly as at `@3`, neither loaded nor
  stamped). The file-level `inert_mechanics` list also gets `"reactions"` / `"bonus_actions"` when at
  least one entry was stamped (§8.2), so reporting stays uniform.
* **Authored `inert` is still rejected on disk.** `validate_packs.py` (~L379) errors on an authored `inert`
  in every version and `docs/CONTENT_EFFECTS.md` says the same ("reserved for the engine migrations").
  That does not change: only the **schema** (so migration output validates) and the **in-memory migration
  output** accept `inert`; a file on disk that carries it is a validator error. New validator negative cases:
  authored `inert` on a `reactions[]` entry and on a `bonus_actions[]` entry (and the existing effect case).
  The §8.4 migrated-file fixtures are validated in memory, not as authored files.
* `cost: "reaction"` on trait/feature effects becomes legal at `@4`. There is **no v2 `effect_cost`
  def**: v1's `effect_cost` enum already contains `reaction` (documented as reserved); the v2 *validator row*
  accepts it where the v1 row rejects it for `@≤3` files, and the new arms `$ref` v1's def.

```json
"$defs/reaction": { "type":"object", "additionalProperties": false,
  "required": ["id","name","window","effect"],
  "properties": {
    "id": {"$ref":"../common/v1.schema.json#/$defs/local_id"}, "name": {"type":"string"}, "description": {"type":"string"},
    "window": { "type":"object", "additionalProperties": false, "required":["kind"], "properties": {
        "kind":  {"enum": ["being_hit","damaged","leaving_reach","ally_attacked","spell_cast","roll_attack"]},
        "melee_only": {"type":"boolean"}, "ranged_only": {"type":"boolean"},
        "source": {"enum": ["self","ally_within_5ft"], "default": "self"},
        "visible": {"type":"boolean","default":true} } },
    "effect": { "$ref": "#/$defs/reaction_effect" },
    "usage": {"$ref":"#/$defs/usage"} } }
```

`window.kind: roll_attack` and `effect.kind: subtract_roll` are **reserved names that the `@4`
validator rejects** (Q6 defers roll-modifying reactions); they stay in the enums so adding the window later
is a validator-table change, not a schema change.

```json
"reaction_effect": { "type":"object", "additionalProperties": false, "required":["kind"],
  "properties": {
    "kind":   {"enum":["ac_bonus","reduce_damage","impose_disadvantage","retaliate","attack","cast","subtract_roll"]},
    "bonus":  {"type":"integer","minimum":1,"maximum":10},
    "fraction": {"const":"half"},
    "dice":   {"$ref":"../common/v1.schema.json#/$defs/dice"},
    "damage": {"type":"array","minItems":1,"items":{"$ref":"#/$defs/damage"}},
    "save":   {"$ref":"#/$defs/save"}, "on_success": {"enum":["half","none"]},
    "attack_id": {"$ref":"../common/v1.schema.json#/$defs/local_id"},
    "spell":  {"$ref":"../common/v1.schema.json#/$defs/content_id"} },
  "allOf": [
    { "if": {"required":["kind"], "properties":{"kind":{"const":"ac_bonus"}}},            "then": {"required":["bonus"],  "propertyNames":{"enum":["kind","bonus"]}} },                    // Parry, Shield
    { "if": {"required":["kind"], "properties":{"kind":{"const":"reduce_damage"}}},       "then": {"oneOf":[{"required":["fraction"]},{"required":["dice"]}], "propertyNames":{"enum":["kind","fraction","dice"]}} },  // Uncanny Dodge, Deflect Missiles
    { "if": {"required":["kind"], "properties":{"kind":{"const":"impose_disadvantage"}}}, "then": {"propertyNames":{"enum":["kind"]}} },                                                    // Protection
    { "if": {"required":["kind"], "properties":{"kind":{"const":"retaliate"}}},           "then": {"required":["damage"], "dependentRequired":{"save":["on_success"],"on_success":["save"]}, "propertyNames":{"enum":["kind","damage","save","on_success"]}} },  // Hellish Rebuke
    { "if": {"required":["kind"], "properties":{"kind":{"const":"attack"}}},              "then": {"required":["attack_id"], "propertyNames":{"enum":["kind","attack_id"]}} },              // Riposte
    { "if": {"required":["kind"], "properties":{"kind":{"const":"cast"}}},                "then": {"required":["spell"],  "propertyNames":{"enum":["kind","spell"]}} },                      // monster casts a listed spell
    { "if": {"required":["kind"], "properties":{"kind":{"const":"subtract_roll"}}},       "then": {"required":["dice"],   "propertyNames":{"enum":["kind","dice"]}} } ] }                    // reserved (Q6)
```

The reaction's trigger field is named **`window`** so it is never confused with `on_event` effects (§3)
or the rules `ReadiedTrigger`. All reactions share the one-reaction-per-round budget
(`Combatant::reaction_used`, unchanged).

### 4.2 Rules sketch

New `ReactionTrigger` values (append): `Damaged` (after damage applies to the reactor),
`AllyAttacked` (attack roll against an ally the reactor can reach), `RollAttack` (phase 2: attack
roll about to resolve, for roll-modifying reactions). `BeingHit` stays "hit confirmed, damage not
rolled". New `ReactionOptionKind` values (append): `ReduceDamage`, `ImposeDisadvantage`,
`Retaliate`, `Rider` (§3), `SubtractRoll`. `ReactionDecision` is unchanged except an optional
appended `uint8_t choice = 0` (slot level for `Rider`/`cast`).

```cpp
// types.hpp: enum class ReactionTrigger { LeavingReach, BeingHit, SpellCast, ReadiedTrigger,
//                                         HitRider /* row 9c, landed: value 4 */,
//                                         Damaged, AllyAttacked /* row 11, appended after HitRider */
//                                         /*, RollAttack (phase 2)*/ };
// kReactionTriggerCount = 5 after 9c, 7 after row 11 -> Combatant::reaction_policy grows; defaults stay Ask
// Combatant (appended): std::vector<Reaction> reactions;   // compiled from content, id/trigger/effect/usage
```

Options come from the existing hooks: sim's `being_hit_options` / `spell_cast_options` read
`Combatant::reactions`; add `damaged_options` and `ally_attacked_options` to `EncounterHooks`
(appended, empty = none). Eligibility for `ally_attacked` uses the existing `in_range` hook
(Engine Core supplies adjacency). Opportunity attacks keep their current separate path; a
`leaving_reach` reaction with `effect.kind: "attack"` is the same thing and is **not** offered in
addition (deduplicated by `attack_id`), so a creature never gets two OAs.

### 4.2a Row 11 as implemented (rules side)

* **Values.** `ReactionTrigger`: `HitRider` 4, `Damaged` 5, `AllyAttacked` 6 (`kReactionTriggerCount` 7; `RollAttack` stays deferred, Q6). `ReactionOptionKind` appended after `Rider`: `ReduceDamage`, `ImposeDisadvantage`, `Retaliate`, `SubtractRoll` (reserved, never offered). `ReactionOption` gains `half` and `dice`; `ReactionWindow` gains `damage` and `ranged`. `ReactionDecision` is unchanged (no `choice` field was needed: Rider options enumerate the slot levels).
* **Deviation from the sketch: options are built in rules.** `Combatant::reactions` (`Reaction{id, window, kind, melee_only, ranged_only, half, dice, spell_id, attack_index}`, filled by `Combatant` creation) is read by rules5e itself for the `Damaged` and `AllyAttacked` windows, so Engine Core only compiles content into rows; `EncounterHooks::damaged_options` / `ally_attacked_options` are appended as extension points (their options come after the compiled ones, and either source alone opens the window). `being_hit_options` / `spell_cast_options` are unchanged (sim still feeds `ac_bonus` reactions through them).
* **`Damaged` window.** Opened by `damage_creature` (weapon, spell-attack and save damage alike) when the damage after mitigation is above 0, the creature is alive, and it has compiled reactions or a hook. **It opens before the damage is committed** (the sketch says "after damage applies"; reducing damage needs it before): `window.target` = the reactor, `trigger_actor` = the damager (0 if unknown), `damage` = amount after mitigation and before temporary HP, `ranged` = the attack was ranged. Reactor eligibility: alive, can act, reaction unspent, policy not `Never`, and `in_range` (default: the reactor is the creature damaged). Options: `ReduceDamage` (`half` = floor(damage / 2), else the `dice` are rolled and subtracted, clamped to the damage; temporary HP, drop-to-zero and massive damage are recomputed by `reduce_applied`), `Retaliate` and `Attack` (only when a living damager other than the reactor exists). Order inside `damage_creature`: Damaged window, `drop_to_zero` triggers (on the reduced damage), commit and `DamageDealt`, `damage_dealt` / `reduce_to_zero` triggers, then retaliate / riposte, then concentration and repeat saves. A reduced hit that no longer drops the creature to 0 does not fire Undead Fortitude. Events: `ReactionWindowOpened`, `ReactionTaken`/`Declined`; the effect is visible in `DamageDealt` (no extra event kind).
* **`Retaliate`** (Hellish Rebuke): `Reaction::spell_id` names a synthetic `SpellDef` (Save kind with `fixed_dc`, `save_ability`, `dice`, `damage_type`, `on_save`; or an auto-hit def for "no save") resolved against the damager with the existing resolution, at reaction depth +1, **only if the reactor still stands** (a lethal hit still spends the reaction). **`Attack`** (Riposte): the reactor makes `attacks[attack_index]` against the damager, same rule. A `retaliate` whose def does not exist is not offered.
* **`AllyAttacked` window** (Protection): opened by `do_attack` before the attack roll, for each creature on the target's team other than the target (initiative order, `poll_order`) that can react, **passes the `in_range` hook (no hook: nobody; Engine Core supplies adjacency)**, and has an option. Options: `ImposeDisadvantage` (`melee_only` / `ranged_only` filter on the attack). The first taken adds disadvantage to that attack (it does not stack, so later protectors are not asked).
* **Nesting.** Both windows obey `allow_chain` (they open at depth 0, or deeper only if the hook allows). Triggers run from a context-less turn_start `area_save` get a depth-1 context, so no reaction window opens inside them.
* **Not in this PR.** `cast` reactions (a monster casting a listed spell from a window; sim can already hand SpellBook reaction spells through the existing hooks), `usage` limits on reactions, the `visible` window flag and `source: ally_within_5ft` for Damaged, and de-duplicating a `leaving_reach` attack reaction against the opportunity attack (all content/sim-side: the compiled `Reaction` rows simply omit them; rules need no change when they arrive).
* Goldens unchanged: no compiled reactions and no hooks mean no windows, events or rng draws. Event schema stays v7.

### 4.3 Version gate and inert behaviour

* `@4` documents: `reactions[]` entries are validated and compiled.
* `@≤3` documents migrate with each `reactions[]` entry stamped `inert: true` and `"reactions"` added to
  `inert_mechanics` (sim: `Archetype::reactions` stays empty, `Archetype::inert_mechanics` reports it). Any
  `cost: "reaction"` effect cannot exist at `@≤3` (schema).
* No options means no window and no `NeedsDecision`, which is the pre-bump flow.

## 5. Monster spellcasting (T&C-G10)

`@3` already has slot-based `spellcasting` (`ability`, `caster_level`, `slots`, `pact`, `spells[]`
content ids; spells.hpp consumes it: slots, `CastSpell`, `spell_ids`). The `@4` block is a strict
superset in `common@2 $defs/spellcasting`:

| Field | Type | Notes |
|---|---|---|
| (all v1 fields) | | unchanged meaning |
| `dc` | integer | optional override; SRD monsters print DC/attack bonus and the derived `8+prof+mod` can disagree |
| `attack_bonus` | integer | optional override |
| `innate` | object | below |
| `innate.ability` | `$defs/ability` | |
| `innate.components` | `"none"` \| `"as_listed"` | default `"none"` (SRD innate casting needs no components) |
| `innate.at_will` | `content_id[]` | spell content ids (`$defs/content_id`, same as `spells[]`) |
| `innate.per_day` | `{ "1": [content_id...], "3": [content_id...] }` | key = uses/day (`1..9`), values `content_id[]` |
| `innate.recharge` | `[{spell: content_id, min}]` | rare ("Recharge 6"), `usage.recharge` semantics |

```json
"spellcasting": { "ability": "int", "caster_level": 9, "slots": {"1":4,"2":3,"3":3,"4":3,"5":1},
  "spells": ["srd51.spell.fireball","srd51.spell.shield"], "dc": 17, "attack_bonus": 9,
  "innate": { "ability": "cha", "at_will": ["srd51.spell.detect_magic"], "per_day": {"3": ["srd51.spell.fly"]} } }
```

Why `@4` and not an additive `common@1` change: v1 `spellcasting` is `additionalProperties:false`
and shared by `@3` files; widening it would let `@3` documents carry `innate`, contradicting the
`@3` contract. The block is *not* inert at `@3` (slot casting is live since #47), so the
`3→4` migration changes nothing for it.

Rules:

```cpp
struct InnateSpell { uint32_t spell_id; uint8_t uses_max;   // 0 = at will
                     uint8_t uses_left; uint8_t recharge_min = 0; };
// Combatant (appended): std::vector<InnateSpell> innate; int16_t spell_dc_override = 0, spell_attack_override = 0;
// ActionRequest (appended at END): bool innate = false;   // CastSpell via innate list: level = spell.level,
//                                                          // no slot, consumes a use, no material components
```

`legal_actions` offers `CastSpell{spell, innate=true}` for each innate spell with uses left
(`spell_level` is the spell's own level; upcasting disallowed). The DC/attack override feeds
`caster_spell_dc()` / `caster_spell_attack()` ahead of the derived values. `InnateSpell` uses are part
of encounter state and start full each encounter (no rest model, Q16). Spells cast by `cast` reactions
(§4) go through the same list.

#### 5.1 Row 8 as implemented (rules) and hand-off to Engine Core

Rules side (`InnateSpell` in `types.hpp`, `Combatant::{innate, spell_dc_override, spell_attack_override}`,
`ActionRequest::innate` appended last):

* `InnateSpell{spell_id, uses_max (0 = at will), uses_left, recharge_min (>0 = "Recharge N-6", one use), has_ability/ability, as_listed}`.
  Two fields beyond the sketch above: `has_ability`/`ability` (the innate block's own ability, e.g. Cha, applied for
  that cast only; slot casting keeps `spellcasting_ability`) and `as_listed` (content `components: "as_listed"`;
  default false = no components, `can_provide_components` is not consulted).
* `CastSpell` with `innate = true`: the spell must be in `Combatant::innate` and in the `SpellBook` (else "not an innate
  spell" / "innate spells must be in the spell book"); `spell_level` must equal the spell's own level ("innate spells
  cannot be upcast"); no slot, `spells_known` not required; uses: at will none, per-day `--uses_left`, recharge sets
  `uses_left = 0` ("innate spell has no uses left" when none). It is a normal spell otherwise (`SpellCast` window, Counterspell,
  concentration, `SpellResolved`).
* `legal_actions` offers `CastSpell{innate = true, spell_level = spell.level}` per valid target while uses remain.
* DC / attack: `spell_save_dc(c)` / `spell_attack_bonus(c)` use the override when non-zero; `SpellDef::fixed_dc/fixed_attack` still win.
* Recharge: at the start of the owner's turn, after the row 5 action-use rolls and before death saves, one d6 per expended
  innate recharge spell (`RechargeRolled`, `a` = spell id). No draw otherwise.
* Digest: `has_innate_state(state)` / `innate_digest(state)` (rules). **Engine Core folds it in `Combat::hash` under tag `0xA4`,
  only when `has_innate_state` is true**, and folds `ActionRequest::innate` into `log_hash` only when true, under a tag byte (9.3 item 5).

Engine Core hand-off: in `Combat::create`, for an archetype with `spellcasting.innate`, fill `Combatant::innate`
(at_will -> `uses_max 0`; `per_day{"N":[..]}` -> `uses_max = uses_left = N`; `recharge[{spell,min}]` -> `recharge_min = min, uses_left = 1`;
`ability` -> `has_ability = true`; `components == "as_listed"` -> `as_listed = true`), resolve `spell_id = spell_id_of(content_id)` against the
loaded `SpellBook`, and set `spell_dc_override = dc`, `spell_attack_override = attack_bonus` from the block. Nothing is added to
`spells_known` for innate spells. Pass `ActionRequest::innate` through `Combat::submit` unchanged (it is part of the request).
Old archetypes leave all of it empty, so nothing is hashed and no rng is drawn.

## 6. Timed buffs and debuffs (R-G3)

### 6.1 Model

A *modifier* is a timed, sourced change to one number or roll. It generalizes `RollTrait` (#54)
and the `ac_bonus` spell effect (instantaneous reaction today). **Integers only**: speed scaling is
`speed_scale: "half" | "double"` or `{num, den}`.

```json
"$defs/modifier": { "type":"object", "additionalProperties": false, "required": ["stat","duration"],
  "properties": {
    "stat": { "enum": ["attack_roll","save","ability_check","ac","speed","damage","temp_hp","resistance","attacks_against"] },
    "bonus": {"type":"integer","minimum":-32767,"maximum":32767},
    "bonus_table": {"$ref":"#/$defs/level_table"},                            // stat damage only (Rage, §7.1)
    "die":   {"$ref":"../common/v1.schema.json#/$defs/dice"},                 // Bless: stat attack_roll/save, die 1d4
    "mode":  {"enum":["advantage","disadvantage"]},                           // Faerie Fire: attacks_against + advantage
    "speed_scale": { "oneOf": [ {"enum":["half","double"]},
                    {"type":"object","additionalProperties":false,"required":["num","den"],
                     "properties":{"num":{"type":"integer","minimum":1},"den":{"type":"integer","minimum":1}}} ] },
    "damage_type": {"$ref":"../common/v1.schema.json#/$defs/damage_type"},   // resistance, damage
    "ability": {"$ref":"../common/v1.schema.json#/$defs/ability"},            // save / ability_check scope
    "applies_to": {"enum":["self","target","allies_within_30"]},
    "duration": {"$ref":"#/$defs/modifier_duration"},
    "stacks": {"enum":["replace","stack"],"default":"replace"} },
  "allOf": [   // gated by `stat`: allow-list per stat (closed-union convention, §3.1)
    { "if": {"required":["stat"], "properties":{"stat":{"enum":["attack_roll"]}}},       "then": {"anyOf":[{"required":["bonus"]},{"required":["die"]},{"required":["mode"]}],
        "propertyNames":{"enum":["stat","bonus","die","mode","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"enum":["save","ability_check"]}}}, "then": {"anyOf":[{"required":["bonus"]},{"required":["die"]},{"required":["mode"]}],
        "propertyNames":{"enum":["stat","bonus","die","mode","ability","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"ac"}}},                 "then": {"required":["bonus"], "propertyNames":{"enum":["stat","bonus","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"speed"}}},              "then": {"required":["speed_scale"], "propertyNames":{"enum":["stat","speed_scale","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"damage"}}},             "then": {"anyOf":[{"required":["bonus"]},{"required":["die"]},{"required":["bonus_table"]}],
        "not": {"required":["bonus","bonus_table"]},                            // bonus XOR bonus_table (D24)
        "propertyNames":{"enum":["stat","bonus","bonus_table","die","damage_type","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"temp_hp"}}},            "then": {"required":["bonus"], "propertyNames":{"enum":["stat","bonus","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"resistance"}}},         "then": {"required":["damage_type"], "propertyNames":{"enum":["stat","damage_type","applies_to","duration","stacks"]}} },
    { "if": {"required":["stat"], "properties":{"stat":{"const":"attacks_against"}}},    "then": {"required":["mode"], "propertyNames":{"enum":["stat","mode","applies_to","duration","stacks"]}} } ] }

"modifier_duration": { "type":"object", "additionalProperties": false, "required":["kind"],
  "properties": { "kind": {"enum":["rounds","until_start_of_caster_turn","until_end_of_target_turn","concentration","until_rest"]},
                  "n": {"type":"integer","minimum":1} },
  "allOf": [ { "if": {"required":["kind"], "properties":{"kind":{"const":"rounds"}}}, "then": {"required":["n"]}, "else": {"not":{"required":["n"]}} } ] }
```

`ac` modifiers are `bonus`-only in this batch (Q15); there is no `set`. Surfaces:

* **Spells (additive, no bump)**: `spell@1` effect kind `apply_modifier` with `modifier` (Bless,
  Bane, Shield of Faith, Haste, Slow, Faerie Fire via `attacks_against`+`advantage`, Hunter's
  Mark via `damage`).
* **Features/traits (`@4`, Q2 decided)**: effect kind `apply_modifier` (Rage, Bardic Inspiration
  token, Leadership). Frightful Presence's conditions use §2's `apply[]`, not this.

### 6.2 Rules sketch

```cpp
struct ActiveModifier { uint32_t id; uint32_t source;          // spell/feature id
                        CombatantId caster, target;
                        uint8_t stat; int16_t bonus; DiceExpr die; int8_t mult; DamageType dtype;
                        Ability ability; uint8_t expiry_kind; int16_t rounds_left; bool concentration; };
// EncounterState (appended): std::vector<ActiveModifier> modifiers;   // serialized + hashed; empty for old content
// Queries (pure): modified_ac(), modified_speed(), roll_bonus(CombatantId, RollKind, Ability) ...
```

Evaluation points: attack/save/check totals add `bonus` and roll `die` *at the moment of the roll*
(one rng draw per die, drawn only when a modifier with a die exists: rng invariant); `ac`/`speed`
read when needed; `attacks_against` merges into the existing advantage/disadvantage resolution as
an extra source (same code path as `RollTrait`). Expiry: `begin_turn`/`end_turn` decrement;
concentration modifiers drop when `ActiveSpellEffect` concentration ends (the same drop function).
`ModifierApplied` / `ModifierExpired` events (§9.1).

### 6.2a Row 12 as implemented (rules side) and hand-off to Engine Core

Rules side only (`feat/rules-modifiers-12`); sim, `content.cpp` and `Combat::create` are untouched. Event schema stays v7
(`ModifierApplied` / `ModifierExpired` were already reserved). Goldens are unchanged: with no modifier on the board every
new code path is skipped and no extra rng is drawn.

- **Types.** `ModStat` (AttackRoll, Save, AbilityCheck, Ac, Speed, Damage, TempHp, Resistance, AttacksAgainst),
  `ModAppliesTo` (Self, Target, AlliesWithin30), `ModifierSpec` (id, source, stat, bonus, die + die_mult, mode,
  speed_num/den, damage_type, ability, scope, applies_to, expiry, rounds, until_caster_turn, concentration, stack),
  `ActiveModifier {spec, caster, target, Timed}` in `EncounterState::modifiers`. Timers reuse `Timed` (`make_timed`,
  `tick_round_start`, `tick_target_turn_end`).
- **Evaluation.** Flat bonuses add; dice are rolled at the moment of the roll, after the d20, one rng draw per die and only
  if a die exists, in application order. Modes (attack_roll, attacks_against, save, ability_check) merge into the existing
  advantage/disadvantage resolution. Ac adds to the cover AC bonus of the attack. Resistance is mirrored into
  `Combatant::mod_resist` (a rules-maintained cache OR-ed into `effective_resistances`).
- **Events.** `ModifierApplied` actor=caster target a=id b=ModStat c=magnitude (bonus, else die count*100+sides, else the
  RollMode, else speed num*100+den, else 0). `ModifierExpired` actor=caster target a=id b=reason (0 duration ran out,
  1 concentration ended, 2 replaced by a same-id modifier).
- **Producers.** `SpellEffectKind::ApplyModifier` with `SpellDef::modifier`; `EncounterHooks::feature_modifiers(state, actor,
  feature_id)` applied after `FeatureUsed` on UseFeature (Self = actor, Target = req.target, AlliesWithin30 = `area_targets`
  with a 30-ft sphere, same team, caster excluded).
- **Deviations from section 6.1.** (1) Speed is read when the target's turn starts (budget), not mid-turn. (2) Damage
  modifiers apply to weapon attacks only (spell_id == 0), and `ModifierSpec::scope` (0 any, 1 melee, 2 ranged) was added for
  Rage. (3) ApplyModifier is a primary spell effect only, not an `extra_effects` entry. (4) TempHp is instantaneous (larger
  pool kept), emits ModifierApplied and leaves no record. (5) One `ModifierSpec` serves spells and features. (6)
  `bonus_table` is resolved by the loader into `bonus`. (7) `until_rest` has no in-encounter expiry (expiry 0 and no
  concentration). (8) The legal-action enumeration is unchanged.
- **Hand-off (Engine Core).** (a) Compile spell and feature `apply_modifier` content into `SpellDef::modifier`
  (`kind = ApplyModifier`) and the `feature_modifiers` hook rows; register the SpellDefs in the SpellBook. (b) Fold
  `modifiers_digest(state)` in `Combat::hash` under tag **0xA2**, only when `has_modifier_state(state)` is true. (c) Make
  `hooks.area_targets` answer a 30-ft sphere centred on the actor (AlliesWithin30). (d) Decide AI use of modifier spells and
  features. (e) Sim code reading AC or resistances directly should add `modified_ac_bonus` / `effective_resistances`; rules
  already apply them internally.

### 6.2b Row 12 follow-ups as implemented (rules side) and hand-off

Rules side only (`feat/rules-modifiers-followups`); goldens unchanged (new fields default to "off", and a combat without modifiers
runs none of this). `ModifierSpec` gains, appended:

- `consume` (0 never; 1 after the target's next attack roll; 2 after the next attack roll made against the target; 3 when the
  target takes damage). It combines with any expiry (first wins). `ModifierExpired` reasons gain **3** (spent by an attack roll,
  emitted just before that `AttackResolved`) and **4** (the target took damage, via `damage_creature`). A modifier gated off by
  `when` is not consumed. Use: True Strike (AttackRoll advantage, consume 1), Guiding Bolt (AttacksAgainst advantage, consume 2 +
  `until_caster_turn`), Vicious Mockery (AttackRoll disadvantage, consume 1), "until damaged" effects (consume 3).
- `speed_sub` (feet, subtracted after the num/den scaling, clamped at 0; Ray of Frost 10). `ModifierApplied.c` for a Speed
  modifier is `speed_sub` when num == den, else num*100+den.
- `ac_floor` / `ac_floor_dex` on `ModStat::Ac`: AC is at least `ac_floor` (+ Dex mod) before the bonuses (Barkskin 16, Mage Armor
  13 + Dex). `modified_ac_bonus` returns the floor shortfall plus the flat bonuses. The caller (loader) decides "not wearing
  armor". `ModifierApplied.c` = `ac_floor` when there is no flat bonus.
- `per_turn` on `ModStat::TempHp` (Heroism): the first grant is immediate (`ModifierApplied`), the record is kept, and the target
  gets `bonus` again (the larger pool is kept) at the start of each of its turns, with a `ModifierApplied` each time, until the
  modifier expires. A TempHp modifier without `per_turn` is unchanged (instant, no record).
- `when` (a `std::vector<Predicate>`, reusing the row 9 predicate kinds): every predicate must hold. Evaluated for AttackRoll
  (owner = attacker), AttacksAgainst and Ac (owner = the defender) before the roll (so `NotCrit` is true and the roll mode is the
  pre-modifier one), and for Damage with the real crit and damage type (owner = attacker, subject = target). Save-rolling
  predicates never hold. Not evaluated for Save / AbilityCheck / Speed / Resistance / TempHp.
- `SpellEffect::modifier`: `ApplyModifier` is now allowed as an **extra (rider) effect**, with `affected_only` (the earlier
  "primary only" deviation is lifted): Guiding Bolt = SpellAttack + rider.
- `modifiers_digest` folds all the new fields (still only folded, under 0xA2, when `has_modifier_state`).
- **Deviations / limits.** `consume` 3 fires through `damage_creature` only (spell, weapon and trigger damage; not a direct
  `apply_damage` caller). `when` is not available on Save / AbilityCheck. "True Strike next attack" does not distinguish
  weapon from spell attacks (any attack roll spends it).
- **Hand-off (Engine Core / T&C).** Compile the new `modifier` properties (`consume`, `speed_sub`, `ac_floor`, `per_turn`,
  `when` with the row 9 predicate vocabulary) into `ModifierSpec`; allow `apply_modifier` in a spell's `effects[]` after the
  first (maps to `SpellEffect::modifier`); no new hash tag (the 0xA2 digest covers them); no new event kinds.

- **T&C schema follow-up (done).** `common@2 $defs/modifier` gains the optional properties `consume`, `speed_sub`,
  `ac_floor`, `ac_floor_dex` (needs `ac_floor`), `per_turn` and `when` on the stats `parse_modifier` accepts them for (an `ac`
  modifier needs `bonus` or `ac_floor`); `spell@1` declares the additive effect kind `apply_modifier` (`modifier`, `applies_to`
  for riders). Additive, no tag change: see `docs/CONTENT_CONVENTIONS.md` section 1.

### 6.3 Inert behaviour

Spell side is additive (old spells contain no such kind). Feature side: `3→4` marks any pre-existing
`apply_modifier`-kind effect inert (§8.2). Pre-existing descriptive spell text (Bless etc.) is unchanged
until a spell document is *re-authored* with the new effect kind, which is a content change, not
an engine change.

## 7. Class resource pools (R-G5)

### 7.1 Content shape

`resources[]` on `character@4` and `monster@4` stat blocks (Legendary Resistance 3/day is a
monster pool; Ki, Rage, Sorcery Points, Channel Divinity, Lay on Hands, Bardic Inspiration for
characters):

```json
"$defs/resource": { "type":"object", "additionalProperties": false,
  "required": ["id","max","recharge"],
  "properties": {
    "id": {"$ref":"../common/v1.schema.json#/$defs/local_id"}, "name": {"type":"string"},
    "max": { "oneOf": [
        {"type":"integer","minimum":1},
        {"type":"object","additionalProperties":false,
         "anyOf":[{"required":["per_level"]},{"required":["plus_ability"]}],
         "properties": { "per_level": {"type":"integer","minimum":1},
                         "class":      {"$ref":"../common/v1.schema.json#/$defs/local_id"},
                         "plus_ability":{"$ref":"../common/v1.schema.json#/$defs/ability"},
                         "min":        {"type":"integer","minimum":1} } },
        {"type":"object","additionalProperties":false,"required":["table"],
         "properties":{"table":{"$ref":"#/$defs/pool_level_table"}}} ] },          // level table, see below (D24)
    "start": {"type":"integer","minimum":0},                       // default = max
    "recharge": {"enum":["short_rest","long_rest","dawn","never"]} } }
```

**Object-form `max` semantics (`character@4` only; a monster pool must use an integer, validator check 14):**
`max = per_level x L + mod(plus_ability)`, then at least `min` (default 1), where `L` is the level in
`class` when given (else total character level), a missing `per_level` counts as 0, and a missing
`plus_ability` contributes 0 (at least one of the two is required). Examples: Ki
`{per_level:1, class:"monk"}`; Lay on Hands `{per_level:5, class:"paladin"}`; Sorcery Points
`{per_level:1, class:"sorcerer"}`; Bardic Inspiration `{plus_ability:"cha", min:1}`; Legendary
Resistance (monster) `3`.

**Level tables (D24, decided).** `resource.max` has a **third `oneOf` arm** `{ "table": <pool_level_table> }`
(mutually exclusive with the integer and with the `per_level`/`plus_ability` object, which stays
`additionalProperties:false`), and `modifier` with `stat: damage` gains **`bonus_table`** (a `level_table`),
mutually exclusive with `bonus`. A table is a **step function resolved by the loader to a plain integer**:
for level `L` (level in `class` when given, else total character level; fixed when the archetype is built,
since sim has no mid-combat level changes) the value is that of the last entry with `level <= L`, or 0 /
feature inert when `L` is below the first entry (only possible with a `class` the character has no levels
in). No runtime lookup, no hashing impact. Integers only, no null, no decimals, no formulas.

```json
"level_table_entry":      { "type":"object","additionalProperties":false,"required":["level","value"],
  "properties":{"level":{"type":"integer","minimum":1,"maximum":20},"value":{"type":"integer","minimum":0,"maximum":255}} },
"pool_level_table_entry": { "type":"object","additionalProperties":false,"required":["level"],
  "properties":{"level":{"type":"integer","minimum":1,"maximum":20},"value":{"type":"integer","minimum":0,"maximum":255},"unlimited":{"const":true}},
  "oneOf":[{"required":["value"]},{"required":["unlimited"]}] },
"level_table":      { "type":"object","additionalProperties":false,"required":["by_level"],
  "properties":{ "class":{"$ref":"../common/v1.schema.json#/$defs/local_id"},
    "by_level":{"type":"array","minItems":1,"maxItems":20,
      "prefixItems":[{"allOf":[{"$ref":"#/$defs/level_table_entry"},{"required":["level"],"properties":{"level":{"const":1}}}]}],
      "items":{"$ref":"#/$defs/level_table_entry"}} } },
"pool_level_table": { /* identical to level_table with pool_level_table_entry in both positions */ }
// resource.max, third oneOf arm:   { "type":"object","additionalProperties":false,"required":["table"],"properties":{"table":{"$ref":"#/$defs/pool_level_table"}} }
// modifier base property:          "bonus_table": {"$ref":"#/$defs/level_table"}
// modifier stat=damage arm:        "anyOf":[{"required":["bonus"]},{"required":["die"]},{"required":["bonus_table"]}],
//                                  "not":{"required":["bonus","bonus_table"]}, propertyNames enum += "bonus_table"
```

`unlimited` (pools only) is an explicit boolean flag, not a magic number (`-1`, `null` and `0` were
rejected: `0` is a legitimate table value, `null` is not integers-only and the C++/Python parsers diverge).
Rules maps it to the existing `FeatureUses` "255 = unlimited" convention: `can_spend` is always true and
nothing is decremented. The damage-table entry def has no `unlimited`, so it is a schema error there.
Values are 0..255 (the uint8 `FeatureUses` range; widening later is additive).

**Validator rules for tables** (one negative case each, in `validate_packs.py`): (1) `level` strictly
ascending; (2) `unlimited` only on the **last** entry; (3) **float tokens rejected** (`2.0` passes JSON
Schema `integer`; check `isinstance(x, int) and not bool`); (4) `class` is `character@4`-only (monster tables
rejected); (5) `start` is not allowed with `max.table` (pools start full; with `unlimited` there is nothing
to start); (6) `usage.cost` / `spend.cost` must not exceed the table's largest finite `value` (warn if it
exceeds the level-1 value: legal but unusable at low levels). The schema deliberately accepts unsorted and
duplicate levels so rules (1)/(2) own them.

**Rage** (`character@4`, SRD barbarian: 2/3/4/5/6/unlimited at levels 1/3/6/12/17/20; damage +2/+3/+4 at 1/9/16):

```json
"resources": [ { "id":"rage","name":"Rage","recharge":"long_rest",
  "max": { "table": { "class":"barbarian", "by_level": [
    {"level":1,"value":2},{"level":3,"value":3},{"level":6,"value":4},{"level":12,"value":5},{"level":17,"value":6},{"level":20,"unlimited":true} ] } } } ],
"features": [ { "id":"rage","name":"Rage",
  "effect": { "kind":"apply_modifier","cost":"bonus_action","usage":{"kind":"pool","pool":"rage","cost":1},
    "modifiers": [
      { "stat":"damage","applies_to":"self","duration":{"kind":"rounds","n":10},
        "bonus_table": { "class":"barbarian","by_level":[{"level":1,"value":2},{"level":9,"value":3},{"level":16,"value":4}] } },
      { "stat":"resistance","damage_type":"bludgeoning","applies_to":"self","duration":{"kind":"rounds","n":10} },
      { "stat":"resistance","damage_type":"piercing","applies_to":"self","duration":{"kind":"rounds","n":10} },
      { "stat":"resistance","damage_type":"slashing","applies_to":"self","duration":{"kind":"rounds","n":10} } ] } } ]
```

Rage's resistances are separate entries of the effect's **`modifiers[]`** (the `apply_modifier` arm takes `modifier` or `modifiers[]`), with `stat: resistance`, one per damage type, as in the example. **Not covered**
(separate rows, because `modifier` has no `when` scope yet): Rage's melee-only Strength scope, the Rage
ending conditions, and the heavy-armor restriction. The static `damage_bonus` effect (`@3`, live) is
**not** extended with `amount_table` unless a non-Rage static case appears (it would need a v2 arm).
`recharge` is when it refills, **by the §7.2 rule (no rest model): pools start full each encounter and
`short_rest`/`long_rest`/`dawn` only matter once a rest model exists**. Resource ids are unique within a
document (validator, §8.3).

Features spend a pool through `usage: {kind:"pool", pool, cost}` (§2.2). The existing per-feature
`uses` / `recharge` on `self_heal` stays valid at `@4` and is exactly an implicit single-feature
pool (same `FeatureUses` row), so nothing rewrites.

Feature effect kinds that become available at `@4` and need pools or grants:

| Kind | Example | Rules |
|---|---|---|
| `grant_action` | Action Surge, Haste-like | `UseFeature` increments `TurnBudget::extra_actions` (Q7, decided) |
| `apply_modifier` | Rage (+dmg, resistances) | §6 |
| `save_effect` action with `usage.pool` | Turn Undead, Stunning Strike | §2 |
| `heal_pool` | Lay on Hands: spend N from a pool to heal N | `UseFeature` with `amount` (reuses the **existing** `ActionRequest::amount`, as Heal does) |
| `on_event` + `spend` | Divine Smite, Sneak-style resource riders | §3 |

### 7.2 Rules sketch

```cpp
struct ResourcePool { CombatantId actor; uint32_t pool_id; uint16_t max, cur; FeatureRecharge recharge; };
// EncounterState (appended): std::vector<ResourcePool> pools;     // serialized/hashed; empty for old content
// recover_features(state, long_rest) also refills pools by recharge kind
bool can_spend(const EncounterState&, CombatantId, uint32_t pool_id, uint16_t n);
```

`UseFeature` is unchanged for the old path; a feature with a pool cost requires `can_spend` in
`legal_actions` and emits `ResourceSpent`. There is **no rest model** in this batch (Q16, decided). `recover_features` exists in rules but
nothing in sim or game calls it: `Combat::create` registers fresh `FeatureUses` rows from the archetype
and copies spell slots from the archetype each time, so uses, innate spells and pools **start full in
every encounter** and recharge only through their in-combat rules (recharge d6). A real rest model
needs persistent party/creature state outside `Combat` and is its own design.

### 7.3 Inert behaviour

`resources[]` does not exist pre-`@4` (schema). `character@3` features keep `uses`/`recharge`
(still live). Nothing is marked.

### 7.2a Row 13 as implemented (rules side) and hand-off to Engine Core

Rules side only; sim, `content.cpp` and `Combat::create` are untouched. Schema stays v7, no new event kinds
(`ResourceSpent` is reused), goldens unchanged: with no `feature_spec` hook, no `legendary_pool` and no extra actions
every path is the pre-row-13 one and no extra rng is drawn. The pool table, `register_pool`, `can_spend`/`spend_pool`,
`ResourceSpent` and `pools_digest` (tag 0xA3) already existed (#79 / 9c).

- **`FeatureSpec` + `EncounterHooks::feature_spec(state, actor, feature_id)`** (nullopt = plain UseFeature): `pool` +
  `pool_cost` (0 = 1) spend from a registered pool; `cost` 0 follow `as_bonus_action`, 1 action, 2 bonus action,
  3 reaction, 4 free; `grant_actions` (grant_action); `heal_pool` (Lay on Hands).
- **UseFeature order:** validate (registered pool, enough in it, heal target/amount, feature uses, action economy) with no
  state change on failure, then spend the economy, bump `FeatureUses`, emit `FeatureUsed`, `ResourceSpent`
  (actor, a = pool, b = amount, c = remaining), the heal (`Healed` actor = user, target), then modifiers (row 12).
- **grant_action:** `TurnBudget::extra_actions` (appended, 0) += `grant_actions`; `spend_action` takes one when the
  action is already spent; it is gone with the turn (new budget). `legal_actions` counts it as an available action.
- **heal_pool:** the amount is `ActionRequest::amount` (target 0 = self); the pool is charged the full amount
  (no clamping to missing hit points) and the heal is capped by max hp as usual.
- **Legendary Resistance:** `Combatant::legendary_pool` (appended, 0 = none). When that creature fails a saving throw
  (spell Save, repeat save, trigger predicate save, concentration check) and the pool has 1, rules spend it
  automatically: `ResourceSpent` is emitted, then `SavingThrowRolled` reports success. No decision window.
- **Deviations.** (1) No `unlimited` pool: the loader registers no pool for an `unlimited` table entry and leaves
  `FeatureSpec::pool = 0`, so nothing is spent. (2) LR is auto-spent, with no choice. (3) Haste's restricted action is
  not expressible (as §7.1 says). (4) No rest model change.
- **Hand-off (Engine Core).** (a) At archetype build resolve `resources[]` (integer, `per_level`/`plus_ability`, `table`)
  and `register_pool(state, actor, id, max, max, recharge)` in `Combat::create`; set `Combatant::legendary_pool` for a
  Legendary Resistance pool. (b) Provide `hooks.feature_spec` from the feature registry (usage.pool/cost, effect.cost,
  grant_action count, heal_pool) and `hooks.feature_modifiers` (row 12). (c) For `heal_pool`/Lay on Hands the AI or
  caller sets `ActionRequest::amount` and `target`. (d) `TurnBudget::extra_actions` is a new budget field; hash it only
  if sim already hashes the budget. (e) Pools are already folded under 0xA3 (non-empty only).

### 7.2b R-G6 bonus-action attacks (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-bonus-attacks`); no schema change, no new event kind, goldens unchanged (nothing sets
`bonus_attacks`; the new `ActionRequest` / `TurnBudget` fields default to 0).

- `BonusAttack` rows in `Combatant::bonus_attacks`: `attack_index` (or `any_melee`), `count` (Flurry 2), `requires_attacks`
  (bitmask: the Attack action this turn used one of those attacks), `different_weapon` (two-weapon fighting), `requires_modifier`
  (a modifier id in force on the attacker: Frenzy's Rage), `pool` + `pool_cost` (Flurry's ki), `has_damage` + `damage` (the
  off-hand dice without the ability modifier).
- Request: `ActionRequest{Attack, as_bonus_action = true, bonus_option = 1 + index, attack_index}`; `attack_index` must be the
  option's (any melee attack for `any_melee`). The first attack spends the bonus action and the pool (`ResourceSpent`);
  the next `count - 1` attacks of the same option are free and need no new prerequisite (`TurnBudget::bonus_option` /
  `bonus_remaining`). The Rampage grant (`bonus_attack_grant`) is unchanged.
- `TurnBudget::attacks_used_mask` records the attack indices used by the Attack action (including Extra Attack and multiattack
  steps; not bonus attacks); it resets with the turn. `bonus_attack_available(state, actor, option)` is the pure check.
- **Hand-off (Engine Core / T&C).** (a) Compile: two-weapon fighting = a light off-hand attack with `requires_attacks` = the
  light weapons, `different_weapon`, and `damage` = the dice without the ability modifier (omit `has_damage` when the Two-Weapon
  Fighting style applies); Martial Arts = an unarmed attack with `requires_attacks` = unarmed and monk weapons; Flurry of Blows
  = `count` 2 with the ki pool; Frenzy = `any_melee` with `requires_modifier` = the Rage modifier id. (b) Fill
  `ActionRequest::bonus_option` from the AI / `legal_actions`-style enumeration (rules `legal_actions` does not list bonus
  options yet); sim's `log_hash` should fold `bonus_option` when non-zero. (c) `TurnBudget` is not hashed by sim.
- **Deviations / limits.** The off-hand damage without the modifier needs the loader to supply the dice (damage bonuses are baked
  into `Attack::damage`). Frenzy's exhaustion at the end of Rage and Rage ending when the creature did not attack are not
  modelled (they need condition-source work, R-G8 / R-G3). The "light" property is not checked by rules; the loader lists the
  light attacks in `requires_attacks`. Open Hand Technique riders on Flurry are not modelled.

### 7.2c R-G8 conditions with a source, grapple / shove / escape (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-grapple-shove`); no new event kind (event schema stays v7, sim's layout test is untouched), no new
`ActionKind`, no change to existing content behaviour. Goldens are unchanged until sim folds the new digest (below).

- **State.** `EncounterState::sourced`: `SourcedCondition{condition, target, source, escape_dc}`. Written by
  `apply_sourced_condition` / `grapple(...)` (also applies the condition; refuses a dead, immune or self target, and a Grappled
  target more than one size larger), and automatically for spell-applied **Charmed** and **Frightened** (the caster is the source;
  both `ApplyCondition` and save-and-condition effects). Entries are pruned after every step (`prune_sourced`): the condition is gone,
  the target is dead, or (Grappled) the grappler is dead or incapacitated, in which case Grappled is removed with a `ConditionRemoved`
  unless another holder remains. Queries: `condition_source`, `grappled_by`, `size_within_one`.
- **Digest.** `sourced_digest` / `has_sourced_conditions`: **sim must fold under the new tag byte 0xA7, only when non-empty**
  (like 0xA2..0xA6). Until sim does, goldens cannot move. Once it does, a scenario that has a spell-applied Charmed / Frightened will
  carry entries and its hash changes: Engine Core should regenerate those goldens and document it (or fold only entries with a
  Grappled condition; the entries for Charmed / Frightened are the only ones that exist without a grapple).
- **Actions.** `ActionRequest::special` (append-only, with `Attack`): 1 Grapple, 2 Shove prone, 3 Shove 5 ft away, 4 Escape.
  Grapple / shove replace one attack of the Attack action (Extra Attack aware: they consume `attacks_remaining`, else the action);
  Escape takes the action. Requires `target_within_5ft` (sim fact), `multiattack == 0`, no other multiattack in progress.
  The attacker rolls Athletics, the target the better of Athletics / Acrobatics (higher modifier, Athletics on a tie); the attacker
  wins only with a strictly higher total. Both rolls are `CheckRolled` events, attacker first; on the second, `c & 1` = "the target
  held". Result events: `ConditionAdded` (Grappled / Prone) or, for the 5 ft shove, `Moved{actor = target, target = shover, a = 5}`
  (forced movement; sim applies it, rules know no positions). Escape: against the recorded `escape_dc` (a `CheckRolled` with that DC),
  or, when 0, a contest against the grappler's Athletics (`req.target` picks the grappler; 0 = the first); `req.dc > 0` supplies a DC
  when the Grappled condition has no recorded source. Success removes the entry and the condition (`ConditionRemoved`).
- **Movement.** Speed 0 while Grappled was already in `effective_speed`. A Move by a grappler now costs double while it holds a creature
  that is less than two sizes smaller. Rules do not move the grappled creature: sim asks `grappled_by(state, mover)` and moves them
  with the mover (SRD drag). Crawling (double cost while Prone) was already in `Move`.
- **Charmed.** A charmed creature cannot Attack (or grapple / shove) its recorded charmer: the request is rejected.
- **Hand-off (Engine Core / T&C).** (a) Monster grapple riders (crocodile, constrictors): call `grapple(state, attacker, target, escape_dc, ev)`
  from the hit rider with the monster's DC; (b) fold `sourced_digest` under 0xA7 when `has_sourced_conditions`; (c) sim: drag
  grappled creatures on Move, apply the shove push from `Moved{actor != mover}`, end the grapple when the target is moved out of
  reach by an effect (remove the Grappled condition from the target; the next step's pruning drops the entry);
  (d) AI: emit `special` requests; (e) Frightened "cannot willingly move closer" and spell target restrictions for Charmed use
  `condition_source`.
- **Remaining / not done.** Swallow / engulf (the contained state, the swallower's escape and damage-per-turn, the half cover and
  blinded / restrained rules) and web-style ranged restrain with an escape action are not modelled: they need a position-aware
  container state. Charmed's social advantage and Frightened's movement limit are sim facts. Other conditions (Restrained, Paralyzed)
  from monster actions could use `apply_sourced_condition` but are not recorded automatically. Shoving or grappling a creature
  that is not within reach, and the one-free-hand requirement, are the caller's checks.

### 7.2d R-G5 static feature kinds (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-static-features`); no schema change, no event, no digest, no new action. Goldens unchanged: the
fields default to 0 / 0 / 20, which reproduces the old behaviour exactly.

- **`attack_bonus`** (Archery +2 ranged): `Combatant::attack_bonus_melee` / `attack_bonus_ranged` (int8) are added to the attack roll of
  *weapon* attacks (spell attacks excluded), in `do_attack` and `resolve_attack`. The bonus is reported only through the roll total.
- **`crit_range`** (Improved Critical 19, Superior Critical 18): `Combatant::crit_min` (default 20). A natural 20 still always hits and
  crits; a lower natural >= `crit_min` crits only if the attack hits (SRD / Sage Advice). Values below 2 clamp to 2. Weapon attacks only.
  Re-evaluated after a BeingHit reaction changes the hit.
- **`ac_bonus`** (Defense +1 in armor): `AcInput::armored_bonus` is added by `compute_ac` only when `has_armor`; `flat_bonus` stays
  the unconditional bonus (ring of protection). Engine Core applies the result at archetype build as before.
- **Hand-off.** The loader maps `attack_bonus {ranged|melee, amount}` -> `attack_bonus_*` (sum several), `crit_range {min}` ->
  `crit_min` (lowest wins), `ac_bonus {amount, requires_armor}` -> `AcInput::armored_bonus` / `flat_bonus`. If sim hashes a
  `Combatant` field by field, fold the three new fields only when they differ from the defaults.
- **Not covered.** Great Weapon Fighting (damage reroll window, R-G7) and Dueling (already a baked `damage_bonus` with a predicate).
  Per-attack bonuses (a magic weapon's +1) stay in `Attack::to_hit`. A conditional "while wielding a ranged weapon" for Archery is
  implied by the melee / ranged split.

### 7.2e R-G7 d20 reroll / subtract windows, Great Weapon Fighting, Evasion (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-roll-windows`); no schema change, no new event kind, no digest. Goldens unchanged: every new rng draw
happens only when the feature is present (and, for Lucky, only on a natural 1; for Great Weapon Fighting, only on a die showing 1-2;
the AttackRolled window opens only when an enemy has a compiled `SubtractRoll` reaction, is in range by the hook, and can pay).

- **Lucky** (Halfling): `Combatant::reroll_ones` bitmask (1 attack roll, 2 ability check, 4 saving throw). A natural 1 is rerolled
  once and kept (`reroll_natural_one`: with advantage / disadvantage the die showing 1 is replaced and the mode re-applied).
  Applied before modifiers' dice and before Legendary Resistance. Death saves are not covered.
- **Cutting Words** (subtract): appended `ReactionTrigger::AttackRolled` (= 7; `kReactionTriggerCount` 7 -> 8, so
  `Combatant::reaction_policy` grows by one entry) and `ReactionEffectKind::SubtractRoll`; the reserved
  `ReactionOptionKind::SubtractRoll` is now used (`dice` = the die). `Reaction::pool` / `pool_cost` (appended) gate the option and are
  spent when it is taken (`ResourceSpent` after `ReactionTaken`). The window opens after an attack roll (not a natural 1 or 20,
  after Lucky and Bless-style dice), before hit / miss and before the BeingHit window, for every enemy of the attacker that may react
  and that `hooks.in_range` accepts (no hook = nobody, like AllyAttacked). `ReactionWindow::attack_total` / `target_ac` are set.
  The first taker ends it. Auto takes it only when the die's maximum could turn a hit into a miss. The subtracted total is the
  `AttackResolved` b.
- **Great Weapon Fighting**: `Combatant::great_weapon_fighting`; melee weapon attacks (not spells) with `kWeaponTwoHanded`, or
  `kWeaponVersatile` with the appended `ActionRequest::two_handed_grip` (caller fact), roll the weapon dice with
  `roll_dice_reroll_low(.., 2)`: each die showing 1 or 2 is rolled once more. Extra damage parts and rider dice are not rerolled.
- **Evasion**: `Combatant::evasion`; a Dex save against an effect with `OnSave::Half` deals 0 on a success and half on a failure.
- **Hand-off (Engine Core / T&C).** (a) Loader: Halfling Lucky -> `reroll_ones = 7`; Fighting Style GWF -> `great_weapon_fighting`;
  Rogue / Monk Evasion -> `evasion`; Cutting Words -> a `Reaction{window AttackRolled, kind SubtractRoll, dice = bardic die,
  pool = bardic inspiration}` (register the pool with `register_pool`). (b) sim: `in_range` must answer `AttackRolled` (Cutting
  Words: within 60 ft and can see the attacker); until then the window never opens in sim. Default decider: decline or Auto as
  for other windows; the sim layout / policy code that iterates triggers must accept 8. (c) AI: set `two_handed_grip` for a versatile
  weapon held in two hands.
- **Not covered / deviations.** Cutting Words on ability checks and damage rolls, Bardic Inspiration spent by the ally on its own
  roll, Guidance / Resistance (d4 on a check / save: expressible as row 12 modifiers with `consume`), the Lucky feat (luck points),
  Lucky on death saves, and reroll windows that the roller chooses (Indomitable, Portent) are not modelled. The AttackRolled
  window does not open for attack rolls inside resolve_attack (pure API, no state).

### 7.2f R-G15 spell kinds: HP pool, dispel, revive (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-spell-kinds`); no schema change, no new event kind, no digest. Goldens unchanged: the new kinds only act
for spells that use them, and the new state fields are not hashed (see hand-off).

- **`SpellEffectKind::HpPool`** (Sleep, Color Spray; appended = 8). Rolls `scaled_dice` (so `upcast_dice` gives Sleep's +2d8 per level)
  as a pool. Candidates are the targets (sim passes the area's creatures) that are alive, above 0 HP, not already under
  `condition`, not immune to it, and not of an `excluded_types` type (Sleep: undead). They are taken by current HP, then id; each is
  affected while its HP fits in the remainder, and the first that does not fit ends the pass. Each affected creature gets
  `condition` (`ConditionAdded`) and an `ActiveSpellEffect` with the spell's `expiry` / `duration_rounds`. `SpellDef::ends_on_damage`
  (appended) ends the effect when the creature takes damage (Sleep), before the damage is applied, so damage that drops it to 0 still
  leaves it unconscious. The pool roll itself has no event.
- **`SpellEffectKind::Dispel`** (Dispel Magic; = 9). For each target, every (spell, caster) on it, from `effects` and from `modifiers`
  whose `spec.source` is a SpellBook spell, ends when its level is at most the slot level; a higher one ends on a d20 + spellcasting
  modifier check against `dispel_dc_base` (10) + its level (`SpellCheckRolled actor = dispeller, target = creature, c = success`).
  Ending removes that spell's effects and modifiers **on that target** (`SpellEffectEnded` / `ConditionRemoved`,
  `ModifierExpired` reason **5** = dispelled); if the spell then has nothing left anywhere, the caster's concentration on it ends
  (`ConcentrationEnded`). Spell levels come from the new `ActiveSpellEffect::slot_level` / `ActiveModifier::slot_level` (appended;
  recorded by rules when a spell creates them; 0 = the spell's base level). Feature modifiers (no spell def) are not dispelled.
- **`SpellEffectKind::Revive`** (Revivify; = 10). `Combatant::died_round` (appended) is stamped by rules after each step for a newly
  dead creature. The target must be dead and dead for at most `revive_within_rounds` (10 = 1 minute) rounds; it returns with 1 HP,
  death saves reset, Unconscious removed (`ConditionRemoved`), and a `Healed{actor = caster, target, a = 1, b = 1, c = 1}`. A dead
  target is accepted by `CastSpell` only for this kind.
- **Hand-off (Engine Core / T&C).** (a) Loader: `spell@1` kinds `hp_pool {dice, condition, ends_on_damage, excluded_types}`,
  `dispel {dc_base}`, `revive {within_rounds}`; Color Spray's "until the end of your next turn" maps to `expiry` 1 / 1 round
  (approximation, see below). (b) sim: pass the area's creatures as `targets` for Sleep / Color Spray (as for other area spells);
  Revivify's material (300 gp diamond, consumed) goes through `can_provide_components`. (c) Hashing: `died_round`,
  `ActiveSpellEffect::slot_level` / `ends_on_damage` and `ActiveModifier::slot_level` are not in any digest. They only matter while
  a spell effect or a dead creature exists; if sim wants them in the state hash, fold them only when non-zero under the existing
  0xA5 / 0xA2 sections (that would move goldens with timed effects: regenerate and document), or leave them out (they are derived
  deterministically from the logged actions).
- **Not covered / deviations.** Color Spray's end-of-caster's-next-turn expiry is approximated by one round; waking a sleeper with an
  action (shake awake) is not an action kind yet; Dispel Magic on an area / object, and dispelling effects not recorded as spell effects
  or modifiers (summons, terrain), are not covered; Lesser Restoration-style "end one condition" and delayed / sustained damage
  (Acid Arrow, Heat Metal) are not in this PR; Revivify only works for creatures that died during the encounter.

### 7.2g R-G15 (rest): end a condition, delayed / sustained damage, Wake (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-spell-kinds-2`); no schema change, no new event kind, no digest. Goldens unchanged: the new kinds and the
Wake action only act when used, and the new fields are not hashed (see hand-off).

- **`SpellEffectKind::EndCondition`** (Lesser Restoration; appended = 11). `SpellDef::ends_conditions` (appended, bitmask of
  `Condition`) lists what the spell can end. On each target, the **lowest** `Condition` value both present and listed is ended (one
  per cast; the SRD lets the caster choose, so a caller wanting another order passes one condition in the mask). Spell effects that
  imposed it end through the usual path (`SpellEffectEnded` + `ConditionRemoved`); if the creature still has the condition (another
  source, or no effect), it is removed with a bare `ConditionRemoved`. Nothing to end = cast succeeds, no event. (Lesser Restoration
  on a disease is out of scope: there is no disease model.)
- **`SpellEffectKind::DelayedDamage`** (Acid Arrow's second hit; = 12). Pushes an `ActiveSpellEffect` with no condition
  (`no_condition`, appended) and the damage to deal (`damage_when` = 1, `damage`, `damage_type`, `magical`, appended). The spell's
  `expiry` / `duration_rounds` pick the timer (Acid Arrow: `expiry` 2 = end of the target's next turn). When the timer runs out, the
  damage is rolled and applied (`DamageDealt`, through `damage_creature`: resistances, concentration, Sleep-style wake, death), then
  the effect ends (`SpellEffectEnded` with `b = -1`, as for any effect whose condition stays; no `ConditionRemoved`). Use it as a rider
  (`extra_effects` with `affected_only`) on a spell attack so it only follows a hit; as a primary kind it always lands.
- **`SpellEffectKind::SustainedDamage`** (Heat Metal; = 13). Deals the (slot-scaled) damage now and pushes a no-condition effect
  with `damage_when` = 2 and `cast_stamp` (round << 16 | turn index, appended). If `concentration`, the caster concentrates on it,
  so the effect ends with concentration. **Repeat:** `ActionRequest{caster, CastSpell, target}` with `spell_id` and
  `sustain = true` (appended) spends the **bonus action**, no slot, no components, no `SpellCast` / counter window; it rolls the
  stored dice, applies them (`DamageDealt`), and emits `SpellResolved{actor = caster, target, a = spell, b = slot level}`.
  Refused on the turn of the cast, without a bonus action, or without that caster's effect on that target. (Heat Metal's
  drop-the-object / disadvantage rider is not modelled.)
- **Wake** (`ActionRequest::special = 5`, documented choice: special-attack style, like Escape = 4, so no new `ActionKind`).
  `ActionRequest{actor, Attack, target}`, `special = 5`, `target_within_5ft = true`: spends the action and ends every
  `ends_on_damage` effect on the target (Sleep), with `SpellEffectEnded` + `ConditionRemoved`. Refused if not within 5 ft, as a
  bonus action, or if the target has no such effect. Unconsciousness from 0 HP is not affected.
- **Hand-off (Engine Core / T&C).** (a) Loader: `spell@1` kinds `end_condition {conditions[]}`, `delayed_damage {dice, damage_type,
  expiry}` (also as a rider in `extra_effects`), `sustained_damage {dice, damage_type, upcast_dice, concentration}`. (b) sim / UI: offer
  "Heat Metal again" as a bonus action (`CastSpell` + `sustain`) while the caster's effect exists; offer "Wake" (special 5) next to a
  sleeping creature; Lesser Restoration's condition choice can be passed by narrowing `ends_conditions` per cast only if the loader
  builds per-condition defs, otherwise lowest-first applies. (c) Hashing: `no_condition`, `damage_when`, `damage`, `damage_type`,
  `magical` and `cast_stamp` are not in `timed_effects_digest` (0xA5 folds spell, caster, target, condition, timer). They exist only
  while one of these spells is active; if sim wants them hashed, fold them only when `damage_when != 0` (moves no goldens without
  these spells), or leave them out (derived deterministically from logged actions).
- **Not covered / deviations.** Lesser Restoration ends the lowest listed condition rather than a caster choice; diseases are not
  modelled; Heat Metal's object-holding rider is not modelled; Wake does not wake a creature at 0 HP; delayed damage has no own event
  (only `DamageDealt`, with actor = the caster).

### 7.2h R-G16 surprise, Hit Dice, rests (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-surprise-rests`); no schema change, no new event kind, no rng draw unless a Hit Die is spent. Goldens
unchanged: the new `Combatant` fields default to off / 0, and surprise state is hashed only when present (tag **0xA8**).

- **Surprise.** `Combatant::surprised` (appended) is set by the caller before `begin_encounter` (`apply_surprise(combatants, ids)`).
  While set, the creature's turn accepts only `EndTurn` (no move, action, bonus action, feature, or spell; `legal_actions` lists only
  `EndTurn`), and `may_react` is false, so no reaction window offers it anything (opportunity attacks, Shield, readied actions, Ask
  windows). Rules clear it when that creature's `EndTurn` resolves (after `TurnEnded`), so it can react for the rest of round 1
  after its turn. Start-of-turn upkeep (regeneration, repeat saves, recharge rolls) still runs. Ability checks and `Heal` requests
  made for it by sim are not blocked.
- **`surprised_by_stealth(combatants, hiders)`** (pure): a living creature is surprised when it has a living enemy, every living
  enemy is listed in `hiders` (`StealthTotal{id, total}`, the Dexterity (Stealth) results), and every such total is **>=** its
  `passive_perception` (new in checks.hpp: 10 + Perception modifier + caller bonus, +5 / -5 for advantage / disadvantage).
  Ties go to the hider (the stealth total meets the passive "DC"). An enemy that is not hiding is noticed, so nobody on that side is
  surprised; the caller can always pass its own set instead.
- **Hashing.** `has_surprised(state)` / `surprised_digest(state)` (FNV-1a over the surprised ids): sim folds it under tag **0xA8**
  only when `has_surprised`. It is non-empty only during round 1 of an ambush.
- **Hit Dice.** `Combatant::hit_die` (die size, 0 = none), `hit_dice` (left), `hit_dice_max` (appended). `spend_hit_dice(c, rng, n)`
  spends up to `n` dice one at a time: each is 1d`hit_die` + Con modifier, minimum 0, healed through `apply_healing` (so from 0 HP it
  resets death saves and ends Unconscious) and capped at `effective_hp_max`. It stops once HP is full (no wasted dice). One rng draw
  per die spent; the caller passes the rng it uses between encounters.
- **Rests** (new `rest.hpp`, pure, outside the encounter; no events). `RestTables{features, pools, action_uses}` points at the
  caller's per-actor rows (same types as `EncounterState`); null pointers are skipped and only rows of the resting actor change.
  - `short_rest(c, rng, hit_dice_to_spend, tables)`: Hit Dice, Pact slots, ShortRest features and pools, recharge actions.
  - `long_rest(c, tables, food_and_drink = true)`: refused below 1 HP or when dead. Exhaustion -1 (with food and drink) first, then
    all HP to the adjusted maximum, temp HP lost, death saves reset, Hit Dice back up to half the maximum (minimum 1), all spell and
    Pact slots, innate uses, ShortRest + LongRest features and pools, recharge actions.
  - `recharge_tables(actor, long_rest, tables)` is the table part on its own; `recover_features(EncounterState&, bool)` stays.
  - `recover_spell_slots(c, add[10], max_total_levels, max_slot_level = 5)` with `recovery_budget(level) = ceil(level / 2)`: Arcane
    Recovery / Natural Recovery. All-or-nothing; refuses over budget, a slot above level 5, or more than `spell_slots_max`.
- **Hand-off (Engine Core / T&C).** (a) Loader: `character@N` `hit_die` per class (d6 wizard / sorcerer, d8 most, d10 fighter /
  paladin / ranger, d12 barbarian) and `hit_dice_max` = level; Arcane / Natural Recovery as a `once per long rest` FeatureUses row.
  (b) sim encounter setup: build the surprised set (from an ambush flag or `surprised_by_stealth` over the hiders' Stealth rolls),
  call `apply_surprise` before `begin_encounter`, fold `surprised_digest` under 0xA8 when `has_surprised`. (c) Campaign / party layer:
  keep combatants and their tables between encounters, call `short_rest` / `long_rest`, and seed a dedicated rng stream for Hit
  Dice (not the encounter stream). The Combatant hash in sim does not include `hit_die` / `hit_dice` / `hit_dice_max`; add them only
  in a hash of out-of-encounter state if one exists.
- **Not covered / deviations.** Choosing Hit Dice one at a time after seeing each roll is modelled by "stop when full" plus the
  caller's `n`; multiclass mixed Hit Dice sizes are not modelled (one die size); rest interruption, the 1-per-24h long-rest limit and
  rest durations are the caller's; exhaustion is reduced only by `long_rest`'s flag (no other source); surprise does not model a
  creature that is surprised by only part of the enemy side (any noticed enemy cancels surprise, the SRD's "doesn't notice a threat").

### 7.2i R-G14 per-cast options: Metamagic, Sculpt Spells, cast-time choices (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-per-cast-options`); no schema change, no new event kind. Goldens unchanged: every new field defaults to
off, a cast without options takes the old path, and new rng draws happen only for Heightened (second save d20) and Empowered
(rerolled dice), while Careful / Sculpt skip the spared creatures' save rolls.

- **Request fields** (`ActionRequest`, appended, CastSpell only): `metamagic` (bitmask `kMeta*`: Careful 1, Distant 2, Empowered 4,
  Extended 8, Heightened 16, Quickened 32, Subtle 64, Twinned 128), `spared` (creature ids), `heighten_target`, `damage_choice`,
  `condition_choice` (1 + index; 0 = none). All are **log-relevant**: sim's `log_hash` must fold them under tag **0xB1** (claimed in
  `docs/SIM_EVENTS.md`), only when any is set, so existing logs hash as before.
- **Caster facts** (`Combatant`, appended): `metamagic_known` (bitmask), `sorcery_pool` (pool id of sorcery points, a registered
  `ResourcePool`), `sculpt_spells`. **Spell facts** (`SpellDef`, appended): `evocation`, `damage_choices`, `condition_choices`.
- **Validation** (a refusal changes nothing): options must be known; one option per spell, except Empowered may be added to
  another; the pool must cover `metamagic_cost` (Careful / Distant / Empowered / Extended / Subtle 1, Quickened 2, Heightened 3,
  Twinned = slot level, 1 for a cantrip); only for spells (`is_spell`). Per option:
  - **Careful**: a Save spell; `spared` 1 to max(1, Cha mod) creatures; they succeed automatically (still half damage).
  - **Distant**: range >= 5 ft; touch becomes 30 ft, otherwise doubled (what `spell_target_ok` sees: sim's range check).
  - **Empowered**: rerolls up to max(1, Cha mod) damage dice that rolled below the die's average, lowest first (rules policy for
    the caster's choice); Save damage and spell attack damage.
  - **Extended**: a duration of 1 minute or more (`Minutes` / `Hours` or `duration_rounds` >= 10); doubles `duration_amount`,
    `duration_rounds` and modifier `rounds` (primary and extra effects).
  - **Heightened**: a Save spell; `heighten_target` must be a target; disadvantage on its first save against the spell.
  - **Quickened**: a 1-action spell becomes a bonus action (the "bonus-action spell, then only a cantrip" rule is not enforced).
  - **Subtle**: V and S are dropped from `components` (what `can_provide_components` sees).
  - **Twinned**: a single-target spell (no area, range > 0, one beam / one target at this slot level; spell attack, save, heal,
    apply condition / modifier); `targets` must be exactly two different creatures.
  - **Sculpt Spells** (no Careful, `spared` non-empty): `sculpt_spells` and an `evocation` Save spell, up to 1 + slot level
    creatures; they succeed automatically and take **no** damage from a half-on-save spell.
  - **Choices**: a spell with `damage_choices` / `condition_choices` requires the matching choice, which replaces the primary
    effect's `damage_type` / `condition` for the cast. `legal_actions` fills choice 1.
- **Events.** The sorcery-point spend is a `ResourceSpent{actor, a = sorcery_pool, b = cost, c = remaining}` right after
  `SpellCast` (after the spell's own pool spend, before the SpellCast window). A spared creature's save is a `SavingThrowRolled`
  with natural 0, total 0, success 1 (no roll).
- **Hand-off (Engine Core / T&C).** (a) Loader: `character@N` Sorcerer `metamagic` (known options) and `sorcery_points` as a
  `resources[]` pool (long rest) with `sorcery_pool` pointing at it; Evocation Wizard `sculpt_spells`; `spell@1` `school`
  (evocation -> `evocation`), `damage_choices[]`, `condition_choices[]`. (b) sim: pass the options from UI / AI into
  `ActionRequest`; fold them into `log_hash` under 0xB1 (fields in the order of the SIM_EVENTS registry). (c) AI: may ignore the
  options (casts without them behave exactly as before).
- **Not covered / deviations.** Font of Magic (slot <-> point conversion) and Eldritch Spear are not in this PR; Quickened's
  bonus-action-spell restriction is not enforced; Empowered's die choice is a fixed policy; Careful / Heightened / Sculpt act on the
  primary Save effect (and Careful / Sculpt also on later Save effects of the same cast); Twinned on spells with extra effects
  applies them to both targets.

### 7.2j Small leftovers: Haste, Slow, Life Drain, Reckless Attack, Aggressive, Petrifying Gaze (post-batch rules follow-up) and hand-off

Rules side only (`feat/rules-small-leftovers`); no schema change, no new event kind. Goldens unchanged: every new field defaults to
off, the new marker stats are only read when a modifier carries them, and new rng draws happen only for Slow's repeat save and Life
Drain's Con save (when `drain_save_dc` is set).

- **Haste.** Content: a concentration ApplyModifier spell whose primary modifier is the marker `ModStat::Haste`, with riders (all
  existing stats) `Ac` +2, `Save` advantage with `ability = Dex`, `Speed` `speed_num = 2`. While a Haste marker is on a creature,
  its turn starts with `TurnBudget::haste_action`; `ActionRequest::haste_action` takes Attack (one weapon attack: no multiattack,
  special or bonus option), Dash, Disengage or Hide with it, leaving the creature's own action, Action Surges and Attack-action
  sequence (Extra Attack) as they were. `legal_actions` lists the Haste options (`haste_action = true`). **Lethargy**: when the
  last Haste marker on a creature goes (any reason but "replaced"), `Combatant::lethargy` = 1; at its next turn start it becomes 2
  and that turn has no action, bonus action, Haste action or movement; it clears when that turn ends. A lethargic creature also
  cannot react (rules reading of "can't take actions").
- **Slow.** Content: a concentration Save spell (Wis; up to six targets via `targets`) with `affected_only` ApplyModifier riders:
  the marker `ModStat::Slow` (with `repeat_save = 1`, `repeat_ability = Wis`), `Ac` -2, `Save` -2 with `ability = Dex`, `Speed`
  1/2. Rules fills `ModifierSpec::repeat_dc` from the caster's spell save DC when a spell applies a repeat-save modifier. While
  slowed: no reactions (`may_react`); spending the action, bonus action or Haste action spends the others; one weapon attack per
  turn (`TurnBudget::slow_attacked`; Extra Attack and bonus attacks are cut). At the end of each of its turns the target repeats
  the save (`SavingThrowRolled`); a success removes every modifier of that source and caster on it (`ModifierExpired` reason
  **6**), then ends the caster's concentration once nothing of the spell remains.
- **Life Drain.** `Attack::drain_max_hp` (+ optional `drain_save_dc`, Con): on a hit the target's `hp_max_reduction` grows by the
  damage it took (HP + temp HP lost to the hit), HP over the new maximum is lost, and a maximum of 0 kills (`CombatantDied`).
  `effective_hp_max` subtracts it; `long_rest` clears it. Event: `ModifierApplied{actor = attacker, target, a = 0, b =
  ModStat::MaxHp, c = -amount}` (a reserved v7 kind; no new kind), after the save's `SavingThrowRolled`.
- **Reckless Attack.** `Combatant::reckless_attack`; `ActionRequest::reckless` on the turn's first Attack (Attack action, not
  bonus, no special): `reckless_active` is set before that roll; its melee weapon attacks using Str (not finesse with a better
  Dex) have advantage; attacks against it have advantage until its next turn starts. Not listed by `legal_actions` (a choice).
- **Aggressive.** `Combatant::aggressive`; `ActionRequest{Dash, as_bonus_action, aggressive}` spends the bonus action for a Dash
  (`TurnBudget::aggressive_dash` marks it); `legal_actions` lists it. Sim keeps that movement toward a hostile.
- **Petrifying Gaze progression.** `SpellDef::escalates`, `escalate_to` (default Petrified), `escalate_margin`: a primary Save with
  a condition (Restrained, `repeat_save` EndOfTurn) whose failed repeat save turns the condition into `escalate_to` for good (no
  more saves, no timer; `ConditionRemoved` + `ConditionAdded`); failing the first save by `escalate_margin` or more applies
  `escalate_to` at once (medusa 5; basilisk 0).
- **Hashing.** `Combat::hash` tag **0xAA** (claimed as 0xA9 in #111; moved by #112, since EC #104 uses 0xA9 for `special` in `log_hash`): `leftover_digest` (per combatant with `hp_max_reduction`, `lethargy` or
  `reckless_active` set, then escalating effects), only when `has_leftover_state`. `modifiers_digest` (0xA2) folds `repeat_save`,
  `repeat_ability`, `repeat_dc` only for a modifier with `repeat_save != 0`. `log_hash` tag **0xB3**: one byte, bit0
  `haste_action`, bit1 `reckless`, bit2 `aggressive`, only when non-zero (0xB2 is reserved for R-G9's teleport destination, per
  `FORCED_MOVEMENT.md`). The new `TurnBudget` fields are not hashed (sim does not hash the budget). Both tags are in the
  `docs/SIM_EVENTS.md` registry.
- **Hand-off (Engine Core / T&C).** (a) Loader: `apply_modifier` stats `haste` / `slow` (markers) and `repeat_save` /
  `repeat_ability` on modifier content; spell content for Haste and Slow as above; monster `attack.drain_max_hp` (+ `drain_save_dc`)
  for wight / specter / wraith / vampire spawn; `reckless_attack` (Barbarian 2, minotaur, berserker); `aggressive` (orc); gaze
  saves with `escalates` / `escalate_to` / `escalate_margin` (basilisk, medusa). (b) sim: carry `haste_action` / `reckless` /
  `aggressive` into `ActionRequest` and fold them into `log_hash` under 0xB3; fold `leftover_digest` under 0xAA; keep Aggressive
  movement toward a hostile; fire the gaze (a creature starting its turn within 30 ft that can see the gazer) as a Save request
  (sim owns positions and sight). (c) AI: may use `legal_actions`' Haste and Aggressive entries; ignoring them is valid.
- **Not covered / deviations.** Haste's "Use an Object" (no such action in rules); Slow's spell-casting delay (a somatic spell
  fails on d20 < 11 and takes effect next turn: needs a delayed-cast model); ability drain (shadow's Strength Drain); the gaze
  trigger and averting the eyes (positions / sight, sim); lethargy also blocks reactions (reading).

### 7.2k R-G9 forced movement and self-teleport (rules side) and hand-off

Spec: `docs/proposals/FORCED_MOVEMENT.md` as agreed in #110 (Director rulings Q4 / Q9-Q11, Engine Core's §3; merged while this
PR was open and merged in, unchanged from its reviewed head 1453b5c). Scope per Q10: pushes and self-teleport. Branch `feat/rules-forced-movement`.

- **Effect kinds** (`SpellEffectKind`, appended): `ForcedMove` (`ForcedMoveSpec forced`: `mode` 1 push / 2 pull / 3 line,
  `feet`, `up_to`, per-effect `max_size` (-1 = none; a bigger creature skips the push only), `provokes_oa`) and `Teleport`
  (`TeleportSpec teleport`: `max_feet`, `needs_sight`, the latter a sim check). On `SpellDef` (primary) and `SpellEffect`
  (riders; `effect_view` copies them). Thunderwave = Save + `ForcedMove` rider `affected_only` (failed saves only); Repelling
  Blast = spell attack + `ForcedMove` rider `up_to` 10 (one push per beam hit); Misty Step = `Teleport`, bonus action, max 30.
- **Requests** (`ActionRequest`, appended, CastSpell only; otherwise refused): `forced_feet` (an "up to" distance: multiple of 5,
  at most the effect's; 0 = the maximum), `teleport_feet` (distance to the destination when sim knows positions: multiple of 5,
  at most `max_feet`; 0 = not supplied, no check), and the destination `has_teleport_dest` / `teleport_x` / `teleport_y` (a sim
  tile rules never reads; passed to the hook). Teleport is self only (Dimension Door companions deferred). `legal_actions`
  lists a Teleport spell once per slot level, at the caster, with no destination (sim fills it from `legal_teleport_tiles`).
- **Hook** (`EncounterHooks`, appended): `forced_move(state, ForcedMoveQuery) -> ForcedMoveResult`, synchronous, resolving and
  committing at once (§3, Q3). Query: mover, source, mode (0 teleport), feet, teleport, step_seq (index among the step's forced
  moves), destination. Result: `feet_moved`, `stopped_short`, `hazard` / `fell_ft` (reserved for R-G11), `out_of_reach_of`.
  **No hook = intent only**: `Moved.a` = the requested feet, no grapple ends. `forced_move_order(state, queries)` returns a
  permutation (sim: farthest first, Q2); anything else, or no hook, keeps the target order.
- **Resolution (deferred, EC's double-call fix).** Effects queue their moves; a cast turns its queue into one group of
  placeholder `Moved` events after all of the spell's saves and damage and before `SpellResolved` (moves queued outside a cast,
  such as on-hit riders and the shove, form a group at the end of the step, before Ready triggers). The hook is **not** called
  there: `forced_move_order` and `forced_move` run only in `finalize_forced`, once, after every decision point of the step
  (reaction / Ready windows, Ask decisions, `step()`'s internal decide loop) and only in the run that is kept, i.e. never in a
  run that ends with a pending decision and is discarded or re-run. Each group's placeholders are then replaced in place by
  `Moved{actor = mover, target = source, a = feet moved, b = 0, c = 1 forced / 2 teleport}` in the hook's order, each followed
  by `ConditionRemoved{Grappled}` for each grapple (either direction) with a creature in `out_of_reach_of` (sourced entries
  removed); the event positions relative to saves, damage and `SpellResolved` are unchanged. `step_seq` is the move's index
  among the step's forced moves; a debug-build `assert` checks each (step, step_seq) reaches the hook only once (and that
  finalize runs once per kept step). Forced moves and teleports open no `LeavingReach` window, spend no movement, ignore
  speed 0 (Grappled / Restrained), and fire Ready triggers like any `Moved`. **Known limitation:** a Ready trigger fires on the
  placeholder, i.e. the intent (`a` = requested feet), because it runs before the hook; a trigger that needs the distance
  actually moved would need a second pass (none in SRD scope).
- **Shove** (`special` 3) now goes through the same deferred path (hook, grapple ends). **EC #104 has landed** and sim flags
  the shove push itself (`Combat::complete` sets `c |= kMovedForced` on its copy), so rules' own shove event stays `c = 0`
  unless `EncounterHooks::shove_forced_flag` is true (default false): nothing is flagged twice and #104's locked digests don't
  move. EC will later set `shove_forced_flag = true` and drop its own flagging. With no `forced_move` hook (all of sim today) a
  shove is `Moved{a = 5, c = 0}` from rules as before.
- **Schema v8** comes from EC #104 (merged into this branch with a merge commit): `kSimEventSchemaVersion` 8 and the v8 rule
  (`event_stream_version` returns 8 only for a stream with a `Moved` whose `c != 0`) are #104's; this PR's duplicates and its
  duplicate v8 test were dropped in the merge. The flag bits (`kMovedForced` 1, `kMovedTeleport` 2, `kMovedDrag` 4) are
  defined once, in `rules5e/encounter.hpp`, and sim's `kMoved*` in `events.hpp` alias them (deviation: rules cannot include
  sim headers, since sim depends on rules, so the single definition has to live on the rules side; values are #104's).
  SIM_EVENTS.md has one `Moved` row and one v8 changelog entry. Every golden and locked digest, #104's included, is unchanged.
- **Hashing.** No `Combat::hash` tag (no lasting state; positions are sim's). Registry fix: EC #104 folds `special` under
  0xA9 in `log_hash`, which collided with #111's 0xA9 `Combat::hash` claim for `leftover_digest` (not folded by sim yet), so
  `leftover_digest` moves to **0xAA**, the 0xA9 `log_hash` row is added (main), 0xA7 is marked main, and the next free
  `Combat::hash` tag is 0xAB. `log_hash` **0xB4**: `forced_feet` u16 then
  `teleport_feet` u16, only when either is non-zero (claimed in the registry); the destination is **0xB2**, claimed by the R-G9
  sim PR (not taken here).
- **Hand-off (Engine Core).** (a) Implement `forced_move` (§3 path rules: 8-way direction in doubled coordinates, tile-by-tile
  walk, any creature blocks, stop at the last free tile, commit immediately, report `out_of_reach_of`) and
  `forced_move_order` (farthest first, lower id); apply `Moved` with `c != 0` to tokens (`TileMoved`), not as the actor's own
  move. (b) Teleport: `legal_teleport_tiles`, re-validate at `Combat::submit` (range, free footprint, sight, Frightened "not
  closer"), then fill `teleport_feet` and the destination. (c) Later: set `shove_forced_flag = true` and drop sim's own shove flagging in `Combat::complete` in the same change (same events, so no digest should move). (d) Fold
  0xB4 and 0xB2 into `log_hash`. **(T&C)** loader: `forced_move` / `teleport` effect kinds and fields for spell@1 effects,
  `provokes_oa: true` is a load error in R-G9, Thunderwave / Misty Step / Repelling Blast / blink dog content.
- **Not covered / deviations.** Out of scope per Q10: Dimension Door companions and the 4d6 failure, Gust of Wind (mode 3 is data
  only), falling / hazard damage (the hook keeps `hazard` / `fell_ft`). Open Hand Technique's push needs a bonus-attack rider
  with a save (not in this PR). The hook signature adds `forced_move_order` and destination fields to §2.4's query (rules needs
  a way to take sim's order and to pass the destination). `ForcedMoveSpec::mode` defaults to 1 (push). The R-G9 load error is
  the loader's (no rules check). Rules does not re-check Frightened "not closer" (sim's re-validation, §3).

### 7.2l R-G11 zones and auras, rules PR 1 (rules side) and hand-off

Spec: `docs/proposals/ZONES_AURAS.md` on main (#113, merged as a9f7c98): Director rulings D1-D9 and Engine Core's §3 / Q1-Q12.
Scope: §2.10 item 1. Branch `feat/rules-zones` from main after #112 (b106b45) and #113. The registry claims (`Combat::hash`
0xAB, `log_hash` 0xB7) are their own first commit.

- **Data** (`spells.hpp`, appended). `SpellEffectKind::Zone` (16), primary effect only, with `SpellDef::zone` (`ZoneSpec`):
  - shape and sizes, `anchor` (`Point` or `Creature`: an aura on the caster), `kZone*` flags (terrain / sense data for sim),
    `move_cost` (0 none, 1 action, 2 bonus action), `move_ft`, `chooses_excluded`, `needs_conscious_owner` (D7, unused);
  - `triggers`, a list of `ZoneTriggerRow`s: `on` (`Appear`, `Enter`, `StartTurn`, `EndTurn`, `MoveWithin`), `group`
    (once-per-turn group, 0 = no limit; Enter + StartTurn share group 1 by default, D2), `own_turn_only` (Web),
    `completely_within`, `counts_aura_move_as_enter` (D1), `while_in_zone` (the condition lasts while in the zone: Web), and
    `effect` (a `SpellEffect`). Kind `Save` is a save against the zone's DC with damage (half on a success with
    `OnSave::Half`) and a condition on a failure. Any other kind applies the damage and condition with no save (Spike Growth).
  - Damage dice scale by `SpellDef::upcast_dice` per slot level above the spell's level.
  - The spell's timer is the usual `expiry` / `duration_rounds` (Grease).
- **State** (`encounter.hpp`, appended). `EncounterState::zones` (`ActiveZone`: id, spell, owner, slot level, DC,
  concentration, timer, `cast_stamp`, the opaque anchor tile (Q1), sorted `excluded`, and `fired` bookkeeping
  `{creature, turn stamp, group}`, pruned at each turn start) and `next_zone_id` (from 1).
  `ActiveSpellEffect::zone_id` marks a "while in the zone" effect.
- **Requests** (`ActionRequest`, appended).
  - Membership facts, filled by sim at submit (Q2):
    - Move: `zones_entered` / `zones_left` / `zones_within`, read for a readied Move's response too.
    - `zone_changes` (`{creature, zone_id, entered}`): other creatures' changes from the step (Q8).
  - CastSpell inputs (0xB7):
    - `has_zone_dest` / `zone_x` / `zone_y`: a point zone's anchor. An aura refuses one.
    - `zone_excluded`: only for `chooses_excluded` spells.
    - `zone_move`: the zone id, with `spell_id` = its spell.
  - `zone_move_ft`: the distance, sim-derived, at most `move_ft`; 0 means no check.
  - Zone fields on any other kind are refused.
- **Hook.** `zone_contains(state, zone_id, creature, completely)` answers Appear, StartTurn and EndTurn, and any
  `completely_within` row. No hook means nobody is in a zone.
- **Triggers and order.**
  - **Appear** runs at creation for each combatant in initiative order, after `ZoneCreated` and before `SpellResolved`.
  - **Enter / MoveWithin** run after the step's `Moved`, per zone ascending, in this order:
    1. Enter.
    2. MoveWithin, once per occurrence of the zone in `zones_entered` + `zones_within`.
    3. Leaving ends the creature's "while in the zone" effects. Exits never deal damage.
  - **`zone_changes`** sort by zone, then initiative, then id (Q9). An entry counts as Enter only for rows with
    `counts_aura_move_as_enter`.
  - **StartTurn** runs right after `TurnStarted` and Heroism's temp HP, before `RepeatSave::StartOfTurn` and the row 9
    `TurnStart` triggers. It runs with no Ctx, so no reaction windows open, like delayed damage. A creature dropped to 0 HP
    there makes no death save that turn.
  - **EndTurn** runs first in `EndTurn`, before `RepeatSave::EndOfTurn`.
  - **Who is affected:** dead creatures, `excluded` creatures and the aura's own owner trigger nothing. `own_turn_only` needs
    the creature to be the current actor. A creature already held by the zone's while-in effect is not saved again (Web).
  - **Events:** each trigger emits
    `FeatureTriggered{owner, subject, a = spell id, b = TriggerOn::Zone* (8..12), c = 0 saved / 1 no save / 3 failed}`, then
    the save, damage (`damage_creature`, magical, source = owner) and condition events.
- **Endings.**
  - `end_concentration` ends the caster's zones of that spell, emitting `ZoneEnded{reason 1}` after `ConcentrationEnded` and
    before the spell's `SpellEffectEnded`.
  - Zone timers tick at round start (`ZoneEnded{0}`). The caster's concentration ends once nothing of the spell is left.
  - A live zone keeps its spell's concentration "remaining" for repeat saves, expiries and Dispel Magic.
- **Zone move (Moonbeam).** CastSpell with `zone_move` checks:
  - the owner, the spell, `move_cost != 0`, a point zone, and not the casting turn (`cast_stamp`);
  - `zone_move_ft` (≤ `move_ft`, multiple of 5);
  - no metamagic, spared, innate or excluded fields.

  It spends the action or bonus action (D3: data), moves the anchor when a destination is given, and emits
  `ZoneMoved{owner, a = zone id, b = feet}`, then `zone_changes`. No slot is spent and no SpellCast window opens.
  `legal_actions` lists zone spells at the caster without a destination, and zone moves without a destination; sim adds
  them from `legal_zone_tiles`.
- **Web's escape.** `special` 4 when the creature is restrained by a zone effect and not grappled, or with `spell_id` = the
  zone's spell. It costs an action: a plain Strength check (`CheckRolled.target = 0x100 | Ability`, all of `roll_check`'s
  Help / modifier / Lucky rules) against the zone's DC. Success ends the restraint (`SpellEffectEnded` + `ConditionRemoved`).
- **Forced moves (Q7).** `ForcedMoveResult` gains `zones_entered` / `zones_left` / `zones_within`.
  - For a multi-tile move, `zones_within` repeats a zone once per further 5 ft inside it. This gives D6's per-5-ft Spike
    Growth damage.
  - `finalize_forced` resolves each move's zone triggers right after its `Moved` / `ConditionRemoved`, before the next move.
  - `attempt` now runs `finalize_forced` before the pause check, so a trigger there may open an Ask window. The run then
    pauses with `PendingDecision::forced_results` (step_seq → result).
  - The re-run reuses those results and does not call the hook, so `forced_move` still runs once per (step, step_seq), and
    #112's assert stays. Deaths, concentration and sourced conditions are swept again only when a zone trigger fired there.
- **Schema v9** (sim `events.hpp` / `events.cpp`; not `content.cpp` or `Combat::create`):
  - `kSimEventSchemaVersion` is 9, `kLastRulesEventKind` is `ZoneEnded`, and there is a new `kLastRulesEventKindV8`.
  - `event_stream_version` returns 9 only for a stream with a kind past `ActionUsed` or a `FeatureTriggered` with
    `b >= ZoneAppear`.
  - The events_tests layout table and the version cases are updated.
  - No content has a zone, so every golden and locked digest is unchanged.
- **Hashing.** `zones_digest` hashes `next_zone_id`, every `ActiveZone` field in order, then the zone-bound effects. It is
  folded under `Combat::hash` **0xAB** only when `has_zone_state` holds (a zone exists or has existed). `log_hash` **0xB7**
  (`zone_move`, `has_zone_dest`, `zone_x`, `zone_y`, `zone_excluded`) is defined in the registry. Membership lists,
  `zone_changes` and `zone_move_ft` are not folded (sim-derived, Q11). Both folds are pending on the sim side.
- **Hand-off (Engine Core)**, per §3:
  - (a) Geometry and `zone_contains`.
  - (b) Fill the Move lists, `zone_changes` (aura owner moves, Moonbeam moves) and `zone_move_ft` inside `Combat::submit`,
    overwriting caller values. Fill the readied Move's lists in `refresh_ready`.
  - (c) The `forced_move` / teleport result lists, with `zones_within` repeated per 5 ft.
  - (d) `legal_zone_tiles` and re-validating the destination.
  - (e) The `kZoneDifficult` overlay and the `kZoneHalveSpeed` cost for non-excluded creatures (Q5).
  - (f) Fold 0xAB (`has_zone_state`) and 0xB7.
  - (g) v9 consumers render from `ZoneCreated` / `ZoneMoved` / `ZoneEnded` (Q12).
- **Hand-off (T&C).** spell@1 `zone` keys (shape and sizes, anchor, flags, `move_cost` / `move_ft`, `chooses_excluded`,
  and trigger rows with their effect). Content for Spirit Guardians, Moonbeam, Spike Growth, Grease and Web.
- **Not covered / deviations.**
  - Zones are primary-effect only: an extra effect of kind `Zone` is refused. This avoids a recursive SpellEffect / ZoneSpec
    type (the doc allowed riders).
  - Web's restraint is an `ActiveSpellEffect` with `zone_id`, not a sourced condition with an `escape_ability` (§2.4).
    Concentration, leaving and escape all end it through the effect.
  - `fired` is a `ZoneFired` struct instead of the doc's packed pair, and it is pruned at each turn start rather than each
    round start.
  - The zone move is a CastSpell with `zone_move` (sustain precedent), with an optional destination (teleport precedent).
  - Added `zone_changes` and `while_in_zone`, which the doc's data model implied but did not name.
  - StartTurn triggers open no reaction windows.
  - Ready triggers don't see zone events from `finalize_forced` (as with #112's placeholders).
  - Moonbeam's shapechanger disadvantage and revert are not modelled.
  - Spirit Guardians' evil-caster necrotic is content.
  - `needs_conscious_owner` and the passive aura fields are deferred (D7).

### 7.2m R-G11 zones and auras, rules PR 2 (rules side) and hand-off

Scope per `docs/proposals/ZONES_AURAS.md` §2.10 item 2 (Silence, Stinking Cloud, Wall of Fire, Fog Cloud / Darkness flags) plus
what PR 1 (#118, 7.2l) deferred: Moonbeam's shapechanger rule, Spirit Guardians' evil-caster damage, and the D7 creature-anchored
aura fields (the Paladin aura content stays deferred by D7). Tests: `engine/rules/tests/zones2_tests.cpp`. No new event kind (v9
unchanged), no golden moved.

**Rules API (all appended, defaults keep PR 1 behaviour):**
- `ZoneTriggerRow`: `region` (1 = the zone or its damaging side, for hook-answered rows), `loses_action`, `auto_success_damage`,
  `auto_success_conditions`, `auto_success_no_breath`, `shapechanger_disadvantage`, `has_evil_damage_type` / `evil_damage_type`.
- `ZoneSpec`: `upcast_size_ft` (Fog Cloud), `ring_size_ft`, `sided`, `side_ft` (Wall of Fire), and the D7 fields
  `aura_modifier` (stat Save only for now), `has_aura_bonus_ability` / `aura_bonus_ability`, `aura_immunity`,
  `aura_allies_only`; `needs_conscious_owner` (from PR 1) now suppresses the zone's rows and passive effects while its owner is
  dead, at 0 HP or unconscious.
- `ActiveZone`: `size_ft` (after upcast; a ring's `ring_size_ft`), `has_end_tile` / `end_x` / `end_y`, `side`, `ring`, `feature`.
- `ActionRequest` (CastSpell of a zone, log_hash **0xB9**): `has_zone_end`, `zone_x2`, `zone_y2`, `zone_side` (required +1 / -1
  for a sided zone, else 0), `zone_ring`. Refused on other spells, on `zone_move`, and on non-CastSpell requests.
- `TurnBudget::action_lost`; `Combatant::no_breath`, `shapechanger`, `evil` (content facts, not hashed).
- Hook `zone_side_contains(state, zone_id, creature)`; functions `in_silence(state, id)` and
  `add_feature_aura(state, owner, spell_id, events*)`.
- `FeatureTriggered.c = 2` for a zone row's automatic success.

**Behaviour:**
- Silence (`kZoneSilence`): completely inside (`zone_contains(..., true)`) = no spell with a V component (cast refused before
  `can_provide_components`, nothing rolled; also filtered from `legal_actions` and reaction-spell options; Subtle Spell lifts it),
  thunder damage immunity at damage time, and Deafened as a zone-bound effect applied at creation, on Enter / MoveWithin, on an
  aura or zone move onto the creature, and at its turn start; it ends on leaving or with the zone.
- Stinking Cloud: a StartTurn row with `completely_within` and `loses_action`. The StartTurn rows still run before the turn's
  budget is built (as in PR 1); the lost action is carried over the reset, so the creature keeps its movement, bonus action and
  Haste action (D9). A row with `loses_action` outside the creature's own turn does nothing.
- Wall of Fire: shape Line (start tile = `zone_x` / `zone_y`, end tile = `zone_x2` / `zone_y2`) or a ring; Appear Dex half;
  EndTurn with `region = 1` (in the wall or on the chosen side, no save, D4); Enter (wall only, no save). `legal_actions` lists
  each form: line / ring x side +1 / -1.
- Fog Cloud / Darkness: flags only until R-G13 (Q6); Fog Cloud's radius grows with `upcast_size_ft`.
- Moonbeam: shapechangers save with disadvantage. **Reverting the form on a failure is not modelled** (no shapechange state
  before R-G12).
- Spirit Guardians: an evil owner (`Combatant::evil`) deals `evil_damage_type` (necrotic).
- D7: `aura_modifier` adds a flat save bonus (`bonus`, or max(1, owner's `aura_bonus_ability` modifier)) to the owner and
  creatures in the aura (owner's team only with `aura_allies_only`); the same spell's auras don't stack. `aura_immunity` blocks
  those conditions from being applied (spell saves, ApplyCondition, HP pools, zone rows, shove prone, sourced conditions); a
  condition already present is not removed. A feature aura (`add_feature_aura`) of a dead owner ends at the next round start
  (`ZoneEnded{2}`).

**Engine Core hand-off (sim), in addition to 7.2l:**
1. Implement `zone_side_contains` (§3 Q4: within `side_ft` / 2 tiles of a wall tile on the chosen side, wall tiles excluded;
   ring inside / outside) and the Line / ring geometry in `zone_contains`, from `ActiveZone::anchor_*`, `end_*`, `ring`,
   `size_ft` (use `ActiveZone::size_ft`, not `ZoneSpec::size_ft`, so upcast Fog Cloud is right).
2. Fill `has_zone_end` / `zone_x2` / `zone_y2` from the UI / AI choice for line walls (and validate range / LOS); pass
   `zone_side` / `zone_ring` through from `legal_actions`.
3. Fold `has_zone_end` byte, `zone_x2` i32, `zone_y2` i32, `zone_side` byte, `zone_ring` byte under `log_hash` 0xB9 when any is set.
   `zones_digest` (0xAB) already covers the new `ActiveZone` fields and `action_lost`; no new `Combat::hash` tag.
4. Call `add_feature_aura` for creatures whose feature carries an aura, once content exists (D7 deferred), and forward its
   `ZoneCreated`.

**T&C hand-off (content):** `spell@1` `zone` keys for the new fields (`upcast_size_ft`, `ring_size_ft`, `sided`, `side_ft`; row
`region`, `loses_action`, `auto_success_*`, `shapechanger_disadvantage`, `evil_damage_type`; `line` shape with `length_ft`) and
`Combatant` keys `no_breath`, `shapechanger`, `evil` on monster / character archetypes. Silence needs only `flags: ["silence"]`
(as in #119's fixture): rules derives the V block, thunder immunity and deafness from the flag. `needs_conscious_owner` keeps
that name (T&C #119).

**Loader (folded into this PR, own commit):** `rules5e_content` `spell_loader.cpp` parses kind `zone` per
`docs/CONTENT_CONVENTIONS.md` section 4 (final): every section 4 key, `needs_conscious_owner` onto the D7 field, the section 4.4
cross-field rules with the validator's messages, and an expiry timer from the duration. It parses only the keys the schema has
today; the PR 2 fields above (`upcast_size_ft`, `ring_size_ft`, `sided`, `side_ft`, `line`, the new row keys) are refused as
`zone: unknown key ...` until T&C adds them to `schemas/spell/v1.schema.json` / `check_spell_zone`, and the loader gains them in
the same PR.

### 7.2n Slot recovery feature kind: Arcane Recovery, Natural Recovery (rules side) and hand-off

Rules (`engine/rules/include/rules5e/rest.hpp`, on top of the R-G16 `recover_spell_slots` / `recovery_budget` of 7.2h):

- `SlotRecoverySpec { feature_id, class_level, max_slot_level = 5, uses = 1 }`; `slot_recovery_budget(spec)` = ceil(class_level / 2).
- `slot_recovery_uses(actor, spec)` -> the `FeatureUses` row to register (max = `uses`, recharge `LongRest`): `long_rest` /
  `recharge_tables` reset it, `short_rest` does not ("once per day"; Natural Recovery's "finish a long rest" reset).
- `apply_slot_recovery(Combatant&, spec, std::vector<FeatureUses>&, std::array<uint8_t,10> add)` -> `SlotRecoveryResult`:
  `Ok` (slots added, one use spent), or, changing nothing, `NoFeature` (no row for `(c.id, feature_id)` / class_level 0),
  `NoUsesLeft`, `Empty` (nothing chosen), `LevelTooHigh` (a slot above `max_slot_level`), `OverBudget`, `AboveMax` (more than
  `spell_slots_max`: only expended slots return). Checked in that order (per-level checks in ascending slot level).
- No events, no rng, no hash tags: rests stay outside `Combat::hash` (7.2h). Goldens unchanged.

Hand-off (Engine Core, `sim/rest.hpp` / `sim/content.cpp`, not edited by rules):

1. Content -> spec: a feature kind `slot_recovery` on character@N features, loaded by the sim feature loader (rules' content
   library only parses spells). Proposed keys (T&C schema): `kind: "slot_recovery"`, `class` (`wizard` | `druid`; the class
   level comes from `classes[]`), `max_slot_level` (integer 1..9, default 5), `uses` (integer 1..255, default 1). The loader
   sets `SlotRecoverySpec` and registers `slot_recovery_uses(id, spec)` in the roster's `features` table.
2. Short rest: after `eng::sim::short_rest(roster, id, ...)` succeeds, call
   `rules5e::apply_slot_recovery(combatant, spec, roster.features, chosen)` with the caller's choice (sim AI / UI input at
   short-rest time); a non-`Ok` result is a refused choice, report it to the caller.
3. Long rest: nothing extra; `long_rest` resets the row through the roster's `RestTables`.

### 7.2o Font of Magic: sorcery points and Flexible Casting (rules side) and hand-off

(Numbered 7.2o because 7.2n is taken by the slot recovery PR, #146.)

Builds on R-G14 (7.2i): sorcery points are the existing `ResourcePool` named by `Combatant::sorcery_pool` (the Metamagic
pool, folded under `Combat::hash` 0xA3 when pools exist). No new Combatant or EncounterState field.

Rules (`engine/rules/include/rules5e/font_of_magic.hpp`):

- `sorcery_points_max(sorcerer_level)` = level from 2nd (0 below); `flexible_casting_cost(level)` = 2 / 3 / 5 / 6 / 7 for 1st..5th
  (0 = not creatable).
- `check_flexible_casting(const Combatant&, const std::vector<ResourcePool>&, flexible, level)` / `flexible_casting(...)`:
  the pure out-of-encounter helper (and the encounter action's core). `flexible` 1 = expend a spell slot of `level` (1..9) for
  `level` points; 2 = spend `flexible_casting_cost(level)` points for a slot of `level` (1..5). Results: `Ok`, `NoPool`,
  `BadDirection`, `BadLevel`, `NoSlot`, `OverMax`, `NotEnoughPoints`; anything but `Ok` changes nothing.
- In an encounter: `ActionKind::FlexibleCasting` (appended) with `ActionRequest::flexible` / `flexible_level` (appended), a
  bonus action (needs `TurnBudget::bonus_action`; not while incapacitated; no SpellCast window). Emits one `ResourceSpent`
  {actor, a = pool id, b = points spent (negative = gained), c = points after}; the slot level follows from `b` (the costs are
  distinct). Errors: `no bonus action available`, `incapacitated`, `no sorcery points`, `bad flexible_level`,
  `no spell slot of that level`, `sorcery points would exceed the maximum`, `not enough sorcery points`,
  `flexible must be 1 (slot to points) or 2 (points to slot)`.
- `legal_actions` lists every valid conversion (as_bonus_action) while the bonus action is unused.
- Long rest: `long_rest` restores `spell_slots` to `spell_slots_max`, so created slots (which may exceed the maximum) vanish;
  the pool refills when registered with recharge `LongRest`.

Deviations / readings: going above the point maximum is refused (`OverMax`), not capped (SRD: "can't have more sorcery points
than your maximum"; refusing keeps the slot). Pact Magic slots are not converted (SRD Flexible Casting names spell slots; the
Warlock is not a sorcerer). No new event kind (event_stream_version unchanged); `ResourceSpent.b` may now be negative.

Hand-off:

- **Engine Core (sim)**: `Combat::log_hash` folds 0xBA + `flexible` + `flexible_level` when `flexible != 0` (claimed in
  SIM_EVENTS.md); no `Combat::hash` section (pool 0xA3 + events). The feature loader (`sim/content.cpp`) registers the sorcery
  pool with `max = sorcery_points_max(sorcerer level)`, recharge `LongRest`, and sets `sorcery_pool` for a `font_of_magic`
  feature even without Metamagic (Sorcerer 2). AI: optional (no conversion by default). Out of encounters: call
  `rules5e::flexible_casting(combatant, roster.pools, flexible, level)` on the kept roster.
- **Tools & Content (schema)**: character@N feature kind `font_of_magic { pool }` (the pool id, the same `resources[]` pool as
  `metamagic.pool`); the pool's `max` should equal the sorcerer level (validator warning otherwise).
- Goldens unchanged: nothing folds unless a combat registers pools and logs a FlexibleCasting request.

### 7.2p Petrifying Gaze feature kind (basilisk, medusa) (rules side) and hand-off

(Numbered after 7.2o, Font of Magic, #147; 7.2n is slot recovery, #146.) Agreed ruling with Engine Core; reuses the small
leftovers escalation (7.2j: `SpellDef::escalates` / `escalate_to` / `escalate_margin`) and the repeat-save machinery.

Rules:

- `Combatant::gazes` (`std::vector<Combatant::Gaze{spell_id, range_ft}>`): each gaze names a synthetic Save `SpellDef`
  (`is_spell` false, `fixed_dc`, `save_ability`, `applies_condition` Restrained, `repeat_save` EndOfTurn, `escalates`,
  `escalate_to` Petrified, `escalate_margin` 5 for the medusa, 0 for the basilisk). `range_ft` is for sim only.
- Turn start: when a living gazer other than the creature is on the board and the creature is alive above 0 HP,
  `TurnBudget::gaze_pending` is set (last step of `start_turn`). Every turn request except `GazeResponse` is then refused
  (`gaze pending: answer with GazeResponse first`); `legal_actions` offers only `GazeResponse` (avert false, and avert true
  unless surprised). Answering clears the flag, so a re-run / second answer is refused (`no gaze pending`): no double trigger.
- `ActionKind::GazeResponse` (appended) with `ActionRequest::avert_gaze` (caller input) and `gaze_seen` (sim fact):
  - `avert_gaze`: refused while `surprised` (`surprised: cannot avert the eyes`); sets `Combatant::gaze_averted`, no save.
    Until the start of its next turn it treats every gazer as unseen: its attacks against a creature with gazes have
    disadvantage, and a gazer's attacks against it have advantage (SRD unseen attacker / target; see deviations).
  - otherwise, per gazer in initiative order that is alive, not incapacitated and listed in `gaze_seen`, each of its gazes
    resolves on the creature (no reaction windows, like turn-start triggers), unless the creature already carries that gaze's
    effect (restrained, awaiting the repeat save) or its `escalate_to` condition. A surprised creature still saves.
  - Medusa: failing by 5 or more = Petrified at once, no repeat save; otherwise restrained. Basilisk: restrained, the repeat
    save at the end of its turn ends it on a success and petrifies on a failure.
- `has_gaze_state` / `gaze_digest` (Combat::hash 0xAE): ids of creatures with eyes averted, only while any.

Hand-off:

- **Engine Core (sim)**: loader (`sim/content.cpp`) compiles a `gaze` feature into a synthetic Save SpellDef (from the wrapped
  save_effect fields) and a `Combatant::Gaze`; at each turn start with `gaze_pending`, submit `GazeResponse` with
  `gaze_seen` = gazers the actor can see (line of sight, within `range_ft`, not Blinded, the gazer not invisible to it;
  until R-G13) and the actor's averting choice (AI default: do not avert; UI asks). Fold `log_hash` 0xBB + 1 when
  `avert_gaze`; `Combat::hash` 0xAE + `gaze_digest` when `has_gaze_state`. The headless driver must answer pending gazes
  (a surprised creature's turn: `GazeResponse`, then `EndTurn`). `gaze_seen` is not folded (derived, like zone membership).
- **Tools & Content (schema)**: monster@4 feature kind `gaze { range_ft, save: {ability, dc}, condition, repeat_save,
  escalates, escalate_to, escalate_margin }` (the save_effect escalation fields wrapped; `range_ft` integer 5..65530,
  multiple of 5). SRD: basilisk DC 12 Con, 30 ft; medusa DC 14 Con, 30 ft, margin 5.
- Goldens unchanged: no gazer = no pending flag, no fold.

Deviations / readings: the SRD conditions "the gazer can see the creature" and "the creature can see the gazer" are both
folded into the one sim fact `gaze_seen`. Averting applies to all gazers at once. The gazer's advantage against an averted
creature follows SRD unseen-attacker rules (the ruling named only the disadvantage). Gaze saves hit every other creature
(allies too), as the SRD text has no team limit.

### 7.2q R-G12 rules PR 1: transforms (Wild Shape, Polymorph, shapechanger forms) (rules side) and hand-off

(Numbered after 7.2p, Petrifying Gaze, #149.) Spec: `docs/proposals/SUMMONS_TRANSFORMS.md` (D1-D15), PR split §2.10 item 1.
Transforms only: no roster growth, no summons (rules PR 2). True Polymorph is not in PR 1 (§2.1: outside our levels).

Rules:

- **Data.** `FormKind` (None 0, Polymorph 1, WildShape 2, Shapechanger 3) and `FormStats` (abilities, hp / hp_max, AC,
  proficiency bonus, speed, save / skill proficiencies, resistances / immunities / vulnerabilities / condition immunities and
  the qualified masks, attacks, multiattacks, attacks_per_action, size, creature type, roll traits, triggers, reactions, bonus
  attacks, and `blocked_features`) in `types.hpp`. `Combatant` (appended): `form`, `form_source`, `form_caster` (Polymorph),
  `form_archetype`, `form_end_round`, `base` (the saved own `FormStats`), `form_blocked_features`. `EncounterState::transforms`
  counts transforms begun. Pure helpers `apply_form(Combatant&, FormKind, const FormStats&)` / `revert_form(Combatant&)`.
- **Replaced vs kept** (§2.2 table). Polymorph: every `FormStats` field (mental scores and proficiencies too), a fresh HP pool
  (`hp = hp_max = form hp_max`), features from the form only. Wild Shape: physical stats, AC, speed, attacks, size, type and
  defences from the beast; Int / Wis / Cha kept; save and skill proficiencies = own | beast's; proficiency bonus the higher;
  its own roll traits / triggers / reactions / bonus attacks kept (D7: "if the new form is physically capable"), the beast's
  appended after them; `blocked_features` (content `requires_humanoid_form`) refused by `UseFeature` while in the form
  (`feature not usable in this form`). Shapechanger: every `FormStats` field as sim builds it ("statistics other than size are
  the same", content lists the differences), **one HP pool** (hp / hp_max unchanged). All kinds keep conditions, exhaustion,
  concentration, temp HP (D8: they stay on the creature and are not part of `base`), spell slots, feature uses and pools.
- **Gear.** Polymorph / Wild Shape: equipment melds; the form's `attacks` and `ac` are the beast's natural ones, so no gear
  attack or armour applies (sim's `build_form` must not add any). Shapechanger: equipment is not transformed (sim keeps the
  creature's weapons in the form's `attacks` when the form can use them). Rules has no equipment model; this is a `build_form`
  contract.
- **Spellcasting.** In a Polymorph or Wild Shape form `CastSpell` (incl. innate) is refused (`cannot cast spells in this
  form`) and `legal_actions` lists no spell; concentration on a spell cast before continues (D7). A shapechanger can cast.
- **Wild Shape / shapechanger**: `UseFeature` of a feature whose `FeatureSpec::form` is 2 / 3 (appended fields `form`,
  `form_rounds`, `form_choices`) with `ActionRequest::form_archetype` (1 + index; logged, `log_hash` 0xBD). Refusals before any
  change: `form_archetype required`, `bad form_archetype` (above `form_choices`), `already transformed`, `incapacitated`,
  `form not available` (no hook, or `build_form` returns nullopt: CR / level / fly / swim limits are sim's). Cost: the
  FeatureSpec `cost` (SRD Land druid: 1 = action). Uses: the usual `FeatureUses` row (Wild Shape: 2, `ShortRest`).
  Events: `FeatureUsed`, then `Transformed`. Duration: `form_end_round = round + form_rounds` (Wild Shape: 600 * (druid
  level / 2) rounds; 0 = none); it reverts at the start of the creature's turn in that round (`Reverted{b = 0}`, right after
  `TurnStarted`).
- **Polymorph**: `SpellEffectKind::Polymorph` (appended), one target, `ActionRequest::form_archetype`. Refused (D13: "no effect on
  a shapechanger or a creature with 0 hit points"): `polymorph: target is a shapechanger`, `polymorph: target has 0 hit points`,
  also `polymorph: one target`, `polymorph: target already transformed`, `form_archetype required`,
  `form not available`, `polymorph: primary effect only`; `form_archetype` on another spell: `form_archetype: not a polymorph
  spell`; on another request kind: `form_archetype: UseFeature or CastSpell only`. Unwilling = another team: Wisdom save vs the
  spell DC (`SavingThrowRolled`), success = no effect (no concentration). Willing (same team, or self): no save. Then
  `Transformed`; concentration is set only when someone transformed. `legal_actions` does not list Polymorph casts (the form
  choice is sim's, like teleport destinations).
- **Revert** (`ActionKind::Revert`, appended): Wild Shape a bonus action, shapechanger an action (`no bonus action available` /
  `no action available`, `incapacitated`, `not transformed`); Polymorph: `polymorph ends with the spell, not by choice`.
  `Reverted{b = 3}`. `legal_actions` lists it when affordable.
- **HP overflow** (inside `after_damage`, the one place damage lands): when a Polymorph / Wild Shape form drops to 0 HP the
  step emits the form's `DamageDealt{a = what the form absorbed (temp HP + its remaining HP), b = 0}`, then `Reverted{b = 2,
  c = overflow}` (overflow = damage past temp HP and the form's HP), restores the normal form, and applies the overflow as a
  second `DamageDealt{a = overflow}` (the two sum to the hit, no double count) with the normal drop / death-save /
  **massive damage** logic against the **normal form's hp_max** (D12). No massive damage check against the form. A
  shapechanger has no overflow (one pool) and reverts on death.
- **Other ends.** Death in any form (no 0-HP path): `Reverted{b = 4}` before `CombatantDied`. Wild Shape while Unconscious
  (Sleep): `Reverted{b = 2}` at the end of the step. Polymorph: the caster's concentration ending reverts the target
  (`ConcentrationEnded`, then `Reverted{b = 1}`); the target reverting for any other reason ends the spell, so the caster's
  concentration ends at the end of that step.
- **Encounter end (D15).** In the step that ends the encounter, `check_end` reverts every transformed creature in ascending id
  (`Reverted{b = 5, c = 0}`) right before `EncounterEnded`; `StepResult::teardown_begin` (appended) is the index of the first
  of them (nullopt when there are none). `Combat::state()` after the end has no forms.
- **Events (schema v10).** `EventKind` appends `Summoned` 48, `SummonEnded` 49, `Transformed` 50, `Reverted` 51, `Commanded`
  52 (the proposal's numbering); PR 1 emits only 50 / 51. Payloads: SIM_EVENTS.md table and v10 changelog.
- **Hash.** `has_summon_state` (`transforms != 0`, stays true after the revert) / `summons_digest` (Combat::hash **0xAF**):
  summon id counter u32 and summon count u32 (both 0 until PR 2, which fills them and appends its records), `transforms` u32,
  then per transformed combatant in `combatants` order: id, FormKind, form_source, form_caster, form_archetype,
  form_end_round, base hp, base hp_max. `log_hash` **0xBD** + `form_archetype` byte when non-zero. 0xBC stays reserved for
  summons. Fight without a transform: nothing emitted or folded, so every golden is unchanged.

Hand-off:

- **Engine Core (sim)**: install `EncounterHooks::build_form(state, creature, kind, source, archetype_index)` in
  `install_hooks()` (pure: content only). Polymorph: the beast monster@4 stat block, nullopt above the target's level / CR.
  Wild Shape: the beast block, nullopt above the druid's max CR or for fly / swim forms below the level; fill
  `blocked_features` from `requires_humanoid_form`. Shapechanger: the trait's `forms[i]` merged over the creature's own block.
  No gear AC / attacks in Polymorph / Wild Shape forms. Return a `FeatureSpec` with `form` / `form_rounds` / `form_choices`
  from `feature_spec` for Wild Shape (cost 1, rounds 600 * (level / 2)) and shapechanger features (cost 1, rounds 0), register
  Wild Shape uses (2, ShortRest). Fold `Combat::hash` 0xAF + `summons_digest` when `has_summon_state`; `log_hash` 0xBD +
  `form_archetype` when non-zero. Bump `kSimEventSchemaVersion` to 10 (done in this PR in `events.hpp` / `events.cpp`, see
  deviations) and apply §3.5's v10 placement of the ending step's sim events (`teardown_begin`). The token keeps its id; a size
  change (shapechanger Small / Large forms) needs the footprint update when it lands (sim). AI per §3.4.
- **Tools & Content** (as agreed and merged in T&C #163, `docs/CONTENT_CONVENTIONS.md` §9): spell@1 effect kind
  `polymorph { forms: [monster content id] (required, 1..255, unique, each a beast), max_cr: "target_level" (default) | CR
  string, save: {ability} (default wis) }` (primary effect only, one target, a concentration spell; `form_archetype` = 1 + index
  into `forms[]`); common@2 feature kind `wild_shape { pool (required, a short_rest pool), cost (action), class (druid), forms
  (required), max_cr_by_level (required, levels strictly ascending), no_fly_below (8), no_swim_below (4) }` with **no duration
  key** (the engine computes it); common@2 trait `shapechanger { cost, forms: [{id, name, size?, armor_class?, speed?, actions?,
  creature_type?}] }`, which **requires the creature fact `shapechanger: true`** (without it: hard error); trait flag
  `requires_humanoid_form` (refused in a Wild Shape form: `feature not usable in this form`). Refusal strings as above. The rules
  spell loader parses `polymorph` (loader key `polymorph` -> `SpellEffectKind::Polymorph`, concentration from the spell; the
  loader PR after #168); the unknown-form / not-a-beast checks need monster data and stay with the validator.

Deviations / readings: willingness is "same team" (a request flag can come later). Wild Shape's "higher of each" proficiency is
a union of the bitmasks (proficiency bonus the higher). The Wild Shape "falls unconscious" end uses reason 2. Polymorph's 1-hour
cap is its concentration (no separate timer). A Polymorph on an already transformed creature is refused. Hit Dice are not
replaced (rest-only). The sim event-version files (`engine/core/sim/include/sim/events.hpp`, `src/events.cpp`,
`tests/events_tests.cpp`) and the vertical-slice feed test were edited for v10, as #118 did for v9, because the new rules kinds
otherwise fail sim's layout lock; no other sim file was touched.

### 7.2r R-G12 rules PR 2: summons (Conjure Animals, Find Familiar, Animate Dead) (rules side) and hand-off

(Numbered after 7.2q.) Spec: `docs/proposals/SUMMONS_TRANSFORMS.md` (D1-D15), PR split §2.10 item 2. Spiritual Weapon and Pact
of the Chain are rules PR 3. Conjure Elemental's hostile turn (D5) is not in this PR (TODO in `end_concentration`).

Rules:

- **Data.** `SpellEffectKind::Summon` (appended) with `SpellDef::summon` = `SummonSpec{max_count, initiative (Own / Group),
  control (None / Verbal / BonusAction / Telepathic), vanishes_at_zero_hp}`. `Combatant` (appended): `summoner`,
  `summon_source`, `summon_group`, `summon_control`, `summon_vanishes`, `summon_concentration`, `commanded`, `gone`.
  `EncounterState` (appended): `next_summon_id` (rules-owned counter, sim seeds it), `summons` (count made). `ActionRequest`
  (appended, `log_hash` 0xBC): `summon_ids`, `summon_archetypes`, `summon_tiles` (opaque, sim's anchors), `command`,
  `command_ids`. `ActionKind::CommandSummons` (appended). Hooks: `build_summon(state, summoner, def, slot_level,
  archetype)` -> `optional<Combatant>` (pure; rules sets id, team and the summon fields) and optional `summon_choice_ok`
  (the CR-band count table; empty = any 1..max_count).
- **Cast (D1).** Cast on the caster (no creature targets). The caster's request picks the creatures (`summon_archetypes`).
  Refused before anything is spent: `summon: no creature targets`, `summon: bad creature count`, `summon: choice not allowed`,
  `summon not available` (no hook / nullopt), `summon_tiles: one per creature`, `summon: primary effect only`, summon fields
  on another spell or request: `summon: not a summon spell`.
- **Ids.** Rules assigns ids from `next_summon_id` (never at or below an existing id; unseeded 0 = above every combatant) and
  writes them into the logged request (`StepResult::logged->request.summon_ids`). Live: a non-empty `summon_ids` is refused
  (`summon_ids: assigned by rules (leave empty)`). Replay: the logged ids must equal what the counter assigns, else
  `summon_ids: do not match the summon counter`. Ids are never reused.
- **Initiative (D2, D14).** After the step's own events (after `SpellResolved`, before Ready triggers), each creature joins:
  `Own` = its own d20 + Dex; `Group` = one roll for the cast (first creature's Dex), members consecutive by id, `summon_group` =
  the first id. Placed after every entry with a higher total, or an equal total and an equal or higher Dex modifier. Placed at
  or before the current turn: `turn_index` + 1 (the current actor keeps its turn) and every stored stamp of this round at or
  after the slot (`ActiveSpellEffect::cast_stamp`, `ActiveZone::cast_stamp`, `ZoneFired::stamp`, `trigger_turn_stamp`) + 1,
  so it names the same turn; a slot that has passed acts next round. `Summoned{actor = summoner, target = creature, a = spell,
  b = archetype, c = initiative total}` per creature, id order. Concentration spells set the caster's concentration.
- **Commands (D4).** `CommandSummons{command 1 act / 2 defend, command_ids (empty = all its summons that take commands)}`:
  Verbal / Telepathic free, any BonusAction summon in the set costs the summoner's bonus action (`no bonus action available`).
  Refusals: `command: 1 act or 2 defend`, `command: not your summon`, `command: it takes no commands`, `command: duplicate
  id`, `command: no summons to command`, `incapacitated`; `command` fields on another kind: `command: CommandSummons only`.
  `Commanded{actor, target (0 = all), a = command}`. A Verbal / BonusAction summon not commanded to act may only Dodge or
  end its turn (`summon not commanded: Dodge only`; `legal_actions` = Dodge, EndTurn); reactions unchanged (SRD: "defends
  itself"). The command clears when the summon's turn ends. Telepathic (familiar) / None act on their own.
- **Ends (D10, D15).** 0 HP with `vanishes_at_zero_hp`: `SummonEnded{b = 3}` at the end of the step (after `CombatantDied` if
  killed outright). Concentration: `ConcentrationEnded`, then `SummonEnded{b = 1}` per summon (combatants order). Encounter
  end: `SummonEnded{b = 5}` ascending id, then the `Reverted{b = 5}` block, then `EncounterEnded`; `teardown_begin` covers
  both. A transformed summon just ends (form cleared, no `Reverted`). An ended summon is `gone` and `dead`, skipped in the turn
  order, never acts again. Animate Dead (no vanish) drops as a monster.
- **0xAF.** `summons_digest`: counter u32, count u32, then per summon (`summoner != 0`, combatants order) id, summoner,
  summon_source, summon_group (u32), gone, commanded (byte), then the PR 1 transform fields. `has_summon_state` = summons != 0
  || transforms != 0 (a seeded counter alone never folds).

Hand-off (sim / T&C):

- Sim: seed `next_summon_id` after `begin_encounter` above every chunk spawn id; fold 0xAF while `has_summon_state` and 0xBC
  (summon_ids, archetypes, tiles, command, command_ids) per SIM_EVENTS.md; implement `build_summon` / `summon_choice_ok`
  (CR band table, D1) and place the creatures on `summon_tiles`; read `teardown_begin`; drop `gone` combatants from views.
- Content / T&C: `summon` effect kind (max_count, initiative, control, vanishes) for Conjure Animals / Woodland Beings / Minor
  Elementals, Find Familiar, Animate Dead; archetype lists per CR band.
- Sim events: `TokenSpawned` comes right after its own `Summoned` (one per creature, id order).
- **Sim stamps EC will fix** (they key on `turn_index`, which a summon inserted before the current actor shifts; rules rewrites
  only its own stored stamps): `Combat::aggressive_` and `run_to_end`'s `(round, turn_index)` turn key (SUMMONS_TRANSFORMS.md
  §3.4: key on `(round, current_actor, TurnStarted count)`).
- Ended summons stay in `order` (`gone`, dead, skipped); nothing is removed, so ends never shift `turn_index`.
- A group shares one slot: every member's `InitiativeEntry` carries the group roll and the first creature's Dex (D2).
- An ending summon's own concentration goes through `end_concentration` (`ConcentrationEnded`, modifiers / zones / Polymorph
  links cleaned up); at encounter end those come before the contiguous `SummonEnded{b = 5}` block.
- Deviation: filling the 0xAF counter / count fields changes transform-only 0xAF bytes vs PR 1 (no golden folds 0xAF yet).

### 7.2s R-G12 rules PR 3: Spiritual Weapon and Pact of the Chain (rules side) and hand-off

(Numbered after 7.2r.) Spec: `docs/proposals/SUMMONS_TRANSFORMS.md` D3, D6, PR split §2.10 item 3.

Rules:

- **Spiritual Weapon (D3).** `SpellEffectKind::SummonWeapon` (appended) with `SpellDef::summon_weapon` = `SummonWeaponSpec{
  upcast_every (2), reach_ft (5), move_ft (20)}`; the attack uses `dice`, `damage_type`, `add_spellcasting_modifier`,
  `upcast_dice` (per `upcast_every` levels above the spell's; SRD +1d8 per two). Not a combatant: `EncounterState::summon_weapons`
  (appended) rows `ActiveSummonWeapon{token_id, owner, spell_id, slot_level, end_round}`; no HP, no turn, not a target. The cast
  (content: bonus action) targets the creature of its first attack, or the caster for none (`spiritual weapon: one target`,
  `spiritual weapon: primary effect only`). The token id comes from the summon counter like PR 2's ids (filled into the logged
  `summon_ids`; live refused, replay checked). Events: `Summoned{actor = caster, target = token id, a = spell, b = 0, c = -1}`,
  then its melee spell attack (`AttackResolved` actor = caster), before `SpellResolved`. Later turns: `ActionKind::SummonWeaponAttack`
  (appended, `spell_id` names the row, target): the caster's bonus action (`spiritual weapon: none active`, `no bonus action
  available`, `bad target`, `target is dead`, `incapacitated`); the 20 ft move is sim's. `legal_actions` lists it per hostile with no reach check: **sim filters weapon-attack reach** (5 ft of the token after its move) in its legal-action view. Tiles: the cast's `summon_tiles[0]` (spawn) and a `SummonWeaponAttack`'s `summon_tiles[0]` (move) are stored in the row (`has_tile`, `tile_x`, `tile_y`) and logged (0xBC); tile contract (EC, SUMMONS_TRANSFORMS.md §3.2): the cast needs exactly one spawn tile (zero or several: `spiritual weapon: one
tile`), any in-bounds tile within 60 ft of the caster, the token occupying nothing; `SummonWeaponAttack`: empty = no move, else
one tile at most 20 ft from the stored tile (several: `spiritual weapon: one tile`), reach 5 ft from the tile after the move.
Sim validates live; on replay sim re-checks the logged tiles (failure = replay desync error); rules trusts the log. 0xAF adds the weapon rows only while `summon_weapons` is non-empty. A familiar never attacks on its own turn (`familiar_attack: a familiar cannot attack on its own turn`; `legal_actions` lists no Attack for it): the Pact trade is its only attack. It also takes no opportunity attack (`may_react` excludes a familiar from LeavingReach windows, so no window is offered) and cannot ready an attack (`familiar_attack: a familiar cannot ready an attack`).
  Duration from the spell (1 minute = 10 rounds): `SummonEnded{b = 0}` at the start of the owner's turn in `end_round`. No
  concentration. Encounter end: in the same pass as summons, ascending by id (`SummonEnded{b = 5}`). `summons` counts it, so 0xAF
  folds; 0xAF layout gains the weapon rows after the summon records (SIM_EVENTS.md).
- **Pact of the Chain (D6).** `Combatant::pact_of_chain` (appended, content). `ActionRequest::familiar_id` (appended, `log_hash`
  0xBE): an `Attack` whose attack is made by the warlock's familiar (its Telepathic summon) with the familiar's attack
  `attack_index`, spending one of the warlock's attacks (an Extra Attack remaining, or the Attack action) and the familiar's
  reaction (`reaction_used`). Refusals: `familiar_attack: needs Pact of the Chain`, `familiar_attack: one attack of the Attack
  action` (bonus / multiattack), `familiar_attack: not your familiar`, `familiar_attack: the familiar cannot react`, `bad
  target`, `bad attack index`, `no action available`; on another kind `familiar_id: Attack only`. Events: the familiar's
  `AttackResolved` (actor = familiar); no reaction window is opened (the trade is the warlock's choice).

Hand-off: sim folds 0xBE (non-zero only) and the 0xAF weapon rows; spawns / moves / removes the weapon token (`TokenSpawned` /
`TileMoved` / `TokenRemoved`, ids from `Summoned.target`); content: spell@1 `summon_weapon` kind and the Chain feature flag.
Deviation: the proposal's "reuse `sustain` with a weapon flag" is a dedicated `SummonWeaponAttack` kind instead (clearer
validation, no Heat Metal coupling).

### 7.2t Cover (rules side) and hand-off

(Numbered after 7.2s.) Gap list item 1 (`docs/SRD_COMBAT_COVERAGE.md`, "Level 1-5 gap list"). SRD 5.1 "Cover": half cover +2
to AC and Dexterity saving throws, three-quarters cover +5, total cover: can't be targeted directly by an attack or a spell,
though a creature can be affected by an area of effect only if the cover does not block the area's point of origin.

Rules:

- **Types (encounter.hpp).** `enum class Cover : uint8_t { None, Half, ThreeQuarters, Total }`;
  `struct CoverQuery { CombatantId source; CombatantId target; bool from_area; uint32_t spell_id; bool has_origin;
  int32_t origin_x, origin_y; }`; `constexpr int cover_bonus(Cover)` (Half 2, ThreeQuarters 5, else 0).
- **Hook (appended to `EncounterHooks`).**
  `std::function<Cover(const EncounterState&, const CoverQuery&)> cover;` Empty = `Cover::None` everywhere, so every
  existing golden and digest is unchanged. Never asked for `source == target`.
- **Origin.** `has_origin` true = measure from the sim tile `origin_x / origin_y` (opaque to rules); false = the source's
  space (for an area: the caster's space, i.e. a self-centred area or a cone / line from the caster). Rules fill it with:
  - the Spiritual Weapon token's tile for its attacks: the cast's first attack from the spawn tile
    (`summon_tiles[0]`), `SummonWeaponAttack` from the tile after its move (`summon_tiles[0]` when given, else the row's tile);
  - an aimed area's `ActionRequest::has_area_origin / area_x / area_y` (appended, CastSpell; logged under **log_hash 0xC0**,
    folded only when set: `has_area_origin` byte 1, `area_x` i32, `area_y` i32; sim fold added in `Combat` log_hash).
    Refused on a non-area spell: `area_origin: area spells only`.
- **When rules call it.**
  - Validation of `Attack` (`source` = attacker, `spell_id` 0), of a Pact of the Chain familiar attack (`source` = the
    familiar), of each target of a targeted `CastSpell` (`SpellDef::area == None`; the Spiritual Weapon cast from its spawn
    tile), and of `SummonWeaponAttack` (from the tile after the move). `Total` refuses with `target has total cover`
    before anything is spent (no slot, no action / bonus action, the weapon does not move).
  - Eligibility of reactions: an opportunity attack reactor, or a readied `Attack`, whose target has total cover from it is
    not offered (no window opens, no reaction is spent; the readied action is kept).
  - Once per attack roll in `do_attack` (weapon, natural, spell, Spiritual Weapon, reaction attacks): the bonus goes into
    `AttackContext::cover_ac_bonus` (so `BeingHit.target_ac` and the `AttackRolled` window include it). `Total` there (only
    reachable by an attack no validation covered, e.g. a Retaliate reaction) makes no roll and no event.
  - Area casts (`SpellDef::area != None`), spells and save actions alike: once per listed target at resolution with
    `from_area` true and the area origin; `Total` drops the creature from every effect of the cast (no save, no event, no
    rng draw).
  - Save spells and save actions (monster breath weapons are save actions: `is_spell` false, same `resolve_effect` path;
    tested with an aimed cone): once per target; the Dex save total gets `cover_bonus` (other abilities: nothing).
    `SpellDef::ignores_cover` (spell@1 top-level `ignores_cover: true`, Sacred Flame; schema line OK'd by T&C) drops
    Half / ThreeQuarters, never Total.
- **Deferred: zones, auras, `area_save` riders.** Their saves get no cover yet. When picked up they use the same
  `CoverQuery` origin fields: a point zone's anchor tile (`ActiveZone::anchor_x / anchor_y`), the owner's space for a
  creature-anchored aura (has_origin false, source = owner), and the trigger's area origin for `area_save` riders.
- **Logging.** The hook's answer is a pure function of the state, the logged request and sim's positions, so replay
  recomputes it. Only the aimed origin is a new input (0xC0). No event change, no `event_stream_version` bump.

Hand-off (Engine Core, sim):

- Fill `ActionRequest::has_area_origin / area_x / area_y` from `Command::aim` for an aimed area (a point the caster
  chooses: Fireball, Thunderwave is self so unset); the 0xC0 fold is in place.
- Implement `EncounterHooks::cover` in `sim::Combat` from the grid, synchronous and pure (no rng, no mutation), using the
  positions at the moment of the call (replayed positions in replay). Origin: `has_origin` = that tile; otherwise the source's
  space (a footprint: the corner / tile that gives the target the least cover, per SRD "the attacker's space"). Lines run from
  the origin to the target's footprint tiles. Creatures (any size, either side) and low obstacles (low wall, furniture,
  narrow tree trunk) give **half**; heavier obstacles (portcullis, arrow slit, thick tree trunk) give **three-quarters**; a
  complete obstruction (wall, closed door) gives **total**. Only the highest degree counts; sources do not add.

### 7.2u Next-roll spell riders (rules side) and hand-off

(Numbered after 7.2t.) Gap list item 8. SRD 5.1 Guiding Bolt, Vicious Mockery, Ray of Frost, Shocking Grasp, Chill Touch.

Already expressible (verified with tests, no change): Vicious Mockery (AttackRoll disadvantage, `consume` 1, expiry 2) and
Ray of Frost (`Speed` `speed_sub` 10, `until_caster_turn`).

Rules (all appended; defaults keep every existing digest and golden):

- **Durations.** `Expiry::EndOfCasterNextTurn` (`ModifierSpec::expiry` 3): ends at the end of the caster's (`ActiveModifier::caster`)
  next turn; applied during the caster's own turn, that turn does not count. `Expiry::StartOfTargetNextTurn` (expiry 4): ends
  when the target's next turn starts. Modifiers only. ModifierExpired reason 0.
- **Guiding Bolt** uses expiry 3 with `consume` 2 (was `until_caster_turn`, which dropped it at the start of the caster's next
  turn; the test and the v4 validator fixture `guiding_bolt.json` are fixed; no golden moved).
- **Markers.** `ModStat::NoReactions`: `may_react` refuses (no window offered, no reaction spent) - Shocking Grasp with expiry 4.
  `ModStat::NoHealing`: every hit point regain returns 0 healed (`Healed` with a = 0 still emitted where it was): spell / feature
  healing, the Heal request, regeneration, heal_self riders, pool healing, feature heal dice; a natural-20 death save regains no
  hit point (its counts still reset). Temporary hit points are not hit point regain and are unaffected. Chill Touch with
  `until_caster_turn`.
- **Predicates.** `SubjectIsCaster` (the subject is the modifier's caster; `TrigCtx::mod_caster`, set for `when` gating of
  modifiers in `do_attack`), `OwnerCreatureType` (mask over CreatureType, the owner). Chill Touch's undead clause: AttackRoll
  disadvantage, `when` [OwnerCreatureType undead, SubjectIsCaster], expiry 3.
- **Shocking Grasp vs metal armor:** not modelled (no armor material anywhere); documented, no rule added.
- **Loader (spell@1 / common@2 modifier).** Stats `no_reactions`, `no_healing`; duration kinds `until_end_of_caster_next_turn`,
  `until_start_of_target_turn`; predicate kinds `subject_is_caster`, `owner_creature_type {creature_types}`. Schema arms added in
  `schemas/common/v2.schema.json` and `tests/validate/test_union_arms.py` (T&C to confirm).
- **Hashing.** Everything lives in `EncounterState::modifiers`, folded by `modifiers_digest` under the existing `Combat::hash`
  tag 0xA2 (stat, predicate kinds and `timed.kind` / `rounds_left` are already folded). No new tag; no log field.

Hand-off (sim, EC):

- **Speed.** Sim reads the movement budget (`TurnBudget::movement_ft`, from `modified_speed` at turn start), so Ray of Frost
  needs nothing new for normal movement. **Rules' Dash is fixed here** (EC review): it adds `modified_speed(s, a)` (speed after
  conditions, exhaustion and Speed modifiers), so Ray of Frost gives 20 + 20 = 40 ft, grappled 0, exhaustion 2 half; tested.
  `int modified_speed(const EncounterState&, const Combatant&)` is public in `rules5e/encounter.hpp` for sim. **EC is
  switching the sim side** to it: the Aggressive dash bookkeeping (`AggressiveDash`, which still reads `effective_speed`), AI
  Dash planning, stand-up cost, and a readied Move's distance. (Rules' own StandUp still uses `effective_speed` for its half-
  speed cost; it follows with the sim change.) Haste follows from the same fix: a hasted Dash adds the doubled speed (60 + 60), and the small-leftovers Haste test
  expectation moved from 90 to 120 (no golden moved).
- **Reactions.** Nothing to read: the marker is enforced inside rules' `may_react`. Sim's reaction UI / AI should not offer a
  reaction when rules wouldn't (it already polls rules' windows).

### 7.2v Control conditions (rules side) and hand-off

SRD 5.1 Turn Undead (with the Destroy Undead hook), Command, Fear and Hideous Laughter. Rules restricts the controlled
creature's legal actions (`step` refuses, `legal_actions` drops); sim picks the path. All new fields are appended; content that
uses none of them hashes and replays as before (no golden moved).

- **Data (SpellDef, kind Save).** `control` (`ControlKind`: Turned, Fear, Laughter, Command), `control_excluded` (creature-type
  mask, no save: Command's undead), `control_min_int` (Hideous Laughter 4: Intelligence 4 or less is unaffected),
  `destroy_cr8_max` (Destroy Undead: CR × 8, CR 1/2 = 4). `Combatant::cr8` (-1 = unknown, never destroyed). New
  `RepeatSave` arms: `EndOfTurnUnseen` (Fear) and `EndOfTurnOrDamageAdv` (Hideous Laughter).
- **Turned** (Turn Undead: `target_types` undead, Wis save, 10 rounds, `ends_on_damage`). No SRD condition: the effect has
  `no_condition`. Allowed: Move, Dash, Dodge, StandUp, Attack `special` 4 (escape), EndTurn; no reactions (`may_react`).
  Sim: moves it away so it ends no closer than 30 ft when it can, only Dodges when there is nowhere to move, and supplies
  the targets (undead within 30 ft that can see or hear the cleric).
- **Destroy Undead.** On a failed save an undead with `cr8 <= destroy_cr8_max` is killed instead of turned. Hook only: T&C
  wires the cleric 5 feature (a shipped-class leftover) and the CR table (1/2 at 5, 1 at 8, ...).
- **Command.** `ActionRequest::command_word` (1 Approach, 2 Drop, 3 Flee, 4 Grovel, 5 Halt) is required on a Command cast
  and refused elsewhere; log_hash **0xC4** (sim fold added). The effect lasts to the end of the target's next turn
  (`EndOfTargetNextTurn`). On that turn: Approach / Flee = Move, Dash, StandUp, EndTurn (sim: toward the caster, stopping
  within 5 ft, or away by the fastest means); Drop / Halt = EndTurn only (dropped items are not modelled); Grovel = prone at the
  start of the turn, then EndTurn only. Undead are unaffected; language and "obviously harmful" commands are not modelled.
  `legal_actions` lists a Command cast once per word.
- **Fear.** Frightened (`applies_condition`), concentration, cone from data. While frightened by it: Move, Dash, Dodge, StandUp,
  EndTurn (sim: Dash away by the safest route). End-of-turn Wis save only when `EncounterHooks::can_see(target, caster)` is
  false; no hook = seen (no save). Dropped items are not modelled.
- **Hideous Laughter.** Prone plus incapacitated (`has_also`, only when this effect added Incapacitated, so its end never strips another source's), can't StandUp; when it ends the creature stays prone (stands up as usual). Save at the end of each turn and each time it takes
  damage, with advantage on the damage save.
- **Hashing.** Control effects fold into `leftover_digest` (Combat::hash 0xAA) after a 0xFE separator, only while one exists:
  `spell_id`, `caster`, `target` u32, `control`, `command_word` bytes. The rest of the effect (timer, dc, repeat save) is in
  the existing effect digests. No new Combat::hash tag.
- **Hand-off (EC / sim).** Path choice for Turned / Fear / Approach / Flee; the AI should pick a `command_word`; the Turn
  Undead target list; `can_see` for Fear. **T&C:** loader keys (below) and the cleric's Channel Divinity / Destroy Undead.
- **Schema keys (loaded; spell@1 save effect, defs in common@2, additive, no version bump):** `control` (`turned` | `fear` |
  `laughter` | `command`; first effect only), `control_excludes_creature_types` (array), `control_min_int` (int 1-30),
  `destroy_undead_max_cr8` (CR × 8: 4, 8, 16, 24, 32), `repeat_save` arms `end_of_turn_unseen` and
  `end_of_turn_or_damage_advantage`. A save effect may carry `control` instead of damage / condition. Monster
  `challenge.rating` maps to `Combatant::cr8` (sim content.cpp).

## 8. Migrations and inert markers

### 8.1 Chain

`monster_3_to_4`, `character_3_to_4` registered in `engine/core/assets/.../content_migrations.hpp`
alongside `2→3`. `common@2` has no document type, so no `common` migration. Both are pure,
idempotent, preserve key order, and are **functions of old data only**.

### 8.2 What `3→4` writes

1. Set `$schema` to the `@4` tag.
2. `reactions[]`: stamp `inert: true` on **every** entry; if any were stamped, add `"reactions"` to
   `inert_mechanics`.
3. `bonus_actions[]`: stamp `inert: true` only on entries `@4` would load (`kind != "special"`); if any,
   add `"bonus_actions"`.
4. For every `traits[]` / `features[]` entry whose `effect.kind` is in the **frozen literal list**
   `{on_event, apply_modifier, grant_action, heal_pool, save_effect, reaction}` set `effect.inert = true`.
   The list is a literal inside the migration, **not** "whatever this batch introduces": a later
   batch gets its own `4→5` list. (`self_heal`, `damage_bonus`, `bonus_action_grant`, **`damage_mitigation`, `magic_weapons`** (#58) and
   **`multiattack`** (#59) are live at `@3` and are never marked; `qualifiers` on actions is likewise a live `@3` field.)
5. **Merge, never overwrite**, `inert_mechanics`: read the existing array (`2→3` writes entries such
   as `damage[1..]`), union, sort, dedup. The `2→3` helper `mark_defenses_inert` assigns the whole
   array; the `3→4` migration must not copy that pattern (test: a file with both `damage[1..]` and
   `reactions`).
6. Never touch `actions[]` (`special` stays descriptive), `spellcasting`, `size`, defenses, `legendary_actions`,
   or anything live at `@3`.

#### 8.2.1 The `common@2` trait / trait_active / effect defs

A trait (or feature) `effect` at `@4` is an `anyOf` of **three arms**, so every migrated and every
native file validates and a typo in a new kind is still an error:

```json
"effect": { "anyOf": [
  { "$ref": "#/$defs/effect_inert" },       // (a) migrated/stamped: any parameters, never applied
  { "$ref": "#/$defs/effect_live" },        // (b) live @3 kinds and unknown kinds, v1 behaviour
  { "oneOf": [ {"$ref":"#/$defs/on_event_effect"}, {"$ref":"#/$defs/apply_modifier_effect"},
               {"$ref":"#/$defs/grant_action_effect"}, {"$ref":"#/$defs/heal_pool_effect"} ] } ] }   // (c) new closed arms

"effect_inert": { "type":"object", "required":["kind","inert"],
  "properties": { "kind": {"enum":["on_event","apply_modifier","grant_action","heal_pool","save_effect","reaction"]},   // the FROZEN 3->4 list (§8.2 step 4)
                  "inert": {"const": true} } },                           // otherwise open: parameters are not validated (§8.3)

"effect_live": { "allOf": [ {"$ref":"../common/v1.schema.json#/$defs/effect"},                   // v1: multiattack, damage_bonus, self_heal, bonus_action_grant, unknown kinds
                            {"properties":{"kind":{"not":{"enum":["on_event","apply_modifier","grant_action","heal_pool","save_effect","reaction"]}}}} ] },

"apply_modifier_effect": { "type":"object","additionalProperties":false,"required":["kind"],
  "properties":{"kind":{"const":"apply_modifier"},"cost":{"$ref":"../common/v1.schema.json#/$defs/effect_cost"},"usage":{"$ref":"#/$defs/usage"},
                "modifier":{"$ref":"#/$defs/modifier"},
                "modifiers":{"type":"array","minItems":1,"maxItems":16,"items":{"$ref":"#/$defs/modifier"}}},   // Rage: one modifier per damage type of resistance
  "oneOf":[{"required":["modifier"]},{"required":["modifiers"]}] },
"grant_action_effect":   { "type":"object","additionalProperties":false,"required":["kind"],
  "properties":{"kind":{"const":"grant_action"},"cost":{"enum":["action","bonus_action","reaction","free"]},   // omitting cost means action; `free` for Action Surge
                "action_list":{"enum":["any"]},"usage":{"$ref":"#/$defs/usage"},"count":{"type":"integer","minimum":1,"maximum":2}} },
"heal_pool_effect":      { "type":"object","additionalProperties":false,"required":["kind","pool"],
  "properties":{"kind":{"const":"heal_pool"},"cost":{"$ref":"../common/v1.schema.json#/$defs/effect_cost"},"pool":{"$ref":"../common/v1.schema.json#/$defs/local_id"}} }
```

`grant_action.action_list` has the single value `any` today: **Haste's restricted extra action** (one weapon
attack, Dash, Disengage, Hide or Use Object only) is **not expressible**; the field exists so a later closed
enum value can add it without a new effect kind. Gaps against the legacy kinds that stay live under arm (b):
`damage_bonus.condition` has no `modifier.when` counterpart yet (a conditional static bonus stays a
`damage_bonus` effect), and `usage.uses` is bounded at **255** in v2 (`self_heal.uses` goes to 255; the
schema above raises the max from 9) so a migrated-style pool can mirror it.

`on_event_effect` (§3.1) has **no** `inert` property: an inert `on_event` is arm (a). A native `@4` effect
whose kind is in the frozen list but is neither stamped inert nor a valid closed arm (e.g. `save_effect`
or `reaction` as a trait effect kind: they are action/reaction kinds, not effect kinds) is a schema error.
`trait` and `trait_active` (v2) differ from v1 only in `effect` -> this `effect` and in `cost` accepting
`reaction` (the v2 validator row, no new def). **Schema-level test** (the §8.4 migrated-file row): a `@4` document with an
`inert: true` `on_event` effect whose `on` is bogus validates through arm (a) and fails once `inert` is removed.

### 8.3 Loader contract (Engine Core / T&C)

`sim::ContentLibrary` already ignores whatever `inert_mechanics` lists, and `content.cpp` checks
`effect.inert` **before** hook lookup, so a marked effect lands in `inert_features` even once the
registry learns the kind. New: the loader honours `reactions` and `bonus_actions` markers and the
per-entry `inert` stamps.

**Registry refusal rule (Q12, approved), made implementable.** A second line of defence: a
new-set kind in a document that *declared* a version below 4 is **listed as inert and skipped** (the
same outcome as the marker path), **never a load error**: compat content must always load. The
migrated document's `$schema` is already `@4`, so the check needs the *declared* version
(`LoadResult::declared.version`), which today never reaches `ContentLibrary::add()` or
`FeatureRegistry::add()`. Sim-side work (Engine Core), agreed API: `ContentLibrary::add(doc, schema_type, pack_id, uint32_t declared_version = 0)`,
an **optional trailing parameter where 0 means "parse the document's own `$schema` tag"** (never "latest",
so a direct `add()` of an `@3` doc behaves as `@3`). **A missing or unparsable `$schema` with `declared_version == 0` means no refusal**, so the many existing direct `add()` callers with hand-built documents (e.g. `casting_tests.cpp`) behave identically. A migrated document has `$schema` rewritten to `@4`, so the parse path only refuses *unmigrated* `@3` documents; `load()` passes `LoadResult::declared.version`, which is the path that matters; `load()` passes `LoadResult::declared.version`. It is
plumbed through `FeatureBuildCtx` and `FeatureRegistry::add`, and the refusal applies there. Existing
callers (`sample_set.hpp`, packset tests, direct users) keep compiling and behave identically. Test: a
**migrated** doc (declared 3, `$schema` `@4`) with a new-kind effect and **no** marker loads cleanly and
lands in `inert_features`. The "new set" lives in two places (sim's registry list and the migration's frozen literal list), so a **test keeps the two equal**.

**Validator (`tools/validate`):**

* Per-tag table (§1.2); no ad hoc version branches.
* **Migrated files skip or relax `inert: true` effects:** a `@4` document produced by migration may
  carry effects of new kinds with `inert: true` whose parameters were never validated at the time
  (they were descriptive). The validator accepts an `inert: true` effect of a new-set kind without
  validating its parameters beyond `kind` (it is data kept for reporting), and does *not* count it as
  unknown-kind noise.
* Errors on `@≥4`: `grapple`/`swallow` riders (until R-G8); `cost` outside the v2 enum; unknown
  `window` / predicate / rider kinds. Warnings (unchanged): unknown effect kinds.
* **Cross-reference checks (new):**
  1. `on_hit[]`/rider `attack_id`, `reaction_effect.attack_id`, and `bonus_attack.attack_id` must name an `id` in the same document's `actions[]`.
  2. `usage.pool` / `spend.pool` / `heal_pool.pool` must name a `resources[].id` of the same document; `cost` may not exceed the pool's `max` (when `max` is a constant).
  3. `spellcasting.spells` / `innate.*` / `reaction_effect.spell` content ids must resolve in the loaded pack set (as `spells[]` does today).
  4. Every `save_effect.id` is unique within the document and `"<archetype id>#<id>"` contains exactly one `#` (spell-id derivation, §2.3).
  5. `multiattack` effect `options[].attacks` **and the top-level `attacks` shorthand** (the #59 shape, carried unchanged, §2.1) reference attack `id`s in the same document's `actions[]`; option ids are unique. **Ask on #59** (T&C relays): close the option items with `additionalProperties: false`, so `options[]` entries cannot carry stray fields at `@3` or `@4`.
  6. `item@1` ids referenced from `character@4 equipment` resolve (see "Item references" below).
  7. `forward_compat` values are in the known enum and unique.
  8. Derived trigger ids (`<owner id>#<index>`, §3.1) are unique within the document; `resources[].id` values are unique.
  9. `window.kind: roll_attack` and `effect.kind: subtract_roll` are **rejected at `@4`** (reserved, Q6).
     **Lifted by R-G7** (sim #114): both are live; the validator now mirrors the loader's rules instead (`docs/CONTENT_CONVENTIONS.md` section 3).
  10. A `save_effect` that violates the schema rules in §2.1 (no `damage[]`/`apply[]`, `damage[]` without `on_success`, `origin: point` without `range_ft`) has one negative case each.
  11. **`item` references** (below).
  12. A Legendary Resistance-style pool is *declaration-only* until row 13: the validator warns when a `resources[]` entry is never referenced by any `usage.pool` / `spend.pool` / `heal_pool.pool`.
  13. `save_effect.range_ft` is **forbidden iff `origin` is `self` and `area` is present** (an area centred on the caster has no range; a single-target action with `origin: self` may still carry `range_ft`). One negative case.
  14. **`resource.max` for a monster must be an integer** (object/table forms are `character@4`-only). One negative case.
  15. `item@1` weapons: `range[1] >= range[0]`; `reach` is 10 **iff** `properties` includes `reach` (otherwise 5, or absent for pure ranged).
  16. Level tables (§7.1): the rules listed there (ascending levels, `unlimited` last only, integer tokens, class character-only, `start` forbidden with a table, cost <= largest finite value).
  17. **Item riders** of kind `bonus_attack` are rejected (items have no `actions[]` to reference).
  18. `modifier.bonus_table` is forbidden in `spell@1`'s additive `apply_modifier` (tables resolve from class level, which spells do not have).

**Item references.** Cross-document id resolution lives in **`tools/validate/validate_packs.py`**
(the `ids` / `spell_ids` sets built from the pack's own documents plus each resolved dependency's
`content_ids`, with `_is_spell`), not in `packset.py`. `item` is a new reference kind there: an
`items` set built the same way (documents whose `$schema` starts with `item@`, plus dependencies'
`content_ids` where an `_is_item` check on the file matches). An `equipment` entry `{item, equipped?}`
resolves **by content id** against that set (own pack or any pack in the resolved dependency order; an
`srd51.*` id not defined anywhere follows the same escape-hatch rule as spells). `packset.py` is unchanged.
At runtime the loader resolves the id in the final `PackSet`. For a **native `@4` file** (not produced by migration): bare string entries in `equipment` are
ordinary descriptive data and are never loaded; `{item, equipped}` objects are live. `inert_mechanics` is
written by the `3→4` migration, not by authors, but a hand-written `@4` file *may* carry it (it is
declared in the schema, §1.3) and sim honours whatever it lists. The migration adds `"equipment"` only when the
*migrated* `@3` list is non-empty, so a native file normally never lists it. **If a file lists `equipment`
as inert and also holds an `{item}` object** (only possible by hand-editing): the marker wins (the same rule as
every inert key: sim ignores exactly what is listed), the objects are **not** equipped, no AC is recomputed, and
the validator emits a **warning** ("equipment marked inert but contains item objects; they are ignored"), never an
error, so such a file still loads.

### 8.4 Compat behaviour goldens (CONTENT_COMPAT §2)

New *decorated* fixtures per piece prove "descriptive stays descriptive":

| Fixture (at `@3`) | Must digest identically to |
|---|---|
| fl1 goblin + descriptive `special` breath weapon, `reactions: [Parry]`, `bonus_actions: [Nimble Escape]` | the undecorated fl1 goblin |
| fl1 fighter with trait effects `{kind:"on_event", ...}` and `{kind:"apply_modifier", ...}` | the undecorated fighter |
| Same two, run through `3→4` | themselves (the migrated form plays like the `@3` one) |
| A `@4` file produced by migration that contains an `inert: true` effect of a new-set kind with *unvalidated* parameters (e.g. `on_event` with a bogus `on`) | validates under the relaxed rule (§8.3), digests identically to its `@3` source, and the effect is listed in `Archetype::inert_features` |

Expected migration output for existing frozen fixtures: the `fl*` files gain `inert_mechanics` /
per-entry `inert` stamps **only** where they have non-empty `reactions` / loadable `bonus_actions`
(the goblin's descriptive entries do). No frozen file is edited (`compat_freeze` unaffected); the
stamps exist only in migrated in-memory form, and tests assert exactly that.

### 8.5 Luau API level 2 and manifest `api_level` (approved)

`manifest@1.api_level` is `const: 1` today and `tests/validate/run_negative.py` hardcodes `1` in its
manifests. This batch widens the manifest schema **additively** to `enum [1, 2]` (no manifest tag
change). **Level 2** is the script API that can see `@4` content (reaction/resource/modifier queries, the
new rules `EventKind`s 39+ in the event feed). Rules for **mixed levels**:

* The **host level is the max** it supports (`kMaxApiLevel` becomes 2); each **pack** runs at its own
  declared level. Loading is **per pack with filtering**: a level-1 pack keeps loading under a level-2
  host and sees exactly today's API and a feed that filters rules kinds above `SpellCheckRolled`; a pack
  declaring level 2 on a level-1 host is **rejected** (the existing `level > kMaxApiLevel` check).
* `load_pack_scripts` today requires **equality** (`level != host.api_level()` is an error). That
  becomes `level <= host_max` with the pack's scripts bound to the *pack's* level (the host already
  runs the strict typecheck per pack at that pack's level). **Level-N definitions are a superset of
  level 1..N-1**: today `typecheck()` (`typecheck.cpp` ~L58-62) loads one defs file standalone and treats
  empty defs as the error "no API definitions for api_level N", and `api_definitions()`
  (`script_host.cpp` ~L457) hard-codes level 1. So the level-2 file is **not empty and not standalone**:
  `api_definitions(N)` returns the **concatenation** of `engine.level1.d.luau` .. `engine.level<N>.d.luau`
  in order, and `typecheck` type-checks a pack at level N against that concatenation. A level-2 pack
  therefore type-checks level-1 calls. `engine.level2.d.luau` in 16a is a **minimal non-empty additive file**
  (the existing check is only `defs.empty()`, so any non-empty text passes; to make the superset behaviour
  testable it contains one level-2-only declaration, a marker such as `declare engine_api_level2: boolean`, that the
  typecheck test can reference). This is script_host work.
* **Level-2 queries are free functions or new types, never `SimContext` extensions.** Level 1's
  `SimContext` is frozen with the rest of level 1; level-2 API (row 16b: reaction/resource/modifier
  queries) is added as new `declare function`s and new `declare extern type`s in `engine.level2.d.luau`.
  Row 16a ships only a marker type (`Level2Marker`) so the superset behaviour is testable.
* **`FROZEN.sha256` keeps level 1 only until row 16b.** The `api_freeze` ctest's `INCLUDE` regex is narrowed from
  `\.d\.luau$` to `^engine\.level1\.d\.luau$`, so the unfrozen level-2 file is not flagged as an added file; 16b
  freezes it (adds its hash and widens the regex back) when its content is final.
* A pack containing any `@4` or `item@1` document must declare `api_level >= 2` (validator error otherwise).
* **Who passes the max.** The embedder (the process that owns the `ScriptHost`: `game/vertical_slice`
  and the sim test hosts) constructs `ScriptHost(kMaxApiLevel, limits)`; the constructor's `api_level`
  parameter now means the **host maximum**, not a single pack's level (default stays 1 until the embedder opts in).
  `load_pack_scripts` type-checks and binds each pack at **its own** manifest level (`<= host max`).
* **What `PackScripts::api_level` reports.** It keeps meaning "the host maximum" (unchanged value for
  existing callers: it is `host.api_level()`), and 16a adds `PackScripts::pack_levels` (pack id → declared
  level) so tools can see the mix. Nothing reads a single "the" level once levels are per pack.
* **Manifest schema text.** `api_level`'s description changes from "only level 1 exists for feature_level 1"
  to "Script API level; levels 1 and 2 exist (level 2's definitions file is minimal until row 16b)".
* **Order (explicit).** Row **16a has no dependencies and lands first**; row 2 depends on it, so the
  validator's level-2 rule never outruns the host. `kMaxApiLevel = 2`, the manifest enum and `run_negative.py`
  (levels 1, 2, 3) are all in 16a; 16b (level-2 content and feed filtering) follows rows 4 and 5. Until 16b
  no pack gains anything from level 2, and a level-1 pack is observably unchanged.

## 9. Event schema impact, hashing and golden hashes

### 9.1 New `rules5e::EventKind` values (append after `SpellCheckRolled` = 38)

| Value | Kind | Payload (`actor, target, a, b, c`) | From |
|---|---|---|---|
| 39 | `RechargeRolled` | actor, -, action/spell id, d6 natural, recharged 0/1 | §2 |
| 40 | `FeatureTriggered` | owner, subject, trigger id, `TriggerOn`, outcome (0 none, 1 applied, 2 declined, 3 failed save) | §3, §4 |
| 41 | `ResourceSpent` | actor, -, pool id, amount, remaining | §7 |
| 42 | `ModifierApplied` | caster, target, modifier id, stat, magnitude | §6 |
| 43 | `ModifierExpired` | caster, target, modifier id, reason, - | §6 |
| 44 | `ActionUsed` | actor, first target, action/spell id, `is_spell` (0/1), usage kind | §2 |

`ActionUsed` is a dedicated kind (Q9, revised on Engine Core's review): flag bits inside an existing
payload are invisible to event-schema checks. Non-spell save actions emit `ActionUsed` *instead of*
`SpellCast`, and still emit `SpellResolved`/save events as usual. Ordering of `ActionUsed` relative to `SpellResolved` is pinned in `docs/SIM_EVENTS.md`'s ordering rules in the row 5 PR. Sim's viewer events: `SpellTarget` keys on a depth-0 `ActionUsed` as well (row 6), so breath weapons still produce target tiles. As implemented in row 5, `ActionUsed` is emitted **only for non-spell actions** (`is_spell == false`), so its `is_spell` payload is 0 today; spells with a `usage` (innate, row 8) keep `SpellCast`. `RechargeRolled`, `ResourceSpent` and `ActionUsed` are live in row 5; `FeatureTriggered`, `ModifierApplied` and `ModifierExpired` are reserved and appear with rows 9 and 12. `event_stream_version` keys on `kLastRulesEventKindV6` (the last pre-v7 kind), not on `kLastRulesEventKind`, so kinds 39-44 hash as v7. `kEventKindCount` grows by 6 and
`kLastRulesEventKind` moves to `ActionUsed`. `SimEvent::Kind` is unchanged.

### 9.2 Sim event schema: **v7**, on top of #55's v6

#55 (footprint) already sets `kSimEventSchemaVersion = 6`, adds `SimEvent::Kind::Footprint` and
rewrites `event_stream_version` to return 6 on a Footprint event. This batch is therefore **v7** and
**depends on #55's v6 landing first** (or rebases onto it). Whichever PR merges second renumbers
nothing: #55 keeps v6, this batch is v7 everywhere (code, `docs/SIM_EVENTS.md` table and changelog).

**`event_stream_version` must change** (Engine Core, sim). Today it only inspects `SimEvent::Kind`
(`SpellTarget` → 5, else 4) and never looks at `rules.kind`; kinds 39..44 are *rules* kinds that sim
passes straight through (`apply_rules_events`), so the current function would hash a stream containing
`RechargeRolled` as version 4/5 and the "lowest fitting version" argument would be false. New rule: a
**max over constructs**, no early returns:

```cpp
uint32_t v = 4;
for (const auto& e : events) {
    if (e.kind == SimEvent::Kind::SpellTarget) v = std::max(v, 5u);
    if (e.kind == SimEvent::Kind::Footprint)   v = std::max(v, 6u);     // #55
    if (e.kind == SimEvent::Kind::Rules && uint8_t(e.rules.kind) > uint8_t(rules5e::EventKind::SpellCheckRolled))
        v = std::max(v, 7u);
}
return v;
```

(The Footprint branch in #55 returns early; under v7 an early `return 6` would mask a later kind-39+
event in the same stream, so #55's function is restructured into the max form by this change.) Tests:
Footprint-only stream = 6; new-rules-kind-only = 7; both = 7; neither = unchanged (4/5). Old streams
keep their digests **only if old content can never emit kinds 39+**, which is what the §8 gates
guarantee and the compat goldens verify.

**Timing (decided).** Row 4 only restructures `event_stream_version` (tested with a synthetic
`EventKind(39)`) and **leaves `kSimEventSchemaVersion` at 6**. Row 5, the PR that appends kinds 39-44,
bumps the constant to 7, per `docs/SIM_EVENTS.md` policy ("sim bumps the version and
`kLastRulesEventKind` in the same PR that surfaces it"). Row 5's same-PR pin list:
`kSimEventSchemaVersion` to 7 and `kLastRulesEventKind` to `ActionUsed`; `events_tests.cpp` (~L37
`kSimEventSchemaVersion == 6`, and the L57/L60 layout pins); the sim "event layout" test;
`event_feed_tests.cpp` (~L64 `static_assert(sim::kSimEventSchemaVersion == 6)`, and the `known_rules_kind`
"kLast+1 is unknown" tests); `docs/SIM_EVENTS.md` table, ordering rules and v7 changelog row; and the
`event_stream_version <= kSimEventSchemaVersion` invariant test.

### 9.3 What is actually hashed (rewritten against `main`)

There is **no** `EncounterState` serializer or hash in production. The digests are:

* `Combat::hash()`: seed, every event field, per-slot position/hp/dead flag, then
  `feature_uses_digest` **only if `!state_.features.empty()`**;
* `Combat::log_hash()`: the request fields listed explicitly;
* `event_stream_hash` (`events.cpp`): stream version, count, every event field.

(The rules tests' `state_hash`/`hash_state` helpers are test-local and cover a hand-picked subset.)
New `Combatant`/`EncounterState` vectors are therefore **digest-neutral unless sim folds them in**,
and the rule that keeps goldens identical is the existing `features` precedent: **fold only when
non-empty** (Q10, decided), with these obligations:

1. New sections fold in `Combat::hash()` *after* the features fold, each guarded by its own `!empty()`.
2. **One tag byte before each folded section** (e.g. `0xA1` action_uses, `0xA2` modifiers, `0xA3`
   pools, `0xA4` innate). The precedent writes a bare `u64`; with several optional bare `u64`s "pools only"
   and "modifiers only" would hash identically. The tags also keep the existing features fold unambiguous.
3. Rows are registered **at setup only for new content** (like `register_feature`); an old archetype
   must leave all new vectors empty. An old archetype registering any row would re-pin every golden.
4. Hashing of the new state is for desync detection, not golden coverage (the events already expose
   it); it is included for completeness but each section is individually droppable.
5. **`log_hash` omits `spell_id`, `spell_level`, `targets`, `multiattack`, `ally_near_target` and
   `amount`.** This proposal does *not* fix that (doing so re-pins `vertical_slice_tests.cpp`'s
   `log_hash`). New `ActionRequest` fields (`innate`, `choice`, any aim tile) are folded **only when
   non-default, under a tag byte**, so every existing log hashes as before.

### 9.4 Guards that make "no golden change" true

* **RNG draw order.** New code draws only when new content is present: recharge d6 (creatures with a
  recharge action only), modifier dice (only with a die modifier), trigger saves/extra dice (only on a
  *fired* trigger). Placement is fixed: the recharge d6 is rolled in `begin_turn` **after `TurnStarted`
  and repeat saves, before death saves** (rules `encounter.cpp` ~L497-520). A test replays every
  existing golden fight and asserts the rng position after it is unchanged.
* **`ActionRequest` / `ReactionDecision`** gain only trailing fields whose defaults equal the old behaviour.
* **`kReactionTriggerCount`** growth enlarges `reaction_policy` (not hashed by raw bytes; `Combatant`
  is not hashed at all, see 9.3); a test pins that the default policies are unchanged.
* **Result:** no existing golden (`compat_*`, fl1, vertical_slice `log_hash`, event digests) changes.

## 10. `item@1` (weapons, armor, shields; Director-approved)

`item@1` makes equipment data so Sneak Attack (`finesse`), riders and `same_as_weapon` damage have a
source and so AC can be computed (§12 row 15). **The discriminator is `category`**
(`weapon | armor | shield`, plus `valuable` appended later: gems with only the common fields, docs/CONTENT_CONVENTIONS.md
section 7.1); each arm uses the closed-union convention (§3.1). Common fields: `$schema`
(`const "item@1"`), `id` (`content_id`), `name`, `source`, `category`, `cost_cp` (integer, copper
pieces), `weight_oz` (integer, ounces; integers only), `description`.

**Weapons** reuse the v1 attack vocabulary rather than inventing new one:

| Field | Type | Notes |
|---|---|---|
| `reach` | `feet` (v1) | melee reach in feet (5, or 10 with `reach`); **absent for pure ranged weapons** |
| `range` | `[normal, long]` of `range_ft` (v1) | presence of `range` **is** what makes it ranged; a thrown dagger has both `reach: 5` and `range: [20, 60]`. There is no separate `ranged` flag |
| `damage[]` | `$defs/item_damage` items | optional; **required unless `properties` contains `special`** (a net deals no damage) |
| `versatile_damage[]` | `item_damage` items | required iff `properties` contains `versatile`; the loader emits the second Attack entry exactly as `weapon.hpp` does |
| `properties[]` | closed enum, `uniqueItems: true` | `ammunition, finesse, heavy, light, loading, reach, special, thrown, two_handed, versatile, lance` (= `weapon.hpp`'s vocabulary). **`two-handed` is rejected** (the C++ parser's alias is a legacy tolerance, not a content spelling) |
| `weapon_class` | `simple` \| `martial` | |
| `qualifiers[]` | enum `magical`, `silvered`, `adamantine`, unique | bits **magical = 1, silvered = 2, adamantine = 4** (`AttackQualifier`, `types.hpp`); loader ORs them |
| `magic_bonus` | integer 0..3 | **adds to to-hit and to damage; implies `magical`** (the loader sets bit 1); `damage[]` is **base-only** (weapon die, never the bonus or ability mod) |
| `on_hit[]` | `rider` | e.g. Flame Tongue extra damage |

```json
"item_damage": { "oneOf": [
    { "type":"object","additionalProperties":false,"required":["dice","type"],"properties":{"dice":{"$ref":"../common/v1.schema.json#/$defs/dice"},"type":{"$ref":"../common/v1.schema.json#/$defs/damage_type"}} },
    { "type":"object","additionalProperties":false,"required":["flat","type"],"properties":{"flat":{"type":"integer","minimum":0},"type":{"$ref":"../common/v1.schema.json#/$defs/damage_type"}} } ] }   // blowgun: {"flat":1,"type":"piercing"}
```

Closed arms for `item@1` (same convention as §3.1; the per-arm allowed == required ∪ optional test covers
them):

```json
"item@1": { "type":"object","additionalProperties":false,"required":["$schema","id","name","category"],
  "properties": { "$schema":{"const":"item@1"}, "id":{"$ref":"../common/v1.schema.json#/$defs/content_id"}, "name":{"type":"string"},
                  "source":{"$ref":"../common/v1.schema.json#/$defs/source"}, "category":{"enum":["weapon","armor","shield"]},
                  "cost_cp":{"type":"integer","minimum":0}, "weight_oz":{"type":"integer","minimum":0}, "description":{"type":"string"},
                  /* + every arm's fields, declared once in the base */ },
  "allOf": [
    { "if": {"required":["category"],"properties":{"category":{"const":"weapon"}}},
      "then": { "required":["weapon_class"],
                "propertyNames":{"enum":["$schema","id","name","source","category","cost_cp","weight_oz","description",
                                          "weapon_class","reach","range","damage","versatile_damage","properties","qualifiers","magic_bonus","on_hit"]},
                "allOf":[ {"if":{"required":["properties"],"properties":{"properties":{"contains":{"const":"special"}}}}, "else":{"required":["damage"]}},
                          {"if":{"required":["properties"],"properties":{"properties":{"contains":{"const":"versatile"}}}}, "then":{"required":["versatile_damage"]}, "else":{"not":{"required":["versatile_damage"]}}} ] } },
    { "if": {"required":["category"],"properties":{"category":{"const":"armor"}}},
      "then": { "required":["armor_category","base_ac","dex_bonus"],
                "propertyNames":{"enum":["$schema","id","name","source","category","cost_cp","weight_oz","description",
                                          "armor_category","base_ac","dex_bonus","dex_cap","str_min","stealth_disadvantage","magic_bonus"]},
                "allOf":[ {"if":{"required":["armor_category"],"properties":{"armor_category":{"const":"heavy"}}}, "then":{"properties":{"dex_bonus":{"const":false}}}},
                          {"if":{"required":["armor_category"],"properties":{"armor_category":{"const":"medium"}}}, "then":{"required":["dex_cap"]}, "else":{"not":{"required":["dex_cap"]}}} ] } },
    { "if": {"required":["category"],"properties":{"category":{"const":"shield"}}},
      "then": { "propertyNames":{"enum":["$schema","id","name","source","category","cost_cp","weight_oz","description","ac_bonus","magic_bonus"]} } } ] }
```

Validator-only checks (cross-field, not expressible in the schema): `range[1] >= range[0]`; `reach` is `10`
**iff** `properties` includes `reach`; `magic_bonus` and `qualifiers` consistency (§8.3 items 15).

**Armor** (`category: "armor"`; the AC formula inputs):

| Field | Type | Notes |
|---|---|---|
| `armor_category` | `light` \| `medium` \| `heavy` | **renamed from `armor_class`** (that name is the stat block's AC object) |
| `base_ac` | integer | |
| `dex_bonus` | boolean | **`if armor_category = heavy then dex_bonus = false`** |
| `dex_cap` | integer | **required iff `armor_category = medium`** (2); forbidden otherwise |
| `str_min` | integer, default 0 | Str shortfall reduces speed by 10 ft (rules, in the AC PR) |
| `stealth_disadvantage` | boolean | |
| `magic_bonus` | integer 0..3 | adds to AC |

AC = `base_ac` + (`dex_bonus` ? min(Dex mod, `dex_cap` if present) : 0) + `magic_bonus`.

#### 10.1 Row 15 as implemented (rules side) and hand-off

`rules5e/armor.hpp` + `src/armor.cpp`; pure, no loader or `item@1` dependency, no schema change, no sim / `content.cpp` /
`Combat::create` change, goldens unchanged (nothing calls it yet).

- `ArmorPiece` (category, base_ac, dex_bonus, has_dex_cap/dex_cap, str_min, stealth_disadvantage, magic_bonus) and `ShieldPiece`
  (ac_bonus, magic_bonus) mirror the `item@1` fields above; `AcInput` adds the ability scores, optional armor and shield, the
  unarmored formula (`unarmored_base` 10, optional `alt_ability` for Unarmored Defense), natural armor (`natural_base`,
  `natural_dex_bonus`), a `flat_bonus` and `base_speed_ft`.
- `compute_ac(in) -> AcResult {ac, speed_ft, str_shortfall, stealth_disadvantage}`: armor worn: `base_ac` + Dex term (none for
  heavy, capped by `dex_cap`, a negative Dex is never capped away) + `magic_bonus`; no armor: the best of 10 + Dex,
  10 + Dex + `alt_ability` and natural armor; a shield adds `ac_bonus + magic_bonus` to either; then `flat_bonus`; clamped
  to 0..100. Str below `str_min` takes 10 ft off `base_speed_ft` (never below 0).
- Hand-off (Engine Core / T&C): (a) at archetype build, only when the character lists an equipped `{item, equipped:true}` armor
  or shield, fill `AcInput` (item fields 1:1, scores from the stat block, `alt_ability` from a barbarian / monk feature,
  `natural_base` from a monster) and store `AcResult::ac` into `Combatant::ac` and `speed_ft` into `speed_ft`; every other
  stat block keeps its baked `armor_class.value`. (b) Surface `stealth_disadvantage` to the Stealth check path if wanted (rules
  do not yet read it). (c) A Monk's Unarmored Defense must not pass a shield (the caller decides); Mage Armor is a modifier
  (row 12 `ac`), not an input here.

**Shield** (`category: "shield"`): `ac_bonus` (integer, default 2) and `magic_bonus`. No
`stealth_disadvantage` (shields never impose it in the SRD).

`character@4 equipment` becomes `array<string | {item: content_id, equipped?: bool}>` (the bare string
is the descriptive `@3` entry). Migration `3→4` adds `"equipment"` to `inert_mechanics` when the
migrated list is non-empty, so fl1's equipment list stays **inert**, and `armor_class` (baked AC) stays
authoritative for every `@≤3` file. At `@4` the AC recompute (§12 row 15) applies **only** when the character
declares an equipped `armor` or `shield` *item object* (`{item, equipped: true}`); otherwise
`armor_class.value` is used as written. Reference resolution is in §8.3.

## 11. Test plan

Rules (mine; each PR carries its own):

| Area | Cases |
|---|---|
| Save actions | breath weapon: half/none/conditions, repeat save, non-magical vs Magic Resistance (#54), no SpellCast window, recharge 5-6 sequence (seeded), per-day depletion + `recover_features`, rng untouched when absent |
| Triggers | Sneak Attack once/turn (advantage, ally adjacent, neither), crit doubles rider dice, Rampage chain stops at bonus attack budget, Undead Fortitude (radiant, crit, DC), Death Burst area targets from caller list, Divine Smite ask/auto/never and slot spend, predicate vocabulary |
| Reactions | Parry/Protection/Uncanny Dodge/Hellish Rebuke windows, one reaction per round, policies, no double OA, nested windows (`allow_chain`) |
| Innate casting | at-will, per-day use counting, no components, DC override, rejected upcast |
| Modifiers | Bless d4 draws, Haste/Slow speed, expiry at turn boundaries, concentration drop, stacking rule |
| Pools | spend/refuse, recharge by kind, Lay on Hands amount, grant_action |
| Events | layout/enum value pins, event schema **v7** roundtrip, `event_stream_version` max-over-constructs cases (Footprint-only 6, new-kind-only 7, both 7, neither unchanged), `known_rules_kind` tests |
| Hashing | tagged fold only when non-empty (pools-only vs modifiers-only differ), old archetype leaves all new vectors empty, `log_hash` unchanged for old logs, `vertical_slice` pin unchanged |
| Synthetic spells | derived id, duplicate detection, `fixed_dc`, per-archetype override rollback, caster without `spellcasting` block |
| Replay | every new decision kind round-trips through the action log and replays identically |

Tools & Content: **common@2 lint** (no stray local `$ref`, no byte-identical re-declared defs), migrated-`@3`-fixtures-validate-as-`@4`, schema positive/negative per `$defs` (unknown predicate, missing `on_success`,
`grapple` rejected, `cost: reaction` at `@3` rejected, `resources` at `@3` rejected), migration
tests (idempotence, marker contents, key order), `validate_packs.py` across `tests/compat/fl*`,
`compat_freeze` unaffected (no frozen file edited).

Engine Core: loader tests (archetype fields compile; `inert_*` lists), registry refusal of
new kinds from `@≤3`, area-template targeting (R-G10), `moved_ft`/ally-adjacency facts,
digests: `compat_*` goldens and the existing `fl1` golden **unchanged** (CI proves §9).

## 12. Implementation order (PRs, owners, dependencies)

**Gate (not part of this batch):** #55 (footprint, event schema v6) lands first; #58/#59 (content
wiring: `common/v1.schema.json`, `validate_packs.py`, `content.cpp`/`features.cpp`/`driver.cpp`) land
before row 2, otherwise `common@2` would fork stale v1 defs and rows 6, 8 and 10 would conflict.
Everything below is event schema **v7**.

| # | PR | Owner | Depends on | Notes |
|---|---|---|---|---|
| 1 | **Sim facts: `ally_near_target` and `move_tiles`.** `ally_near_target`: same-team, conscious, non-incapacitated creature other than the attacker within 1 tile of the target footprint, set in `do_rules` (Attack branch) and `hooks.refresh_ready` | **Engine Core** | none (can land now) | Supplies the fact #54 expects. Sim does not load `roll_traits` from content yet, so this changes nothing observable today (golden-neutral); it becomes observable when a loader or trait uses it (Sneak Attack, rows 9/10). `move_tiles` (appended `ActionRequest` field, default 0): set in `do_step` (1 per Step) and the readied-move path (path length). Both stay out of `log_hash` (§9.3) |
| 2 | `common@2` (changed defs only, absolute refs to v1), `monster@4`/`character@4`, lint test, per-tag validator table, cross-ref checks (§8.3), `3→4` migrations (§8.2), the `api_level >= 2` validator rule for `@4` packs (§8.5; the manifest enum itself ships in 16a), decorated compat fixtures (§8.4) | **T&C** | #58, #59, **16a** | No behaviour; golden-neutral by construction. **Carries the validator code and negative cases for authored `inert` on `reactions[]` and `bonus_actions[]`** (those arms only exist from the `@4` schemas in this row; at `@≤3` the closed v1 `action` already rejects an authored `inert`) |
| 3 | Loader: accept `@4`, **`declared.version` plumbing** into `ContentLibrary::add()` and `FeatureBuildCtx`, `reactions`/`bonus_actions` markers, registry "list as inert" rule (§8.3), `forward_compat` listing | **Engine Core** | 2 | Public `add()` signature: T&C signs off |
| 4 | `event_stream_version` restructured into the max-over-constructs form (§9.2), including the `rules.kind > SpellCheckRolled` → 7 branch; tests use a **synthetic `EventKind(39)`** event (Footprint-only 6 / new-kind-only 7 / both 7 / neither unchanged). **`kSimEventSchemaVersion` stays 6** in this PR and no `<= kSimEventSchemaVersion` invariant is added here | **Engine Core** | #55 | Digest-neutral: the digest uses the lowest fitting version |
| 5 | rules5e foundation: `usage`, `ActionUse`, `ResourcePool`, recharge roll, `moved_ft` (rules half: adds 5 x `move_tiles` on the actor's own turn, **only when the move completes**, i.e. when `Moved` is emitted; an OA-interrupted move does not count), `SpellBook::erase`/rebuild support (§2.3), `ReactionOption::uses_reaction` (appended, default true), `SpellDef::{is_spell, magical, fixed_dc, fixed_attack}`, kinds 39-44. **This PR bumps `kSimEventSchemaVersion` to 7** together with `kLastRulesEventKind`, SIM_EVENTS.md (v7 table/changelog row and the `ActionUsed` ordering), the pins in `events_tests.cpp` (~L37 `kSimEventSchemaVersion == 6`, plus the L57/L60 layout pins) and `event_feed_tests.cpp` (~L64 `static_assert(... == 6)`, plus `known_rules_kind`), and adds the `event_stream_version <= kSimEventSchemaVersion` invariant (SIM_EVENTS.md policy: bump in the PR that surfaces the kinds) | **Rules** | 4, #55 | Tagged `Combat::hash` folds are part of the sim side of this PR (Engine Core co-reviews) |
| 6 | **Save actions, caller-supplied targets (Q8), single-target only for AI**: synthetic-spell compile in `ContentLibrary` (§2.3), `spells_known` append; **`Combat::complete` also emits `SpellTarget` for a depth-0 `ActionUsed` by the actor** (today it keys on a depth-0 `SpellCast`; non-spell save actions emit `ActionUsed` instead, §9.1; `docs/SIM_EVENTS.md`'s "`SpellTarget` (v5): depth-0 `SpellCast`" wording is updated in this PR; no new `SimEvent` kind, so no version change); AI offers save actions **without an `area`** on top of #59's multiattack ordering (save action if worthwhile, else multiattack, else single attack) | **Engine Core** (compile + AI), Rules for any `legal_actions` change | 3, 5 | AI reads `state_` (expended or not), never rolls. Interim: single-target save actions only (no geometry exists before row 7); area actions are usable by explicit callers/tests with listed targets but the built-in AI does not choose them yet |
| 7 | **Area templates (R-G10)**: aim input via `Command::to` for `CastSpell` (already in `LogEntry`/`log_hash`), geometry over `footprint_tiles_at`, LOS/cover from origin, opt-in (derive targets only when an aim tile is given and `targets` is empty), `spell_in_reach` fix for self-origin | **Engine Core** | #55, 6 | Later PR (Q8); also serves `area_save` riders. **Area-action AI moves here**: use when >= 2 hostile targets are inside the template (or 1 if no attack is legal), never include allies |
| 8 | Innate/slot monster spellcasting, DC/attack overrides, `ActionRequest::innate` | **Rules** + **Engine Core** (archetype) | 3 | |
| 9 | Triggers/riders in rules: `triggers.hpp`, hook points, `FeatureTriggered`, `HitRider` reaction trigger | **Rules** | 4, 5 | AI default: `ask` riders **decline** unless policy/content opts in |
| 10 | Rider/`on_event` loader (`on_hit[]`, effects) | **Engine Core** (loader), schema in row 2 | 1, 3, 9 | |
| 11 | Reactions: new triggers/options, hooks, `reactions[]` compile | **Rules** then **Engine Core** (hooks + deterministic default order by content order, honouring `uses_reaction`; a free `HitRider` is never option 0 ahead of a real reaction). **Default policy:** `IActorPolicy::decide_reaction` today answers `{react=true, option 0}` for every window; the new default **declines `HitRider` windows** and keeps option 0 for every existing trigger (so existing goldens, e.g. the vertical_slice reaction count, are unchanged); `ReactionOption::uses_reaction` is a new appended field defaulting to `true` | 3, 9 | `RollAttack` window deferred (Q6) |
| 12 | Timed modifiers (+ `spell@1` `apply_modifier`, additive) | **Rules**; **T&C** adds the spell kind to validation | 5 | Spell side can ship before row 2 |
| 13 | Class pools + feature kinds (`grant_action` via `TurnBudget::extra_actions`, `heal_pool` reusing existing `ActionRequest::amount`, pool usage), Legendary Resistance as a pool (**declaration-only until this row** unless a hook is added earlier: the pool and `usage` exist in the schema from row 2, but nothing spends it before row 13) | **Rules** + **Engine Core** (FeatureRegistry hooks) | 5, 9, 12 | |
| 14 | `item@1` schema + loader (weapons, armor, shields) | **T&C** (schema/validator), **Engine Core** (loader) | 2, 3 | |
| 15 | **AC recompute from equipped armor/shield** (`rules5e::compute_ac`, `character@4 equipment` objects; fl1's baked AC and equipment list stay inert) | **Rules** (formula, Str-shortfall speed) + **Engine Core** (apply at archetype build) | 14 | Own PR; goldens unaffected because only `{item, equipped}` objects opt in |
| 16a | **API level plumbing** (no dependencies): `kMaxApiLevel = 2`; per-pack levels (`level <= host_max`, replacing the equality check in `load_pack_scripts`, `script_host.cpp` ~L532); a **minimal non-empty `engine.level2.d.luau`** (§8.5: defs are a superset of level 1, `api_definitions(N)` concatenates levels 1..N), embedded alongside level 1 in `engine/core/script_host/CMakeLists.txt` (which today embeds only `engine.level1.d.luau` via `tools/embed/embed_file.cmake`: add a second `add_custom_command` / `SYMBOL=kEngineLevel2Defs`); C++ test updates in `script_host_tests.cpp`: `typecheck(2, ...)` is asserted non-empty as "unknown level" (L206) and the `api_level: 2` pack is expected to fail (L220-223), so **both move to level 3**; **new** positive cases: `host(2)` loading a level-1 pack and a level-2 pack, a **typecheck test that a level-2 pack calling a level-1 API passes**, and the `PackScripts::pack_levels` test (§8.5); the `manifest@1` enum `[1, 2]` and its description; `run_negative.py` cases for levels 1, 2 and 3 (reject) | **T&C** / script_host owner | none | Row 2 depends on it (validator rule "`@4` packs need `api_level >= 2`") |
| 16b | **Level-2 API content**: reaction/resource/modifier queries, level-1 event-feed filtering of kinds above `SpellCheckRolled` | **T&C** / script_host owner, **Engine Core** (feed) | 4, 5, 16a | Level 1 observably unchanged |
| 17 | SRD content passes (breath weapons, riders, innate lists, PC features) | **T&C** | 6, 8, 10, 11, 13, 15 | Re-authors descriptive content; separate PRs per tier |

One PR per row, from fresh `main`, normal pushes, full suite on GCC Debug and Clang Release, CI green
before review. Engine Core offered rows 1, 3, 4 and 7; they are recorded above as theirs.

## 13. Decisions log

All questions are **DECIDED** (Director, 2026-10-18; Engine Core and T&C review folded in). Entries
marked "(Director)" are explicit rulings; "(adopted)" are this proposal's recommendations accepted by
the Director's instruction.

| Q | Decision | Status |
|---|---|---|
| Q1 | Save actions reuse the spell resolver via synthetic `SpellDef`s (`is_spell=false`, `magical=false`, `fixed_dc`; id = `spell_id_of("<archetype>#<action>")`, duplicates are a load error; per-archetype compile; §2.3) | DECIDED (Director; design corrected by Engine Core review) |
| Q2 | Pools and trait/feature-side modifiers wait for `@4` (one bump, no interim kinds); spell-side `apply_modifier` stays additive | DECIDED (Director) |
| Q3 | Divine Smite is a post-hit reaction-window option (free `HitRider`); sim default declines unless opted in | DECIDED (Director) |
| Q4 | Charge/Pounce use "moved at least N ft this turn"; `moved_ft` is a rules-side counter fed by sim's appended `ActionRequest::move_tiles` (tiles x 5; rules cannot derive tiles from the cost-valued `move_ft`); no path history; limitation documented in §3.1 | DECIDED (Director) |
| Q5 | `drop_to_zero` triggers run before the regeneration-down check | DECIDED (adopted) |
| Q6 | Roll-modifying reactions (`RollAttack`/`RollSave`, `subtract_roll`) deferred; `subtract_roll` reserved | DECIDED (adopted) |
| Q7 | Extra actions via `TurnBudget::extra_actions` (appended, default 0) | DECIDED (adopted) |
| Q8 | Save actions ship first with caller-supplied targets; area templates are a later Engine Core PR (needs an aim input, built on #55 footprint code) | DECIDED (Director) |
| Q9 | One generic `FeatureTriggered`; **dedicated `ActionUsed`** rules event (kind 44) for save actions instead of a flag bit | DECIDED (adopted; revised per Engine Core) |
| Q10 | New state is folded into `Combat::hash()` **only when non-empty, one tag byte per section**; no `EncounterState` hash exists; new `ActionRequest` fields fold into `log_hash` only when non-default (§9.3) | DECIDED (Director; mechanism corrected by Engine Core review) |
| Q11 | `common@2` re-declares **only the changed defs** and references the rest absolutely into `common@1`; lint test enforces it (§1.2) | DECIDED (adopted; revised per T&C) |
| Q12 | Inert-marking of new-kind effects in old documents (frozen literal list) plus a registry rule that **lists as inert and skips**, never a load error; needs `declared.version` plumbing (Engine Core PR, row 3) | DECIDED (Director; wording per Engine Core) |
| Q13 | `spellcasting.dc` / `attack_bonus` overrides win over derived values | DECIDED (adopted) |
| Q14 | **Legendary Resistance is in the bump** as a plain `resources[]` pool; only legendary *actions* are deferred (T&C-G11). `legendary_actions` stays descriptive/ignored in `@4` | DECIDED (Director) |
| Q15 | `ac` modifiers are `bonus`-only (no `set`); base-AC formulas come from `item@1` armor (row 15) | DECIDED (adopted) |
| Q16 | **Dropped from this batch.** No rest model: sim never calls `recover_features`; uses/pools/innate/slots start full each encounter. A rest model needs persistent party state and is its own design | DECIDED (Engine Core review) |
| D17 | Event schema **v7** on top of #55's v6; `event_stream_version` becomes max-over-constructs (§9.2) | DECIDED (Director) |
| D18 | `item@1` covers weapons, armor and shields; AC recompute is its own PR (row 15); fl1's baked AC and equipment list stay inert | DECIDED (Director) |
| D19 | Elevation forward-compat flag (`forward_compat: ["elevation"]`, inert) in `@4` | DECIDED (Director) |
| D20 | Luau API level 2 and `manifest@1.api_level` `enum [1, 2]`; `@4`/`item@1` packs require level 2 | DECIDED (Director) |
| D21 | Engine Core owns: `ally_near_target`, `declared.version` plumbing, `event_stream_version`, area-template targets | DECIDED (Engine Core offer, recorded in §12) |
| D24 | Level tables: `resource.max.table` (third `oneOf` arm) and `modifier` `bonus_table`, step function resolved by the loader to an integer, `unlimited` flag for pools (§7.1; adopts T&C's `rage_table_proposal`) | DECIDED (Director) |
| D22 | Multiattack's content shape is PR #59's `multiattack` feature effect; `@4` carries it unchanged and adds no stat-block field | DECIDED (adopted; follows #59) |
| D23 | Mixed API levels: host = max, per-pack levels, level-1 packs load under a level-2 host (§8.5) | DECIDED (adopted) |

## 14. What to ask reviewers (rev 2)

**Tools & Content:** (a) §1.2/§1.3 (absolute refs, lint test, `@3`-carry-over incl. `inert_mechanics`
and `forward_compat`), (b) the `oneOf` of mechanical vs `legacy_action` for `reactions[]` /
`bonus_actions[]` and the per-entry stamping (§4.1, §8.2), (c) the closed `if/then` unions (§2.2, §4.1,
§6.1), (d) the validator cross-reference list (§8.3), (e) `item@1` field names (§10), (f) the
`manifest@1` `api_level` enum change and the level-2 rule (§8.5).

**Engine Core:** (a) rows 1, 3, 4, 7 as yours; (b) the `event_stream_version` max form (§9.2) and the
hash rules (§9.3: tagged folds, `log_hash` stays unchanged for old logs); (c) the synthetic-spell
compile (§2.3 points 1-4); (d) AI defaults (§12 rows 6, 7, 9, 11), `move_tiles` in row 1; (e) the declared-version plumbing and the
public `add()` signature change (§8.3).
