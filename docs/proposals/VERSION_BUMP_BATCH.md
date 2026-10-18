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
| `common@2` (new file) | **Only the defs that change**, re-declared locally: `action` (v2), `reaction`, `on_event_effect`, `rider`, `predicate`, `scaling`, `usage`, `area`, `resource`, `modifier`, `damage` (new named def, §1.2), `spellcasting` (v2, superset of v1), `trait` / `trait_active` / `effect_cost` (v2), `stat_block` (v2) | `common@1 $defs/action` is `additionalProperties:false`; `reactions`/`bonus_actions` exist in v1 but are descriptive; widening v1 would let `@3` files carry fields the `@3` contract says are inert |
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

* Every `$ref` to an unchanged def is `../common/v1.schema.json#/$defs/<name>`. A **lint test**
  (`tests/validate/`, runs in `validate_packs`/ctest) fails on any *local* `#/$defs/x` ref whose
  target is not declared in the same file, and on any re-declared def that is byte-identical to its v1
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
`@4` schemas accept an optional top-level `forward_compat: array<enum ["elevation"]>`, **descriptive
and ignored by sim** (listed with the inert mechanics in the archetype report). `area` stays 2D until
the engine implements elevation; `cylinder.height_ft` is validated but unused by targeting. When
elevation lands, sim honours the flag per document, so packs that declare it never change behaviour
for packs that do not.

## 2. Save-based actions with area templates (T&C-G4, R-G10)

### 2.1 Content shape (`common@2 $defs/action`, new kind `save_effect`)

`common@2 $defs/action.kind` is a **strict superset of v1**:
`melee_weapon_attack`, `ranged_weapon_attack`, `melee_spell_attack`, `ranged_spell_attack`,
`special` (all unchanged in shape and required-field rules: `_attack` kinds require `to_hit` and
`damage`, `melee_*` requires `reach`, `ranged_*` requires `range`), plus the new **`save_effect`**.
There is no `multiattack` action kind in v1 or v2. Multiattack remains sim's/rules'
**ordered sequence of attack refs** (each ref is an attack `id` from `actions[]`, in order, repeats
allowed; e.g. `["bite","claw","claw"]`), and `@4` does not change how it is expressed. `save_effect` adds:

| Field | Type | Req | Notes |
|---|---|---|---|
| `kind` | `"save_effect"` | y | breath weapons, Frightful Presence, Turn Undead, Stunning-Strike-style actions |
| `id`, `name`, `description` | as v1 | y/y/n | `id` is the `local_id` used by AI/UI and in the synthetic spell id (§2.3) |
| `save` | `{ability, dc}` | y | `dc` integer 1..30; no derived DCs for actions (SRD blocks print them). `ability` is `$defs/ability` |
| `area` | `$defs/area` | n | absent = single target (`targets: 1`, `range_ft`) |
| `range_ft` | integer | n | single-target range; ignored when `area.origin` is `self` |
| `damage[]` | `$defs/damage` items | n | at least one of `damage[]`/`apply[]` required |
| `on_success` | `"half"` \| `"none"` | when `damage[]` | |
| `apply[]` | `[{condition, duration?, repeat_save?}]` | n | maps to `SpellEffectKind::ApplyCondition`; `duration`: `{kind:"rounds",n}` \| `{kind:"until_save"}` \| `{kind:"until_end_of_target_next_turn"}`; `repeat_save`: `"start_of_turn"` \| `"end_of_turn"` \| `"on_damage"` |
| `usage` | `$defs/usage` | n | default at-will (§2.2) |
| `magical` | boolean | n | default **false**. Breath weapons and Frightful Presence are not spells; this is what Magic Resistance keys on |

`$defs/area` (same vocabulary as the `spell@1` inline `area`, duplicated rather than shared so
`spell@1` is untouched):

```json
"area": { "type": "object", "additionalProperties": false,
  "required": ["shape", "size_ft"],
  "properties": {
    "shape":   { "enum": ["cone", "line", "sphere", "cube", "cylinder"] },
    "size_ft": { "type": "integer", "minimum": 5 },      // cone length / line length / radius / edge
    "width_ft":{ "type": "integer", "minimum": 5 },      // line only (default 5)
    "origin":  { "enum": ["self", "point"], "default": "self" },
    "range_ft":{ "type": "integer", "minimum": 0 } } }   // point origin only
```

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
"usage": { "type":"object", "required":["kind"],
  "properties": { "kind": {"enum":["at_will","recharge","per_day","per_rest","pool"]},
                  "min": {"enum":[2,3,4,5,6]}, "uses": {"type":"integer","minimum":1,"maximum":9},
                  "rest": {"enum":["short","long"]}, "pool": {"$ref":"#/$defs/local_id"},
                  "cost": {"type":"integer","minimum":1} },
  "additionalProperties": false,
  "allOf": [
    { "if": {"properties":{"kind":{"const":"recharge"}}},  "then": {"required":["min"],  "not":{"anyOf":[{"required":["uses"]},{"required":["rest"]},{"required":["pool"]},{"required":["cost"]}]}} },
    { "if": {"properties":{"kind":{"const":"per_day"}}},   "then": {"required":["uses"], "not":{"anyOf":[{"required":["min"]},{"required":["rest"]},{"required":["pool"]},{"required":["cost"]}]}} },
    { "if": {"properties":{"kind":{"const":"per_rest"}}},  "then": {"required":["uses","rest"], "not":{"anyOf":[{"required":["min"]},{"required":["pool"]},{"required":["cost"]}]}} },
    { "if": {"properties":{"kind":{"const":"pool"}}},      "then": {"required":["pool"], "not":{"anyOf":[{"required":["min"]},{"required":["uses"]},{"required":["rest"]}]}} },
    { "if": {"properties":{"kind":{"const":"at_will"}}},   "then": {"not":{"anyOf":[{"required":["min"]},{"required":["uses"]},{"required":["rest"]},{"required":["pool"]},{"required":["cost"]}]}} } ] }
```

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
on character `attacks`): effects that happen *because this attack hit* (Grapple on hit, Poison
bite save, Pounce prone, Stunning Strike-style save_condition).

**(b) Event effects**: effect `kind: "on_event"` (renamed from "trigger" so it cannot be confused with
the reaction `window` of §4 or `ReadiedTrigger`) in `traits[]` / `features[]` (character `features[]`
carry the same effect object): effects that happen on an *event*, independent of one action
(Sneak Attack, Rampage, Undead Fortitude, Death Burst, Divine Smite).

```json
"$defs/on_event_effect": { "type":"object", "additionalProperties": false,
  "required": ["on", "effects"],
  "properties": {
    "on": { "enum": ["hit", "crit", "damage_dealt", "reduce_to_zero", "drop_to_zero", "death", "turn_start", "turn_end"] },
    "when": { "type":"array", "items": {"$ref":"#/$defs/predicate"} },
    "limit": { "enum": ["once_per_turn","once_per_round","once_per_attack","none"], "default": "none" },
    "choice": { "enum": ["auto","ask"], "default": "auto" },   // Divine Smite: "ask"
    "usage": { "$ref": "#/$defs/usage" },
    "effects": { "type":"array", "minItems":1, "items": {"$ref":"#/$defs/rider"} } } }
```

`on` meanings (all evaluated by rules5e; `self` = the trait's owner):

| `on` | Fires when | Notes |
|---|---|---|
| `hit` / `crit` | owner's attack hits / crits | `limit: once_per_turn` for Sneak Attack. Extra dice are rolled with the attack's damage so a crit doubles them |
| `damage_dealt` | owner damages a target | after resistances; `types_dealt` available (#56) |
| `reduce_to_zero` | owner's damage drops a creature to 0 | Rampage |
| `drop_to_zero` | owner would drop to 0 | *before* it applies: Undead Fortitude, Relentless Endurance |
| `death` | owner dies | after `CombatantDied`: Death Burst |
| `turn_start`/`turn_end` | owner's turn | regeneration-style, recharge, aura ticks |

`$defs/predicate` (closed vocabulary; unknown predicate = validation error at `@4`):

| Predicate | Fields | Used by |
|---|---|---|
| `weapon` | `{melee|ranged|finesse}` (weapon `properties` from #53) | Sneak Attack (finesse or ranged) |
| `advantage_or_ally_adjacent` | none (attack had advantage, or a non-incapacitated ally within 5 ft of target and no disadvantage) | Sneak Attack |
| `target_creature_type` / `not_creature_type` | creature type list | smites vs undead/fiends |
| `damage_type_not` / `damage_type_is` | damage types | Undead Fortitude (not radiant) |
| `not_crit` | none | Undead Fortitude |
| `moved_at_least_ft` | `ft` (counted by rules as tiles x 5, §3.2) | Charge, Pounce (20 ft). Approximation, see below (Q4 decided) |
| `self_hp_below` | `{num, den}` | Rampage-style gates |
| `save` | `{ability, dc, dc_from_damage?: true}` | Undead Fortitude: Con save DC `5 + damage taken` (`dc_from_damage`) |

`$defs/rider` (effects, closed set of kinds):

| `kind` | Fields | Rules mapping |
|---|---|---|
| `extra_damage` | `damage[]` (`$defs/damage`, type may be `rider_damage_type`), `scale?: $defs/scaling` | added to the attack's damage roll (crit doubles) |
| `save_condition` | `save`, `apply[]` (as §2.1), `damage[]?`, `on_success` | `ActiveSpellEffect`, same as §2 |
| `apply_condition` | `apply[]` (no save) | prone on hit etc. |
| `heal_self` / `temp_hp` | `dice` or `amount` | |
| `set_hp` | `{value}` (e.g. 1) | Undead Fortitude: cancels the drop to 0 |
| `bonus_attack` | `{attack_id}` | Rampage: a bonus-action attack granted to `TurnBudget` |
| `area_save` | `$defs/area` + `save` + `damage[]` + `on_success` | Death Burst (centered on owner) |
| `spend` | `{pool, cost}` or `{slot: "any"|level}` | Divine Smite spends a slot, scaling `extra_damage` by slot level (`scaling.per_slot_level`) |
| `grapple`, `swallow` | reserved | **rejected by the validator** until R-G8 (conditions with a source) lands |

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
`ft` feet this turn (`TurnBudget::moved_ft`, rules-side, tiles x 5) before the attack". There is no path history, so the
SRD requirement that the movement be *in a straight line toward the target* is **not** enforced: a
creature that walks 20 ft sideways or away and then attacks still qualifies. This is slightly
generous to the attacker. If it matters later, a sim-supplied `charged_straight` fact can be added
as a new predicate without changing this one.

`$defs/scaling` (one shape for Sneak Attack dice, Divine Smite slots and any future scaling):

```json
"scaling": { "type":"object", "additionalProperties": false, "minProperties": 1, "properties": {
  "per_levels":     {"type":"object","additionalProperties":false,"required":["every","add_dice"],
                     "properties":{"class":{"$ref":"#/$defs/local_id"},"every":{"type":"integer","minimum":1},
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
// moved_ft is RULES-side: incremented by Move in rules5e as (tiles moved x 5). NOT move_ft, which sim
// passes from grid_.move_cost_ft and doubles on difficult terrain. Out-of-turn readied moves are not counted.
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
"bonus_actions": { "type":"array", "items": { "oneOf": [
    { "$ref": "#/$defs/action" },                      // mechanical: any v2 action kind except "special"
    { "$ref": "#/$defs/legacy_action" } ] } }
"legacy_action": { "allOf": [ {"$ref":"../common/v1.schema.json#/$defs/action"} ],
                   "required": ["inert"], "properties": { "inert": {"const": true} } }
```

(`legacy_action` is v1's action plus the marker; v1's closed `properties` get `inert` added by
re-declaring the def in `common@2`, since a plain `allOf` over a closed v1 def would reject it.
The two arms are mutually exclusive: `reaction` requires `window`/`effect` and forbids `inert`;
`legacy_action` requires `inert`.)

* **Migration stamps `inert: true` per entry** (not just a file-level marker): every `reactions[]`
  entry, and every `bonus_actions[]` entry **that `@4` would load**, meaning `kind` other than
  `special` (a `special` bonus action stays descriptive at `@4` exactly as it is at `@3`, so it is
  neither loaded nor stamped). The file-level `inert_mechanics` list also gets `"reactions"` /
  `"bonus_actions"` when at least one entry was stamped (§8.2), so reporting stays uniform.
* `cost: "reaction"` on trait/feature effects becomes legal at `@4`. This is a **`common@2
  effect_cost` change** (v1's enum documents `reaction` as reserved; v2's enum is the same three
  values but the v2 validator row *accepts* it, where the v1 row rejects it for `@≤3` files).

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

"reaction_effect": { "type":"object", "required":["kind"], "additionalProperties": false,
  "properties": { "kind": {"enum":["ac_bonus","reduce_damage","impose_disadvantage","retaliate","attack","cast","subtract_roll"]},
                  "bonus":{"type":"integer"}, "fraction":{"const":"half"}, "dice":{"$ref":"../common/v1.schema.json#/$defs/dice"},
                  "damage":{"type":"array","minItems":1,"items":{"$ref":"#/$defs/damage"}},
                  "save":{"$ref":"#/$defs/save"}, "on_success":{"enum":["half","none"]},
                  "attack_id":{"$ref":"../common/v1.schema.json#/$defs/local_id"},
                  "spell":{"$ref":"../common/v1.schema.json#/$defs/content_id"} },
  "allOf": [
    { "if": {"properties":{"kind":{"const":"ac_bonus"}}},            "then": {"required":["bonus"], "not":{"anyOf":[{"required":["fraction"]},{"required":["dice"]},{"required":["damage"]},{"required":["attack_id"]},{"required":["spell"]}]}} },   // Parry, Shield
    { "if": {"properties":{"kind":{"const":"reduce_damage"}}},       "then": {"oneOf":[{"required":["fraction"]},{"required":["dice"]}]} },                                                                                  // Uncanny Dodge, Deflect Missiles
    { "if": {"properties":{"kind":{"const":"impose_disadvantage"}}}, "then": {"not":{"anyOf":[{"required":["bonus"]},{"required":["fraction"]},{"required":["dice"]},{"required":["damage"]},{"required":["attack_id"]},{"required":["spell"]}]}} },  // Protection
    { "if": {"properties":{"kind":{"const":"retaliate"}}},           "then": {"required":["damage"]} },                                                                                                                       // Hellish Rebuke (save optional)
    { "if": {"properties":{"kind":{"const":"attack"}}},              "then": {"required":["attack_id"]} },                                                                                                                    // Riposte
    { "if": {"properties":{"kind":{"const":"cast"}}},                "then": {"required":["spell"]} },                                                                                                                        // monster casts a listed spell
    { "if": {"properties":{"kind":{"const":"subtract_roll"}}},       "then": {"required":["dice"]} } ] }                                                                                                                      // Cutting Words: reserved (Q6)
```

(`save` is a shared `{ability, dc}` def, the same object as `save_effect.save`.)
The trigger field of v1-style prose is renamed **`window`** so it is never confused with
`on_event` effects (§3) or the rules `ReadiedTrigger`. All reactions share the one-reaction-per-round
budget (`Combatant::reaction_used`, unchanged).

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

## 6. Timed buffs and debuffs (R-G3)

### 6.1 Model

A *modifier* is a timed, sourced change to one number or roll. It generalizes `RollTrait` (#54)
and the `ac_bonus` spell effect (instantaneous reaction today). **Integers only** (no float
`multiply`): speed scaling is `speed_scale: "half" | "double"` or a `{num, den}` pair.

```json
"$defs/modifier": { "type":"object", "additionalProperties": false, "required": ["stat","duration"],
  "properties": {
    "stat": { "enum": ["attack_roll","save","ability_check","ac","speed","damage","temp_hp",
                       "resistance","attacks_against","advantage_on","disadvantage_on"] },
    "bonus": {"type":"integer"},
    "die":   {"$ref":"../common/v1.schema.json#/$defs/dice"},                 // Bless: stat attack_roll/save, die 1d4
    "speed_scale": { "oneOf": [ {"enum":["half","double"]},
                    {"type":"object","additionalProperties":false,"required":["num","den"],
                     "properties":{"num":{"type":"integer","minimum":1},"den":{"type":"integer","minimum":1}}} ] },
    "damage_type": {"$ref":"../common/v1.schema.json#/$defs/damage_type"},   // resistance / damage
    "ability": {"$ref":"../common/v1.schema.json#/$defs/ability"},            // save/check scoped
    "applies_to": {"enum":["self","target","allies_within_30"]},
    "duration": {"$ref":"#/$defs/modifier_duration"},
    "stacks": {"enum":["replace","stack"],"default":"replace"} },
  "allOf": [
    { "if": {"properties":{"stat":{"const":"speed"}}}, "then": {"required":["speed_scale"]},
      "else": {"not":{"required":["speed_scale"]}} } ] }

"modifier_duration": { "type":"object", "required":["kind"], "additionalProperties": false,
  "properties": { "kind": {"enum":["rounds","until_start_of_caster_turn","until_end_of_target_turn","concentration","until_rest"]},
                  "n": {"type":"integer","minimum":1} },
  "allOf": [ { "if": {"properties":{"kind":{"const":"rounds"}}}, "then": {"required":["n"]}, "else": {"not":{"required":["n"]}} } ] }
```

`ac` modifiers are `bonus`-only in this batch (Q15); there is no `set`. Surfaces:

* **Spells (additive, no bump)**: `spell@1` effect kind `apply_modifier` with `modifier` (Bless,
  Bane, Shield of Faith, Haste, Slow, Faerie Fire via `attacks_against`, Hunter's Mark via `damage`).
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
    "id": {"$ref":"#/$defs/local_id"}, "name": {"type":"string"},
    "max": { "oneOf": [ {"type":"integer","minimum":1},
                        {"type":"object","required":["per_level"],
                         "properties":{"per_level":{"type":"integer"},"ability":{"$ref":"#/$defs/ability"},"min":{"type":"integer"}}} ] },
    "start": {"type":"integer","minimum":0},                       // default = max
    "recharge": {"enum":["short_rest","long_rest","dawn","never"]} } }
```

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
   batch gets its own `4→5` list. (`self_heal`, `damage_bonus`, `bonus_action_grant` are live at `@3`
   and are never marked.)
5. **Merge, never overwrite**, `inert_mechanics`: read the existing array (`2→3` writes entries such
   as `damage[1..]`), union, sort, dedup. The `2→3` helper `mark_defenses_inert` assigns the whole
   array; the `3→4` migration must not copy that pattern (test: a file with both `damage[1..]` and
   `reactions`).
6. Never touch `actions[]` (`special` stays descriptive), `spellcasting`, `size`, defenses, `legendary_actions`,
   or anything live at `@3`.

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
`FeatureRegistry::add()`. Sim-side work (Engine Core): pass `declared.version` into `add()`
(public API, so T&C must agree) and onto `FeatureBuildCtx`, and apply the refusal there.

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
  2. `usage.pool` / `spend.pool` must name a `resources[].id` of the same document; `cost` may not exceed the pool's `max` (when `max` is a constant).
  3. `spellcasting.spells` / `innate.*` / `reaction_effect.spell` content ids must resolve in the loaded pack set (as `spells[]` does today).
  4. Every `save_effect.id` is unique within the document and `"<archetype id>#<id>"` contains exactly one `#` (spell-id derivation, §2.3).
  5. `multiattack` sequence entries (as the existing field expresses them) reference existing attack ids.
  6. `item@1` ids referenced from `character@4 equipment` resolve (§10).
  7. `forward_compat` values are in the known enum.

### 8.4 Compat behaviour goldens (CONTENT_COMPAT §2)

New *decorated* fixtures per piece prove "descriptive stays descriptive":

| Fixture (at `@3`) | Must digest identically to |
|---|---|
| fl1 goblin + descriptive `special` breath weapon, `reactions: [Parry]`, `bonus_actions: [Nimble Escape]` | the undecorated fl1 goblin |
| fl1 fighter with trait effects `{kind:"on_event", ...}` and `{kind:"apply_modifier", ...}` | the undecorated fighter |
| Same two, run through `3→4` | themselves (the migrated form plays like the `@3` one) |

Expected migration output for existing frozen fixtures: the `fl*` files gain `inert_mechanics` /
per-entry `inert` stamps **only** where they have non-empty `reactions` / loadable `bonus_actions`
(the goblin's descriptive entries do). No frozen file is edited (`compat_freeze` unaffected); the
stamps exist only in migrated in-memory form, and tests assert exactly that.

### 8.5 Luau API level 2 and manifest `api_level` (approved)

`manifest@1.api_level` is `const: 1` today. This batch widens it **additively** to `enum [1, 2]` (no
manifest tag change). **Level 2** is the script API that can see `@4` content: reaction/resource/
modifier queries, the new `EventKind`s 39+ in the event feed, and `ActionUsed`-style casts. Rules:

* A pack containing any `@4` or `item@1` document must declare `api_level >= 2` (validator error otherwise,
  because the engine exposes level-2 semantics to that pack's scripts).
* **Level-1 scripts see exactly what they see today**: the script event feed filters rules kinds
  above `SpellCheckRolled` for `api_level 1` packs (ARCHITECTURE.md, script API levels: "old semantics stay behind the
  old level"), so a level-1 pack that loads against the new engine is observably identical.
* `engine_min`/`feature_level` handling is unchanged. Details of the level-2 definitions file
  (`engine.level2.d.luau`) are script_host/T&C work, listed in §12.

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
`SpellCast`, and still emit `SpellResolved`/save events as usual. `kEventKindCount` grows by 6 and
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

Same-PR obligations: `kLastRulesEventKind`, `events_tests.cpp` layout pins (L57/L60 area), the sim
"event layout" test, `docs/SIM_EVENTS.md` table and changelog row for v7, and the
`game/vertical_slice` `known_rules_kind` tests in `event_feed_tests.cpp` ("kLast+1 is unknown").

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
source, and so AC can be computed (§12 row 15). It discriminates on `category`, using the same
`allOf`/`if` closed-properties pattern as everything else.

```json
"item@1": { "$schema": "item@1", "id": "content_id", "name": "string", "source": "$defs/source",
  "category": "weapon | armor | shield" }
```

| `category` | Fields | Notes |
|---|---|---|
| `weapon` | `weapon_class` (`simple`\|`martial`), `range` (`melee`\|`ranged`), `damage[]` (`$defs/damage`), `properties[]` (vocabulary = `rules5e/weapon.hpp`, #53), `reach_ft`, `normal_range_ft`, `long_range_ft` (ranged), `magic_bonus` (int), `qualifiers[]` (`magical`, `silvered`, ... as #53), `on_hit[]` (`rider`, e.g. Flame Tongue) | |
| `armor` | `armor_class` (`light`\|`medium`\|`heavy`), **`base_ac`** (int), **`dex_bonus`** (bool), **`dex_cap`** (int or absent; medium = 2), **`str_min`** (int, default 0), **`stealth_disadvantage`** (bool), `magic_bonus` | AC = `base_ac` + (`dex_bonus` ? min(Dex mod, `dex_cap` if present) : 0) + `magic_bonus`; Str shortfall reduces speed by 10 ft (rules, flagged in the AC PR) |
| `shield` | **`ac_bonus`** (int, default 2), `magic_bonus`, `stealth_disadvantage` (false) | adds to AC when equipped |

`character@4 equipment` becomes `array<string | {item: content_id, equipped?: bool}>`; the bare string
form is the `@3` descriptive entry. Migration `3→4` adds `"equipment"` to `inert_mechanics` when the
list is non-empty, so fl1's equipment list stays **inert**, and `armor_class` (baked AC) stays
authoritative for every `@≤3` file. At `@4` the AC recompute (§12 row 15) applies **only** when the
character declares an equipped `armor`/`shield` item *object* (`{item, equipped:true}`); otherwise
`armor_class.value` is used as written. Fields for which the validator cross-checks `item` ids are in
§8.3. A `character@3` file's inline attack objects are untouched.

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
| 1 | `ally_near_target` sim fact: same-team, conscious, non-incapacitated creature other than the attacker within 1 tile of the target footprint, set in `do_rules` (Attack branch) and `hooks.refresh_ready` | **Engine Core** | none (can land now) | Fixes Pack Tactics (#54; dead today because sim never sets it); Sneak Attack reuses it. Stays out of `log_hash` (§9.3) |
| 2 | `common@2` (changed defs only, absolute refs to v1), `monster@4`/`character@4`, lint test, per-tag validator table, cross-ref checks (§8.3), `3→4` migrations (§8.2), `manifest@1` `api_level` enum + validator rule (§8.5), decorated compat fixtures (§8.4) | **T&C** | #58, #59 | No behaviour; golden-neutral by construction |
| 3 | Loader: accept `@4`, **`declared.version` plumbing** into `ContentLibrary::add()` and `FeatureBuildCtx`, `reactions`/`bonus_actions` markers, registry "list as inert" rule (§8.3), `forward_compat` listing | **Engine Core** | 2 | Public `add()` signature: T&C signs off |
| 4 | `event_stream_version` max-over-constructs (§9.2), v7 constant, SIM_EVENTS.md v7 row, tests (Footprint-only 6 / new-kind-only 7 / both 7 / neither unchanged) | **Engine Core** | #55 | Lands before any kind 39+ is emitted |
| 5 | rules5e foundation: `usage`, `ActionUse`, `ResourcePool`, recharge roll, `moved_ft` (tiles x 5, rules-side), `SpellDef::{is_spell, magical, fixed_dc, fixed_attack}`, kinds 39-44, `kLastRulesEventKind` | **Rules** | 4, #55 | Tagged `Combat::hash` folds are part of the sim side of this PR (Engine Core co-reviews) |
| 6 | **Save actions, caller-supplied targets (Q8)**: synthetic-spell compile in `ContentLibrary` (§2.3), `spells_known` append, AI offer on top of #59's multiattack ordering (save action if worthwhile, else multiattack, else single attack) | **Engine Core** (compile + AI), Rules for any `legal_actions` change | 3, 5 | AI reads `state_` (expended or not), never rolls; heuristic: use when >= 2 hostile targets (or 1 if no attack is legal) and never include allies |
| 7 | **Area templates (R-G10)**: aim input via `Command::to` for `CastSpell` (already in `LogEntry`/`log_hash`), geometry over `footprint_tiles_at`, LOS/cover from origin, opt-in (derive targets only when an aim tile is given and `targets` is empty), `spell_in_reach` fix for self-origin | **Engine Core** | #55, 6 | Later PR (Q8); also serves `area_save` riders |
| 8 | Innate/slot monster spellcasting, DC/attack overrides, `ActionRequest::innate` | **Rules** + **Engine Core** (archetype) | 3 | |
| 9 | Triggers/riders in rules: `triggers.hpp`, hook points, `FeatureTriggered`, `HitRider` reaction trigger | **Rules** | 4, 5 | AI default: `ask` riders **decline** unless policy/content opts in |
| 10 | Rider/`on_event` loader (`on_hit[]`, effects) | **Engine Core** (loader), schema in row 2 | 1, 3, 9 | |
| 11 | Reactions: new triggers/options, hooks, `reactions[]` compile | **Rules** then **Engine Core** (hooks + deterministic default order by content order, honouring `uses_reaction`; a free `HitRider` is never option 0 ahead of a real reaction) | 3, 9 | `RollAttack` window deferred (Q6) |
| 12 | Timed modifiers (+ `spell@1` `apply_modifier`, additive) | **Rules**; **T&C** adds the spell kind to validation | 5 | Spell side can ship before row 2 |
| 13 | Class pools + feature kinds (`grant_action` via `TurnBudget::extra_actions`, `heal_pool` reusing existing `ActionRequest::amount`, pool usage), Legendary Resistance as a pool | **Rules** + **Engine Core** (FeatureRegistry hooks) | 5, 9, 12 | |
| 14 | `item@1` schema + loader (weapons, armor, shields) | **T&C** (schema/validator), **Engine Core** (loader) | 2, 3 | |
| 15 | **AC recompute from equipped armor/shield** (`rules5e::compute_ac`, `character@4 equipment` objects; fl1's baked AC and equipment list stay inert) | **Rules** (formula, Str-shortfall speed) + **Engine Core** (apply at archetype build) | 14 | Own PR; goldens unaffected because only `{item, equipped}` objects opt in |
| 16 | Luau **API level 2** definitions (`engine.level2.d.luau`), level-1 event-feed filtering (§8.5) | **T&C** / script_host owner, **Engine Core** | 4, 5 | Level 1 observably unchanged |
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
| Q4 | Charge/Pounce use "moved at least N ft this turn"; `moved_ft` is rules-side, **tiles x 5** (not `move_ft`); no path history; limitation documented in §3.1 | DECIDED (Director) |
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

## 14. What to ask reviewers (rev 2)

**Tools & Content:** (a) §1.2/§1.3 (absolute refs, lint test, `@3`-carry-over incl. `inert_mechanics`
and `forward_compat`), (b) the `oneOf` of mechanical vs `legacy_action` for `reactions[]` /
`bonus_actions[]` and the per-entry stamping (§4.1, §8.2), (c) the closed `if/then` unions (§2.2, §4.1,
§6.1), (d) the validator cross-reference list (§8.3), (e) `item@1` field names (§10), (f) the
`manifest@1` `api_level` enum change and the level-2 rule (§8.5).

**Engine Core:** (a) rows 1, 3, 4, 7 as yours; (b) the `event_stream_version` max form (§9.2) and the
hash rules (§9.3: tagged folds, `log_hash` stays unchanged for old logs); (c) the synthetic-spell
compile (§2.3 points 1-4); (d) AI defaults (§12 rows 6, 9, 11); (e) the declared-version plumbing and the
public `add()` signature change (§8.3).
