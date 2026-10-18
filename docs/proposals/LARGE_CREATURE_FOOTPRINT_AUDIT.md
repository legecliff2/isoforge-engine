# Audit: multi-tile footprints for Large+ creatures in the sim

Status: findings only, no code changed. Base: main `6012567`. Question: does `sim` handle a Large
(2x2), Huge (3x3) or Gargantuan (4x4) creature occupying several tiles, for occupancy, movement,
line of sight, reach and the grid?

**Answer: no. Every creature is exactly one tile, end to end, and there is no size anywhere in the
data path.** A Large monster loaded today plays as a Medium one. The change is not trivial (it
touches all five areas below plus the event/ECS contracts), so nothing was implemented.

## 0. Size never reaches the sim

- `schemas/common/v1.schema.json:53` defines `size` (`tiny` .. `gargantuan`) and the stat block carries
  it (`:260`).
- `sim::ContentLibrary::add` (`engine/core/sim/src/content.cpp`) never reads `size`, and
  `rules5e::Combatant` (`engine/rules/include/rules5e/types.hpp`) has no size field. `Archetype`
  (`engine/core/sim/include/sim/content.hpp`) has none either.
- `grep -ri "footprint|multi-tile|2x2"` over `engine/` and `docs/` finds nothing. The only mention
  is the coverage audit (`docs/SRD_COMBAT_COVERAGE.md`, row "Creature size and space", mechanic C6).

## 1. Occupancy

- A spawn is one tile: `engine/core/sim/src/combat.cpp:58` (`Grid::to_world(ch->coord, sp.tile)`)
  and the ECS position is one `TilePos {x, y}` (`engine/core/sim/include/sim/components.hpp:14`,
  attached at `combat.cpp:90`). `Combat::pos(id)` returns that single tile (`combat.cpp:115-120`).
- `Combat::occupied_except` returns one `Vec2i` per living creature (`combat.cpp:126-133`).
  Nothing marks the other three tiles of a 2x2.
- Spawn validation does not check overlap with other creatures or walls beyond what the chunk
  author did; a footprint would also need an anchor convention (top-left vs centre; centre is
  undefined for even sizes) and a rule for which tile is `pos()`.
- Same-tile stacking is not rejected at spawn either (no occupancy check in `Combat::create`,
  `combat.cpp:75-94`), so two spawns on one tile are accepted today.

## 2. Movement

- `Combat::do_step` (`combat.cpp:467-482`) moves one tile: it requires `chebyshev(from, to) == 1`
  (`:471`), `grid_.walkable(to)` for that one tile (`:472`) and `to` not in the occupied list (`:474`).
  A 2x2 would need all newly entered tiles of the footprint to be free and walkable, and squeezing
  (SRD: a creature can squeeze through a space one size smaller, at extra cost) has no hook.
- `Grid::path` (`engine/core/sim/src/grid.cpp:92-130`) is a point-sized Dijkstra: `walkable(q)` and
  `blocked.count(q)` per single tile (`:119`), cost from `step_cost_ft(q)` of that one tile
  (`:120`, `grid.hpp:49`). It takes `occupied` as a list of tiles, so a caller could pass a
  footprint's tiles, but there is no way to say "the mover is 2x2, test the footprint at each
  node". Difficult terrain cost is per entered tile, not per footprint.
- `NearestEnemyPolicy` / `next_command` (`engine/core/sim/src/driver.cpp:86-90`) build goal tiles as
  a ring around the target's one tile and path to them.

## 3. Line of sight and cover

- `Grid::line_of_sight(a, b)` (`grid.cpp:74-90`) is tile-centre to tile-centre. SRD 5.1 lets a
  creature of any size trace lines from a corner of its space; with a footprint the test would be
  "any corner/tile of A to any corner/tile of B". No call site passes anything but `pos()`:
  `combat.cpp:313` (ranged facts), `:222` (readied triggers).
- No cover model exists at all (`rules5e::AttackContext::cover_ac_bonus` is a caller-supplied
  number that sim never sets), so footprint cover is part of the larger missing cover work.

## 4. Reach and range

- All distance tests are Chebyshev distance between two single tiles:
  `combat.cpp:169` (`leaves_reach`), `:186` (reaction `in_range`), `:199`, `:213`, `:218-224`
  (readied triggers), `:311-312` (ranged range), `:321` (hostile within 5 ft), `:413-417` (melee
  reach check on `Attack`), `driver.cpp:14,48`.
- For a 2x2 attacker or target, reach is measured from the nearest tile of the footprint
  (distance between rectangles). The code would need a `distance_tiles(a, b)` over footprints
  and every site above switched to it. `melee_reach_tiles` (`combat.cpp:151-158`) is per-creature
  and fine; the inputs are what is wrong. `req.target_within_5ft` (`:420`) and
  `ranged_hostile_within_5ft` (`:321`) would also use it.
- Opportunity attacks: `leaves_reach` (`:160-172`) compares the mover's `from`/`to` tile to the
  reactor's tile. With footprints, "leaves reach" is "no tile of the mover's new footprint is within
  reach of any tile of the reactor's footprint", for both parties being Large.

## 5. Grid, ECS and rendering contracts

- `Grid` knows nothing about creatures; it only holds walk/sight bits per chunk tile
  (`grid.hpp:57-65`). Footprints belong in a new occupancy layer in `Combat`, not in `Grid`, but
  `Grid::path` needs a footprint parameter (or a predicate).
- Event contract: `SimEvent::TileMoved` (`docs/SIM_EVENTS.md`) carries one destination tile.
  Consumers (render, `vertical_slice`) would have to treat it as the anchor tile and know the size.
  A new field or a size lookup through `Combat::archetype(id)` is needed; additive, so a minor
  schema bump.
- The ECS `TilePos` (`components.hpp:14`) is the only place scripts read position
  (`engine/core/script_host/src/script_host.cpp:171`). A size component would be needed for Luau AI
  to reason about footprint.
- Chunk spawns have a single `tile` (`combat.cpp:58`), and the chunk hash/codec
  (`engine/core/world/src/chunk_codec.cpp`) hashes it; size comes from content, so no chunk-format
  change is needed if the anchor convention is fixed.
- Cross-chunk: a footprint may straddle a chunk border; `Grid` already resolves world tiles across
  chunks (`grid.hpp` header comment), so that is workable.

## 6. What a minimal implementation needs (not done)

1. `size` into `Archetype` / `Combatant` (gated by schema version like other new mechanics; @3 for
   monster, pre-@3 stays 1x1 so goldens do not move). Medium and smaller stay 1x1 (Tiny/Small share
   a tile only with SRD special cases, out of scope).
2. `Combat` footprint helpers: `footprint(id)` (anchor = top-left, tile list), `distance_tiles(a, b)`,
   and `occupied_except` returning the union of footprints.
3. `do_step` / `Grid::path` footprint-aware (every entered tile of the footprint walkable and free;
   cost = max step cost over newly entered tiles).
4. Replace the Chebyshev call sites listed in section 4, and `line_of_sight` callers, with the
   footprint versions.
5. `TileMoved` consumer notes + size in the viewer; a golden proving 1x1 content is bit-identical.

Expected touch: `combat.cpp` (about 12 sites), `grid.cpp`, `driver.cpp`, `content.cpp`, `types.hpp`,
`docs/SIM_EVENTS.md`, plus new tests. Mechanic C6 in the coverage audit covers movement modes as
well; footprint can be split from it. Also related: grapple size limits and swarm shared spaces
(`docs/SRD_COMBAT_COVERAGE.md`).
