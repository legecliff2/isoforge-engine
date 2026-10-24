# chunk@1 / chunk@2 / chunk@3 content_hash and spawn stable_id: byte layout

Reference: `engine/core/world/src/chunk_codec.cpp` (C++) and `tools/chunk_hash/chunk_hash.py`
(Python, `pip install xxhash`). Test vectors: `tools/chunk_hash/vectors/`. `world_tests` checks
that both implementations agree.

**Hash:** `StableHash` = XXH3-64, seed 0 (`XXH3_64bits`), over the canonical bytes below.
All integers are **little-endian** fixed width. There is no padding and no separators.
`str(s)` = `u32 byte_length` followed by the raw UTF-8 bytes (no terminator).

## spawn stable_id
```
h  = XXH3_64( i32 coord.x | i32 coord.y | i16 coord.level | u32 spawn_index )   // 14 bytes
id = u32(h) ^ u32(h >> 32)
```
`spawn_index` = 0-based position in the file's `spawns` array. Files never carry `stable_id`.

## content_hash
Fields are written in this order:

| # | Field | Encoding |
|---|---|---|
| 1 | format_version | `u16` (= 1) |
| 2 | coord | `i32 x`, `i32 y`, `i16 level` |
| 3 | palette | `u32 count`, then `str(entry)` for each entry, **including slot 0 `""`** |
| 4 | layers | for layer 0..3 (ground, detail, walls, roof), cells 0..1023 (index = y*32+x): `u16 tile`, `u8 elevation`, `u8 flags`. Omitted arrays are zeros. Always 4×1024×4 = 16384 bytes. |
| 5 | spawns | `u32 count`, then for each spawn: `str(archetype)`, `i32 tile.x`, `i32 tile.y`, `u32 stable_id` (computed as above), `str(overrides)` |
| 6 | triggers | `u32 count`, then `str(canonical(trigger_object))` for each trigger |

| 7 | chunk@2 tail (only when the chunk has `lights` or `ambient`) | `u32 count`, then per light (file order): `i32 tile.x`, `i32 tile.y`, `u16 bright_ft`, `u16 dim_ft`, `u8 flags` (1 magical, 2 sunlight, 4 starts_unlit), `str(preset)` (`""` if absent), `u32 stable_id`; then `u8 has_ambient`, and if 1: `u8 kind` (0 none, 1 sun, 2 moon), `u8 level` (0 dark, 1 dim, 2 bright) |

- **chunk@2 (R-G13):** field 1 is written as `1` when the chunk has no `lights` and no `ambient`, so every chunk@1
  hash is unchanged after the identity migration; otherwise `2`, followed by field 7. Light `stable_id` =
  `u32(h) ^ u32(h >> 32)` with `h = XXH3_64(i32 x | i32 y | i16 level | u32 light_index | u8 'L')`; never authored.
- **chunk@3 (altitude PR2):** no new field. Field 1 is written as `3` when the chunk is chunk@3 (`format_version` 3,
  or `"$schema": "chunk@3"` without `format_version`), and field 7 is then **always** written (an empty tail is
  `u32 0`, `u8 0`). chunk@1 / chunk@2 hashes are unchanged: the identity migrations keep the declared
  `format_version` (1 or 2), and only a declared 3 changes field 1.
- **Not hashed:** `origin`, `content_hash`, `$schema`, and a light's `visual` block (colour, flicker, glow).
- `overrides`: `""` if the field is absent or `{}`. Otherwise `canonical(obj)`.
- `canonical(x)` = nlohmann `dump()`: keys sorted (byte order), no whitespace, integers
  in decimal, UTF-8 left unescaped. In Python: `json.dumps(x, sort_keys=True, separators=(",", ":"), ensure_ascii=False)`.
  Floats are rejected at load, so number formatting is never ambiguous.
- In files, `content_hash` is 16 lowercase hex digits (optional; the loader verifies it when present).

## Vectors
| File | content_hash |
|---|---|
| `tools/chunk_hash/vectors/v1_minimal.json` (empty chunk at 3,4,-1) | `01fa4a2fde49834b` |
| `tools/chunk_hash/vectors/v2_small.json` (tiles/elevation/flags, 2 spawns with overrides, 1 trigger) | `963a7d22563ff71a` |
| `engine/core/tests/fixtures/chunks/0_-1_0.json` | `1e0ef519c7654a57` |
| `tools/chunk_hash/vectors/v3_lights.json` (chunk@2: 2 lights, one with `visual`, moon ambient) | `cb3d7a42055e8f52` |

| `tools/chunk_hash/vectors/v4_ground.json` (chunk@3: ground elevation 0..3 on every 7th cell) | `c13bac9f6120463f` |

stable_id: `(0,0,0,#0)` = `0xe9ca1f53`, `(-1,7,-2,#3)` = `0x3660e539`.

## chunk@3 ground (sim rule; altitude PR2)

`elevation` (`u8` per layer cell) is the **ground height in 5 ft steps**: ground_ft = 5 × elevation (0..1275 ft).
The sim reads it as ground **only for chunk@3**; chunk@1 / chunk@2 chunks (and procedural ones) are flat for the
sim and `elevation` stays render-only (iso stacking), so older maps and the compat goldens behave identically.

Per cell (chunk@3), over the 4 layers:
- **ground_ft** = 5 × max `elevation` over the layers. On a **water** cell the value is instead the water **depth**
  (the surface is ground 0): a swimmer may descend to `alt_ft = -depth`.
- **obstacle top** of a `blocks_move` cell = 5 × max `elevation` over its blocking layers; 0 means full height (no
  flyer passes over it). chunk@1 / chunk@2 blockers are always full height.
- A creature's ground is the **max** over its footprint; `Combat::abs_height_ft(id)` = that + `alt_ft`.

Walking step from ground `a` to ground `b` (Python mirrors: `tools/validate/validate_packs.py`
`ledge_ft` / `step_extra_ft`; C++ `eng::sim::ledge_ft` / `eng::sim::step_cost_ft` in `sim/grid.hpp`):
- `ledge_ft(a, b) = b - a` (signed rise; negative = drop).
- extra movement: descending (`rise <= 0`) and a step up of 5 ft or less cost nothing extra.
  A rise above 5 ft is a climb: `rise` with a climb or fly speed, else `2 * rise`.
  A drop of more than 5 ft is a **fall** (a flyer becomes airborne instead).
