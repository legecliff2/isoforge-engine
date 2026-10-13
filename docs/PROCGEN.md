# Procedural generation v0 (`engine/procgen`)

Status: v0, `gen_version` 1. Owner: Tools/Content. Implements ARCHITECTURE.md §4.2, §4.4 and §4.6.

## Module layout and dependency rule

| Target | Path | Depends on | Purpose |
|---|---|---|---|
| `eng::procgen` | `engine/procgen/` | `base`, `detmath`, `rng` only (DETERMINISTIC) | seed strings, chunk seeds, dungeon generator (noise from `detmath::noise`), chunk@1 JSON + content hash |
| `eng::procgen_world` | `engine/procgen/world_glue/` | `world`, `procgen` | `ProceduralProvider : IChunkProvider` adapter (thin glue) |
| `procgen_dump` | `engine/procgen/tools/` | `procgen` | writes a chunk@1 preview pack for a seed + coord list |

**Why `engine/procgen` and not `tools/procgen`:** the generator runs at runtime, inside the streamer, on worker threads (§5.2 `ProceduralProvider`). So it is an engine module, not an offline tool. Only the preview dumper is a tool.

**Why procgen doesn't emit `world::Chunk` directly:** `world` depends on `ecs` and `assets`. Procgen may only use `base` + `detmath`/`rng`, so it can't include `world/chunk.hpp`. Instead:
- It emits `procgen::GenChunk`, a field-for-field mirror of `world::Chunk`.
- It reimplements `spawn_stable_id` and `compute_content_hash` over the identical canonical bytes.
- `world_glue` converts a `GenChunk` to a `Chunk` and fails loudly if the two hashes ever disagree. `procgen_world_tests` cross-checks this.

`rng` lives in `engine/detmath/`, and §4.2 places `derive_seed` there, so it counts as part of the detmath dependency.

## Seed strings (§4.6)
- Format: `g<gen_version>-<base32(seed64)>`.
  - `gen_version` is decimal, 1..65535, with no leading zeros.
  - The base32 part uses RFC 4648 uppercase (`A–Z2–7`) with no padding. It is exactly 13 characters (65 bits, big-endian), so the first character is always `A`..`P`.
- The doc's example `g1-K3QZ7M2XAB4PE` decodes to seed `0xADC33F66AE00F1E4`.
- There is no checksum, because the doc specifies none. Parsing is case-insensitive (`G1-k3qz…` is accepted); printing is always the canonical form (`g` + uppercase base32). Padding, wrong length, a first character above `P` (more than 64 bits), and bad prefixes are rejected.

## Chunk seeds (§4.2)
`chunk_seed = rng::derive_seed(world_seed, {"CHNK", gen_version, x, y, level})`.

The doc gives `derive_seed(world_seed, tag, x, y, level)`. We add `gen_version` as a key, so a new generator version never reuses streams from an old one. The layout RNG is `rng::Pcg32(chunk_seed, stream 1)`.

World-continuous noise uses `noise_seed = derive_seed(world_seed, {"NOIS", gen_version})`. It is sampled at world tile coordinates, so detail features line up across chunk borders.

## Noise (§4.4)
Integer value noise:
- The lattice value is the top 16 bits of `StableHash(u64 seed, i64 ix, i64 iy, u32 octave)` (xxHash3-64 via `eng::CanonicalHasher`).
- Interpolation uses smoothstep in Q16.16 with int64 intermediates.
- Inputs are integer world-tile coordinates (int64). Each octave has a cell size of 2^k tiles. The fraction inside a cell is computed with shifts, so there is no Fx overflow even at int32 chunk extremes.
- fBm uses amplitudes 2^-o and is normalised to [0, 1).

Lives in `detmath::noise` (`engine/detmath`, owned by Rules), moved there unchanged from procgen; hashing uses `detmath::stable_hash`. Goldens in `detmath_tests` and procgen's content-hash goldens pin the outputs.

## Dungeon generator (gen_version 1)
1. Place 3–6 rooms, each 4–9 × 4–9 floor cells, within [2, 29]. Rooms are kept at least 2 cells apart, with up to 64 placement attempts.
2. Join each room to the previous one with an L-shaped corridor between room centres. The elbow direction is picked by the RNG. All floor is one 4-connected component by construction, and the tests also check it with a BFS.
3. Ground layer: floor tile wherever there is floor. Walls layer: any non-floor cell 8-adjacent to floor becomes a wall (`blocks_move | blocks_sight`).
4. Detail layer, from fBm noise: rubble (`difficult`) or water (`water`). Neither blocks movement, so connectivity is preserved.
5. One `srd51.monster.goblin` spawn at the centre of the last room.

Output is chunk@1 exactly as `chunk_codec` decodes it:
- compact layers, with all-zero arrays omitted
- `palette[0] == ""`
- only flag bits 0–3
- `origin: "procedural"`
- no `stable_id`
- `content_hash` included

The outer ring of every chunk is empty, so a chunk never depends on its neighbours (§4.4).

## Tests
- **`procgen_tests`**
  - Seed round-trips for edge values and 1000 random seeds; rejection cases; doc-example golden.
  - Chunk-seed goldens, cross-checked against an independent Python port of `derive_seed`.
  - Noise range, continuity and goldens.
  - Format invariants and connectivity over 51 coords × 4 seeds, including int32/int16 extremes.
  - Content-hash goldens for 6 coords, including negative coords.
  - Shuffled-order and multi-thread (2, 3 and 8 threads) generation must match serial generation.
- **`procgen_world_tests`**
  - `spawn_stable_id` parity with `world`.
  - Procgen JSON decodes through `world::chunk_from_json`, whose `content_hash` check passes, and matches `to_chunk`.
  - `ProceduralProvider` loads chunks, is consistent across threads, and handles NotFound and an unknown `gen_version`.
  - A baked procgen chunk loads through `FileChunkProvider` and keeps `origin: procedural`.
- **`procgen_pack_dump` + `procgen_pack_validate`:** dumps 4 chunks for the doc seed (including `-1000000,123456,7`) and runs `tools/validate` on them. This covers the schema and an independent Python recomputation of `content_hash`.
- **`procgen_detlint`** (`tools/detlint/detlint.cmake`): fails on `float`/`double`, float literals, `<cmath>`/`<random>`/`<chrono>`, `std::hash`, `unordered_*`, and `rand` anywhere under `engine/procgen`.

Changing any golden requires a new `gen_version` (§4.6/§4.7, D-007). `generate()` dispatches through an append-only registry (`generator_versions()`, contiguous from 1, `static_assert`ed); a test requires goldens for every registered version, and old versions stay callable forever.

## What Engine Core needs to do
1. **Adopt or relocate the adapter.**
   - Keep `engine/procgen/world_glue` (`eng::procgen_world`) as is, or move it to `engine/core/world_procgen/`.
   - Either way, `world` itself must not depend on `procgen`.
   - Register the edges `procgen → base, detmath, rng` and `procgen_world → world, procgen` in `tools/depcheck` once it exists.
2. **Wire it into streaming.** `LayeredProvider` / `ChunkStreamer` should construct `ProceduralProvider` and fill `ChunkRequest{coord, world_seed, gen_version}` from the parsed seed string (`procgen::parse_seed`).
3. **Baked procedural chunks:** resolved. `FileChunkProvider` (formerly `HandcraftedProvider`) serves chunk files of any origin and preserves it.
4. **Confirm the `ChunkRequest` mapping.** `ChunkRequest.gen_version` (uint16) is the seed string's `g<N>`, and `world_seed` is the decoded 64-bit value.

## Open questions
1. Is including `gen_version` in the chunk-seed keys OK? It goes beyond the doc's `tag, x, y, level`.
2. Should the noise move to `detmath::noise` (Rules)? If so, should it be the doc's `Fx`-coordinate signature, or this integer-coordinate one (safe at world scale, §9 Q11)?
3. Cross-chunk connectivity: v0 chunks are self-contained islands. Corridors between chunks need region passes (§4.4) and a policy for which border cells are doorways.
4. Should the generic `tools/detlint` be applied to `rules5e`/`sim`/`world/gen` as §4.7 describes? Today it only covers procgen.
