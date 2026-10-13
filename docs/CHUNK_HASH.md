# chunk@1 content_hash and spawn stable_id: byte layout

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

- **Not hashed:** `origin`, `content_hash`, `$schema`.
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

stable_id: `(0,0,0,#0)` = `0xe9ca1f53`, `(-1,7,-2,#3)` = `0x3660e539`.
