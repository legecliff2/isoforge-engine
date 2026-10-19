# Persistent zones and auras (R-G11): rules / sim co-design proposal

Status: **proposal, docs only.** Rules wrote the RULES section. The SIM CONTRACT section is a stub for Engine Core to complete,
built only from facts on main and from `docs/proposals/FORCED_MOVEMENT.md` §3. Nothing here is implemented. This tracks
`docs/SRD_COMBAT_COVERAGE.md` §11.5 order 11 (R-G11) and the spell rows marked "Zone (R-G11)".

Source: SRD 5.1 (CC-BY-4.0), as mirrored for `SRD_COMBAT_COVERAGE.md` §12. Code facts are as of main at `33997b3` (after #104 and
#100). Where this doc relies on rules PR #112 (R-G9 rules side, open), it says so.

## 1. Where things stand today

- **Positions and geometry are sim's.** rules5e has no coordinates. It sees space only through request facts (`target_within_5ft`,
  `leaves_reach`, `move_tiles`) and synchronous hooks (`in_range`, `area_targets`, `spell_target_ok`, `targets_within`,
  `forced_move`).
- **Grid (sim, `grid.hpp`).** Integer tiles of 5 ft. A multi-tile footprint is anchored at its min-x / min-y tile
  (`footprint_walkable(anchor, n)`). Distance between creatures is the nearest-tile Chebyshev distance between footprints
  (`rect_distance`), in tiles, × 5 ft. Tile walk values: 0 blocked, 1 normal, 2 difficult (`step_cost_ft`: 10 ft into a difficult
  tile). Chunk tile flags: bit0 blocks_move, bit1 blocks_sight, bit2 difficult, bit3 water. These flags are static terrain, with
  no dynamic overlay.
- **Movement is one tile per rules step.** A sim `Step` command becomes `ActionRequest{Move, move_ft = cost, move_tiles = 1,
  leaves_reach}` (`combat.cpp`). So sim knows `from` / `to` for every 5 ft of voluntary movement, which suits Spike Growth.
- **Area templates exist (R-G10, #78).** Sim computes cone / line / cube / sphere / cylinder targets over footprint tiles with LOS
  from the origin (`area_targets`). `AreaShape` = {None, Cone, Cube, Cylinder, Line, Sphere}. Targets are computed once, when
  the spell is cast. Nothing persists.
- **Machinery to reuse (rules).**
  - `SpellEffect` (save, damage, condition, modifier riders).
  - `ActiveSpellEffect`: lasting conditions, `RepeatSave`, `Timed`, `concentration`, `ends_on_damage`, `cast_stamp`.
  - `ActiveModifier` (row 12).
  - `Timed` / `Expiry` (Rounds, EndOfTargetNextTurn).
  - Triggers: `TriggerOn` {Hit, Crit, DamageDealt, ReduceToZero, DropToZero, Death, TurnStart, TurnEnd} and `TriggerLimit`
    {None, OncePerTurn, OncePerRound, OncePerAttack} (row 9).
  - Sourced conditions with an escape DC (R-G8).
  - Concentration (`concentration_spell`, `ConcentrationEnded`).
  - `can_provide_components`.
- **Forced movement (R-G9).**
  - Rules #112 (open) defers every forced move and teleport until after all of the step's decision points. `forced_move` is
    called once per (step, step_seq), only in the run that is kept.
  - `ForcedMoveResult` already reserves `hazard` / `fell_ft` "for R-G11".
  - Director Q11 (#110): falling and hazard damage wait for zones, and falling also waits for elevation.

## 2. RULES section (Rules: filled in)

### 2.1 SRD 5.1 behaviour to support

Two families: **auras** are centred on a creature and move with it; **fixed zones** stay at a point, unless the spell lets the
caster move them.

| Spell / feature | Shape, anchor | Duration | Trigger(s) and effect | Terrain / sense flags |
|---|---|---|---|---|
| Spirit Guardians (3) | 15 ft radius, centred on the caster, moves with them | conc., 10 min | Enters for the first time on a turn **or** starts its turn there: Wis save, 3d8 radiant (necrotic if the caster is evil), half on a success; +1d8 per slot level above 3. The caster chooses creatures that are unaffected. | Affected creatures' speed is halved in the area |
| Aura of Protection (Paladin 6) | 10 ft, centred on the paladin | while the paladin is conscious | Passive: the paladin and friendly creatures in the aura add the paladin's Cha modifier (min +1) to saving throws | none |
| Aura of Courage (Paladin 10) | 10 ft, centred on the paladin | while conscious | Passive: the paladin and friendly creatures in the aura can't be frightened | none |
| Moonbeam (2) | 5 ft radius, 40 ft high cylinder, at a point within 120 ft | conc., 1 min | Enters for the first time on a turn or starts its turn there: Con save, 2d10 radiant, half on a success (shapechangers: disadvantage, and revert on a fail); +1d10 per slot level above 2. **On each of the caster's later turns, an action moves the beam up to 60 ft** (SRD 5.1 says action; see D3). | dim light |
| Cloud of Daggers (2) | 5 ft cube | conc., 1 min | Enters for the first time on a turn or starts its turn there: 4d4 slashing, no save; +2d4 per slot level above 2. **Not in the SRD mirror** (§12 UNVERIFIED); see D5 | none |
| Web (2) | 20 ft cube | conc., 1 h | **Starts its turn in the webs, or enters them during its own turn**: Dex save or restrained while in the webs or until it breaks free (an action: Str check vs the spell save DC). Flammable: a 5 ft cube exposed to fire burns away in 1 round and deals 2d4 fire to a creature starting its turn in the fire. Collapses without anchors. | difficult terrain, lightly obscured |
| Stinking Cloud (3) | 20 ft radius sphere | conc., 1 min | **Completely within** the cloud at the start of its turn: Con save or spend its action that turn retching. Creatures that don't breathe or are immune to poison succeed automatically. Wind disperses it. | heavily obscured |
| Fog Cloud (1) | 20 ft radius sphere (+20 ft per slot level above 1) | conc., 1 h | none | heavily obscured |
| Darkness (2) | 15 ft radius sphere; if cast on an object, it moves with the object | conc., 10 min | none. Dispels overlapping light spells of level 2 or lower. | magical darkness: darkvision can't see through it, nonmagical light can't illuminate it |
| Spike Growth (2) | 20 ft radius | conc., 10 min | **Moves into or within** the area: 2d4 piercing for every 5 ft it travels, with no save and **no per-turn limit**. Camouflaged: a Perception check vs the DC to notice it. | difficult terrain |
| Wall of Fire (4) | wall up to 60 ft long × 20 ft high × 1 ft thick, or a ring 20 ft across | conc., 1 min | **When it appears**: Dex save, 5d8 fire, half on a success. **One chosen side**: 5d8 fire to each creature that ends its turn within 10 ft of that side or inside the wall, and the same when a creature enters the wall for the first time on a turn (see D4 on the save); +1d8 per slot level above 4 | opaque (blocks sight) |
| Grease (1) | 10 ft square | 1 min, **no conc.** | **When it appears**, each creature standing in it, and later **each creature that enters it or ends its turn there**: Dex save or fall prone | difficult terrain |
| Silence (2) | 20 ft radius sphere | conc., 10 min | Passive: a creature or object entirely inside is immune to thunder damage and deafened; casting a spell with a verbal component there is impossible | none |

Out of scope for R-G11: Entangle, Sleet Storm, Call Lightning, Wind Wall, Flaming Sphere (an object, R-G12) and Rust Metal
(T&C-G5) reuse the same machinery later. So do the wind dispersal of Fog Cloud and Stinking Cloud, Web's anchoring, and Darkness
dispelling light (with R-G13).

### 2.2 Trigger semantics (rules)

Each zone has trigger rows. Rules resolves a row when sim reports the matching membership fact (§2.6, §3).

| Trigger | When it fires | Default limit |
|---|---|---|
| `Appear` | once, when the zone is created, for each creature in it (Wall of Fire, Grease) | none |
| `Enter` | the creature's footprint moves into the zone: its own move, a forced move, or a teleport | **once per creature per zone per turn** (the "first time on a turn" family) |
| `StartTurn` | at the start of the creature's turn while it is in the zone | same once-per-turn group as `Enter` |
| `EndTurn` | at the end of the creature's turn while it is in the zone (Wall of Fire, Grease) | once per turn by construction |
| `MoveWithin` | every 5 ft the creature travels into or inside the zone (Spike Growth) | none: per tile step |

Rules for the triggers:

- **Turn identity** is the turn being taken, `round << 16 | turn_index`, the same stamp as `cast_stamp`. The once-per-turn
  bookkeeping is keyed by (zone, creature, turn stamp). By default `Enter` and `StartTurn` share one group, so a creature that
  starts its turn in Spirit Guardians, leaves and re-enters is hit once that turn. Whether SRD allows twice is D2.
- **Per-spell variants** are row flags, not code:
  - `own_turn_only`: Web's "enters during its turn", so a creature shoved into the web on someone else's turn is not caught then,
    only at the start of its next turn.
  - `completely_within`: Stinking Cloud, Silence. The whole footprint must be inside, which matters for Large creatures.
  - `limit = None`: Grease's "enters", Spike Growth.
- **Order inside a step**:
  1. The move's own events (`Moved`, and #112's `ConditionRemoved{Grappled}` for out-of-reach grapples).
  2. Zone triggers, in zone creation order (`zone_id` ascending). Within a zone, creatures go in the order sim reports them
     (§3: initiative order, then lower id).
  3. Each trigger's save, damage and condition events, exactly as the matching `SpellEffect` would emit them today.
- **At turn boundaries**, `StartTurn` rows resolve right after `TurnStarted`, before the turn's other start-of-turn work in the
  existing order. That work is the row 9 `TurnStart` triggers and `RepeatSave::StartOfTurn`, and they run after zones. `EndTurn`
  rows resolve before `RepeatSave::EndOfTurn` and the row 9 `TurnEnd` triggers.
- **Excluded creatures** (Spirit Guardians' "you can designate any number of creatures") are a per-zone list from the cast
  request. The caster is never affected by their own aura's damage.
- **Death and incapacitation.** A dead creature triggers nothing. An aura of a creature that dies or is incapacitated ends or
  pauses:
  - Concentration auras end through concentration.
  - Aura of Protection and Aura of Courage need the owner to be conscious. They are suppressed, not ended, while it is
    unconscious.

### 2.3 Moving zones, auras and concentration

- **Auras** (`anchor = Creature`) move with their creature. Sim recomputes membership after every step that moves the anchor
  creature. A creature the aura moves onto does **not** "enter" by default: SRD's trigger is the creature entering, and that is
  D1. Its next start of turn catches it.
- **Moonbeam's move** is a new request on the caster's later turns: `ActionRequest::zone_move = zone_id` plus an opaque
  destination (`has_zone_dest`, `zone_x`, `zone_y`, like #112's teleport destination). Rules checks:
  - the action cost from data (`ZoneSpec::move_cost`: Action for Moonbeam; BonusAction available for later content such as
    Flaming Sphere);
  - "not on the casting turn" via `cast_stamp`;
  - `zone_move_ft` ≤ `move_ft` when sim supplies the distance.

  Sim validates the destination. Creatures the moved beam lands on do not "enter" (D1).
- **Concentration linkage.** When concentration ends (`ConcentrationEnded`, any cause), every zone with
  `concentration && spell_id == concentration_spell` of that caster ends, with `ZoneEnded{reason 1}`, before the
  `SpellEffectEnded` events of the same spell. Effects a zone applied "while in the area" end with it:
  - Web's restrained condition (`SpellEffectEnded`, then `ConditionRemoved`);
  - Aura of Protection's modifier (`ModifierExpired`).

  Non-concentration zones (Grease) end on their `Timed` timer (reason 0). Paladin auras end only when the feature does.
- **Leaving.** "While in the area" effects end when the creature leaves:
  - Web's restraint;
  - Silence's deafened condition and thunder immunity;
  - Aura modifiers.

  Sim reports exits like entries (§3). Exits never deal damage.

### 2.4 Terrain, obscurement and Silence

- **Difficult terrain** (Web, Spike Growth, Grease) is a zone flag. Movement cost is sim's (`move_cost_ft`), so sim overlays zone
  tiles as walk value 2 while the zone exists. Rules does nothing, because sim already charges `move_ft`. Spirit Guardians'
  "speed is halved in the area" is a separate flag, `halve_speed`, applied only to affected (non-excluded) creatures. It is
  modelled as doubling the cost of each step that starts or ends in the area, on top of difficult terrain (D6, Q5).
- **Obscurement** (`light_obscured`, `heavy_obscured`, `magical_darkness`) are stored flags only until R-G13 lands lighting and
  vision. Q6 asks whether sim should treat heavy obscurement and Wall of Fire's opacity as a dynamic `blocks_sight` overlay for
  LOS and targeting in the meantime. That would be visible in today's LOS-based targeting.
- **Silence**:
  - **V components.** Rules checks this itself. Before `can_provide_components`, a caster whose footprint is completely inside a
    live Silence zone can't cast a spell with `kComponentV`. The cast is refused with a reason, and nothing is rolled.
  - **Thunder immunity** while completely inside: a damage-mitigation check at `apply_damage` time.
  - **Deafened** applies on enter and ends on exit, like Web's restraint.

  All three ask sim for membership with one hook (`zone_contains`, §3), because rules can't see positions.
- **Web's restraint** is a sourced condition (R-G8): `Restrained`, `source = caster`, with `escape_dc` = the spell save DC and
  `escape_ability = Str`. Escape (`special` 4) uses a plain Strength check against `escape_dc`. This differs from the grapple
  escape, which uses Athletics or Acrobatics against the grappler, so it is a per-entry flag. A restrained creature in the web
  is not re-saved each turn.

### 2.5 Forced movement into a zone (R-G9 hook)

- A forced move or teleport into a zone **counts as entering**. Moving inside Spike Growth counts as moving within.
- `ForcedMoveResult` gains appended membership fields: `zones_entered`, `zones_left` and `within_steps` (5 ft steps inside
  `MoveWithin` zones, per zone). These are the same facts as for voluntary moves (§3).
- Rules resolves those triggers right after the move's `Moved` / `ConditionRemoved` events. Each forced move is resolved
  completely, triggers included, before the next one in `forced_move_order`.
- **This is where the R-G9 hazard hook resolves.** The reserved `hazard` bool becomes "entered a zone or hazard tile", and
  `fell_ft` stays reserved. **Falling damage and non-zone hazard damage stay deferred until elevation exists** (Director Q11).
- **Interaction with #112's deferral (Q7, important).** #112 calls `forced_move` once, after all decision points. A zone trigger
  resolved after the hook can open a new decision point, for example damage opening a `Damaged` window (Ask). If that run
  pauses, the re-run would call the hook again. Proposal:
  - Rules keeps the step's `ForcedMoveResult`s, keyed by step_seq, across the attempt's re-runs, the way the decision log is
    replayed.
  - A re-run reuses the recorded result and does not call the hook. The debug assert from #112 stays true.
  - The fix belongs to the zones implementation PR, not to #112.

### 2.6 Data model (proposed; everything appended, defaults keep today's behaviour)

```cpp
enum class ZoneAnchor  : uint8_t { Point, Creature };          // Object (Darkness on an object) later, with R-G12
enum class ZoneTrigger : uint8_t { Appear, Enter, StartTurn, EndTurn, MoveWithin };
inline constexpr uint8_t kZoneDifficult = 1, kZoneLightObscured = 2, kZoneHeavyObscured = 4, kZoneMagicalDarkness = 8,
                         kZoneSilence = 16, kZoneHalveSpeed = 32, kZoneOpaque = 64;

struct ZoneTriggerRow {                  // one row reuses SpellEffect for the effect itself
    ZoneTrigger on = ZoneTrigger::Enter;
    uint8_t group = 1;                   // once-per-turn group (0 = no limit); Enter + StartTurn share group 1 by default
    bool own_turn_only = false;          // Web
    bool completely_within = false;      // Stinking Cloud
    SpellEffect effect{};                // Save / damage / condition / modifier, scaled like the spell's first effect
};
struct ZoneSpec {                        // on SpellDef (primary, SpellEffectKind::Zone) or SpellEffect (rider)
    AreaShape shape = AreaShape::None;   // Sphere, Cylinder, Cube, Line (walls: Q4)
    uint16_t size_ft = 0, height_ft = 0, length_ft = 0;
    ZoneAnchor anchor = ZoneAnchor::Point;
    uint8_t flags = 0;                   // kZone*
    uint8_t move_cost = 0;               // 0 cannot move, 1 action (Moonbeam), 2 bonus action
    uint16_t move_ft = 0;                // Moonbeam 60
    bool chooses_excluded = false;       // Spirit Guardians
    std::vector<ZoneTriggerRow> triggers{};
    ModifierSpec aura_modifier{};        // passive aura (Aura of Protection: save bonus = owner Cha, min 1)
    uint32_t aura_immunity = 0;          // passive condition immunity bitmask (Aura of Courage: Frightened)
    bool needs_conscious_owner = false;  // paladin auras
};
struct ActiveZone {                      // EncounterState::zones (new, appended); empty unless a zone exists
    uint32_t zone_id = 0;                // per-encounter counter (EncounterState::next_zone_id)
    uint32_t spell_id = 0;               // or feature id for paladin auras
    CombatantId owner = 0;
    uint8_t slot_level = 0; int16_t dc = 0;
    bool concentration = false; Timed timed{};
    uint32_t cast_stamp = 0;
    bool has_anchor_tile = false; int32_t anchor_x = 0, anchor_y = 0;   // opaque sim tile (Point zones); Q1
    std::vector<CombatantId> excluded{};
    std::vector<std::pair<CombatantId, uint32_t>> fired{};              // (creature, turn stamp << 8 | group)
};
```

- The new `SpellEffectKind::Zone` is appended. `effect_view` copies `ZoneSpec` like the other per-effect fields. Content can
  express Spirit Guardians, Moonbeam, Web and the others as data. The loader keys belong to T&C, in a follow-up after the
  implementation.
- Paladin auras are features. They create an `ActiveZone` at encounter start, through `hooks.feature_spec` and with no
  concentration. They are suppressed while the owner is unconscious.
- No new rng draws unless a zone exists. Triggers roll exactly as the reused `SpellEffect` does.
- Stale `fired` entries are pruned at each round start, so the list stays small and deterministic.

### 2.7 Event plan

Reused unchanged: `SavingThrowRolled`, `DamageDealt`, `ConditionAdded` / `ConditionRemoved`, `SpellEffectEnded`,
`ConcentrationEnded`, `ModifierApplied` / `ModifierExpired`, `Moved` (v8 flags), and `SpellCast` / `SpellResolved`.

- **Reused with appended values:** `FeatureTriggered{actor = zone owner, target = subject, a = spell / feature id,
  b = TriggerOn, c = outcome}`. `TriggerOn` gains `ZoneAppear`, `ZoneEnter`, `ZoneStartTurn`, `ZoneEndTurn` and
  `ZoneMoveWithin`. One `FeatureTriggered` precedes each trigger's save and damage, so a log reader can tell which zone hit whom
  and why.
- **New kinds** (appended, schema **v9** iff a stream contains one):

  | Kind | Payload |
  |---|---|
  | `ZoneCreated` | actor = owner, target = anchor creature (0 for a point), a = zone id, b = spell / feature id, c = flags |
  | `ZoneMoved` | actor = owner, a = zone id, b = feet moved |
  | `ZoneEnded` | actor = owner, a = zone id, b = reason (0 timer, 1 concentration, 2 owner died or the feature ended, 3 dispelled) |

- `FeatureTriggered` carrying the new `TriggerOn` values only appears together with `ZoneCreated` in the same stream, so the v9
  rule needs nothing more.
- Sim decides whether it needs a `SimEvent` for zone tiles, for rendering (Q12).

### 2.8 Hashing plan

- **`Combat::hash`:** one tag for `zones_digest` (`ActiveZone` list including `fired`), folded only when `zones` is non-empty.
  **Claimed in the implementation PR, not here.**
  - The next free tag is **0xAB**, once #112's registry fix lands. That fix moves #111's `leftover_digest` from 0xA9 to 0xAA,
    because EC #104 uses 0xA9 for `special` in `log_hash`.
- **`log_hash`:** one tag for the new request fields (`zone_move`, zone destination, `excluded` ids), folded only when set.
  The tag is **0xB7**: 0xB4 is R-G9's (#112), and EC #114 moves `special` from 0xA9 to 0xB5 and claims 0xB6 for
  `two_handed_grip`. It is also claimed in the implementation PR.
- Membership facts that sim supplies on a `Move` request (§3) are part of the request. They need a `log_hash` fold under the same
  or another tag (Q11).

### 2.9 Rules / sim split

| Concern | Owner |
|---|---|
| Zone geometry: tiles covered by a shape at an anchor, walls and sides, cylinders on a 2D grid, footprint overlap, "completely within" | **sim** |
| Membership changes: who entered, left or moved within, per tile step, forced move, teleport, aura move or zone move | **sim** (reports facts) |
| Difficult-terrain overlay and the halved-speed cost; LOS overlay (Q6) | **sim** |
| Zone destination validity (Moonbeam's range, line of sight) | **sim** |
| Zone state: lifetime, concentration, timers, excluded creatures, once-per-turn bookkeeping | **rules** |
| Trigger effects: saves, damage, conditions, modifiers, Web's restraint and escape, Silence's checks | **rules** |
| Event order and hashing of zone state | **rules** (`zones_digest`); sim folds it |

### 2.10 First implementation PR (proposed scope)

1. **Rules PR 1 (core):**
   - `ZoneSpec`, `ActiveZone`, `SpellEffectKind::Zone` and the three event kinds (v9).
   - `Appear`, `Enter`, `StartTurn`, `EndTurn` and `MoveWithin` with once-per-turn groups.
   - Concentration and timer endings; the request fields for membership and zone moves; `ForcedMoveResult` membership with the
     recorded-result fix (Q7).
   - Spells: **Spirit Guardians, Moonbeam (with the move), Spike Growth, Grease, Web (restrain on enter, Str escape)**.
   - Claim `Combat::hash` 0xAB and `log_hash` 0xB7.
2. **Rules PR 2:** Silence (V components, thunder immunity, deafened), Stinking Cloud (action loss, completely within), Wall of
   Fire (appear, sides, end of turn), Fog Cloud and Darkness flags, and Aura of Protection and Aura of Courage (passive auras).
   Cloud of Daggers comes only if D5 says yes.
3. **Sim PR (EC):** the §3 contract: geometry, membership facts, overlays and the hash folds.
4. **T&C:** `spell@1` `zone` keys and content for the spells above.

## 3. SIM CONTRACT section (Engine Core: decided)

Engine Core owns sim. Base facts as in FORCED_MOVEMENT.md §3: integer tiles, footprints anchored at min-x / min-y, nearest-tile
Chebyshev distance, synchronous hooks that resolve and commit at once. Integer math only, bit-identical across platforms.

- **Geometry.**
  - A point zone's anchor is a tile stored in **rules state** (`ActiveZone::anchor_x / anchor_y`), opaque to rules and hashed by
    `zones_digest`. It is the single source of truth, like #112's teleport destination (Q1).
  - An aura's centre is its owner's footprint. Radii use the same nearest-tile Chebyshev rule as `rect_distance` and area
    templates (#78). Cylinders ignore height until elevation exists. A tile is in the zone when the template covers it.
  - A creature is **in** a zone when any footprint tile is covered, **completely within** when all are. **Entering** = not in
    before and in after, so a Large creature that stays partly inside doesn't re-enter (Q3).
  - **Line wall:** the supercover of the segment between the start and end tile centres in doubled integer coordinates: every tile
    the segment touches, 1 tile thick. **Ring:** the tiles at exactly radius-in-tiles nearest-tile Chebyshev distance from the
    centre footprint (a square ring, consistent with Chebyshev).
  - **Damaging side:** tiles within 2 tiles (10 ft, Chebyshev) of a wall tile, on the chosen side. Line: side = sign of the integer
    cross product of (tile centre − start) with (end − start), doubled coordinates; chosen at cast as +1 / −1. Ring: inside or
    outside. Tiles on the wall itself are the wall, not a side (Q4).
- **Membership facts to rules (Q2).**
  - Request fields on each `Move` tile step: `zones_entered`, `zones_left`, `zones_within`, ascending zone ids. **Sim fills them
    inside `Combat::submit`** from the `from` / `to` tiles and current state; they are never trusted from the caller (AI / UI
    leave them empty; non-empty caller values are overwritten).
  - `ForcedMoveResult` and the teleport hook result carry the same three lists.
  - **Zone and aura moves (Q8):** membership changes caused by an aura owner's move or a Moonbeam move are reported for the other
    creatures, attached to the step that caused them. Only the final position counts: a moving zone doesn't affect creatures it
    passes over (Q10, matching D1).
  - **Order (Q9):** ascending `zone_id` outer, then initiative order, then lower combatant id within a zone.
- **Membership query hook:** `zone_contains(state, zone_id, creature, completely) -> bool`, pure and synchronous. Answers
  `StartTurn` / `EndTurn`, Silence's V check and Stinking Cloud's "completely within".
- **Forced moves and deferral (Q7).** Accepted: rules records `ForcedMoveResult`s per step_seq across re-runs. Sim's hook is still
  called exactly once per (step, step_seq); #112's debug assert stays.
- **Overlays in `move_cost_ft`**, for live zones only:
  - base 5 ft per tile, +5 for difficult terrain (including `kZoneDifficult` tiles), then doubled for `kZoneHalveSpeed` for
    affected creatures, so both stack to 20 ft per tile. A cost multiplier, not a rules speed modifier (Q5).
  - No `blocks_sight` overlay before R-G13: `kZoneHeavyObscured` / `kZoneOpaque` stay data only, so LOS targeting doesn't
    change now (Q6).
- **Zone moves:** `legal_zone_tiles(state, zone)` lists candidates in row-major order (y, then x); the AI / UI picks.
  `Combat::submit` re-validates (range and LOS from the owner); an invalid destination is rejected with no state change.
- **Hashing (Q11).**
  - `zones_digest` takes `Combat::hash` **0xAB**, folded only when `zones` is non-empty.
  - Membership fields are **not** folded into `log_hash`: sim derives them deterministically at submit from positions, they are
    not inputs. Only caller-chosen inputs (zone move destination, excluded ids) fold, under `log_hash` **0xB7**, only when set.
    (0xB5 is now `special` and 0xB6 `two_handed_grip`, per the `SIM_EVENTS.md` registry; §2.8 already reflects this.)
  - Both tags are claimed in the implementation PR's registry rows. No golden moves for content without zones.
- **Events and rendering (Q12).** No new `SimEvent`; the game renders from rules' `ZoneCreated` / `ZoneMoved` / `ZoneEnded`. Sim
  may keep a zone-tile cache derived from state (rebuilt on those events or on load), never hashed and never a source of truth.

## 4. Open questions

### Engine Core

- **Q1. Anchor storage.** Should the point zone's anchor tile be stored in rules state (opaque, hashed by `zones_digest`, one
  source of truth) or in a sim side table keyed by `zone_id`? Rules proposes rules state, as with #112's opaque teleport
  destination.
  - **EC answer:** Rules state (`ActiveZone::anchor_x / anchor_y`), opaque, hashed by `zones_digest`; single source of truth (§3).
- **Q2. Membership facts.** Should sim pass request fields on each `Move`, the proposal, or should rules call a
  `zone_transitions(state, mover, from, to)` hook?
  - **EC answer:** Request fields on each `Move` step, filled by sim inside `Combat::submit` from `from` / `to` and state, never trusted from the
    caller. Same lists in `ForcedMoveResult` and the teleport hook result (§3).
- **Q3. Large creatures.** Confirm "in" = any footprint tile and "completely within" = all of them. Does a Large creature that
  steps one tile and stays partly inside "enter" again? Rules says no: entering means not in before and in after.
  - **EC answer:** Confirmed: any tile = in, all = completely within; entering = not in before and in after, so no re-entry (§3).
- **Q4. Walls.** How do Wall of Fire (1 ft thick, up to 60 ft, or a 20 ft ring) and its damaging side map to tiles?
  - **EC answer:** Line = supercover of the centre-to-centre segment (doubled coords, 1 tile thick); ring = tiles at exactly the radius
    (Chebyshev). Damaging side = within 2 tiles of a wall tile on the cross-product sign (line) or inside / outside (ring) chosen
    at cast; wall tiles are not a side (§3).
- **Q5. Speed halving.** Is Spirit Guardians' halving a cost multiplier in `move_cost_ft` (the proposal) or a rules speed
  modifier? Does it stack with difficult terrain (rules: yes, 20 ft per tile)?
  - **EC answer:** Cost multiplier in `move_cost_ft`: 5, +5 difficult, then ×2 for affected creatures; stacks to 20 ft per tile (§3).
- **Q6. Obscurement before R-G13.** Should sim overlay `blocks_sight` for heavily obscured and opaque zones now? That changes
  LOS-based targeting in zone content. If not, the flags stay data only until R-G13.
  - **EC answer:** No overlay before R-G13; the flags stay data only (§3).
- **Q7. Deferral vs zone decisions.** Zone triggers resolved after `forced_move` can open Ask windows. Rules proposes to record
  hook results per step_seq across re-runs (§2.5). Is that acceptable to EC as the extension of #112's contract? The hook would
  still be called once per (step, step_seq).
  - **EC answer:** Accepted. Hook still called exactly once per (step, step_seq); #112's debug assert stays (§3).
- **Q8. Aura moves.** Should sim report membership changes caused by an aura's owner moving, and a Moonbeam move, with the step
  that caused them? Rules applies "while in the area" effects from them, but no `Enter` damage (D1).
  - **EC answer:** Yes, attached to the causing step, ordered as Q9 (§3).
- **Q9. Trigger order.** When several creatures enter at once (an aura moves, Grease or Wall of Fire appears), is the order
  initiative order then lower id? Across zones, the order is ascending `zone_id`.
  - **EC answer:** Ascending `zone_id`, then initiative order, then lower combatant id (§3).
- **Q10. Zone moves through creatures.** Does a moving Moonbeam affect creatures it passes over? Rules says no, matching D1.
  - **EC answer:** No; only the final position counts, matching D1 (§3).
- **Q11. `log_hash`.** Should membership request fields be folded (they are inputs, so yes, under one new tag), or derived again
  at replay from positions (no fold)?
  - **EC answer:** Not folded: sim derives membership at submit from positions. Only caller-chosen inputs (zone move destination, excluded ids)
    fold under 0xB7 when set; `zones_digest` takes `Combat::hash` 0xAB when zones is non-empty (§3).
- **Q12. Rendering.** Does sim want a `SimEvent` for zone tiles appearing, moving and ending, or will it render from rules'
  `ZoneCreated` / `ZoneMoved` / `ZoneEnded`?
  - **EC answer:** No new `SimEvent`; render from `ZoneCreated` / `ZoneMoved` / `ZoneEnded`. Any sim zone-tile cache is derived, unhashed, not a
    source of truth (§3).

### Director

- **D1.** Does a zone or aura that moves onto a creature count as that creature entering it (Spirit Guardians as the cleric
  walks up, a moved Moonbeam)? Rules defaults to **no**: SRD 5.1's trigger is "a creature enters", and the start of its turn
  catches it.
- **D2.** Can a creature that starts its turn in Spirit Guardians or Moonbeam, leaves and re-enters on the same turn be hit twice?
  Rules defaults to **once per turn**, from the shared group.
- **D3.** Moonbeam's move: SRD 5.1 says "**action**". The request said bonus action, which is Flaming Sphere's cost. Rules follows
  the SRD and keeps `move_cost` in data.
- **D4.** Is there a Dex save against Wall of Fire's ongoing side damage (end of turn, entering)? Rules defaults to **no save**:
  the SRD text gives the save only "when the wall appears".
- **D5.** Cloud of Daggers is not in the SRD mirror (§12). Should it be included from the PDF, or dropped?
- **D6.** Spike Growth:
  - Do forced moves count? Rules says yes: "moves into or within".
  - The camouflage Perception check is out of scope, so the area is always known.
  - Halved speed and difficult terrain stack (Q5).
- **D7.** Aura of Protection and Aura of Courage are Paladin levels 6 and 10, above the current level-5 content scope. Should they
  go in PR 2 for monsters and NPCs that have them, or be deferred?
- **D8.** Web's flammability and anchoring, and the wind dispersal of Fog Cloud and Stinking Cloud: out of scope, as proposed?
- **D9.** Stinking Cloud's "spends its action retching": modelled as losing the action, with bonus action and movement still
  allowed?
