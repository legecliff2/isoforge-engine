# World streaming (milestone 2, first slice)

This is the code-level companion to ARCHITECTURE.md §4 ("Streaming") and §5.2. That document is unchanged.

## Pieces (`engine/core/world`)
- **`LayeredProvider`:** the first layer whose `covers()` is true serves the coord. For example, `{FileChunkProvider (handcrafted), ProceduralProvider (eng::procgen_world)}`.
- **`ChunkStreamer(provider, worker_threads, StreamerConfig)`:**
  - `set_focus(world_tile, level, radius)` sets the wanted chunks: covered chunks within Chebyshev chunk distance `radius` of the focus chunk.
  - `update()` runs on the caller's thread. It evicts unwanted chunks to a bounded LRU cache, queues missing chunks (nearest first, or shuffled with `request_shuffle_seed` for tests), and integrates finished loads **in ChunkCoord order**.
  - `wait_idle()` loops until every wanted chunk is resident or has failed.
  - Accessors: `get`, `resident(level)` (coord order), `errors`, and `digest`.
  - `worker_threads = 0` loads synchronously inside `update()`.
- **`chunk_of_tile`** converts a world tile to its chunk with floor division, so `-1` is in chunk `-1`.

## Determinism
- Every provider is pure in `(coord, world_seed, gen_version)`.
- The settled resident set is exactly the wanted set, with no hysteresis. Kept chunks live in the cache, which is invisible to results. So after `wait_idle()` the resident set and `digest()` depend only on the focus, never on thread count, completion order, request order, or the walk taken.
- Consumers never see partial chunks: only integrated chunks are returned.

Tests:
- `world_tests` (synthetic provider with load jitter) runs 0, 1, 3 and 8 threads × 4 request orders, forward vs reversed walks, cache reuse, and errors.
- `procgen_world_tests` streams the sample `0_0_0` chunk inside procedural wilds with 1, 4 and 8 threads, shuffled and reversed walks. It pins stream goldens.
- `sim_tests` (`world_tests.cpp`) covers cross-chunk combat.
- The streamer and procgen_world tests are also clean under TSan.

## Sim across chunks
- `sim::Grid(span<const Chunk*>)` covers one level and any set of chunks. It uses world tiles (`Grid::to_world`, `Grid::chunk_of`). Tiles outside the set are out of bounds.
- Pathing uses the same deterministic Dijkstra as v0, with ordered `(cost, y, x)` and a fixed neighbour order, over sparse world tiles, so paths cross chunk boundaries.
- `Combat::create(reg, span<const Chunk*>, ...)` and `replay(...)` sort chunks by coord, then take spawns in file order, so argument order (for example `ChunkStreamer::resident(level)`) doesn't matter. Positions are world tiles.
- The single-chunk `Combat::create` / `Grid(chunk)` keep the chunk-local frame, so every v0–v3 golden is unchanged.

## Not yet
- `ChunkDelta` overlays (save state).
- Streaming combat mid-fight: the grid is fixed at `Combat::create`.
- Multi-level combat.
- Async texture upload (render side).
- `PackedRegionProvider`.
- Stream goldens on all three OSes in CI.
