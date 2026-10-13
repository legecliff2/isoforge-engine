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

## StreamDelta (for Render & Platform)
`StreamDelta update()` returns the change it made to the resident set, and `last_delta()` keeps it until the next call. `StreamDelta wait_idle()` returns the net change over all the updates it ran.

```cpp
struct StreamDelta { std::vector<ChunkCoord> added, removed; };   // each sorted by ChunkCoord
```

- `added` lists chunks that became resident during the call. You can read them with `get()` immediately.
- `removed` lists chunks that stopped being resident. Their `Chunk` is gone, so release GPU resources keyed by coord.
- The two lists are disjoint, and a coord appears at most once.
- If you start from an empty set and apply every delta in order (remove, then add), you get exactly `resident()`. Nothing is ever reported twice.
- Per-`update()` deltas depend on when loads finish, so they are presentation-only. The net delta from `wait_idle()` depends only on the focus sequence, never on thread count or request order.
- The name is not `ChunkDelta`. ARCHITECTURE.md uses that name for save-state overlays.

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

## Stream golden files (cross-OS CI)
Location: `tests/goldens/stream/*.txt`. `procgen_world_tests` ("stream golden files reproduce") reads every file and checks every line with 0 and 4 worker threads.

```
# comment (whole line); blank lines ignored
@world procgen          # directive, required before data lines: sample_layered | procgen
@gen_version 1          # directive: applies to following lines (default 1)
@level 0                # directive: focus level (default 0)
<world_seed_hex> <focus_x> <focus_y> <radius> <digest_u64>
c0ffee 5 5 2 16848166243755828085
```

- `world_seed_hex`: hex without a `0x` prefix, at most 16 digits.
- `focus_x`, `focus_y`: signed decimal world tiles.
- `radius`: chunks.
- `digest_u64`: decimal `ChunkStreamer::digest()` after `wait_idle()`.

Fields are separated by whitespace, and extra fields are an error.

`@world` selects the provider:

- `sample_layered` is the sample pack's `chunks/0_0_0.json` (FileChunkProvider) layered over `ProceduralProvider`. It is used by `sample_layered_gen1.txt`.
- `procgen` is `ProceduralProvider` only. It is used by `procgen_gen1.txt`.

**Adding goldens:** append a line to any file, or add a new `*.txt` file. The test picks up every data line automatically; no code change is needed. To get a digest, run `procgen_world_tests -tc="stream golden files reproduce"` with a placeholder digest (for example `0`). The test fails and prints the actual value as `MESSAGE: <file>: <seed> <x> <y> <radius> <digest>`. Every OS must reproduce every line. A digest changes only on purpose, when the chunk content, gen_version output, or the digest definition changes. Explain any such change in the PR.

## Known limitations
- **Procgen-to-handcrafted edge connectivity is not guaranteed.** gen_version 1 (v0) chunks keep an empty outer ring, so a corridor never runs into a neighbour, handcrafted or procedural. Connectivity across chunk edges arrives with the milestone 3 region passes. Until then, a combat or path spanning a procedural/handcrafted border may find no route.

## Not yet
- `ChunkDelta` overlays (save state).
- Streaming combat mid-fight: the grid is fixed at `Combat::create`.
- Multi-level combat.
- Async texture upload (render side).
- `PackedRegionProvider`.
- Stream goldens on all three OSes in CI.
