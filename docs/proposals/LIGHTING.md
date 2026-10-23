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
Content: map/region `ambient` (in `chunk@2`, §7, and in the map/region manifest) is a **data schedule** (R5, decided):
`{"schedule": "<content id>" | "underground", "moon": "full" | "other"}`. A schedule is a sorted list of
`{from_minute, level, sun}` rows (integers). Shipped default `srd51.core.ambient.outdoor`: day bright (Sun), dawn and
dusk dim (Sun, twilight per SRD), night dim if `moon` is `full`, otherwise dark. Default when absent: `underground`
(= Dark, no sunlight), exactly today's behaviour. Rules only consumes the resulting level.

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
Item light states: `emits_light` may list `states: [{name, bright_ft, dim_ft}]` (default one state); the current state
index is part of `AttachedLight` (`state` byte) and is toggled by a light command logged under 0xCA (op `set_state`). The
bullseye lantern's 60 ft cone is **reserved** (`shape: cone`, later; validator rejects it for now).
Content key `lights: [{ "tile": {x,y}, "preset": "lantern" }]` or explicit `bright_ft` / `dim_ft`. Presets are content
(`srd51.core` light presets): `torch 20/20`, `lantern_hooded` (two item states, R6 decided: `open` bright 30 / dim +30, `hooded` dim 5 only), `brazier 20/20`, `candle 5/5`.
Optional fields default to 0 / false; unknown preset = load error at validate time.

### 3.4 Attached sources (follow a token or item)
```cpp
enum class Carrier : uint8_t { Creature, Item, Tile };   // Tile = dropped on the ground
struct AttachedLight {
    uint32_t id;                 // monotonic per Combat / world session (next_light_id)
    Carrier  carrier; uint32_t carrier_id; Vec2i tile;  // tile used only when carrier == Tile
    uint16_t bright_ft, dim_ft;
    uint8_t  flags;              // bit0 magical, bit1 sunlight (Daylight), bit2 lit
    uint8_t  state = 0;          // item light state index (hooded lantern: 0 open, 1 hooded)
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
   - `magical_darkness`: set `magical_dark`; `L = Dark` from every non-magical source, and from a magical source
     only if it would be dispelled (below). Magical darkness always beats non-magical light; light from a spell of a
     higher level than the Darkness is unaffected. The level comparison is the only rule (no partial override).
   - **Dispel (R1, decided):** Darkness dispels the spell that created an overlapping light of **2nd level or lower**;
     Daylight dispels the spell that created an overlapping darkness of **3rd level or lower** (SRD 5.1). Rules exposes
     `dispels_light(darkness_level, light_level)`; sim does the overlap test **at cast time and whenever either area
     moves** (both can be cast on a carried object), at the end of the moving step, in ascending (zone id, light id)
     order. A dispel is a state change (`LightChanged` / zone end), not a per-tile override.
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
- **Fog doesn't block light (R4, decided):** Fog Cloud and other obscuring zones are obscurement only, never occlusion;
  they don't change the light level or block light passing through. Only `opaque` zones (Wall of Fire) block light.
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
}
```
- `LightWorld` is a view: grid, ambient + clock, static lights of resident chunks, attached lights, zones.
- The cache is built from start-of-step state and frozen for the step, matching #194's "start-of-step" contract. It
  holds no state of its own and is never hashed; a test asserts `cache.at == light_at` for every tile.
- **Rules sight hook (#194):** sim installs `EncounterHooks::sight` and supplies raw inputs per (viewer, subject):
  `light_at_subject` / `light_at_viewer` = `cache.at(tile).level`, the flags (`magical_dark`, `sunlight`, obscured), the
  viewer's senses and line of sight via the shared trace (§5). Senses, in order truesight > blindsight > tremorsense > sight:
  - darkvision within range: Dim -> Bright, Dark -> Dim, **not** inside `magical_dark`;
  - truesight within range: sees normally in magical and normal darkness;
  - blindsight / tremorsense (subject on the ground) within range ignore light and obscurement;
  - otherwise sight needs line of sight, an effective level > Dark and no heavy obscurement.
- **Obscurement (R2, decided):** computed **after** senses: effective Dim = lightly obscured, effective Dark = heavily
  obscured; `Sight::obscured = max(that, zone obscurement such as Fog Cloud)`. Blindsight and truesight ignore it within
  range. Rules derives this from the inputs; sim supplies raw levels and flags.
- **Sunlight facts (R7, decided):** no `in_sunlight` hook. `Sight` gains `subject_in_sunlight` and `viewer_in_sunlight`,
  taken from the tile `sunlight` flag. Rules applies Sunlight Sensitivity (the creature, its target or the perceived thing
  in sunlight) and vampire weakness from those two bits.

## 7. Streaming and chunk format

- `chunk@2` adds `lights` (static list, local tiles) and optional `ambient`; `kChunkFormatVersion = 2`;
  `content_hash` covers the new fields only when non-empty (old chunks hash identically, cf. `docs/CHUNK_HASH.md`).
- **Max radius halo:** `kMaxLightFt = 120` (=> 24 tiles, < `kChunkSize` 32). A tile's light depends only on sources
  within 24 tiles, i.e. its chunk and the 8 neighbours. The validator rejects `bright_ft + dim_ft > kMaxLightFt`.
- **Halo gate (P2, decided):** light at tile `t` is computed only when its chunk and all 8 neighbours (the full
  24-tile halo, 3 x 3 chunks) are each **resident or known-absent** (not covered by the provider: no sources, ambient
  only; a property of the world, not of timing). A **failed or pending** load never yields partial light: the step
  blocks deterministically until the halo is complete, and a failed load is an error state, not an empty chunk.
- **Streamer (R&P owns):** `ChunkStreamer::set_foci(span<const Focus>)`, `Focus{world_tile, level, radius_chunks}`, wanted
  set = union of foci (pure in the foci set; `set_focus` becomes a one-element wrapper). Sim passes one focus per creature
  or combat bounding box with `radius_chunks = 1`, plus the camera focus. **Combat pins its chunks** (and their halo) for
  its whole duration: eviction is deferred while a combat references them (needed for cover too).
- A ring of 1 suffices only because `kMaxLightFt = 120` (24 tiles) < `kChunkSize` (32); the validator rule stays, and
  raising the max past 32 tiles would require a ring of 2.
- Test: identical light digests when the same foci are reached by different walks / `request_shuffle_seed` values;
  multi-focus golden lines in `STREAMING.md`.
- **Invalidation:** on integrate / evict of chunk C, invalidate C and its 8 neighbours' rects; on attached-source move
  or toggle, invalidate the source's halo rect (old and new tile); on zone create/end/move, its bounds + halo; on clock
  minute change crossing a table boundary, invalidate all.
- Chunk-local static source state (a lantern doused) is a `ChunkDelta` entry so it survives eviction and saves.

## 8. Events and hashing

New sim commands (`Light`, `Douse`, `Drop`, `PickUp`) reuse the action log; events: `LightChanged {light_id, flags}`
in the next `SimEvent` version (stream version bumps only for streams that carry one, as v7 did).

Tags (claimed in `SIM_EVENTS.md` by this PR, status proposed; 0xC5 / 0xC6 are Rules' #194; next free 0xCB):

| Tag | Stream | Contents | Folded when |
|---|---|---|---|
| 0xC7 | `Combat::hash` | `lights_digest`: `next_light_id`, then each `AttachedLight` in id order (all fields) | any attached light exists or has existed (`next_light_id != 1`) |
| 0xC8 | `Combat::hash` | `ambient_digest`: `AmbientKind`, profile, `minute_of_day` u32 | ambient differs from the default (underground, 720) |
| 0xC9 | `Combat::hash` | static light overrides from deltas (stable_id, lit byte), sorted | any static light toggled |
| 0xCA | `log_hash` | light command fields (`light_id` u32, op byte incl. `set_state`, state byte, target tile i32 x2) | the logged action is a light command |

Not folded: per-tile levels, the cache, and `Sight` answers (all derived). Static lights themselves are content
(covered by `content_hash`). Every existing golden is unchanged.

## 9. Luau API (append-only, next API level)

```luau
declare function light_at(tile: Vec2i): { level: "bright" | "dim" | "dark", sunlight: boolean, magical_dark: boolean }
declare function light_sources_near(tile: Vec2i, radius_ft: number): { LightInfo }   -- read-only, sorted by id
-- commands go through the normal action path (no direct mutation):
declare function request_light(actor: Entity, op: "light" | "douse" | "drop" | "pickup" | "set_state", item: Entity?): boolean
```
Typed `LightInfo { id: LightId, kind: "static"|"attached", preset: string, tile: Vec2i, bright_ft: number, dim_ft: number, magical: boolean, state: number }`. Sunlight
is read from `light_at(tile).sunlight` (the same flag `Sight` uses); there is no separate sunlight call.
Existing levels untouched; added to `engine.level<N+1>.d.luau` only.

## 10. Render contract (P1, P3 decided)

- **`LightFrame`:** after each step sim publishes an immutable `LightFrame`: for each resident visible chunk a
  `LightSample[32*32]`, the source list, and the **dirty set** (chunk coords / tile rects whose samples changed, plus
  source ids added, removed or changed). Render copies it into its `ViewModel`; it reads only the `LightFrame`, never
  `LightCache`, and never forces evaluation from the render thread.
- **Source record:** `LightId` stable and unique across kinds (static: chunk coord + index in the chunk's `lights`;
  attached: the sim's deterministic `next_light_id`, kind-tagged so the two never collide); world tile; carrier kind and
  id (so glows follow the token's interpolated position); `bright_ft`, `dim_ft`, `lit`, `magical`, `sunlight`; and a
  `preset` key (content id) that render maps to visuals.
- **Presentation fields** (colour, flicker profile, glow sprite) live only in content (a `visual` block on the preset or
  item), are excluded from `content_hash`, every hash, `ChunkDelta` and all sim logic. Changing a torch's colour moves no golden.
- **Never lit when Dark:** render may brighten or smooth across tile edges, but never shows a tile as lit when the sim
  says `Dark`, nor anything visible that the visibility mask says is unseen.
- **Fog of war:** sim/rules own visibility as per-team bitmasks per chunk (`seen_now`, `explored`; 1024 bits each),
  computed at step start from the `sight` inputs and published with the `LightFrame`. They are **derived, not hashed**,
  unless a rule comes to read them (then it gets a tag, claimed first). Render owns the fog visual (shading, edges,
  remembered-tile style, hiding unseen sprites) and which team's mask to show; it never computes visibility or queries `sight`.
- **Mask layout (phase 2, pinned by a test):** `sim::TileMask` is `std::array<uint64_t, 16>`; tile (x, y) of a chunk is
  bit = `y * 32 + x`, word = `bit / 64`, LSB-first within the word (`1ull << (bit & 63)`), so `words[0]` holds rows 0-1.
  `LightFrame` (phase 2, all ascending, derived, unhashed) adds `visibility` (per team: frame chunks with a non-empty
  `explored`), `dirty_rects` (one bounding rect of changed samples per dirty chunk), `added_chunks` / `removed_chunks`
  (tracked = sampled + blocked, against the previous frame), `blocked_chunks` (wanted, halo Pending / Failed, no samples:
  render keeps their last light) and `dirty_vis_chunks`.
- **Cost:** ~18 KiB of samples per 3 x 3 step; render packs one R8 light-map texture per chunk (1 KiB), updated by
  dirty rect. Render-internal choices (borders, atlas, tint vs shader) don't affect this contract.
- A headless build has no render dependency on light.

### 10.1 Phase 3 render prep: attached-light glows (R&P proposal, needs EC answer)

Render now consumes a generic list of light sources (`iso2d::GlowSource`: stable key, optional entity anchor, world
tile, visual radius, colour, intensity), built from `LightFrame::sources` by `slice::glows_from`. Entity-anchored glows
follow the carrier's *drawn* position (`iso2d::follow_anchors`); colour / intensity come from a render-side
`iso2d::GlowPalette` keyed by `preset` (presentation only, unhashed, §10). The glow is a capped, token-local halo: the
lit area itself is always the sim's light map, so a glow never shows a `Dark` tile as lit.

Until phase 3, `vertical_slice` keeps the interim producers in the game session: `pc_torches` (`--no-torches`) and
`Session::Options::carried_lights`, both turned into `sim::AttachedLight{Carrier::Creature}` and published through
`publish_light_frame`. No sim code changes. When phase 3 lands, the session stops building `AttachedLight`s and the
same render path reads the sim's sources.

**Engine Core's answers (2026-10-21), resolved.** Only attachment and position belong to the sim; colour and intensity
are content-only visuals and stay out of every sim hash.

1. **Ownership (resolved).** `Combat` owns its attached lights. It calls `LightDriver::step` once when created and after
   each committed step, so every committed step publishes exactly one frame. Render only reads `driver.latest()`. The
   driver lands in phase 2 PR 2; attached-light ownership arrives in phase 3.
2. **Item carriers (resolved).** `holder_id` (entity id; 0 = on the ground / unheld) is added to the attached source
   record. It joins the 0xC7 fold in phase 3, folded only when non-default.
3. **Lifetime and identity (resolved).** `LightId`s are stable. Each light-capable item gets its `LightId` once, at spawn
   or load, in deterministic id order. Lit / doused changes only its state; drop / pick-up changes only `holder_id` and
   position. An id is never reused within a session.
4. **Dead carriers (resolved).** When the carrier dies the light drops in the same step: it keeps its `LightId`, gets
   `holder_id` 0, and its position becomes the carrier's tile, in the same frame that removes the token. It stays lit
   unless content marks it as going out on drop.
5. **Presentation data (resolved, Tools & Content 2026-10-21).** Optional `visual` block on `item@1` (later on the
   light-preset schema too), additive, no version bump: `visual: {color: {r, g, b}, intensity, flicker?}` with each
   channel an integer 0..255, `intensity` an integer 0..1000 (permille) and `flicker` one of `none|torch|candle`;
   `additionalProperties: false` at every level, integers only (no floats, no hex). Schema:
   `schemas/item/v1.schema.json`. It is excluded from `content_hash` and never read by sim, so it never feeds
   `log_hash` or any golden. The game converts it once at load (`iso2d::glow_style_from_visual`: channel / 255,
   permille / 1000) and registers it under the item's **`light.preset`** (the preset its light uses; the item id need
   not match). The `GlowPalette` uses it for that preset over the built-in defaults, which stay the fallback.
   An item with `visual` but no `light.preset` is skipped with a load warning (no fallback to the item id, so a
   style can never silently attach to the wrong key). If several items style one preset, the lowest content id
   wins (independent of pack / file order) and each differing loser logs a warning (`Session::warnings()`, printed
   by `vertical_slice`). Preset-exists checking is a validator rule Tools & Content adds in `checks_v4`.
   `flicker` is carried through but not animated yet.

## 11. Open questions

Rules (all decided in the Rules review of #195; kept here as the record)
- R1. Decided: dispel at cast and whenever either area moves; Darkness dispels light spells <= 2nd, Daylight dispels darkness <= 3rd (§4).
- R2. Decided: effective Dim = lightly, Dark = heavily obscured, after senses, max with zone obscurement (§6).
- R3. Decided: light occlusion uses `segment_blocked`, the same trace as `Combat::cover_of` (§5).
- R4. Decided: fog is obscurement only and doesn't block light (§5).
- R5. Decided: ambient is a data schedule (day bright, dawn / dusk dim, full-moon night dim, other nights dark) (§3.2).
- R6. Decided: hooded lantern has two item states (open 30/+30, hooded dim 5) toggled via a 0xCA command; bullseye cone reserved (§3.3).
- R7. Decided: `Sight::subject_in_sunlight` / `viewer_in_sunlight` from the tile sunlight flag; no hook (§6).

Render & Platform (all decided in the R&P review of #195)
- P1. Decided: immutable per-step `LightFrame` (samples, sources, dirty set); cross-kind `LightId`, carrier, preset key;
  presentation in content only, unhashed; render never shows `Dark` as lit (§10).
- P2. Decided: R&P adds multi-focus streaming (`set_foci`); sim computes light only with a complete halo, blocks the step
  on pending / failed loads, and combat pins its chunks (§7). Hard dependency for phase 2.
- P3. Decided: sim/rules own visibility (per-team `seen_now` / `explored` bitmasks, derived, unhashed unless rules read
  them); render owns the fog visual (§10).
- P4. Decided: render cost is fine (~18 KiB / step, R8 light maps). Sim LOS cost is benchmarked in phase 1 (§12).

## 12. Phased plan

1. **Phase 1 (EC):** `Light` / `LightSample`, `light_at` over ambient + static sources, occlusion via `segment_blocked`
   (moved to `sim/trace.hpp`, shared with `cover_of`), `LightCache`, `LightFrame` publication; unit and determinism tests
   (all three OSes). No hash change.
   **Benchmark (P4):** worst case N sources x tiles in radius x 16 segments (e.g. 20 torches in a 3 x 3 = 9,216 tiles x
   S x 16 traces) on the CI machines. Budget: full rebuild <= 2 ms per step (Release); if exceeded, fall back to
   incremental invalidation only (full rebuild only on chunk integrate / evict and clock-boundary steps; a source move
   or toggle rebuilds just its halo rect), and per-source visibility caching keyed by source tile.
2. **Phase 2 (EC + T&C), blocked on R&P `set_foci` + combat pinning (P2):** `chunk@2`, `monster@5`, `item@2` schemas
   and identity migrations; validator radius check; halo gate (resident or known-absent, pending / failed blocks the
   step); multi-focus determinism tests. Compat fixtures stay green.
3. **Phase 3 (EC):** attached lights and commands, `LightChanged`, tags 0xC7-0xCA (requires the phase 2 halo gate,
   since carried sources cross chunk edges).
4. **Phase 4 (EC + Rules):** zone overrides, Daylight / Darkness dispel (cast and move), sight inputs for #194 incl. the
   sunlight bits; per-team visibility bitmasks.
5. **Phase 5 (EC):** `ChunkDelta` for static toggles, invalidation tuning, dirty set; streaming-order determinism tests.
   *Order note (approved via Director, Oct 2026):* the per-team visibility masks (phase 4) and the per-rect dirty set
   (phase 5) are pulled forward into phase 2 (PR 1, with added / removed / blocked chunks), ahead of the schemas and the
   sim-driven publishing (phase 2 PR 2). Their contents are unchanged.
6. **Phase 6 (EC + R&P):** Luau API level; R&P light-map textures, fog visual and `LightFrame` read path.
