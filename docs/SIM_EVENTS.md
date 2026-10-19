# sim event stream (schema version 9)

`eng::sim::Combat::events()` is the ordered record of everything that happened in a combat. Render/platform, UI, logs and tests consume it. This document is the contract. The code lives in `engine/core/sim/include/sim/events.hpp`, and the layout is locked by `engine/core/sim/tests/events_tests.cpp`.

## Types

```cpp
inline constexpr uint32_t kSimEventSchemaVersion = 9;
inline constexpr rules5e::EventKind kLastRulesEventKind = rules5e::EventKind::ZoneEnded;
inline constexpr rules5e::EventKind kLastRulesEventKindV8 = rules5e::EventKind::ActionUsed;         // v9 digest versioning keys on this
inline constexpr rules5e::EventKind kLastRulesEventKindV6 = rules5e::EventKind::SpellCheckRolled;   // v7 digest versioning keys on this

struct SimEvent {
    enum class Kind : uint8_t { Rules = 0, TileMoved = 1, SpellTarget = 2, Footprint = 3 };   // append-only
    Kind kind;
    rules5e::Event rules;   // Kind::Rules only (zero otherwise)
    CombatantId actor;      // TileMoved: mover (0 otherwise)
    Vec2i tile;             // TileMoved: destination tile in the combat grid frame (zero otherwise)
};

struct rules5e::Event { EventKind kind; CombatantId actor, target; int32_t a, b, c; uint8_t reaction_depth; };
```

`CombatantId` equals the ECS `StableId` of the combatant's entity and the chunk spawn's `stable_id`. Map it to presentation through the entity (`Combat::entity(id)`) or the spawn.

### `Kind::Rules` payload (`rules5e::EventKind`, values locked)

| value | kind | actor | target | a | b | c |
|---|---|---|---|---|---|---|
| 0 | InitiativeRolled | combatant | | total | | |
| 1 | TurnStarted | combatant | | round | | |
| 2 | AttackResolved | attacker | target | natural d20 | total | bit0 hit, bit1 crit (damage follows as DamageDealt) |
| 3 | DamageDealt | 0 | creature damaged | damage dealt | hp after | one per hit: for a multi-component attack (monster@3 / character@3 `damage[]` with several entries) `a` is the SUM of the components after each one's own immunity/resistance/vulnerability (no per-type breakdown; a fully immune hit is `a = 0`). Single-component hits are unchanged |
| 4 | ConditionAdded | creature | | Condition | | |
| 5 | CombatantDowned | creature | | | | |
| 6 | DeathSaveRolled | creature | | natural | successes | failures |
| 7 | Stabilized | creature | | | | |
| 8 | RegainedHp | creature | | hp regained | | |
| 9 | CombatantDied | creature | | | | |
| 10 | Moved | mover | (v8) source of a flagged move: shover / caster (forced), grappler (drag), the mover itself (self-teleport) | feet actually moved | movement left (0 for a flagged move) | (v8) flag bits: bit0 forced, bit1 teleport, bit2 drag; 0 for an ordinary move |
| 11 | Dashed | creature | | movement left | | |
| 12 | Disengaged | creature | | | | |
| 13 | Dodged | creature | | | | |
| 14 | StoodUp | creature | | movement left | | |
| 15 | TurnEnded | combatant | | | | |
| 16 | EncounterEnded | | | EndReason | | |
| 17 | Healed | healer (0 = no source) | target | healed | hp after | from zero |
| 18 | ConditionRemoved | creature | | Condition | | |
| 19 | MedicineCheck | creature | | natural | total | success |
| 20 | Helped | helper | ally | vs | | |
| 21 | Hid | creature | | stealth total | | |
| 22 | Readied | readier | target | ready kind | trigger id | |
| 23 | ReadyTriggered | readier | target | trigger id | | |
| 24 | ReadyExpired | readier | | | | |
| 25 | CheckRolled | creature | Skill; (v9) `0x100 \| Ability` for a plain ability check (R-G11 Web escape: Strength) | natural | total | bit0 success, bit1 helped |
| 26 | ReactionWindowOpened | trigger actor | (Ready: readier) | ReactionTrigger | window seq | |
| 27 | ReactionTaken | reactor | | ReactionTrigger | window seq | option index |
| 28 | ReactionDeclined | reactor | | ReactionTrigger | window seq | option index |
| 29 | MoveInterrupted | mover | | feet requested | | |
| 30–32 | SpellCast / SpellCountered / SpellResolved | caster | target (counter: countered caster) | spell id | level | |
| 33 | AcBonus | creature | | bonus | new AC | |
| 34 | FeatureUsed (v2) | user | | feature id | 1 if bonus action | |
| 35 | SavingThrowRolled (v4) | saver | Ability | natural | total | success \| concentration<<1 |
| 36 | ConcentrationEnded (v4) | caster | | spell id | | |
| 37 | SpellEffectEnded (v4) | affected creature | | spell id | Condition removed (-1 none) | |
| 38 | SpellCheckRolled (v4) | counterer | | natural | total | success |
| 39 | RechargeRolled (v7) | creature | | action/spell id | d6 natural | recharged 0/1 |
| 40 | FeatureTriggered (v7) | owner | subject | trigger id; (v9) zone trigger: spell id | TriggerOn; (v9) `ZoneAppear` 8, `ZoneEnter` 9, `ZoneStartTurn` 10, `ZoneEndTurn` 11, `ZoneMoveWithin` 12 | outcome (0 none / saved, 1 applied / no save, 2 declined; for a zone trigger 2 = automatic success, immune, no roll (R-G11 PR 2), 3 failed save) |
| 41 | ResourceSpent (v7) | creature | | pool id | amount | remaining |
| 42 | ModifierApplied (v7, reserved) | caster | target | modifier id | stat | magnitude |
| 43 | ModifierExpired (v7, reserved) | caster | target | modifier id | reason | |
| 44 | ActionUsed (v7) | creature | first target | action/spell id | is_spell 0/1 | usage kind (0 at-will, 1 recharge, 2 per-day/rest, 3 pool) |
| 45 | ZoneCreated (v9) | owner | anchor creature (0 = a point zone) | zone id | spell id | `ZoneSpec` flags (`kZone*`) |
| 46 | ZoneMoved (v9) | owner | | zone id | feet moved (0 = not supplied) | |
| 47 | ZoneEnded (v9) | owner | | zone id | reason (0 timer, 1 concentration, 2 owner died / feature ended, 3 dispelled) | |

Kinds 39-44 are locked at v7. Rules emits `RechargeRolled`, `ResourceSpent` and `ActionUsed` today (recharge actions, resource pools, non-spell actions); `FeatureTriggered`, `ModifierApplied` and `ModifierExpired` are reserved for the trigger/modifier rows and no stream carries them yet. No existing content can emit any of them (they need `usage`/pool/non-spell definitions that only the version-bump batch's new content produces), so every pre-v7 digest keeps its recorded version.

`FeatureUsed` feature ids are 1-based indexes into the archetype's declared features (`features[]` then `traits[]`, see `sim/features.hpp`). A self-heal is followed by `Healed`, with actor and target both set to the user.

**`SpellTarget` (v5).** One per creature affected by a `CastSpell` request, after the request's rules events (and after any `TileMoved`), in target order: `actor` = caster, `tile` = the target's tile at cast time (same frame as `TileMoved`), `rules.target` = target id, `rules.a` = spell id; every other `rules` field is zero. It is emitted only when the request produced a depth-0 `SpellCast` or depth-0 `ActionUsed` (a non-spell save action such as a breath weapon; rejected casts append nothing). It gives viewers the tiles of area spells, whose `SpellCast` names only the first target. The effect itself arrives as the usual rules events (`AttackResolved`, `SavingThrowRolled`, `DamageDealt`, `ConditionAdded`, `Healed`, `SpellResolved`, ...).

**Area templates (sim, no new event kind or version).** A `CastSpell` whose spell has an `area` can be aimed: `Command::aim = true` with `Command::to` the aim tile and `request.targets` empty. Sim derives the targets (`Combat::area_targets`), puts them in the request (so the log and replay carry the derived list), logs `to` on the entry (`log_hash` already folds `to`), and emits the usual `SpellTarget` per target. Without `aim`, nothing changes: the caller's list is used, so no existing log or digest moves. Geometry is over footprint tiles, in tiles of 5 ft, measured with the sim's Chebyshev metric: **range 0 = self origin**: `cone` and `line` start at the caster's footprint centre and point toward `to` (the aim distance does not matter, only the direction; `to` may not be the centre tile); a cone is as wide as it is long (half angle atan 1/2); a `line` is `size_ft` long and `width_ft` wide; a `cube` is the `size_ft` square touching the caster's footprint on the side the aim points to (dominant axis, ties prefer x), centred across; `sphere`/`cylinder` are centred on the caster (radius from the nearest footprint tile; cylinder height is ignored, areas are 2D). **Range > 0 = point origin**: `to` must be within range (nearest tile) and in the caster's line of sight; `sphere`/`cylinder` have radius `size_ft` about it, a `cube` is centred on it. A creature is inside when any tile of its footprint is inside the template and line of sight from the origin (the aim tile for a point origin, any caster footprint tile for self origin) reaches that tile. The caster is never a target; allies are (the AI never chooses an aim that catches one). Area spells with range 0 and a caller-supplied list now require each target within `area_size_ft` and in sight of the caster (previously only the caster itself passed).

**`Footprint` (v6).** One per multi-tile creature (Large 2x2, Huge 3x3, Gargantuan 4x4; monster@3 / character@3 content only), emitted once at the start of the combat right after the opening rules events, in spawn order: `actor` = the creature, `tile` = its anchor, `rules.a` = the footprint side in tiles (2, 3 or 4); every other `rules` field is zero. The **anchor** is the footprint's min-x / min-y tile and the footprint is the square `[tile.x, tile.x + side) x [tile.y, tile.y + side)`. Every later `TileMoved` of that creature is its new anchor, and `SpellTarget.tile` is the target's anchor. A creature with no `Footprint` event occupies exactly the tile its events name (1x1, every pre-v6 creature). Combats with no multi-tile creature emit nothing new. Reach, range and opportunity attacks are measured to the nearest footprint tile, so a viewer sees an opportunity attack on a Large's step only when no tile of its new footprint is still in reach.

**Tile frame.** A single-chunk combat (`Combat::create(reg, chunk, ...)`) uses chunk-local tiles, as in every schema version. A multi-chunk combat (`Combat::create(reg, span<const Chunk*>, ...)`) uses world tiles, `chunk.coord * 32 + local`. Field layout and meaning per combat are unchanged, so the version is not bumped. Consumers of a multi-chunk stream map tiles with `Grid::chunk_of`.

`reaction_depth` is 0 for events of the action itself and 1 or more for events produced while a reaction resolves. For example, an opportunity attack's `AttackResolved` has depth 1.

The authoritative per-kind field meanings are in `rules5e/encounter.hpp`. If that header and this table ever disagree, the header wins, and the disagreement is a documentation bug.

## Ordering

1. **Opening.** One `InitiativeRolled` per combatant, then `TurnStarted` for the first actor.
2. **One block per completed request.** Each request that completes (`StepStatus::Done`) appends its rules events in emission order, followed by any sim events for that request. Rejected requests append nothing.
3. **Pauses append nothing.** While a reaction decision is pending (`StepStatus::NeedsDecision`), nothing is appended. The provisional events in `SubmitResult` are not part of the stream. When the decision completes the request, the whole request's events are appended at once. A UI that wants a preview may show the `needs` prompt, but it must not render provisional events as fact.
4. **`TileMoved`.** Sim events come after the request's rules events. There is one `TileMoved` per rules `Moved` that changed a tile, in the order of those `Moved` events, and each has the same actor as its `Moved`.
   - The mover's own Step produces a depth-0 `Moved`.
   - A readied Move that fires inside another creature's request produces a `Moved` at depth ≥ 1 for the readier, after `ReadyTriggered`. It ends on the destination chosen at Ready time.
   - An interrupted move (`MoveInterrupted`) produces no `TileMoved`.
   - Since v3, a `TileMoved` is not necessarily directly after its `Moved`.
   - **Movement flags (v8).** A rules `Moved` carries flag bits in `c` (`kMovedForced` = 1, `kMovedTeleport` = 2, `kMovedDrag` = 4; `c` = 0 for every ordinary move and in every pre-v8 stream). The 5 ft shove push is the rules `Moved{actor = target, target = shover, a = 5}` with `c = 1` (forced); sim moves the target one tile straight away only if it is free, so a forced `Moved` may have no `TileMoved`. When a grappler's own Step drags a creature it holds, sim appends `Moved{actor = held, target = grappler, a = feet, b = 0, c = 4}` (drag, depth 0) directly before that creature's `TileMoved`, after the grappler's `TileMoved`; it is a sim-emitted rules event and opens no reaction or Ready window. Teleport (bit1) is reserved: nothing sets it yet.
5. **Reactions.** These events are nested inside the triggering action's block in this order:
   - `ReactionWindowOpened`
   - then for each polled reactor, `ReactionTaken` or `ReactionDeclined`
   - then the reaction's effects at depth ≥ 1
   - then the rest of the action
   - **`ActionUsed` (v7).** A non-spell action (`SpellDef::is_spell == false`, e.g. a breath weapon) emits `ActionUsed` at the position a spell's `SpellCast` would take, *instead of* `SpellCast` (no `SpellCast` window, so it cannot be Counterspelled). Its `ResourceSpent` (pool usage) follows directly, then the effect's own events (`SavingThrowRolled`, `DamageDealt`, `ConditionAdded`, ...), then `SpellResolved`, which is still emitted. `RechargeRolled` is emitted at the start of the owner's turn, after `TurnStarted` and any repeat saves and regeneration, before death saves.
6. **Turns.** `TurnEnded` is followed by `TurnStarted` of the next actor (dead combatants are skipped).
7. **End.** `EncounterEnded` appears exactly once and is the last event. After it, `submit()` is rejected.

## Digest versioning (v5, v6, v7, v8, v9)

`event_stream_hash` records `event_stream_version(events)`, the lowest schema version that can express the stream, taken as the **maximum over every construct** in it, from both kind families: 9 for a rules event whose kind is past `kLastRulesEventKindV8` (the R-G11 zone kinds 45-47 and anything appended later) or a `FeatureTriggered` whose `b` is a zone `TriggerOn` (>= 8), 8 for a rules `Moved` whose `c` is non-zero (v8 movement flags), 7 for a rules event whose kind is past `kLastRulesEventKindV6` (the v7 kinds 39-44), 6 for a `Footprint`, 5 for a `SpellTarget`, otherwise 4. No construct masks a later, newer one (the function has no early return). `kSimEventSchemaVersion` (9) is the contract version, and no stream's version exceeds it (tested). Streams without a `CastSpell` or a multi-tile creature therefore keep their v4 digests bit-identical, streams with spells but no multi-tile creature keep their v5 digests, and every locked digest from before v6 is unchanged. Only a stream with a multi-tile creature hashes as version 6, only a stream with a v7 kind as version 7, only a stream with a flagged `Moved` (a 5 ft shove push, a grapple drag, an R-G9 spell push or a teleport) as version 8, and only a stream with a zone (R-G11) as version 9.

## Hash tag registry

Tag bytes are globally unique across `Combat::hash` and `Combat::log_hash` (separate streams, one registry). **Rule: a new tag is
claimed in this table first**, in the PR that introduces it (or before), and is folded only when its section is non-empty, so every
existing hash is unchanged. Next free: **0xAE** in `Combat::hash` (0xAD is priced materials), **0xBB** in `log_hash` (0xBA is Font of Magic, VERSION_BUMP_BATCH 7.2o; 0xAC and 0xB8 are the R-G15 spell kinds, sim wiring of rules #105 / #106; 0xAA is `leftover_digest`; 0xAB and 0xB7 are claimed by R-G11 zones rules PR 1, `docs/proposals/ZONES_AURAS.md`; 0xB9 is claimed by R-G11 zones rules PR 2). Status: *main* = verified in
`engine/core/sim/src/combat.cpp` on main; *pending* = claimed by an open PR.

| Tag | Stream | Section (rules5e function) | Folded when | Owner / PR | Status |
|---|---|---|---|---|---|
| (none) | `Combat::hash` | `feature_uses_digest` | `features` non-empty | pre-v7 | main (legacy, untagged) |
| 0xA1 | `Combat::hash` | `action_uses_digest` (recharge actions) | `action_uses` non-empty | v7 | main |
| 0xA2 | `Combat::hash` | `modifiers_digest` (timed modifiers, row 12 + follow-ups) | `has_modifier_state` | row 12 | main |
| 0xA3 | `Combat::hash` | `pools_digest` (resource pools) | `pools` non-empty | v7 / row 13 | main |
| 0xA4 | `Combat::hash` | `innate_digest` (innate uses, DC / attack overrides) | `has_innate_state` | row 8 | main |
| 0xA4 | `log_hash` | one byte per logged `CastSpell` with `innate = true` (same feature as the `Combat::hash` 0xA4 section; same byte value, different stream) | `innate` set | row 8 | main |
| 0xA5 | `Combat::hash` | `timed_effects_digest` (lasting effects with timers) | `has_timed_effects` | row 5 | main |
| 0xA6 | `Combat::hash` | `triggers_digest` (once-per-turn / round bookkeeping, incl. `death_triggers_done`) | `has_trigger_state` | row 9a | main |
| 0xA7 | `Combat::hash` | `sourced_digest` (conditions with a source; sim folds Grappled entries only) | `has_sourced_conditions` | R-G8, rules #99; sim fold EC #104 (merged) | main |
| 0xA8 | `Combat::hash` | `surprised_digest` (surprised creatures, round 1) | `has_surprised` | R-G16, rules #108 (merged); sim fold EC #123 (`Combat::apply_surprise`) | main |
| (0xA9) | both | retired, not reused. EC #104 folded `ActionRequest::special` under 0xA9 in `log_hash`, colliding with the registry's 0xA9 tag for small leftovers (`leftover_digest`, rules #111). `special` moved to 0xB5 in `log_hash` (EC #114), and `leftover_digest` has since moved to 0xAA in `Combat::hash` (rules #112) | n/a | rules #112 / EC #114 | retired |
| 0xAA | `Combat::hash` | `leftover_digest` (Life Drain `hp_max_reduction`, Haste `lethargy`, `reckless_active` per combatant; escalating Petrifying Gaze effects) | `has_leftover_state` | small leftovers, rules #111 (merged; claimed 0xA9, moved to 0xAA in rules #112 because EC #104 folds `special` under 0xA9 in `log_hash`); sim fold EC #129 | main |
| 0xAB | `Combat::hash` | `zones_digest` (R-G11 zones: `next_zone_id`, every `ActiveZone` field incl. the once-per-turn `fired` bookkeeping, then each zone-bound effect, `ActiveSpellEffect::zone_id` != 0, such as Web's restraint; rules PR 2 appends per zone `size_ft`, `has_end_tile`, `end_x`, `end_y`, `side`, `ring`, `feature` after `fired`, and one final `TurnBudget::action_lost` byte) | `has_zone_state` (a zone exists or has existed: `zones` non-empty or `next_zone_id` != 1) | R-G11 rules PR 1 + PR 2 (`docs/proposals/ZONES_AURAS.md` §2.8; rules side written); sim fold not yet written | pending (sim fold) |
| 0xAC | `Combat::hash` | R-G15 spell-kind effect state, folded inline by sim (no rules5e function): count u32, then per effect with `damage_when != 0` or `ends_on_damage` (application order): spell u32, caster u32, target u32, flags byte (bit0 `no_condition`, bit1 `ends_on_damage`, bit2 `magical`), `damage_when` byte, `damage` count / sides bytes + bonus i16, `damage_type` byte, `cast_stamp` u32, `slot_level` byte. `Combatant::died_round` and the `slot_level` of other effects / modifiers are not folded (derived from the logged actions; folding them would move goldens with a death or a timed effect) | some effect has `damage_when != 0` or `ends_on_damage` (Sleep-style pools with `ends_on_damage`, Acid Arrow, Heat Metal) | R-G15, rules #105 / #106; EC #120 | pending |
| 0xAD | `Combat::hash` | priced material components consumed (CONTENT_CONVENTIONS §7), folded inline by sim (no rules5e function): count u32, then per consumption in cast order: caster u32, spell u32, index of the entry in the caster's combat-local equipment at that moment u32, its `value_gp` u32. The remaining equipment is not folded (content plus this list derive it) | at least one consumption (`Combat::materials_consumed()` non-empty) | priced materials, EC (this PR) | main |
| 0xB0 | `log_hash` | `ActionRequest::bonus_option` byte | `bonus_option != 0` | R-G6, EC #100 (merged) | main |
| 0xB1 | `log_hash` | R-G14 per-cast options: `metamagic` byte, `spared` (count u32 + ids u32), `heighten_target` u32, `damage_choice` byte, `condition_choice` byte, in that order | any of them non-zero / non-empty (`condition_choice` also selects Lesser Restoration's condition on an `end_condition` spell: 1 + index into the spell's `conditions` in JSON list order, `SpellDef::ends_condition_list`, never Condition enum order) | R-G14, rules #109 (merged); sim fold in `Combat::log_hash` (Lesser Restoration / Acid Arrow follow-up) | done |
| 0xB2 | `log_hash` | R-G9 teleport destination (reserved: `docs/proposals/FORCED_MOVEMENT.md`, PR #110, names it for the R-G9 sim PR) | destination set | R-G9 sim PR (Engine Core) | reserved |
| 0xB3 | `log_hash` | small leftovers: one byte, bit0 `haste_action`, bit1 `reckless`, bit2 `aggressive` | the byte non-zero | small leftovers, rules #111 (merged); sim fold EC #129 | main |
| 0xB4 | `log_hash` | R-G9 cast choices: `forced_feet` u16, `teleport_feet` u16, in that order (the destination tile itself is 0xB2) | either non-zero | R-G9 rules (this PR); sim fold not yet written | pending (sim fold) |
| 0xB5 | `log_hash` | `ActionRequest::special` byte (1 Grapple, 2 Shove prone, 3 Shove 5 ft, 4 Escape, 5 Wake) | `special != 0` | R-G8, EC #104; tag fixed (was 0xA9) in EC #114 (merged) | main |
| 0xB6 | `log_hash` | `ActionRequest::two_handed_grip`: one byte, 1 | `two_handed_grip` true | R-G7, EC #114 (merged) | main |
| 0xB7 | `log_hash` | R-G11 caller-chosen zone inputs: `zone_move` u32, `has_zone_dest` byte, `zone_x` i32, `zone_y` i32, `zone_excluded` (count u32 + ids u32), in that order. Membership fields (`zones_entered` / `zones_left` / `zones_within`) are **not** folded (sim derives them at submit, Q11) | `zone_move != 0`, `has_zone_dest` or `zone_excluded` non-empty | R-G11 rules PR 1; sim fold not yet written | pending (sim fold) |
| 0xB8 | `log_hash` | `ActionRequest::sustain` (R-G15 Heat Metal again: bonus action, no slot): one byte, 1 | `sustain` true | R-G15, rules #106; EC #120 | pending |
| 0xB9 | `log_hash` | R-G11 rules PR 2 wall choices (Wall of Fire): `has_zone_end` byte, `zone_x2` i32, `zone_y2` i32, `zone_side` byte (int8: +1 / -1, 0 = none), `zone_ring` byte, in that order | `has_zone_end`, `zone_side != 0` or `zone_ring` | R-G11 rules PR 2 (`docs/proposals/ZONES_AURAS.md` §2.10); sim fold not yet written | pending (sim fold) |
| 0xBA | `log_hash` | Font of Magic Flexible Casting (`ActionKind::FlexibleCasting`): `flexible` byte (1 slot to points, 2 points to slot), `flexible_level` byte, in that order. State needs no `Combat::hash` section: points are the 0xA3 pool, slots follow from the `ResourceSpent` event (b = points spent, negative = gained) | `flexible != 0` | Font of Magic rules (VERSION_BUMP_BATCH 7.2o, #147); sim fold in `Combat::log_hash` (Font of Magic sim wiring) | done |

## Determinism guarantees

- **Same inputs, same stream.** The same content, chunk, seed and command log produce a bit-identical stream on every platform and compiler. The sim and rules use integer math only, with fixed iteration orders (ARCHITECTURE §4).
- **Replay.** `replay(seed, log)` reproduces the stream exactly without policies, scripts or decision callbacks.
- **Digest.** `event_stream_hash(events)` is the canonical XXH3-64 digest. It covers the version, the count, and every field of every event in order. Use it for goldens and for desync checks between peers.
- **No time-based fields.** Events carry no timestamps, pointers, floats, or anything else that depends on wall-clock time or memory addresses.

## Versioning policy

- **The contract.** `kSimEventSchemaVersion` versions the contract: `SimEvent` fields, `SimEvent::Kind` values, the surfaced `rules5e::EventKind` values, field meanings, and the ordering rules above.
- **Append-only.** New kinds and new fields are appended only. Existing numeric values and field meanings never change.
- **Version bump.** Any change consumers can observe bumps the version, adds a changelog entry below, and updates `events_tests.cpp`. Observable changes include a new kind, a new field, a new meaning, or an ordering change. The locked digests change deliberately, with an explanation in the PR.
- **New rules kinds.** When rules5e appends an `EventKind`, sim bumps the version and `kLastRulesEventKind` in the same PR that surfaces it. The layout test locks every value up to `kLastRulesEventKind`.
- **Consumer behaviour.** Consumers must ignore unknown `SimEvent::Kind` and `rules5e::EventKind` values (skip them), so newer streams degrade gracefully.
- **Recorded streams.** A stream stored outside the process (save files, replays, network) records the version. Prefer storing seed plus command log and regenerating the stream.

## Changelog

- **v1** (sim v0, PR #14): `Rules` and `TileMoved`; rules5e EventKind values 0–33.
- **v2** (feature hooks): rules5e `FeatureUsed` (34), emitted for content features such as Second Wind. `Healed` from a feature carries the user as actor. Nothing else changed. The digest changes for every stream because it includes the version.
- **v3** (Ready and ranged): readied Moves emit `TileMoved` for the readier. `TileMoved` events are placed after the request's rules events instead of directly after their `Moved` (ordering rule 4). Ranged attacks add no new kinds.
- **v4** (spellcasting, PR #23): rules5e `SavingThrowRolled` (35), `ConcentrationEnded` (36), `SpellEffectEnded` (37) and `SpellCheckRolled` (38). Sim doesn't cast spells yet, so no sim stream emits them; the digest changes for every stream because it includes the version.
- **v5** (CastSpell, sim): `SimEvent::Kind::SpellTarget` (2), appended per affected creature of a `CastSpell`. Sim now surfaces the v4 spell kinds 30-38 (it supplies the spell hooks). The digest records the lowest version that fits the stream (see "Digest versioning"), so no existing stream's digest changed. Viewers that skip unknown kinds (the sample `event_feed`) keep working.
- **v6** (multi-tile footprints, sim): `SimEvent::Kind::Footprint` (3), one per Large+ creature at combat start. `TileMoved` / `SpellTarget` tiles of such creatures are anchors. Emitted only when a footprint is > 1, and the digest records the lowest version that fits, so no existing stream's digest changed.
- **v7** (rules foundation, version-bump batch row 5): rules5e `RechargeRolled` (39), `FeatureTriggered` (40), `ResourceSpent` (41), `ModifierApplied` (42), `ModifierExpired` (43) and `ActionUsed` (44), plus the `ActionUsed` ordering rule. `SimEvent::Kind` is unchanged. Nothing existing emits the new kinds, and `event_stream_version` returns 7 only for streams that carry one, so no existing stream's digest changed. `Combat::hash` gains tagged sections (0xA1 action uses, 0xA3 pools), folded only when non-empty. Innate casting (row 8, sim side) adds tag 0xA4 (`innate_digest`, only when `has_innate_state`); `Combat::log_hash` folds a `0xA4` byte per `CastSpell` logged with `innate = true`, and nothing otherwise. Event triggers (row 9a, sim side) add tag 0xA6 (`triggers_digest`: once-per-turn / round bookkeeping), only when `has_trigger_state`; Row 9b (sim side): `save` predicates, `bonus_attack` (value = attack index resolved from `attack_id`) and `area_save` riders (a synthetic Save `SpellDef`, `is_spell` false, kept in the SpellBook and `Archetype::trigger_spell_ids`, not in `spells_known`) are compiled; `EncounterHooks::area_targets` centres the template on the source's tile even when it is dead (sphere/cylinder need no aim; other shapes aim at the nearest living creature, lowest id on ties); the AI spends a `bonus_attack_grant` as `Attack{as_bonus_action, attack_index = grant - 1}`. `TurnBudget` is not hashed by sim, so the grant adds nothing to the digest. Row 12 (sim side): spell and feature `apply_modifier` content compiles into `SpellDef::modifier` / `SpellEffect::modifier` (rider, `applies_to: affected`) and `Archetype::feature_modifiers`; `Combat::hash` folds `modifiers_digest` under tag 0xA2 only when `has_modifier_state` (it covers the follow-up fields too); `ModifierExpired.b` reasons: 0 duration ran out, 1 concentration ended, 2 replaced, 3 spent by an attack roll (`consume` 1 / 2), 4 the target took damage (`consume` 3). Modifier content properties (loader names): `consume` = `next_attack` | `next_attack_against` | `until_damaged`, `speed_sub` (feet), `ac_floor` + `ac_floor_dex`, `per_turn` (temp_hp only), `when` (row 9 predicates). "Not wearing armor" (Mage Armor) is decided by the floor itself: sim has no equipment model, and `ac_floor` only ever raises AC to at least `floor (+ Dex)`, so a target in armor that already gives more is unaffected and one in lighter armor gains up to the floor; the loader adds no armor check. R-G8 (sim side): `Combat::hash` folds the sourced conditions under tag 0xA7 only for entries that carry Grappled (target, source, escape DC), only when `has_sourced_conditions`; spell-applied Charmed / Frightened entries are left out so no golden moves. `Combat::log_hash` folds `ActionRequest::special` (1 Grapple, 2 Shove prone, 3 Shove 5 ft, 4 Escape) as 0xB5 + value only when non-zero (it was 0xA9 until EC #114, which collided with the `Combat::hash` 0xA9 section); `bonus_option` (R-G6) is 0xB0 + value only when non-zero; `two_handed_grip` (R-G7) is 0xB6 + 1 only when set. Small leftovers (rules #111, sim EC #129): `Combat::hash` folds 0xAA + `leftover_digest` only when `has_leftover_state`; `Combat::log_hash` folds 0xB3 + one byte (bit0 `haste_action`, bit1 `reckless`, bit2 `aggressive`) only when it is non-zero. After an Aggressive Dash (`ActionRequest{Dash, as_bonus_action, aggressive}`) the movement from before the Dash is spent first, and a Step that needs the Dash's movement must end nearer (footprint distance) to a non-dead hostile the mover can see (not Blinded, line of sight); sim refuses any other (`Combat::aggressive_step_ok`; derived from the log, not hashed). The 5 ft shove is applied by sim from `Moved{actor = target, target = shover}` (one tile straight away, only if free); a grappler's Step drags the creatures it holds (same offset, else the vacated tile); an effect that leaves holder and held more than 5 ft apart removes Grappled (sim emits `ConditionRemoved`). triggers are compiled by `ContentLibrary` from `on_event` effects and `on_hit[]` riders (rider scaling: extra dice = (class level - 1) / every).
- **v8** (R-G8 forced movement, sim): flag bits in a rules `Moved` event's `c` (bit0 forced, bit1 teleport (reserved), bit2 drag; `kMovedForced` / `kMovedTeleport` / `kMovedDrag` in `events.hpp`). The 5 ft shove push's `Moved` carries bit0; a creature dragged by its grappler's own Step gets a sim-emitted `Moved{actor = held, target = grappler, a = feet, c = 4}` before its `TileMoved` (ordering rule 4). `SimEvent::Kind` and the rules kinds are unchanged. `event_stream_version` returns 8 only for a stream with a `Moved` whose `c` != 0; nothing before R-G8 sets `c` on a `Moved`, so no existing stream's digest changed and no golden was re-pinned. R-G9 (rules side, `docs/proposals/FORCED_MOVEMENT.md`, VERSION_BUMP_BATCH 7.2k): rules emits `c = kMovedForced` for `ForcedMove` effects (Thunderwave, Repelling Blast) and `c = kMovedTeleport` for `Teleport` (Misty Step), with `target` = the source and `b` = 0; the flag values live in `rules5e/encounter.hpp` and sim's `kMoved*` names alias them (one definition). rules' own shove `Moved` stays `c = 0` unless `EncounterHooks::shove_forced_flag` is set (default false): sim flags the shove push itself today, so nothing is flagged twice and #104's digests don't move. Every forced move / teleport of a step resolves after all of the step's decision points, so `forced_move` is called once per (step, step_seq); order: the effect's saves and damage, then one `Moved` per creature in `forced_move_order`, each followed by `ConditionRemoved{Grappled}` for grapples the hook reports out of reach. `log_hash` 0xB4 (`forced_feet`, `teleport_feet`) and 0xB2 (destination, R-G9 sim PR) are in the registry.
- **v9** (R-G11 zones and auras, rules PR 1, `docs/proposals/ZONES_AURAS.md`, VERSION_BUMP_BATCH 7.2l): rules5e `ZoneCreated` (45), `ZoneMoved` (46) and `ZoneEnded` (47); `TriggerOn` gains `ZoneAppear` 8, `ZoneEnter` 9, `ZoneStartTurn` 10, `ZoneEndTurn` 11 and `ZoneMoveWithin` 12, carried in `FeatureTriggered.b` (a = spell id, c = 0 saved / 1 no save / 3 failed save) before each zone trigger's save, damage and condition events; `CheckRolled.target` = `0x100 | Ability` for a plain ability check (Web's Strength check to break free). Order: a zone's `ZoneEnded{1}` follows `ConcentrationEnded` and precedes the `SpellEffectEnded` of its "while in the zone" effects; `StartTurn` rows follow `TurnStarted` (before `RepeatSave::StartOfTurn` and row 9 `TurnStart` triggers); `EndTurn` rows precede `RepeatSave::EndOfTurn` and `TurnEnded`; `Enter` / `MoveWithin` rows follow the step's `Moved` (for a forced move: its `Moved` and `ConditionRemoved{Grappled}`, before the next forced move). `SimEvent::Kind` is unchanged and no new `SimEvent` exists (Q12). `event_stream_version` returns 9 only for a stream with a zone kind or a zone `FeatureTriggered`; no existing content has a zone, so no existing stream's digest changed and no golden was re-pinned.
  - **R-G11 rules PR 2** (7.2m) adds no kind and keeps v9: a zone `FeatureTriggered` may carry `c = 2` (automatic success: Stinking Cloud against a poison-immune or non-breathing creature, with no `SavingThrowRolled`); Silence's deafness is a plain `ConditionAdded` / `ConditionRemoved` pair with no `FeatureTriggered`; `add_feature_aura` emits `ZoneCreated` (target = owner) when the caller passes an event list, and a feature aura of a dead owner ends at the next round start with `ZoneEnded{2}`.
- **R-G16, sim side (no version change: no new event kind or field).** `Combat::apply_surprise(ids)` marks creatures surprised right after `create()` and before the first command (rules5e reads the flag only during turns, so this equals setting it before `begin_encounter`); `Combat::surprised_by_stealth(hiders)` wraps the rules helper. `Combat::hash` folds `surprised_digest` under 0xA8 only while `has_surprised` (round 1 of an ambush), so every other combat and every golden hashes as before. The surprised set is a replay input like the seed: `replay(..., surprised = combat.surprised_at_start())`; it is not folded into `log_hash`. The headless driver ends a surprised creature's turn. Rests (`sim/rest.hpp`) run outside any Combat on a roster the caller keeps (`EncounterState` copy): no events, no log entry, no hash change; Hit Dice draw only from the rest stream `Pcg32(derive_seed(seed, {0x52455354 "REST"}), 2)`, never the encounter's `Pcg32(seed, 0)`. No new tag was claimed.
