# Forced movement and teleport (R-G9): rules / sim co-design proposal

Status: **proposal, docs only.** The RULES section is written by Rules. The SIM CONTRACT section is a stub that carries Engine
Core's starting design and is for Engine Core to complete. Nothing here is implemented yet. Tracks `docs/SRD_COMBAT_COVERAGE.md`
§11.5 order 8 (R-G9) and the rows it unlocks: Thunderwave, Gust of Wind, Misty Step, Repelling Blast, Open Hand Technique, blink
dog teleport, and the "Opportunity-attack exceptions" row.

Source: SRD 5.1 (CC-BY-4.0), as mirrored for `SRD_COMBAT_COVERAGE.md` §12. Code facts are as of main at `371f2cd` (after #109).

## 1. Where things stand today

- **Positions are sim's.** rules5e has no coordinates. It reasons only through sim facts passed in requests (`target_within_5ft`,
  `leaves_reach`, `move_tiles`) and hooks (`in_range`, `area_targets`, `spell_target_ok`, `targets_within`).
- **One forced move already exists.** Shove to push (`ActionRequest::special = 3`, R-G8, #99) emits
  `Moved{actor = pushed creature, target = shover, a = 5, b = 0, c = 0}`. Sim does not apply it yet (EC #104 is open).
  `Combat::complete` moves a token only for the actor's own depth-0 `Move`, or for a readied move.
- **Moved today:** `actor = mover, a = feet, b = movement left`. `target` and `c` are 0 for voluntary moves. The sim layout test
  (`events_tests.cpp`) locks EventKind values, not payloads. `SIM_EVENTS.md`'s versioning policy treats a new field meaning as an
  observable change, so it needs a version bump.
- **Opportunity attacks** are offered only through `LeavingReach` windows, which a voluntary `Move` with `leaves_reach = true` opens.
  Nothing else opens one, so a forced `Moved` provokes nothing today, and neither would a teleport.
- **Grapple bookkeeping.** `prune_sourced` (after every step) ends Grappled when the grappler is dead or incapacitated. Nothing ends
  it on distance, because rules cannot see distance.
- **Readied actions.** Sim's `ready_trigger` hook fires on any `Moved` whose actor is not the readier, so a forced `Moved` would
  trigger "when a creature moves within N" Readies as written (see open question Q7).

## 2. RULES section (Rules: filled in)

### 2.1 SRD 5.1 behaviour to support

| Case | SRD 5.1 rule | Distance / direction | Save / contest | Size limit |
|---|---|---|---|---|
| Shove (push) | Athletics vs Athletics / Acrobatics; win: push 5 ft away (or knock prone) | 5 ft, away from the shover | contest (done, #99) | target at most one size larger (done) |
| Thunderwave | 15-ft cube from the caster; Con save; fail: 2d8 thunder and pushed 10 ft away; success: half, no push | 10 ft, away from the caster | save; push only on a failed save | **none in SRD 5.1** (see Q9) |
| Gust of Wind | 60-ft line; Str save for each creature that starts its turn in the line; fail: pushed 15 ft away from the caster along the line | 15 ft, along the line, away from the caster | save | none |
| Repelling Blast (invocation) | Eldritch Blast hit: push up to 10 ft away in a straight line (per beam) | up to 10 ft, away from the caster, the caster chooses the distance in 5-ft steps | none (on hit) | none |
| Open Hand Technique | Flurry hit: Dex save or knocked prone, **or** Str save or pushed up to 15 ft away, **or** no reactions | up to 15 ft, away from the monk | save | none |
| Pull (generic) | Some monster actions pull a creature toward the source (none of our SRD spells) | N ft, toward the source, stopping adjacent | per effect | per effect |
| Misty Step | Bonus action; self; teleport up to 30 ft to an unoccupied space you can see | up to 30 ft, destination chosen | none | self only |
| Dimension Door | Action; self, plus one willing creature of your size or smaller that is within 5 ft when you cast (it travels with you; where it lands next to you is a sim decision); up to 500 ft; you can picture the place or state a direction and distance (no sight needed); if you would arrive in an occupied space, you and the creature take 4d6 force damage each and the teleport fails | up to 500 ft, destination chosen | none | the companion: willing, caster's size or smaller |
| Blink dog teleport | Teleport up to 40 ft to an unoccupied space it can see (monster action) | up to 40 ft, destination chosen | none | self |

Rules that cut across these cases:

- **Opportunity attacks.** SRD 5.1 "Opportunity Attacks": you don't provoke one when you teleport, or when someone or something
  moves you without using your movement, action or reaction. A forced move or a teleport **never opens a `LeavingReach` window**
  unless the effect says so (`provokes_oa`, default false). Movement the creature makes with its own reaction (for example a
  readied move) still provokes, as today.
- **Movement budget.** Forced movement and teleport don't spend the moved creature's speed. `Moved.b` is 0 and is not
  "movement left". A speed of 0 (Grappled, Restrained) doesn't stop a forced move or a teleport.
- **Grapple ends when moved out of reach.** SRD 5.1 Grappled: the condition ends "if an effect removes the grappled creature from
  the reach of the grappler or grappling effect, such as when a creature is hurled away by the thunderwave spell". That covers:
  - the grappled creature being pushed;
  - the grappler being pushed: forced movement is not a drag, so the grappled creature stays behind;
  - either one teleporting.
  The grapple ends only if the result leaves them out of reach. Rules ends the Grappled entry and emits `ConditionRemoved`.
  The sourced entry is pruned.
- **Drag is not forced movement.** A grappler's own `Move` with drag (half speed, R-G8) is voluntary movement of the grappler. Sim
  moves the grappled creature along the same path (EC's design). Rules emits `Moved` for the grappler only, as today (Q6).
- **Frightened.** The creature can't *willingly* move closer to the source of its fear.
  - Forced movement is not willing, so a push or pull toward the source is allowed.
  - A teleport the creature chooses itself (Misty Step, blink dog) is willing movement: the destination must not be closer to any
    source it is frightened of. Sim checks this with the distance it owns. Rules passes the sources (`condition_source` /
    `SourcedCondition`, R-G8).
- **Charmed.** No movement restriction in SRD 5.1. A charmed creature is a valid target for push and teleport-other effects as
  usual. "Willing" is the creature's choice (Q4); rules does not make a charmed creature willing.
- **Incapacitated / unconscious creatures** can be pushed or pulled. They can't take a self-teleport action. As a Dimension Door
  companion: an unconscious creature can't consent, so it is not willing (Q4).
- **Prone.** Forced movement doesn't make a creature prone or end prone. Crawling cost doesn't apply, because no movement is spent.
- **Hazards and ledges.** A forced move can end over a pit, off a ledge, or into a damaging zone. SRD 5.1 falling: 1d6
  bludgeoning per 10 ft fallen (max 20d6), and the creature lands prone unless it avoids the damage. This proposal reserves the
  **hook result** for it (2.4). Falling damage itself is a later rules PR. Zones are R-G11.
- **Size limits** are per effect: `max_size` on the effect (Size; none = no limit) and the existing `size_within_one` for shove. A
  creature over the limit is unaffected by the push part only. The damage and save still happen.
- **Several creatures moved by one effect** (Thunderwave on three) resolve in a deterministic order: the targets' order in the
  request, which sim builds from `area_targets` (Q2).
- **Distance rounding.** Feet are multiples of 5 (one tile = 5 ft). "Up to N ft" effects take the chosen distance, which must be a
  multiple of 5 and at most N. Fixed effects use exactly N, and sim may stop short (blocked).

### 2.2 Rules data model (proposed; all appended, defaults keep today's behaviour)

```cpp
// SpellEffect / SpellDef (spell@1 effect fields), and the rider kinds for on-hit features:
enum class SpellEffectKind : uint8_t { /* ..., */ ForcedMove /* = 14 */, Teleport /* = 15 */ };
struct ForcedMoveSpec {
    uint8_t  mode = 0;           // 1 push (away from the source), 2 pull (toward the source), 3 along a line / direction (Gust of Wind)
    uint16_t feet = 0;           // exact distance, or the maximum when up_to
    bool     up_to = false;      // the caster may choose a shorter distance (Repelling Blast, Open Hand)
    int8_t   max_size = -1;      // largest Size moved (-1 = no limit)
    bool     provokes_oa = false;// SRD: never by default
};
struct TeleportSpec {
    uint16_t max_feet = 0;       // Misty Step 30, Dimension Door 500, blink dog 40
    bool     needs_sight = true; // Misty Step yes; Dimension Door no
    uint8_t  companions = 0;     // Dimension Door 1 willing creature within 5 ft, caster's size or smaller
    DiceExpr occupied_damage{0, 0, 0}; DamageType occupied_type = DamageType::Force;   // Dimension Door 4d6 force, teleport fails
};
```

- `ForcedMove` works as a primary effect (rare) and, mainly, as a **rider**: `extra_effects` with `affected_only`, so Thunderwave
  pushes only creatures that failed the save. Repelling Blast is an on-hit trigger rider (`TriggerOn::Hit`, existing row 9 machinery).
  Open Hand Technique is a bonus-attack rider with a save.
- Shove (`special = 3`) keeps its own path. It becomes `ForcedMoveSpec{mode 1, feet 5}` internally, so it emits the same event.
- `Teleport` is a self effect (`targets` = the caster, plus companions in `ActionRequest::targets`). The destination is a sim fact
  carried in the request (2.3).

### 2.3 Requests (proposed)

- **Choices by the caster** ride on the request: `ActionRequest::forced_feet` (uint16; "up to N" distance, 0 = maximum) and, for a
  teleport, the destination. The destination is a tile, so it is a **sim** fact. Rules never interprets it: sim puts it in its log
  entry (as it already does for `to` on Move) and validates it before submitting. Rules gets only `teleport_feet` (distance to the
  destination, for the `max_feet` check). The same applies to `forced_line` (a direction, for Gust of Wind, sim's).
- **Willing companions** (Dimension Door): ids in `ActionRequest::targets` after the caster. Willingness is the caller's
  assertion (`ActionRequest::companions_willing`), because rules has no consent model (Q4). Rules checks size (caster's size or
  smaller), count, `target_within_5ft`-style adjacency through a new hook or a request fact, and that each one is alive.
- All new request fields are log-relevant and would claim a `log_hash` tag in `SIM_EVENTS.md` (next free 0xB2).

### 2.4 How rules emits a forced move: reuse `Moved` plus flags (preferred), else v8 kind

**Preferred: reuse `Moved`, give `c` a meaning, and bump the event schema to v8 for the new meaning only.**

```
Moved  actor = moved creature
       target = source (pusher / caster / teleporter; for a self-teleport = actor)
       a = feet actually moved (after sim's path; see the hook below)
       b = 0 for forced / teleport (it is NOT movement left)
       c = flags: bit 0 forced (not the creature's own movement), bit 1 teleport,
                  bits 2-3 direction (0 n/a, 1 away from target, 2 toward target, 3 to a chosen destination / along a line),
                  bit 4 stopped short (blocked / occupied before the full distance),
                  bit 5 hazard (ended in or fell through a hazard the hook reported)
```

- **Why reuse.** Pushes already flow through `Moved` (EC's design and the shove precedent). Consumers that ignore `c` (the event
  feed, the readied-move bookkeeping) keep working. No new `EventKind`, so the kind layout test, `kEventKindCount` (45) and
  `kLastRulesEventKind` don't move.
- **Versioning.** `c` gains a meaning, which `SIM_EVENTS.md`'s policy calls observable. So: schema v8, and
  `event_stream_version(events)` returns 8 only for a stream with a `Moved` whose `c != 0`. Every existing stream (all `c == 0`)
  keeps its digest. The shove push changes from `c = 0` to `c = forced | away (= 0b0101)`. That moves digests only for streams
  that contain a shove push. None is locked today; EC #104 would be the first, so land them together (Q1).
- **Fallback, if EC prefers a new kind:** `EventKind::ForcedMoved` (= 45) and `Teleported` (= 46), same payload, v8 via a new kind.
  This is cleaner for consumers that switch on kind, but costs two kinds and a layout-test change.

**Hook (preferred over a two-phase request).** Rules needs the outcome inside the same step: whether the move happened, how far, and
whether a grapple ended. So:

```cpp
struct ForcedMoveQuery  { CombatantId mover, source; uint8_t mode; uint16_t feet; bool teleport; uint32_t step_seq; };
struct ForcedMoveResult { uint16_t feet_moved = 0; bool stopped_short = false; bool hazard = false; uint16_t fell_ft = 0;
                          std::vector<CombatantId> out_of_reach_of; };   // grapplers / grappled no longer within reach
std::function<ForcedMoveResult(const EncounterState&, const ForcedMoveQuery&)> forced_move;   // EncounterHooks, appended
```

- The hook is **pure with respect to rules state**: it doesn't change `EncounterState`. Sim records the tentative position for the
  in-progress step (like `moving_to` today), so later queries in the same step see earlier moves. Sim commits from the `Moved`
  events when the step completes, and drops them if the attempt is discarded (an Ask pause re-runs the step from a copy).
- **No hook** (rules-only tests, old content): rules emits the event with `a` = requested feet. It does not end grapples on
  distance. This is today's shove behaviour.

### 2.5 Order of events inside a step (proposed ordering rule)

For each moved creature, in target order: `Moved{forced...}`, then `ConditionRemoved{Grappled}` for each grapple the hook reported
broken (both sides as applicable), then any hazard events (falling damage, later). The forced move comes after the effect's own
save / damage events for that creature (Thunderwave: `SavingThrowRolled`, `DamageDealt`, then `Moved`), and before
`SpellResolved`. A teleport emits `Moved{teleport}` for the caster first, then the companions in request order.

### 2.6 Rules / sim API split

| Decision | Owner |
|---|---|
| Whether a creature is moved at all: save failed, hit landed, size limit, willing companion, effect present | **rules** |
| Distance (fixed, or the chosen "up to"), mode (push / pull / line / teleport), and whether it provokes OAs | **rules** (from content + request) |
| Path: tiles, direction from the source footprint, blocking, occupied tiles, stopping short, multi-tile footprints | **sim** |
| Teleport destination validity: unoccupied, within `max_feet` (Chebyshev, footprint-aware), line of sight if `needs_sight`, Frightened "not closer" | **sim** (rules passes the facts it owns: max_feet, needs_sight, fear sources) |
| Grapple ends: is the pair still within reach after the move | **sim** answers (hook result); **rules** ends the condition and emits |
| Falling / hazard damage amount and prone | **rules** (later PR), from the hook's `fell_ft` / hazard facts |
| Committing positions, `TileMoved` sim events, readied-move destinations | **sim** |

### 2.7 Rules work once agreed (one rules PR, then the sim PR)

1. `ForcedMove` / `Teleport` effect kinds and specs, rider support, shove routed through them. Rules tests: no hook = requested
   feet; hook truncation; grapple pruning from `out_of_reach_of`; no `LeavingReach` window; speed 0 doesn't block; size limit; save gating.
2. `Moved.c` flags plus the v8 note in `SIM_EVENTS.md` (if EC agrees), or the two new kinds.
3. `forced_move` hook, `ActionRequest` fields (`forced_feet`, `teleport_feet`, `companions_willing`) plus their `log_hash` tag claim.
4. Coverage rows: Thunderwave, Gust of Wind, Misty Step, Repelling Blast, Open Hand, blink dog, OA exceptions.
5. Later: falling damage from `fell_ft`, and zone entry (R-G11).

## 3. SIM CONTRACT section (Engine Core: stub with EC's starting design, to be completed by EC)

> **EC to complete.** The bullets are Engine Core's starting proposal as relayed; the TODOs are open for EC.

- **Positions.** Sim positions are integer tiles (`TilePos`, chunk-local). A multi-tile footprint is anchored at its min-x / min-y
  tile (`grid.hpp`: `footprint_walkable(anchor, n)`). Distance between creatures is the nearest-tile Chebyshev distance between
  footprints (`rect_distance`), in tiles, × 5 ft.
- **Event flow.** Pushes already flow through `Moved` events with `actor = moved creature` (the shove precedent). Sim applies them
  to positions. *TODO EC: confirm the `c` flag layout (2.4) or pick the new-kind fallback; confirm the v8 rule "version 8 only when
  a Moved has c != 0".*
- **OAs.** Forced movement and teleport never provoke opportunity attacks unless an effect says so (`provokes_oa`). *TODO EC: how
  a `provokes_oa` forced move is offered. Proposed: rules opens `LeavingReach` itself when the hook reports a reach being left;
  needs `out_of_reach_of`-style data for enemies, not just grapple partners.*
- **Path resolution.** Sim resolves each forced step as a tile path that stops at blocked or occupied tiles.
  *TODO EC:*
  - direction for push / pull from footprint to footprint: centre-to-centre ray, or "increase / decrease Chebyshev distance each
    step"; tie-breaks when several tiles qualify;
  - whether a pushed creature can pass through allies' / enemies' spaces (SRD: you can move through a non-hostile creature's
    space, but a push is not your movement);
  - diagonals;
  - difficult terrain (forced movement ignores the cost: proposed).
- **Teleport.** *TODO EC:*
  - destination validation: unoccupied for the whole footprint, Chebyshev within `max_feet`, LOS when `needs_sight`, Frightened
    "not closer to a fear source";
  - Dimension Door companion placement next to the caster's destination (who picks the tile, tie-break; SRD 5.1 only says it travels with you);
  - the occupied-destination failure (4d6 force) when the destination is unknown or invisible.
- **Grapple drag.** Grapple drag uses the same path machinery: the grappled creature follows the grappler's path. *TODO EC: the
  order of the two tokens on each step; what happens if the grappled creature's footprint is blocked.*
- **Hook.** *TODO EC: accept or replace the `forced_move` hook (2.4), including the in-step tentative-position scratch and rollback
  on an Ask pause; or propose the two-phase alternative (rules emits intent, sim resolves after the step and submits a follow-up
  logged step).*
- **Hashing / log.** *TODO EC:*
  - teleport destination and Gust of Wind direction in the log entry (like Move's `to`);
  - the `log_hash` tag for the new request fields (claim in `SIM_EVENTS.md`, next free 0xB2);
  - whether `Combat::hash` needs anything (positions are already hashed).
- **Readied triggers.** *TODO EC: whether forced `Moved` (c bit 0) and teleports should fire movement-based Readies (Q7).*

## 4. Open questions

**For Engine Core**

- **Q1. Event encoding.** Reuse `Moved` with the `c` flags and a payload-only v8, or add new kinds? If reuse: land the shove push
  `c` change together with EC #104, so no locked digest moves twice.
- **Q2. Order of several forced moves in one effect.** Is "the request's target order" (sim's `area_targets` order) acceptable, or
  should sim order pushes far-to-near so the front creature doesn't block the one behind it?
- **Q3. Hook vs two-phase.** A synchronous `forced_move` hook with tentative positions and rollback on an Ask pause, or a
  follow-up logged step? The hook keeps one step = one log entry. Two-phase avoids sim state inside rules' attempt loop.
- **Q5. Footprint geometry for push direction**, and blocked-path semantics: stop at the last free tile, or no move at all if the
  first tile is blocked?
- **Q6. Drag and forced moves of a grappler.** Does sim move the dragged creature on the grappler's voluntary `Move` only (as
  proposed), never on a forced move of the grappler (where the grapple usually ends instead)?
- **Q7. Readied actions.** Should a forced move or a teleport fire "when a creature moves within N ft" Readies? Proposed: yes for
  both: the trigger is a perceivable circumstance, not a choice by the mover. But sim's `ready_trigger` should see the `c` flags.
- **Q8. Teleport destination UX/AI.** Who picks a destination for the AI (Misty Step out of melee), and how is an invalid one
  reported: request rejected before it reaches rules, or a rules rejection via a hook?

**For the Director**

- **Q4. "Willing."** There is no consent model. Proposed: the controller asserts it for its own team's creatures, and
  unconscious / incapacitated creatures are never willing. Charmed doesn't make a creature willing. Accept?
- **Q9. Thunderwave size limit.** The request mentioned "Thunderwave Large or smaller". SRD 5.1 Thunderwave has **no** size limit
  (and neither do Gust of Wind or Repelling Blast). Size limits come from shove (one size larger) and from some monster actions.
  Proposed: follow SRD 5.1 (no limit on Thunderwave), with `max_size` available per effect. Confirm.
- **Q10. Scope of the first implementation.** Proposed: push (Thunderwave, Repelling Blast, shove, Open Hand) and self-teleport
  (Misty Step, blink dog) first. Then Dimension Door companions and occupied-destination damage, Gust of Wind (line plus repeated
  saves, needs R-G10 / R-G11), and falling damage. Accept the order?
- **Q11. Hazards.** Should forced movement into a hazard wait for R-G11 zones, or ship falling damage (ledges / pits as sim terrain
  facts) with the first PR?
