# sim event stream (schema version 3)

`eng::sim::Combat::events()` is the ordered record of everything that happened in a combat. Render/platform, UI, logs and tests consume it. This document is the contract. The code lives in `engine/core/sim/include/sim/events.hpp`, and the layout is locked by `engine/core/sim/tests/events_tests.cpp`.

## Types

```cpp
inline constexpr uint32_t kSimEventSchemaVersion = 3;
inline constexpr rules5e::EventKind kLastRulesEventKind = rules5e::EventKind::FeatureUsed;

struct SimEvent {
    enum class Kind : uint8_t { Rules = 0, TileMoved = 1 };   // append-only
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
| 3 | DamageDealt | 0 | creature damaged | damage dealt | hp after | |
| 4 | ConditionAdded | creature | | Condition | | |
| 5 | CombatantDowned | creature | | | | |
| 6 | DeathSaveRolled | creature | | natural | successes | failures |
| 7 | Stabilized | creature | | | | |
| 8 | RegainedHp | creature | | hp regained | | |
| 9 | CombatantDied | creature | | | | |
| 10 | Moved | mover | | feet | movement left | |
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
| 25 | CheckRolled | creature | Skill | natural | total | bit0 success, bit1 helped |
| 26 | ReactionWindowOpened | trigger actor | (Ready: readier) | ReactionTrigger | window seq | |
| 27 | ReactionTaken | reactor | | ReactionTrigger | window seq | option index |
| 28 | ReactionDeclined | reactor | | ReactionTrigger | window seq | option index |
| 29 | MoveInterrupted | mover | | feet requested | | |
| 30–32 | SpellCast / SpellCountered / SpellResolved | caster | target (counter: countered caster) | spell id | level | |
| 33 | AcBonus | creature | | bonus | new AC | |
| 34 | FeatureUsed (v2) | user | | feature id | 1 if bonus action | |

`FeatureUsed` feature ids are 1-based indexes into the archetype's declared features (`features[]` then `traits[]`, see `sim/features.hpp`). A self-heal is followed by `Healed`, with actor and target both set to the user.

**Tile frame.** A single-chunk combat (`Combat::create(reg, chunk, ...)`) uses chunk-local tiles, exactly as in v1–v3. A multi-chunk combat (`Combat::create(reg, span<const Chunk*>, ...)`) uses world tiles, `chunk.coord * 32 + local`. Field layout and meaning per combat are unchanged, so the version is not bumped. Consumers of a multi-chunk stream map tiles with `Grid::chunk_of`.

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
5. **Reactions.** These events are nested inside the triggering action's block in this order:
   - `ReactionWindowOpened`
   - then for each polled reactor, `ReactionTaken` or `ReactionDeclined`
   - then the reaction's effects at depth ≥ 1
   - then the rest of the action
6. **Turns.** `TurnEnded` is followed by `TurnStarted` of the next actor (dead combatants are skipped).
7. **End.** `EncounterEnded` appears exactly once and is the last event. After it, `submit()` is rejected.

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
