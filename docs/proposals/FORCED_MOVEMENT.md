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
  unless the effect says so (`provokes_oa`, default false). In R-G9 `provokes_oa = true` is a **load error** (§3): no effect
  needs it yet. Movement the creature makes with its own reaction (for example a readied move) still provokes, as today.
- **Movement budget.** Forced movement and teleport don't spend the moved creature's speed. `Moved.b` is 0 and is not
  "movement left". A speed of 0 (Grappled, Restrained) doesn't stop a forced move or a teleport. Difficult terrain costs nothing
  for forced movement (§3).
- **Blocking (§3).** Any other creature blocks a push, allies included (a push is not the creature's own movement, so the
  move-through-a-non-hostile-space rule doesn't apply), as do blocked tiles and the map edge. The move stops at the last free
  tile. A creature blocked on its first step still gets `Moved{a = 0}`.
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
    source it is frightened of. Sim checks this when `Combat::submit` re-validates the teleport destination (§3), using the
    sources rules records (`condition_source` / `SourcedCondition`, R-G8).
- **Charmed.** No movement restriction in SRD 5.1. A charmed creature is a valid target for push and teleport-other effects as
  usual. "Willing" is the creature's choice (Q4); rules does not make a charmed creature willing.
- **Incapacitated / unconscious creatures** can be pushed or pulled. They can't take a self-teleport action. As a Dimension Door
  companion: an unconscious creature can't consent, so it is not willing (Q4).
- **Prone.** Forced movement doesn't make a creature prone or end prone. Crawling cost doesn't apply, because no movement is spent.
- **Hazards and ledges.** A forced move can end over a pit, off a ledge, or into a damaging zone. SRD 5.1 falling: 1d6
  bludgeoning per 10 ft fallen (max 20d6), and the creature lands prone unless it avoids the damage. This proposal reserves the
  **hook result** for it (2.4: `hazard` / `fell_ft`). Falling and hazard damage wait for zones (R-G11) and elevation
  (deferred), per the Director (Q11).
- **Size limits** are per effect: `max_size` on the effect (Size; none = no limit) and the existing `size_within_one` for shove. A
  creature over the limit is unaffected by the push part only. The damage and save still happen.
- **Several creatures moved by one effect** (Thunderwave on three) resolve farthest from the origin first, ties to the lower id,
  by nearest-tile Chebyshev distance between footprints. Sim sorts (Q2, §3).
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
    bool     provokes_oa = false;// SRD: never by default; true is a load error in R-G9 (§3)
};
struct TeleportSpec {
    uint16_t max_feet = 0;       // Misty Step 30, Dimension Door 500, blink dog 40
    bool     needs_sight = true; // Misty Step yes; Dimension Door no
    uint8_t  companions = 0;     // Dimension Door 1 willing creature within 5 ft, caster's size or smaller
    DiceExpr occupied_damage{0, 0, 0}; DamageType occupied_type = DamageType::Force;   // Dimension Door 4d6 force: deferred (§3, Q10)
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
- **Willing companions** (Dimension Door; **deferred** past the first PR, together with the occupied-destination 4d6 failure,
  per §3 and Q10): ids in `ActionRequest::targets` after the caster. Willingness is the caller's
  assertion (`ActionRequest::companions_willing`), because rules has no consent model (Q4). Rules checks size (caster's size or
  smaller), count, `target_within_5ft`-style adjacency through a new hook or a request fact, and that each one is alive.
- All new request fields are log-relevant. The teleport-destination `log_hash` tag (**0xB2**, the next free one) is claimed in the
  `SIM_EVENTS.md` registry by the R-G9 sim PR that adds the field. `SIM_EVENTS.md` is not part of this PR, so it is only noted here.

### 2.4 How rules emits a forced move: reuse `Moved` plus flags (Resolved (Engine Core), Q1)

**Superseded by §3.** The flag layout originally proposed here (direction, stopped-short and hazard bits) is replaced by §3's:
`c` bit 0 forced, bit 1 teleport, bit 2 drag, bits 3+ reserved 0. Feet actually moved is in `a`, so there are no direction,
stopped-short or hazard bits. Direction and stopping are sim's path decisions, and hazards stay hook results until zones exist.

```
Moved  actor = moved creature
       target = source (pusher / caster / teleporter / grappler; for a self-teleport = actor)
       a = feet actually moved (0 when blocked on the first step)
       b = 0 for forced / teleport / drag (it is NOT movement left)
       c = flags (§3): bit 0 forced, bit 1 teleport, bit 2 drag
```

- **Why reuse.** Pushes already flow through `Moved` (EC's design and the shove precedent). Consumers that ignore `c` (the event
  feed, the readied-move bookkeeping) keep working. No new `EventKind`, so the kind layout test, `kEventKindCount` (45) and
  `kLastRulesEventKind` don't move.
- **Versioning.** `c` gains a meaning, which `SIM_EVENTS.md`'s policy calls observable. So: schema v8, and
  `event_stream_version(events)` returns 8 only for a stream with a `Moved` whose `c != 0`. Every existing stream (all `c == 0`)
  keeps its digest. The shove push changes from `c = 0` to `c = 1` (forced) in EC #104, so no locked digest moves twice (§3).
- The fallback with new kinds (`ForcedMoved` / `Teleported`) was not taken (Q1).

**Hook (Resolved (Engine Core), Q3: synchronous, commits immediately, §3).** Rules needs the outcome inside the same step: whether the move happened, how far, and
whether a grapple ended. So:

```cpp
struct ForcedMoveQuery  { CombatantId mover, source; uint8_t mode; uint16_t feet; bool teleport; uint32_t step_seq; };
struct ForcedMoveResult { uint16_t feet_moved = 0; bool stopped_short = false; bool hazard = false; uint16_t fell_ft = 0;
                          std::vector<CombatantId> out_of_reach_of; };   // grapplers / grappled no longer within reach
std::function<ForcedMoveResult(const EncounterState&, const ForcedMoveQuery&)> forced_move;   // EncounterHooks, appended
```

- **Superseded by §3:** no tentative positions and no rollback. The hook resolves and commits immediately, and later queries in
  the same step see the committed positions. Readied triggers raised during the effect queue and fire after the whole effect.
- **Rules-side consequence.** A forced move itself never pauses (no OAs). Another Ask window in the same step could still pause
  and discard the run, for example a Damaged reaction to a later target's damage. So rules calls `forced_move` only in a step's
  final run: it queues the effect's forced moves and calls the hook once no decision is pending, before `SpellResolved`.
- `ForcedMoveResult::stopped_short`, `hazard` and `fell_ft` stay hook results for rules' own use. `hazard` / `fell_ft` are unused
  until zones (Q11).
- **No hook** (rules-only tests, old content): rules emits the event with `a` = requested feet. It does not end grapples on
  distance. This is today's shove behaviour.

### 2.5 Order of events inside a step (proposed ordering rule)

For each moved creature, in the order sim resolves them (farthest from the origin first, Q2): `Moved{forced...}`, then `ConditionRemoved{Grappled}` for each grapple the hook reported
broken (both sides as applicable), then any hazard events (falling damage, later). The forced move comes after the effect's own
save / damage events (Thunderwave: every target's `SavingThrowRolled` / `DamageDealt`, then the queued `Moved` events), and
before `SpellResolved`. A self-teleport emits one `Moved{c = 2}` for the caster. Companions are deferred.

### 2.6 Rules / sim API split

| Decision | Owner |
|---|---|
| Whether a creature is moved at all: save failed, hit landed, size limit, willing companion, effect present | **rules** |
| Distance (fixed, or the chosen "up to"), mode (push / pull / line / teleport), and whether it provokes OAs | **rules** (from content + request) |
| Path: tiles, direction from the source footprint, blocking, occupied tiles, stopping short, multi-tile footprints | **sim** |
| Teleport destination validity: unoccupied, within `max_feet` (Chebyshev, footprint-aware), line of sight if `needs_sight`, Frightened "not closer" (checked at `Combat::submit` re-validation) | **sim** (rules passes the facts it owns: max_feet, needs_sight, fear sources) |
| Grapple ends: is the pair still within reach after the move | **sim** answers (hook result); **rules** ends the condition and emits |
| Falling / hazard damage amount and prone | **rules**, after zones (R-G11) and elevation (Q11), from the hook's `fell_ft` / `hazard` |
| Committing positions, `TileMoved` sim events, readied-move destinations | **sim** |

### 2.7 Rules work once agreed (one rules PR, then the sim PR)

1. `ForcedMove` / `Teleport` effect kinds and specs, rider support, shove routed through them. Rules tests: no hook = requested
   feet; hook truncation; grapple pruning from `out_of_reach_of`; no `LeavingReach` window; speed 0 doesn't block; size limit; save gating.
2. `Moved.c` flags (bit 0 forced, bit 1 teleport, bit 2 drag; §3) plus the v8 note in `SIM_EVENTS.md`.
3. `forced_move` hook (called only in a step's final run), `ActionRequest` fields (`forced_feet`, `teleport_feet`;
   `companions_willing` with Dimension Door later). The sim PR claims the teleport-destination `log_hash` tag 0xB2.
4. Coverage rows: Thunderwave, Gust of Wind, Misty Step, Repelling Blast, Open Hand, blink dog, OA exceptions.
5. Scope (Q10): pushes (Thunderwave, Repelling Blast, shove, Open Hand) plus self-teleport (Misty Step) first. Later: Dimension
   Door companions and the 4d6 failure, Gust of Wind, and falling / hazard damage with zones (R-G11, Q11).

## 3. SIM CONTRACT section (Engine Core: decided)

> **Engine Core owns sim.** These are EC's decisions for R-G9. Where they differ from the RULES section's proposal (2.4 flag
> layout, 2.1 target order, the hook's tentative-position scratch), this section wins.

- **Positions.** Sim positions are integer tiles (`TilePos`, chunk-local). A multi-tile footprint is anchored at its min-x / min-y
  tile (`grid.hpp`: `footprint_walkable(anchor, n)`). Distance between creatures is the nearest-tile Chebyshev distance between
  footprints (`rect_distance`), in tiles, × 5 ft.
- **Event flow (Q1).** Reuse `Moved`. No new `EventKind`. `c` carries flags:

  ```
  Moved.c  bit 0 forced    (not the creature's own movement: push, pull)
           bit 1 teleport
           bit 2 drag      (a held creature moved along with its grappler's voluntary Move)
           bits 3+ reserved, 0
  ```

  This replaces 2.4's layout: no direction or stopped-short bits (`a` = feet actually moved already says it), and no hazard bit
  until hazards exist. Schema **v8**: `event_stream_version(events)` returns 8 only for a stream containing a `Moved` with
  `c != 0`. All older streams and goldens are unchanged. The shove push (EC #104) is the first user: it emits `c = 1` (forced) and
  lands with #104, so no locked digest moves twice.
- **OAs.** Forced moves and teleports never provoke opportunity attacks, so no `LeavingReach` window and no Ask pause can occur
  mid-move. `provokes_oa` stays in the rules data model but sim does not support `true` in R-G9; content setting it is rejected at
  load until an effect needs it.
- **Order of several forced moves in one effect (Q2).** Farthest from the origin first, ties by lower combatant id. Distance is the
  nearest-tile Chebyshev distance between the source and target footprints. So the back creature moves before the front one and
  doesn't block it. Sim sorts; rules emits in the order the hook resolves them.
- **Path resolution (Q5).**
  - Direction: the sign of each component of (target centre − source centre), computed in doubled integer coordinates (centre of a
    footprint at anchor `p` with size `n` = `2p + n`), giving one of 8 directions. Push uses that direction, pull its negation.
    Exact overlap (zero vector): the source's facing, then +x.
  - Walk tile by tile in that direction (diagonals are single steps). At each step check every tile of the mover's footprint.
  - Stop at the last free tile when the next step would leave the map, or enter a blocked tile or a tile occupied by any other
    creature (ally or enemy: a push is not the creature's own movement, so the pass-through rule doesn't apply). Movement is
    partial, not all-or-nothing; a creature blocked on the first step moves 0 ft and still gets `Moved{a = 0}`.
  - Difficult terrain costs nothing for forced movement.
  - Integer math only, bit-identical across platforms.
- **Hook (Q3).** Synchronous `forced_move` hook as in 2.4 (`ForcedMoveQuery` / `ForcedMoveResult`), but it **resolves and commits
  immediately** as a pure function of state: no tentative positions, no rollback. This is safe because a forced move can never
  pause (no OAs). Later queries in the same step see the committed positions. Readied triggers produced during the effect are
  queued and fire after the whole effect resolves. One step stays one log entry.
- **Teleport (Q8).**
  - Sim exposes `legal_teleport_tiles(state, actor, def)`, returning candidate anchors in deterministic row-major order (y, then x).
    The AI scores them and picks one; sim never chooses for it.
  - `Combat::submit` re-validates the chosen destination: within range (Chebyshev, footprint-aware, ≤ `max_feet`), the full
    footprint free, line of sight when the spell requires it (`needs_sight`), and Frightened "not closer to a fear source" for a
    willing self-teleport. An invalid destination is rejected like any other illegal request, with no state change and no log entry.
  - Dimension Door companions and the occupied-destination 4d6 failure are out of the first sim PR (Q10 order); their placement rule
    is decided with that PR.
- **Grapple drag (Q6).** Only on the grappler's own voluntary `Move`: held creatures move along the same path, each emitting
  `Moved` with the drag bit (`c = 4`) after the grappler's `Moved`. A forced move of the grappler never drags. If the grappler is
  pushed out of reach, Grappled is removed (rules ends it from `out_of_reach_of`; the next step's `prune_sourced` prunes the
  sourced entry), per #99.
- **Hashing / log.** No new `Combat::hash` tag: positions are already hashed. If a request field is added for the teleport
  destination, it is folded into `log_hash` only when set, under the next free tag in the `SIM_EVENTS.md` hash tag registry
  (**0xB2** at the time of writing), claimed in that registry by the PR that adds the field.
- **Readied triggers (Q7).** Forced moves and teleports do fire readied "moves within N" triggers. Trigger predicates can read the
  `c` flags (for example to exclude teleports). Current sim already fires `ready_trigger` on any `Moved`, which matches.

## 4. Open questions

**For Engine Core**

- **Q1. Event encoding.** Reuse `Moved` with the `c` flags and a payload-only v8, or add new kinds? If reuse: land the shove push
  `c` change together with EC #104, so no locked digest moves twice.
  - **Resolved (Engine Core):** reuse `Moved`; `c` bit 0 forced, bit 1 teleport, bit 2 drag. v8 only for streams with a `Moved` whose
    `c != 0`; older streams and goldens unchanged. The shove push (#104) is the first user (§3).
- **Q2. Order of several forced moves in one effect.** Is "the request's target order" (sim's `area_targets` order) acceptable, or
  should sim order pushes far-to-near so the front creature doesn't block the one behind it?
  - **Resolved (Engine Core):** far-to-near: farthest from the origin first, ties by lower combatant id; distance = nearest-tile Chebyshev
    between footprints (§3).
- **Q3. Hook vs two-phase.** A synchronous `forced_move` hook with tentative positions and rollback on an Ask pause, or a
  follow-up logged step? The hook keeps one step = one log entry. Two-phase avoids sim state inside rules' attempt loop.
  - **Resolved (Engine Core):** synchronous `forced_move` hook that resolves and commits immediately, pure function of state, no tentative
    positions or rollback. Forced moves never provoke OAs, so no pause can occur mid-move; readied triggers queue and fire after the
    whole effect resolves (§3).
- **Q5. Footprint geometry for push direction**, and blocked-path semantics: stop at the last free tile, or no move at all if the
  first tile is blocked?
  - **Resolved (Engine Core):** 8-way direction from the signs of (target centre − source centre) in doubled integer coordinates; overlap
    fallback = source facing, then +x. Tile-by-tile walk checking every footprint tile; stop at the last free tile (partial
    movement). Integer math only (§3).
- **Q6. Drag and forced moves of a grappler.** Does sim move the dragged creature on the grappler's voluntary `Move` only (as
  proposed), never on a forced move of the grappler (where the grapple usually ends instead)?
  - **Resolved (Engine Core):** yes. Drag only on the grappler's own voluntary `Move` (`Moved` with the drag bit). A grappler pushed out of
    reach loses the grapple; the next step prunes the entry, per #99 (§3).
- **Q7. Readied actions.** Should a forced move or a teleport fire "when a creature moves within N ft" Readies? Proposed: yes for
  both: the trigger is a perceivable circumstance, not a choice by the mover. But sim's `ready_trigger` should see the `c` flags.
  - **Resolved (Engine Core):** yes for both. Predicates can read the `c` flags (e.g. to exclude teleports). Current sim already fires readied
    triggers on any `Moved` (§3).
- **Q8. Teleport destination UX/AI.** Who picks a destination for the AI (Misty Step out of melee), and how is an invalid one
  reported: request rejected before it reaches rules, or a rules rejection via a hook?
  - **Resolved (Engine Core):** sim exposes `legal_teleport_tiles(state, actor, def)` (row-major order); the AI scores and picks.
    `Combat::submit` re-validates; an invalid destination is rejected like any illegal request, with no state change (§3).

**For the Director**

- **Q4. "Willing."** There is no consent model. Proposed: the controller asserts it for its own team's creatures, and
  unconscious / incapacitated creatures are never willing. Charmed doesn't make a creature willing. Accept?
  - **Resolved (Director):** approved as written.
- **Q9. Thunderwave size limit.** The request mentioned "Thunderwave Large or smaller". SRD 5.1 Thunderwave has **no** size limit
  (and neither do Gust of Wind or Repelling Blast). Size limits come from shove (one size larger) and from some monster actions.
  Proposed: follow SRD 5.1 (no limit on Thunderwave), with `max_size` available per effect. Confirm.
  - **Resolved (Director):** approved: SRD 5.1, with a per-effect `max_size`.
- **Q10. Scope of the first implementation.** Proposed: push (Thunderwave, Repelling Blast, shove, Open Hand) and self-teleport
  (Misty Step, blink dog) first. Then Dimension Door companions and occupied-destination damage, Gust of Wind (line plus repeated
  saves, needs R-G10 / R-G11), and falling damage. Accept the order?
  - **Resolved (Director):** approved. The first implementation PR is pushes plus self-teleport (Misty Step).
- **Q11. Hazards.** Should forced movement into a hazard wait for R-G11 zones, or ship falling damage (ledges / pits as sim terrain
  facts) with the first PR?
  - **Resolved (Director):** falling and hazard damage wait for zones (R-G11). Falling also needs elevation, which is deferred.
    The hook keeps `hazard` / `fell_ft`, so a push into a hazard tile can be resolved later.
