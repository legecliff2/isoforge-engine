# Lighting & vision proposal (R-G13)

Status: **proposal, design only (no code)**. Owner: Engine Core. Reviewers: Rules, Render & Platform.
Inputs: `docs/ARCHITECTURE.md` (§3.2 schemas, §4 determinism, §5.2 chunk), `docs/SIM_EVENTS.md` (hash tag registry),
`docs/STREAMING.md`, `docs/proposals/ZONES_AURAS.md` (R-G11 zone flags), Rules PR #194 (`EncounterHooks::sight`).

## 1. Goals and non-goals

Goals
- One modular light model with three source kinds (ambient, static, attached) composed into one per-tile level:
  `bright | dim | dark`.
- Darkness / Fog Cloud stay R-G11 zones and act as **overrides** on the composed result.
- Vision senses (darkvision, blindsight, tremorsense, truesight) are evaluated against the per-tile result, feeding the
  Rules `sight` hook from #194.
- Deterministic: integers only, radii in feet on the 5 ft grid, a fixed composition order, stable iteration, no floats.
- Renderer-independent: sim owns light; render reads it.
- Works with streaming chunks (edge sources, unloaded neighbours, cache invalidation).
- Older content and old saves / replays keep working; every existing hash is unchanged when no light state exists.

Non-goals
- Coloured light, falloff gradients, shadows-as-art, normal maps (render may do anything cosmetic, §9).
- Light over elevation / roofs (2D only, like R-G11 areas). Vertical light is an open question (§10).
- Burning objects, fire spread, fuel duration beyond a simple turn counter (R-G12 objects later).
- Hearing / Silence (separate; `kZoneSilence` already stored).

## 2. Current state (what this builds on)

- `TileCell::flags` bit1 `blocks_sight`; `sim::Grid::blocks_sight` and `Grid::line_of_sight` (integer supercover,
  symmetric, corner rule: blocked only if both side tiles block).
- R-G11 zone flags in `rules5e/spells.hpp`: `kZoneLightObscured 2`, `kZoneHeavyObscured 4`, `kZoneMagicalDarkness 8`,
  `kZoneOpaque 64`, stored only "until R-G13" (ZONES_AURAS §Q6). `ZoneAnchor` is `Point | Creature` (Object later).
- Cover exists on main (#178, #187): `EncounterHooks::cover` + `CoverQuery` (`rules5e/encounter.hpp`), implemented by
  sim as `Combat::cover_of` (`sim/src/combat.cpp`). `cover_of` traces corner-to-corner segments between the two spots'
  tile corners with `segment_blocked(p, q, blocks)` (file-local in `combat.cpp`: integer breakpoints, a piece inside a tile
  is blocked by that tile, a piece along an edge or through a corner only when both tiles beside it block, as
  `Grid::line_of_sight`), with a `wall` predicate = `grid_.blocks_sight` excluding the source and target spots.
- Rules #194 (open): `EncounterHooks::sight(state, viewer, subject) -> Sight`, pure, start-of-step; no hook = everyone
  sees everyone. Hash tags 0xC5 / 0xC6 reserved by Rules; next free **0xC7**.
- Renderer (`engine/render`, `engine/iso2d`): **no lighting today**. No contract to break.
- `chunk@1` is the only chunk schema; `kChunkFormatVersion = 1`.

## 3. Data model

### 3.1 Light levels
```cpp
enum class Light : uint8_t { Dark = 0, Dim = 1, Bright = 2 };   // ordered: max() composes
```
Also per tile a 1-byte `LightFlags`: bit0 `magical_dark` (inside magical darkness), bit1 `sunlight` (the tile's light
is sunlight per SRD: ambient day, or a Daylight-class source flagged `sunlight`), bit2 `obscured_light`, bit3
`obscured_heavy`. `LightSample { Light level; uint8_t flags; }` is what `light_at` returns.

### 3.2 Ambient (per map / region)
```cpp
enum class AmbientKind : uint8_t { None, Sun, Moon };            // None = underground / indoors
struct Ambient { AmbientKind kind = AmbientKind::None; Light level = Light::Dark; };
struct AmbientClock { uint32_t minute_of_day = 720; };            // 0..1439, hashed only if non-default
```
Content: map/region `ambient` (in `chunk@2`, §7, and in the map/region manifest): `{"profile": "outdoor" | "underground"
| "fixed", "fixed_level": "bright|dim|dark"}`. The `outdoor` profile is a fixed integer table, minute -> level
(06:00-18:59 bright Sun, 05:00-05:59 and 19:00-19:59 dim Sun, otherwise dim Moon or dark, open question R5). Default when
absent: `underground` (= Dark, no sunlight), which is exactly today's behaviour (no light model).

### 3.3 Static point sources (authored)
```cpp
struct StaticLight {
    Vec2i    tile;              // local to chunk
    uint16_t bright_ft = 0;     // multiples of 5
    uint16_t dim_ft    = 0;     // extra dim ring beyond bright (SRD "dim for an additional N ft")
    uint8_t  flags     = 0;     // bit0 magical, bit1 sunlight, bit2 starts_unlit
    uint32_t stable_id;         // StableHash(coord, tile, index), like EntitySpawn; never in files
};
```
Content key `lights: [{ "tile": {x,y}, "preset": "lantern" }]` or explicit `bright_ft` / `dim_ft`. Presets are content
(`srd51.core` light presets): `torch 20/20`, `lantern_hooded 30/30` (dim 5 when hooded, open R6), `brazier 20/20`, `candle 5/5`.
Optional fields default to 0 / false; unknown preset = load error at validate time.

### 3.4 Attached sources (follow a token or item)
```cpp
enum class Carrier : uint8_t { Creature, Item, Tile };   // Tile = dropped on the ground
struct AttachedLight {
    uint32_t id;                 // monotonic per Combat / world session (next_light_id)
    Carrier  carrier; uint32_t carrier_id; Vec2i tile;  // tile used only when carrier == Tile
    uint16_t bright_ft, dim_ft;
    uint8_t  flags;              // bit0 magical, bit1 sunlight (Daylight), bit2 lit
    uint8_t  spell_level = 0;    // 0 = mundane; Light=0 cantrip marked magical, Daylight=3
    uint32_t spell_id = 0, caster_id = 0;   // for dispel / concentration
};
```
SRD values: torch 20/20, lantern 30/30, *Light* 20/20 (magical, on an object), *Daylight* 60/60 (magical, sunlight,
3rd level). Glowing monsters get an `emits_light {bright_ft, dim_ft}` optional block in `monster@5` (default absent).
Items get `emits_light` + `lit_by_default` in `item@2`. A carrier's position is read from its footprint centre tile at
step start (n x n footprints: the anchor tile + (n-1)/2, integer).
State changes: `Light`/`Douse`/`Drop`/`PickUp` (sim commands, logged), dispel (Daylight vs Darkness, Dispel Magic), carrier
death leaves item light on its tile (`Carrier::Tile`).

### 3.5 Schemas and migrations
- `chunk@1 -> chunk@2`: adds optional `lights` (default `[]`) and `ambient` (default `underground`). Migration is the
  identity plus defaults; `chunk@1` stays immutable.
- `monster@4 -> @5`, `item@1 -> @2`: add optional `emits_light`, `senses` already present in monster (darkvision etc.)
  is reused; if a sense is missing it defaults to none. Identity migrations.
- Old content therefore produces no light state at all, and the model degenerates to "ambient none, no sources".
  With the hook unset (Rules #194), behaviour is unchanged.

## 4. Composition rules

Per tile `t`, fixed order (each step is a pure integer function):
1. **Sources:** `L = max over lit sources s reaching t of level(s, t)`, where with `d = chebyshev_tiles(s, t) * 5`:
   `d <= bright_ft` -> Bright; `d <= bright_ft + dim_ft` -> Dim; else none. "Reaching" = §5 occlusion.
   Sources are visited in `(kind, stable_id | id)` order; since max is commutative this only matters for the flags,
   which are OR'd (`sunlight` if any contributing source with the winning level is sunlight).
2. **Zone overrides** (R-G11 zones covering `t`, ascending zone id):
   - `magical_darkness`: set `magical_dark`; `L = Dark` **unless** a *magical* source of spell level > the Darkness
     level reaches `t` (SRD: light from a spell of higher level is unaffected; non-magical light can't illuminate).
     Daylight cast **overlapping** a Darkness of level <= 3 dispels that zone (SRD 5.1 Daylight), handled as a state
     change at cast time, not here; likewise Darkness dispels overlapping light spells of level <= 2.
   - `light_obscured` / `heavy_obscured` (Fog Cloud = heavy): set the obscured flags only; they don't change `L`.
   - `opaque` (Wall of Fire): treated as `blocks_sight` for §5 tracing.
3. **Ambient floor:** `L = max(L, ambient.level)` unless `magical_dark`; `sunlight` flag if the ambient is Sun and
   ambient level >= Dim and the winning level came from it (or from a sunlight source).

The result is `LightSample`. Senses then map it per viewer (§6).

## 5. Occlusion

- Light reuses **the cover trace**: `segment_blocked` from `Combat::cover_of`, with the same `wall` predicate
  (`blocks_sight`, excluding the source and target tiles) and no creature crowd (creatures don't block light). Source
  tile `s` lights tile `t` iff at least one of the 4 x 4 corner-to-corner segments between `s` (1 x 1 spot) and `t` is
  not blocked, i.e. exactly when `cover_of` would *not* report `Cover::Total` from `s` to `t` ignoring creatures. The
  source tile itself always counts. A `blocks_sight` wall tile is lit when the trace to it is clear, so walls facing a
  torch are lit and walls behind them are not.
- Phase 1 moves `segment_blocked` unchanged (a code move, no behaviour change, cover goldens untouched) from the
  anonymous namespace in `combat.cpp` to a shared sim header (`sim/trace.hpp`), called by both `cover_of` and light.
- Zones with `opaque` and heavy-obscured zones **do not** block light (fog is lit, just not seeable through); only
  `opaque` blocks it. Open question R4.
- One trace for cover and light, one golden test table for both; no second ray algorithm.

## 6. Sim API

```cpp
namespace eng::sim {
LightSample light_at(const LightWorld&, Vec2i world_tile) noexcept;   // pure, no caching
class LightCache {             // per step, rebuilt lazily
public:
    void invalidate_all();  void invalidate_rect(Rect tiles);   // edits, source moves, zones, chunk load/unload
    LightSample at(Vec2i world_tile);                             // == light_at, memoised
};
struct SenseRanges { uint16_t darkvision_ft, blindsight_ft, tremorsense_ft, truesight_ft; };
Light perceived_light(LightSample, const SenseRanges&, int dist_ft) noexcept;
bool in_sunlight(const LightWorld&, Vec2i tile) noexcept;              // LightFlags::sunlight
}
```
- `LightWorld` is a view: grid, ambient + clock, static lights of resident chunks, attached lights, zones.
- The cache is built from start-of-step state and frozen for the step, matching #194's "start-of-step" contract. It
  holds no state of its own and is never hashed; a test asserts `cache.at == light_at` for every tile.
- **Rules sight hook (#194):** sim installs `EncounterHooks::sight`. Per (viewer, subject):
  `light_at_subject = cache.at(subject).level`, `light_at_viewer` likewise; `obscured` from the obscured flags plus
  Dark = heavily obscured and Dim = lightly obscured (SRD); then per sense, in order truesight > blindsight >
  tremorsense > sight:
  - darkvision within range: Dim -> Bright, Dark -> Dim, **not** inside `magical_dark`;
  - truesight within range: sees normally in magical and normal darkness;
  - blindsight / tremorsense (tremorsense: subject on the ground, not flying) within range ignore light and obscurement;
  - otherwise sight needs line of sight and a perceived level > Dark and no heavy obscurement.
  `sense` reports the first that succeeds; blinded / invisible stay Rules' inputs.
- **Sunlight facts:** `in_sunlight(tile)` feeds Sunlight Sensitivity (disadvantage on attacks and sight Perception) and
  vampire weakness (Rules-owned effects; sim only supplies the fact as an `EncounterHooks::in_sunlight(state, id)`
  hook proposal, default false = today's behaviour).

## 7. Streaming and chunk format

- `chunk@2` adds `lights` (static list, local tiles) and optional `ambient`; `kChunkFormatVersion = 2`;
  `content_hash` covers the new fields only when non-empty (old chunks hash identically, cf. `docs/CHUNK_HASH.md`).
- **Max radius halo:** `kMaxLightFt = 120` (=> 24 tiles, < `kChunkSize` 32). A tile's light depends only on sources
  within 24 tiles, i.e. its chunk and the 8 neighbours. The validator rejects `bright_ft + dim_ft > kMaxLightFt`.
- **Unloaded neighbours:** sim already never observes partially loaded chunks. Light at tile `t` is only *defined* when
  all chunks within the halo are resident; `ChunkStreamer`'s resident ring for sim must be >= 1 chunk around any chunk
  that holds a creature (STREAMING.md already keeps a ring; we require it to be >= 1, open R&P question). Outside that,
  `light_at` returns ambient only, and nothing in sim reads it.
- **Invalidation:** on integrate / evict of chunk C, invalidate C and its 8 neighbours' rects; on attached-source move
  or toggle, invalidate the source's halo rect (old and new tile); on zone create/end/move, its bounds + halo; on clock
  minute change crossing a table boundary, invalidate all.
- Chunk-local static source state (a lantern doused) is a `ChunkDelta` entry so it survives eviction and saves.

## 8. Events and hashing

New sim commands (`Light`, `Douse`, `Drop`, `PickUp`) reuse the action log; events: `LightChanged {light_id, flags}`
in the next `SimEvent` version (stream version bumps only for streams that carry one, as v7 did).

Proposed tags (claimed in `SIM_EVENTS.md` in the PR that introduces them; 0xC5 / 0xC6 are Rules' #194):

| Tag | Stream | Contents | Folded when |
|---|---|---|---|
| 0xC7 | `Combat::hash` | `lights_digest`: `next_light_id`, then each `AttachedLight` in id order (all fields) | any attached light exists or has existed (`next_light_id != 1`) |
| 0xC8 | `Combat::hash` | `ambient_digest`: `AmbientKind`, profile, `minute_of_day` u32 | ambient differs from the default (underground, 720) |
| 0xC9 | `Combat::hash` | static light overrides from deltas (stable_id, lit byte), sorted | any static light toggled |
| 0xCA | `log_hash` | light command fields (`light_id` u32, op byte, target tile i32 x2) | the logged action is a light command |

Not folded: per-tile levels, the cache, and `Sight` answers (all derived). Static lights themselves are content
(covered by `content_hash`). Every existing golden is unchanged.

## 9. Luau API (append-only, next API level)

```luau
declare function light_at(tile: Vec2i): { level: "bright" | "dim" | "dark", sunlight: boolean, magical_dark: boolean }
declare function in_sunlight(tile: Vec2i): boolean
declare function light_sources_near(tile: Vec2i, radius_ft: number): { LightInfo }   -- read-only, sorted by id
-- commands go through the normal action path (no direct mutation):
declare function request_light(actor: Entity, op: "light" | "douse" | "drop" | "pickup", item: Entity?): boolean
```
Typed `LightInfo { id: number, kind: "static"|"attached", tile: Vec2i, bright_ft: number, dim_ft: number, magical: boolean }`.
Existing levels untouched; added to `engine.level<N+1>.d.luau` only.

## 10. Render contract

- Render reads, per visible chunk, `LightSample[32*32]` from the cache after the step, plus the source list
  (`LightInfo`, for flicker / glow sprites). It never computes gameplay light or writes back.
- Anything cosmetic (smooth gradients, colour, flicker, fog-of-war per player) is presentation, exempt from
  determinism (ARCHITECTURE §4). A headless build has no render dependency on light.

## 11. Open questions

Rules
- R1. Darkness vs light: confirm "magical source of higher spell level than Darkness is unaffected" and dispel on
  overlap at cast time (Daylight <= 3, Darkness <= 2 light spells), not continuous.
- R2. Should Dark / Dim map to heavily / lightly obscured inside `Sight::obscured`, or stay separate from zone obscurement?
- R3. (Settled, not open.) Light occlusion uses `segment_blocked`, the same trace as `Combat::cover_of` (§5).
- R4. Does Fog Cloud (heavy obscurement) block light passing through it? Proposed: no.
- R5. Outdoor day table and moonlight level (SRD treats moonlit nights as dim): fixed profile or content-defined?
- R6. Hooded lantern and other modes: preset modes or out of scope?
- R7. `in_sunlight` as a new hook (default false) vs a `Sight` field?

Render & Platform
- P1. Is a post-step `LightSample` grid + source list enough, or do you need per-source falloff in feet?
- P2. Streaming ring: can sim's resident ring guarantee >= 1 chunk halo around creatures?
- P3. Fog-of-war per player view: computed by render from `sight`, or a sim API?
- P4. Budget: 9 chunks x 1024 tiles cache per step; acceptable for your frame budget on the target CPUs?

## 12. Phased plan

1. **P1 (EC):** `Light`/`LightSample`, `light_at` over ambient + static sources, occlusion via `segment_blocked` (moved to `sim/trace.hpp`, shared with `cover_of`),
   `LightCache`; unit and determinism tests (all three OSes). No hash change.
2. **P2 (EC + T&C):** `chunk@2`, `monster@5`, `item@2` schemas and identity migrations; validator radius check;
   compat fixtures stay green.
3. **P3 (EC):** attached lights and commands, `LightChanged`, tags 0xC7-0xCA claimed in SIM_EVENTS first.
4. **P4 (EC + Rules):** zone overrides, Daylight/Darkness dispel, sight hook implementation for #194, `in_sunlight`.
5. **P5 (EC):** streaming halo, invalidation, `ChunkDelta` for static toggles; streaming-order determinism tests.
6. **P6 (EC + R&P):** Luau API level, render contract read path.
