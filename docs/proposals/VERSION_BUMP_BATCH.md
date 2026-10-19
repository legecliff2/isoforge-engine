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
they are a schema error until R-G8):

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
//                                         Damaged, AllyAttacked, HitRider /*, RollAttack (phase 2)*/ };
// kReactionTriggerCount = 7  -> Combatant::reaction_policy grows; defaults stay Ask
// Combatant (appended): std::vector<Reaction> reactions;   // compiled from content, id/trigger/effect/usage
```

Options come from the existing hooks: sim's `being_hit_options` / `spell_cast_options` read
`Combatant::reactions`; add `damaged_options` and `ally_attacked_options` to `EncounterHooks`
(appended, empty = none). Eligibility for `ally_attacked` uses the existing `in_range` hook
(Engine Core supplies adjacency). Opportunity attacks keep their current separate path; a
`leaving_reach` reaction with `effect.kind: "attack"` is the same thing and is **not** offered in
addition (deduplicated by `attack_id`), so a creature never gets two OAs.

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
(`weapon | armor | shield`); each arm uses the closed-union convention (§3.1). Common fields: `$schema`
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
