# Proposal: the version-bump batch (common@2 / monster@4 / character@4 / item@1)

Status: draft for Director decision. Reviewers: Tools & Content (schemas, validator, loaders,
migrations), Engine Core (sim hooks, event schema, digests). Author: Rules (rules5e).
Basis: `docs/SRD_COMBAT_COVERAGE.md` §11 (batch B rows R-G3..R-G5, R-G7, T&C-G4/G5/G9/G10/G17),
`docs/CONTENT_COMPAT.md`, `docs/SIM_EVENTS.md`, and the code on `main` after #57.

Scope: (1) save-based actions with area templates, (2) on-hit riders and `extra_damage` /
`on_event` triggers, (3) reactions as monster/character actions, (4) monster spellcasting (innate
and slot-based), (5) timed buffs and debuffs, (6) class resource pools. Out of scope, named so
nothing is promised by accident: legendary actions (T&C-G11), elevation (T&C-G12), armor/AC
recomputation from equipment (own proposal, §10 gives only the `item@1` slice these six need).

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
exactly the "interim trait effect" the Director ruled out, so it lands in `@4`. See Q2.

### 1.1 Which schema carries what

| Schema | Change | Why it cannot be additive |
|---|---|---|
| `common@2` (new file) | `$defs`: `action` (v2), `reaction`, `trigger`, `rider`, `usage`, `area`, `resource`, `modifier`, `spellcasting` (v2, superset of v1), `trait`/`trait_active` (v2, new effect kinds + `cost: reaction`), `stat_block` (v2) | `common@1 $defs/action` is `additionalProperties:false`; `reactions`/`bonus_actions` exist in v1 but are descriptive; extending them in v1 would let `@3` files carry fields the `@3` engine contract says are inert |
| `monster@4` | `$ref common@2 stat_block`, `$schema` const | same, plus gating |
| `character@4` | same; adds `resources[]` and class-feature effect kinds | same |
| `item@1` | new type, weapon/attack data only (§10) | new type: no old content |
| `spell@1` | **additive only**: effect kinds `apply_modifier`, optional `usage` (not needed), no tag change | open effect array, no old meaning |

`common@2` is a full standalone `stat_block` that `$ref`s unchanged leaf defs from
`common@1` (`dice`, `ability`, `damage_type`, `condition`, `size`, ...). It cannot be
`allOf [v1 stat_block, overrides]`: `allOf` is an AND, so v1's `actions.items: $ref action` would
still reject the new kinds. Cost: ~15 `$defs` are re-declared (the v1 versions stay authoritative
for `@1..@3` documents; migrations never rewrite them back). See Q11.

## 2. Save-based actions with area templates (T&C-G4, R-G10)

### 2.1 Content shape (`common@2 $defs/action`, new kind `save_effect`)

Existing kinds keep their exact shape (`melee_weapon_attack`, `ranged_weapon_attack`,
`special`, `multiattack`). `save_effect` adds:

| Field | Type | Req | Notes |
|---|---|---|---|
| `kind` | `"save_effect"` | y | breath weapons, Frightful Presence, Turn Undead, Stunning-Strike-style actions |
| `id`, `name`, `description` | as v1 | y/y/n | `id` is the `local_id` used by AI/UI and in the synthetic spell id (§2.3) |
| `save` | `{ability, dc}` | y | `dc` integer 1..30; no derived DCs for actions (SRD blocks print them). `ability` is `$defs/ability` |
| `area` | `$defs/area` | n | absent = single target (`targets: 1`, `range_ft`) |
| `range_ft` | integer | n | single-target range; ignored when `area.origin` is `self` |
| `damage[]` | v1 `damage` items | n | at least one of `damage[]`/`apply[]` required |
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

### 2.2 `$defs/usage` (shared by §1, §3, §4, §6)

```json
"usage": { "oneOf": [
  { "properties": { "kind": {"const": "at_will"} } },
  { "properties": { "kind": {"const": "recharge"}, "min": {"enum": [2,3,4,5,6]} }, "required": ["kind","min"] },
  { "properties": { "kind": {"const": "per_day"},  "uses": {"type":"integer","minimum":1,"maximum":9} }, "required": ["kind","uses"] },
  { "properties": { "kind": {"const": "per_rest"}, "uses": {"type":"integer","minimum":1,"maximum":9},
                    "rest": {"enum": ["short","long"]} }, "required": ["kind","uses","rest"] },
  { "properties": { "kind": {"const": "pool"}, "pool": {"$ref":"#/$defs/local_id"}, "cost": {"type":"integer","minimum":1} },
    "required": ["kind","pool"] } ] }
```

`recharge` is "Recharge 5-6": roll d6 at the start of the owner's turn while expended; `min..6`
recharges. `per_day`/`per_rest` are a `FeatureUses` row; `pool` spends from §7.

### 2.3 Rules mapping: a save action compiles to a synthetic `SpellDef`

All resolution code already exists for spells with `SpellEffectKind::Save` plus `extra_effects`
(damage, half on success, `ApplyCondition`, repeat saves). Rather than a second resolver, sim
compiles each `save_effect` at archetype build into a `SpellDef` with a reserved id and adds it
to the shared `SpellBook`:

```cpp
// rules5e/spells.hpp (append to SpellDef; defaults preserve every existing spell)
struct SpellDef {
    // ... existing fields ...
    bool    is_spell = true;       // false: no SpellCast window (cannot be Counterspelled), no
                                   //        components, SaveSource.magical = (magical flag)
    bool    magical = true;        // feeds SaveSource / RollTrait "against magic" (#54)
    uint8_t usage_kind = 0;        // 0 none/at-will, 1 recharge, 2 per-day/rest (FeatureUses), 3 pool
    uint8_t recharge_min = 0;      // usage_kind == 1
    uint32_t pool_id = 0; uint8_t pool_cost = 0;   // usage_kind == 3
};
```

Synthetic id: `fnv1a("<archetype id>#<action id>")` with the high bit set (so it cannot collide
with content spell ids, which sim already hashes without it). The action is offered through
`legal_actions` as `CastSpell` with `spell_level = 0` (no slot) and carries `usage`. Area targets
(who is inside the cone) are **supplied by the caller** in `ActionRequest::targets` exactly as for
`Fireball` today. R-G10 (Engine Core) turns `area` into a target list; until it lands, AI/tests
list targets explicitly and the template fields are validated but only used for UI hints.

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

**(b) Triggers**: effect `kind: "trigger"` in `traits[]` / `features[]` (character `features[]`
carry the same effect object): effects that happen on an *event*, independent of one action
(Sneak Attack, Rampage, Undead Fortitude, Death Burst, Divine Smite).

```json
"$defs/trigger": { "type":"object", "additionalProperties": false,
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
| `moved_at_least_ft` | `ft` | Charge, Pounce (20 ft). Straight-line requirement approximated, see Q4 |
| `self_hp_below` | `{num, den}` | Rampage-style gates |
| `save` | `{ability, dc, dc_from_damage?: true}` | Undead Fortitude: Con save DC `5 + damage taken` (`dc_from_damage`) |

`$defs/rider` (effects, closed set of kinds):

| `kind` | Fields | Rules mapping |
|---|---|---|
| `extra_damage` | `damage[]` (v1 `damage`), `scale?: {per_levels:{class?,every,add_dice}}` | added to the attack's damage roll (crit doubles) |
| `save_condition` | `save`, `apply[]` (as §2.1), `damage[]?`, `on_success` | `ActiveSpellEffect`, same as §2 |
| `apply_condition` | `apply[]` (no save) | prone on hit etc. |
| `heal_self` / `temp_hp` | `dice` or `amount` | |
| `set_hp` | `{value}` (e.g. 1) | Undead Fortitude: cancels the drop to 0 |
| `bonus_attack` | `{attack_id}` | Rampage: a bonus-action attack granted to `TurnBudget` |
| `area_save` | `$defs/area` + `save` + `damage[]` + `on_success` | Death Burst (centered on owner) |
| `spend` | `{pool, amount}` or `{slot: "any"|level}` | Divine Smite spends a slot, scaling `extra_damage` by slot level (`scale.per_slot_level`) |
| `grapple`, `swallow` | reserved | **rejected by the validator** until R-G8 (conditions with a source) lands |

Examples:

```json
{ "kind": "trigger", "on": "hit", "limit": "once_per_turn",
  "when": [ {"kind":"weapon","is":"finesse_or_ranged"}, {"kind":"advantage_or_ally_adjacent"} ],
  "effects": [ {"kind":"extra_damage","damage":[{"dice":"1d6","type":"same_as_weapon"}],
                "scale":{"every_levels":2,"add_dice":"1d6","class":"rogue"}} ] }

{ "kind": "trigger", "on": "drop_to_zero", "when": [ {"kind":"damage_type_not","types":["radiant"]},
                                                       {"kind":"not_crit"},
                                                       {"kind":"save","ability":"con","dc_from_damage":true,"base":5} ],
  "effects": [ {"kind":"set_hp","value":1} ] }
```

(`type: "same_as_weapon"` is a documented extension of v1 `damage_type` valid only inside riders.)

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
// TurnBudget (appended): uint32_t triggers_used_mask; uint16_t moved_ft;
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

* Old traits whose effect `kind` is `trigger` do not exist (open effect, none shipped).
  `3→4` marks any pre-existing `trigger`-kind effect `"inert": true` (§8.2).
* `on_hit[]` is a new property of attack actions: absent in all `@≤3` files by schema.
* Descriptive Sneak Attack / Rampage / Undead Fortitude text remains description.

## 4. Reactions as actions (T&C-G9, version gate)

### 4.1 Content shape

`reactions[]` exists in `common@1 stat_block` and is descriptive today (sim ignores it). In
`common@2 $defs/reaction` it becomes mechanical:

```json
"$defs/reaction": { "type":"object", "additionalProperties": false,
  "required": ["id","name","trigger","effect"],
  "properties": {
    "id": {"$ref":"#/$defs/local_id"}, "name": {"type":"string"}, "description": {"type":"string"},
    "trigger": { "type":"object", "additionalProperties": false, "required":["kind"], "properties": {
        "kind":  {"enum": ["being_hit","damaged","leaving_reach","ally_attacked","spell_cast","roll_attack"]},
        "melee_only": {"type":"boolean"}, "ranged_only": {"type":"boolean"},
        "source": {"enum": ["self","ally_within_5ft"], "default": "self"},
        "visible": {"type":"boolean","default":true} } },
    "effect": { "oneOf": [
        { "properties": {"kind":{"const":"ac_bonus"},       "bonus":{"type":"integer"}},  "comment": "Parry, Shield" },
        { "properties": {"kind":{"const":"reduce_damage"},  "fraction":{"enum":["half"]}, "dice":{"$ref":"#/$defs/dice"}}, "comment": "Uncanny Dodge, Deflect Missiles" },
        { "properties": {"kind":{"const":"impose_disadvantage"}}, "comment": "Protection" },
        { "properties": {"kind":{"const":"retaliate"},      "damage":{}, "save":{}},       "comment": "Hellish Rebuke, Fire Shield-style" },
        { "properties": {"kind":{"const":"attack"},         "attack_id":{}},              "comment": "OA-style, Riposte" },
        { "properties": {"kind":{"const":"cast"},           "spell":{"$ref":"#/$defs/content_id"}}, "comment": "monster casts Shield/Counterspell via its spellcasting list" },
        { "properties": {"kind":{"const":"subtract_roll"},  "dice":{"$ref":"#/$defs/dice"}}, "comment": "Cutting Words (phase 2, needs roll window)" } ] },
    "usage": {"$ref":"#/$defs/usage"} } }
```

`cost: "reaction"` on trait/feature effects becomes legal at `@4` (it is a schema error at `@≤3`).
All reactions share the one-reaction-per-round budget (`Combatant::reaction_used`, unchanged).

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
* `@≤3` documents migrate with `inert_mechanics: ["reactions"]` whenever `reactions` is non-empty
  (sim: `Archetype::reactions` stays empty, `Archetype::inert_mechanics` reports it). Any
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
| `innate.at_will` | `local_id/content_id[]` | spell content ids |
| `innate.per_day` | `{ "1": [...], "2": [...], "3": [...] }` | key = uses/day |
| `innate.recharge` | `[{spell, min}]` | rare ("Recharge 6"), uses `$defs/usage` semantics |

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
of encounter state and are restored by `recover_features(long_rest)`. Spells cast by `cast` reactions
(§4) go through the same list.

## 6. Timed buffs and debuffs (R-G3)

### 6.1 Model

A *modifier* is a timed, sourced change to one number or roll. It generalizes `RollTrait` (#54)
and the `ac_bonus` spell effect (which is only an instantaneous reaction today).

```json
"$defs/modifier": { "type":"object", "additionalProperties": false, "required": ["stat","duration"],
  "properties": {
    "stat": { "enum": ["attack_roll","save","ability_check","ac","speed","damage","temp_hp",
                       "resistance","attacks_against","advantage_on","disadvantage_on"] },
    "bonus":   {"type":"integer"}, "die": {"$ref":"#/$defs/dice"},           // Bless: stat save/attack_roll, die 1d4
    "set":     {"type":"integer"},                                            // Mage Armor: ac base handled by "ac_base" (Q15)
    "multiply":{"enum":[0.5,2]},                                              // Haste/Slow speed
    "damage_type": {"$ref":"#/$defs/damage_type"},                            // resistance / damage
    "ability": {"$ref":"#/$defs/ability"},                                    // save/check scoped
    "applies_to": {"enum":["self","target","allies_within_30"]},
    "duration": { "oneOf": [
        {"properties":{"kind":{"const":"rounds"},"n":{"type":"integer"}}},
        {"properties":{"kind":{"const":"until_start_of_caster_turn"}}},
        {"properties":{"kind":{"const":"until_end_of_target_turn"}}},
        {"properties":{"kind":{"const":"concentration"}}},
        {"properties":{"kind":{"const":"until_rest"}}} ] },
    "stacks": {"enum":["replace","stack"],"default":"replace"} } }
```

Surfaces:

* **Spells (additive, no bump)**: `spell@1` effect kind `apply_modifier` with `modifier`
  (Bless, Bane, Shield of Faith, Haste, Slow, Faerie Fire via `attacks_against`+advantage, Hunter's
  Mark via `damage`). This is why §1 says R-G3 is batch N on the spell side.
* **Features/traits (`@4`)**: effect kind `apply_modifier` with `cost`/`usage` (Rage, Bardic
  Inspiration token, Leadership, Frightful Presence `apply[]` is §2's condition path, not this).

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
| `grant_action` | Action Surge, Haste-like | `UseFeature` sets `TurnBudget::action = true` (bonus/extra action; Q7) |
| `apply_modifier` | Rage (+dmg, resistances) | §6 |
| `save_effect` action with `usage.pool` | Turn Undead, Stunning Strike | §2 |
| `heal_pool` | Lay on Hands: spend N from a pool to heal N | `UseFeature` with `amount` (appended `ActionRequest::amount`) |
| `trigger` + `spend` | Divine Smite, Sneak-style resource riders | §3 |

### 7.2 Rules sketch

```cpp
struct ResourcePool { CombatantId actor; uint32_t pool_id; uint16_t max, cur; FeatureRecharge recharge; };
// EncounterState (appended): std::vector<ResourcePool> pools;     // serialized/hashed; empty for old content
// recover_features(state, long_rest) also refills pools by recharge kind
bool can_spend(const EncounterState&, CombatantId, uint32_t pool_id, uint16_t n);
```

`UseFeature` is unchanged for the old path; a feature with a pool cost requires `can_spend` in
`legal_actions` and emits `ResourceSpent`. There is no rest model in the encounter: sim/Combat
call `recover_features` between encounters as today.

### 7.3 Inert behaviour

`resources[]` does not exist pre-`@4` (schema). `character@3` features keep `uses`/`recharge`
(still live). Nothing is marked.

## 8. Migrations and inert markers

### 8.1 Chain

`monster_3_to_4`, `character_3_to_4` registered in `engine/core/assets/.../content_migrations.hpp`
alongside `2→3`. `common@2` has no document type of its own, so no `common` migration. Both are
pure, idempotent, order-independent, and preserve key order.

### 8.2 What `3→4` writes

1. Set `$schema` to the `@4` tag.
2. `reactions` non-empty ⇒ append `"reactions"` to `inert_mechanics`.
3. `bonus_actions` non-empty ⇒ append `"bonus_actions"` (`@4` reads `bonus_actions[]` as real
   actions; at `@3` it was never loaded).
4. For every `traits[]` / `features[]` entry whose `effect.kind` ∈ **new-kind set** (`trigger`,
   `apply_modifier`, `grant_action`, `heal_pool`, `save_effect`, `reaction`, any kind introduced by
   this batch) set `effect.inert = true`. Sim already lists these in `Archetype::inert_features`.
5. `inert_mechanics` stays sorted/deduplicated; existing `2→3` entries are preserved.
6. Never touch `actions[]` (`special` keeps its descriptive meaning), `spellcasting`, `size`,
   defenses, anything already live at `@3`.

### 8.3 Loader contract (Engine Core / T&C)

`sim::ContentLibrary` already ignores whatever `inert_mechanics` lists. New rules:

* New keys the loader must honour: `reactions`, `bonus_actions`.
* `FeatureRegistry` must refuse a *new-set* kind from any document whose schema tag < 4 even
  without the marker (defence in depth against hand-edited migrated content).
* Validator (`tools/validate`): `KNOWN_EFFECT_KINDS_V4`; per-tag allowed sets; error on `@≥4`
  grapple/swallow riders until R-G8; warn (not error) on unknown kinds, unchanged.

### 8.4 Compat behaviour goldens (CONTENT_COMPAT §2)

New *decorated* fixtures per piece prove "descriptive stays descriptive":

| Fixture (at `@3`) | Must digest identically to |
|---|---|
| fl1 goblin + descriptive `special` breath weapon, `reactions: [Parry]`, `bonus_actions: [Nimble Escape]` | the undecorated fl1 goblin |
| fl1 fighter with a trait effect `{kind:"trigger", ...}` and `{kind:"apply_modifier", ...}` | the undecorated fighter |
| Same two, run through `3→4` | themselves (the migrated form plays like the `@3` one) |

None of these re-pin anything.

## 9. Event schema impact and golden hashes

### 9.1 New `rules5e::EventKind` values (append after `SpellCheckRolled` = 38)

| Value | Kind | Payload (`actor, target, a, b, c`) | From |
|---|---|---|---|
| 39 | `RechargeRolled` | actor, -, action/spell id, d6 natural, recharged 0/1 | §2 |
| 40 | `FeatureTriggered` | owner, subject, trigger id, `TriggerOn`, outcome (0 none, 1 applied, 2 declined, 3 failed save) | §3, §4 |
| 41 | `ResourceSpent` | actor, -, pool id, amount, remaining | §7 |
| 42 | `ModifierApplied` | caster, target, modifier id, stat, magnitude | §6 |
| 43 | `ModifierExpired` | caster, target, modifier id, reason, - | §6 |

`kEventKindCount` grows by 5 and `kLastRulesEventKind` moves to `ModifierExpired`. Sim events:
`SimEvent::Kind` is unchanged (rules events pass through as today). Casting uses the existing
`SpellCast`/`SpellResolved` kinds for synthetic save spells, with `is_spell = false` recorded in a
flag bit of `c`; Q9 asks whether a dedicated `ActionUsed` is preferred.

### 9.2 Sim event schema

`kSimEventSchemaVersion` 5 → **6**, in the first PR that appends an `EventKind` (the rules PR
that adds `RechargeRolled`; later additions inside this batch extend v6, they do not each bump).
Because `event_stream_version()` returns the *lowest* fitting version, a stream that contains none
of kinds 39..43 still hashes under its old version and its digests are **byte-identical**. That is
the whole golden story: old content never produces the new kinds (it has no recharge actions, no
triggers, no modifiers, no pools, and §8 makes the markers inert), so nothing re-pins.

Same-PR obligations (docs/SIM_EVENTS.md): update `kLastRulesEventKind`,
`events_tests.cpp`, the sim "event layout" test, the §table and the changelog row for v6.

### 9.3 Guards that make "no golden change" true

* **RNG draw order.** New code draws from the stream only when new content is present
  (recharge: only for creatures with a recharge action; modifier dice: only with a die modifier;
  triggers: only the `save` predicate / extra dice of a *fired* trigger). A test replays every
  existing golden fight and asserts the rng position after the fight is unchanged.
* **State hash.** New `EncounterState` vectors (`action_uses`, `modifiers`, `pools`) and new
  `Combatant` vectors are appended and hashed only when non-empty, so an
  old encounter serializes to the same bytes as before (exact rule: Q10).
* **`ActionRequest` / `ReactionDecision`** only gain trailing fields with defaults that equal the old
  behaviour.
* **`kReactionTriggerCount`** growth enlarges `reaction_policy`; confirm `Combatant` is not hashed
  by raw bytes (it is serialized field by field; verified in the PR that makes the change, which
  adds a test pinning it).

## 10. `item@1` (only the slice these six topics need)

Full equipment (armor AC formula, shields, magic bonuses, attunement) is its own proposal. The
batch needs weapons to be data so Sneak Attack (`finesse`), riders and `same_as_weapon` damage have a
source:

```json
"item@1": { "type":"weapon",
  "id": "...", "name": "...",
  "category": "simple|martial", "range": "melee|ranged",
  "damage": [ {"dice":"1d6","type":"piercing"} ],
  "properties": ["finesse","light","thrown"],       // vocabulary = rules5e/weapon.hpp (#53)
  "reach_ft": 5, "normal_range_ft": 20, "long_range_ft": 60,
  "magic_bonus": 0, "qualifiers": ["magical","silvered"],   // damage qualifiers, same as #53
  "on_hit": [ ... §3 rider ... ] }                         // e.g. Flame Tongue extra damage
```

`character@4` `equipment` may reference `item@1` ids; a `character@3` file's inline attack objects
are untouched (no migration needed, nothing becomes mechanical).

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
| Events | layout/enum value pins, event schema v6 roundtrip, lowest-version digest for old streams |
| Replay | every new decision kind round-trips through the action log and replays identically |

Tools & Content: schema positive/negative per `$defs` (unknown predicate, missing `on_success`,
`grapple` rejected, `cost: reaction` at `@3` rejected, `resources` at `@3` rejected), migration
tests (idempotence, marker contents, key order), `validate_packs.py` across `tests/compat/fl*`,
`compat_freeze` unaffected (no frozen file edited).

Engine Core: loader tests (archetype fields compile; `inert_*` lists), registry refusal of
new kinds from `@≤3`, area-template targeting (R-G10), `moved_ft`/ally-adjacency facts,
digests: `compat_*` goldens and the existing `fl1` golden **unchanged** (CI proves §9).

## 12. Implementation order (PRs, owners, dependencies)

| # | PR | Owner | Depends on | Notes |
|---|---|---|---|---|
| 1 | `common@2` + `monster@4`/`character@4` schemas (new kinds, `$defs`), validator tables, `3→4` migrations (§8.2), compat decorated fixtures (§8.4) | **T&C** | this proposal approved | No behaviour: sim treats `@4` like `@3` (`Archetype` reads are identical). Golden-neutral by construction |
| 2 | Engine Core loader: accept `@4`, honour `reactions`/`bonus_actions` markers, registry refusal rule (§8.3) | **Engine Core** | 1 | Still no mechanics |
| 3 | rules5e foundation: `usage`, `ActionUse`, `ResourcePool`, recharge, `RechargeRolled`/`ResourceSpent`, sim event schema **v6** + `kLastRulesEventKind` (§9.2) | **Rules** | none (parallel with 1) | Engine Core co-reviews the event PR; the one bump lives here |
| 4 | Save actions: `SpellDef::is_spell/magical/usage`, synthetic spell compile in sim, AI offers | **Rules** (rules5e) then **Engine Core** (compile + AI) | 1-3 | Caller-supplied targets first |
| 5 | Area templates R-G10 (cone/line/sphere/cube targeting) | **Engine Core** | none; needed for full §2 UX | Independent; breath weapons work without it |
| 6 | Innate/slot monster spellcasting, DC overrides | **Rules** + **Engine Core** (archetype) | 1-2 | `ActionRequest::innate` appended |
| 7 | Triggers/riders: `triggers.hpp`, hook points, `FeatureTriggered`, `TurnBudget` facts | **Rules**; **Engine Core** supplies `moved_ft`, ally adjacency | 3 | Reaction-style smite needs 9 |
| 8 | Trigger/rider loader (`on_hit[]`, `trigger` effects) | **T&C** schema already in 1; **Engine Core** loader | 2, 7 | |
| 9 | Reactions: new triggers/options, hooks, `reactions[]` compile | **Rules** then **Engine Core** (hooks + AI policy) | 2, 7 | Phase 2 `RollAttack` window deferred (Q6) |
| 10 | Timed modifiers + `spell@1 apply_modifier` (additive) | **Rules**; **T&C** adds the spell kind to `spell@1` validation | 3 | Spell side can ship before 1 |
| 11 | Class pools + feature kinds (`grant_action`, `heal_pool`, pool usage) | **Rules** + **Engine Core** (FeatureRegistry hooks) | 3, 7, 10 | |
| 12 | `item@1` weapon slice | **T&C** (schema/loader), **Rules** (none; reuses #53) | 7 | |
| 13 | SRD content passes (breath weapons, riders, innate lists, PC features) | **T&C** | 4,6,8,9,11 | Re-authors descriptive content; separate PRs per tier |

One PR per row, from fresh `main`, normal pushes, full suite on GCC Debug and Clang Release, CI
green before review.

## 13. Open questions (need Director decisions)

1. **Q1: Save actions as synthetic `SpellDef`s.** OK to reuse the spell resolver with
   `is_spell=false` / `magical=false`, or do you want a separate `ActionDef` table? (Recommended:
   synthetic; ~0 new resolution code; one more flag on `SpellDef`.)
2. **Q2: Timed modifiers and pools before the bump?** They are batch N, and spell-side modifiers are
   additive. Do the *trait/feature* surfaces (Rage, Ki, Bardic Inspiration) wait for `@4` (the
   "no interim trait effects" ruling), or may they ship earlier as additive effect kinds? This
   proposal assumes they wait.
3. **Q3: Optional riders (Divine Smite).** Decide via the reaction-window machinery
   (recommended; after the hit, free action), or a declare-before-attack flag (simpler, not RAW).
4. **Q4: Charge/Pounce "straight 20 ft".** Approximate as "moved ≥ N ft this turn before the attack"
   (needs only `TurnBudget::moved_ft`), or require path history from sim?
5. **Q5: Order of `drop_to_zero` triggers vs regeneration-down.** Proposed: Undead Fortitude/Relentless
   Endurance first, then the regen suppression check, since a creature that stays at 1 HP never
   enters the regen-down branch.
6. **Q6: Roll-modifying reactions** (Cutting Words, Bardic Inspiration spend): add a `RollAttack`/
   `RollSave` window now or defer as phase 2? Deferred here.
7. **Q7: Extra actions** (Action Surge, Haste): is mutating `TurnBudget::action` acceptable, or
   should extra actions be a distinct budget field (affects the turn-end checks)?
8. **Q8: Area templates (R-G10) timing.** Ship §2 with caller-supplied targets first, or hold
   breath weapons until R-G10 lands? Also confirm Engine Core owns template→target resolution.
9. **Q9: Events.** One generic `FeatureTriggered` vs a kind per trigger; and `ActionUsed` for
   non-spell save actions vs reusing `SpellCast`/`SpellResolved` with a flag bit.
10. **Q10: State-hash rule for new vectors.** Hash them only when non-empty (digest-neutral) vs bumping
    the state-hash layout version. Recommended: only-when-non-empty. Engine Core to confirm how
    `EncounterState` is hashed today.
11. **Q11: `common@2` as a standalone copy** (recommended) vs a `$ref` overlay mechanism; accepted
    cost is re-declaring ~15 defs and keeping them in step.
12. **Q12: New-kind inert marking (§1, §8.2 step 4)** and the registry refusal rule: approve as a
    behaviour change to the existing `FeatureRegistry`?
13. **Q13: Monster DC/attack overrides** (`spellcasting.dc`, `attack_bonus`): confirm SRD printed
    values win over derived ones.
14. **Q14: Legendary actions/resistance.** Legendary Resistance is a plain pool + `drop to success`
    reaction here; legendary *actions* remain T&C-G11. Confirm no placeholders are reserved now.
15. **Q15: `ac` modifiers** for Mage Armor-style "base AC = 13 + Dex" (`set`) vs `bonus`-only; the
    former touches AC recomputation (§10 note) and may wait for the equipment proposal.
16. **Q16: Rest model.** Pools/uses recharge only via sim calling `recover_features`; is a
    first-class rest API wanted in this batch (owner: Engine Core)?

## 14. What to ask reviewers

**Tools & Content:** (a) field names/casing against the existing schemas (`save`, `on_success`,
`apply[]`, `usage`, `area`, `resources[]`); (b) the standalone `common@2` approach and the
`$defs` list in §1.1; (c) the `3→4` migration contents (§8.2) and the validator tables;
(d) whether `bonus_actions` really deserves a marker; (e) `item@1` slice scope.

**Engine Core:** (a) sim facts needed (`moved_ft`, ally-adjacency, area targets, rest API);
(b) the event-schema v6 plan and the lowest-version digest argument (§9.2) and Q10 hash rule;
(c) synthetic spell compile in `ContentLibrary` and the `FeatureRegistry` refusal rule;
(d) AI policy for recharge/riders/reactions; (e) PR ordering in §12.
