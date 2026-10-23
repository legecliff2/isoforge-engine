#pragma once
// R-G13 light model, phase 1 (docs/proposals/LIGHTING.md §3-§6, §10, §12). Pure and deterministic: integer math only,
// radii in feet on the 5 ft grid, a fixed composition order, stable iteration. Nothing here is hashed, logged or read by
// rules yet: schemas / loaders (phase 2), attached-light commands and tags 0xC7-0xCA (phase 3), the Sight hook (phase 4),
// streaming (phase 2 / 5), Luau and render (phase 6) come later.
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "base/math.hpp"
#include "sim/grid.hpp"
#include "world/chunk.hpp"

namespace eng::sim {

// §3.1 light levels, ordered so that max() composes.
enum class Light : uint8_t { Dark = 0, Dim = 1, Bright = 2 };

// §3.1 per-tile flags (LightSample::flags).
namespace light_flags {
inline constexpr uint8_t kMagicalDark = 1;    // inside magical darkness
inline constexpr uint8_t kSunlight = 2;       // the tile's light is sunlight (SRD)
inline constexpr uint8_t kObscuredLight = 4;  // a zone makes the tile lightly obscured
inline constexpr uint8_t kObscuredHeavy = 8;  // a zone makes the tile heavily obscured (Fog Cloud)
} // namespace light_flags

struct LightSample {
    Light level = Light::Dark;
    uint8_t flags = 0;
    auto operator<=>(const LightSample&) const = default;
};

// §3.2 ambient. The minute-of-day schedule (R5) is content and arrives with the loaders (phase 2); phase 1 takes the
// resolved level. Default = underground: Dark, no sunlight (today's behaviour).
enum class AmbientKind : uint8_t { None, Sun, Moon };
struct Ambient {
    AmbientKind kind = AmbientKind::None;
    Light level = Light::Dark;
};

// Source flags (StaticLight::flags, AttachedLight::flags, LightSource::flags).
namespace light_src {
inline constexpr uint8_t kMagical = 1;
inline constexpr uint8_t kSunlight = 2;       // Daylight-class: its light is sunlight
inline constexpr uint8_t kStartsUnlit = 4;    // StaticLight only
inline constexpr uint8_t kLit = 4;            // AttachedLight only (doc §3.4 bit2 lit)
} // namespace light_src

// §3.3 a static point source, authored in a chunk (tile is chunk-local).
struct StaticLight {
    Vec2i tile;
    uint16_t bright_ft = 0;    // multiples of 5
    uint16_t dim_ft = 0;       // extra dim ring beyond bright
    uint8_t flags = 0;         // light_src::kMagical | kSunlight | kStartsUnlit
    uint32_t stable_id = 0;    // computed by the loader (phase 2); not used for ordering
};

// §3.4 a source attached to a creature, an item, or dropped on a tile.
enum class Carrier : uint8_t { Creature, Item, Tile };
struct AttachedLight {
    uint32_t id = 0;           // monotonic per session (next_light_id), allocated by sim
    Carrier carrier = Carrier::Creature;
    uint32_t carrier_id = 0;
    Vec2i tile;                // world tile, used only when carrier == Tile
    uint16_t bright_ft = 0, dim_ft = 0;
    uint8_t flags = 0;         // light_src::kMagical | kSunlight | kLit
    uint8_t state = 0;         // item light state index (hooded lantern: 0 open, 1 hooded)
    uint8_t spell_level = 0;   // 0 = mundane or cantrip; Daylight = 3
    uint32_t spell_id = 0, caster_id = 0;
};

// §10 LightId: stable and unique across kinds. Static: chunk coord + index in the chunk's light list. Attached: the
// sim-allocated id (index), tagged by kind so the two never collide. Ordered: Static before Attached, then fields.
enum class LightKind : uint8_t { Static = 0, Attached = 1 };
struct LightId {
    LightKind kind = LightKind::Static;
    int32_t cx = 0, cy = 0;
    int16_t level = 0;
    uint32_t index = 0;
    auto operator<=>(const LightId&) const = default;
};

// A source resolved to a world tile for one step (what composition reads). `preset` is a presentation key only (§10):
// sim logic never reads it.
struct LightSource {
    LightId id;
    Vec2i tile;                // world tile
    uint16_t bright_ft = 0, dim_ft = 0;
    uint8_t flags = 0;         // light_src::kMagical | kSunlight
    uint8_t spell_level = 0;
    bool lit = true;
    Carrier carrier = Carrier::Tile;
    uint32_t carrier_id = 0;
    std::string preset;
};

// Anchor + (n - 1) / 2: the footprint centre tile a carried light sits on (§3.4).
constexpr Vec2i carrier_centre(Vec2i anchor, int footprint) noexcept {
    return {anchor.x + (footprint - 1) / 2, anchor.y + (footprint - 1) / 2};
}
LightSource static_source(world::ChunkCoord chunk, uint32_t index, const StaticLight& l, std::string preset = {});
// `carrier_tile`: the carrier's centre tile (ignored for Carrier::Tile, which uses l.tile).
LightSource attached_source(const AttachedLight& l, Vec2i carrier_tile, std::string preset = {});

// An R-G11 zone as light sees it (§4 step 2): rules5e kZone* flags, the spell level (for magical darkness), and the
// world tiles it covers (any order; normalised by LightWorld::normalise).
struct LightZone {
    uint32_t zone_id = 0;
    uint8_t flags = 0;         // rules5e::kZoneLightObscured | kZoneHeavyObscured | kZoneMagicalDarkness | kZoneOpaque
    uint8_t spell_level = 0;
    std::vector<Vec2i> tiles;
};

// §6 the inputs for one step. `grid` may be null (nothing blocks).
struct LightWorld {
    const Grid* grid = nullptr;
    Ambient ambient;
    std::vector<LightSource> sources;
    std::vector<LightZone> zones;
    // Sort sources by id, zones by zone id and each zone's tiles; light_at requires a normalised world.
    void normalise();
};

// §5 does light from source tile `s` reach `t`? True when s == t, else when at least one of the 16 corner-to-corner
// segments between the two 1 x 1 tiles is clear of walls (blocks_sight or an opaque zone, excluding s and t): exactly
// "not Cover::Total from s to t, ignoring creatures", via the shared segment_blocked (sim/trace.hpp).
bool light_reaches(const LightWorld& w, Vec2i s, Vec2i t);

// §4 the level of one source at `t` before occlusion and zones: Bright within bright_ft, Dim within bright + dim, else
// Dark. Distance = Chebyshev tiles * 5 ft. A 0 ft radius lights nothing (the hooded lantern's 0 / 5 is Dim on its own tile).
Light source_level(const LightSource& s, Vec2i t) noexcept;

// §4 composition, pure: (1) max over lit sources reaching t, (2) zone overrides (magical darkness: a source counts only
// if magical with spell_level above the darkness's level; obscurement flags), (3) the ambient floor (not in magical
// darkness).
LightSample light_at(const LightWorld& w, Vec2i t);

// §6 per-step cache: frozen on the world given at begin_step (a copy), memoised, never hashed. at(t) == light_at(t).
class LightCache {
public:
    LightCache();
    explicit LightCache(LightWorld w);
    ~LightCache();
    LightCache(LightCache&&) noexcept;
    LightCache& operator=(LightCache&&) noexcept;
    // Freeze `w` for the step and drop every memoised tile.
    void begin_step(LightWorld w);
    // Freeze `w`, keeping memoised tiles outside the dirty rects (incremental invalidation; the caller names every
    // rect whose light may have changed: a source's old and new halo, a zone's bounds + halo, a changed chunk + halo).
    void begin_step(LightWorld w, std::span<const RectI> dirty);
    // Incremental step (LightDriver; LIGHTING.md §7 invalidation, §12 fallback): freeze `w` and keep every memoised tile
    // whose sample cannot have changed since the previous begin_step_incremental over the same `chunks` (the tiles that
    // will be read; memo outside their bounding box is dropped). The cache diffs the inputs itself: a source added,
    // removed or changed dirties its old and new halo rects; a changed wall tile (blocks_sight or opaque zone) dirties
    // the halo of every source whose halo holds it; a changed zone dirties the bounding boxes of its old and new tiles.
    // A different chunk set, ambient, or reach bound (or no previous incremental step) is a full rebuild. Every at(t)
    // for t in `chunks` then equals what begin_step(w) would give, bit for bit (tested by randomised mutation).
    struct StepStats {
        bool full = false;              // memo dropped entirely
        std::size_t dirty_rects = 0;    // rects invalidated (incremental only)
        std::size_t wall_changes = 0;   // wall tiles that differ from the previous step
    };
    StepStats begin_step_incremental(LightWorld w, std::span<const world::ChunkCoord> chunks);
    void invalidate_all() { memo_.clear(); inc_.valid = false; }
    void invalidate_rect(RectI r);
    LightSample at(Vec2i t);
    const LightWorld& world() const noexcept { return world_; }
    std::size_t memoised() const noexcept { return memo_.size(); }

private:
    struct Field;   // per-source wall bitmap + prefix sums + memoised reach (light.cpp)
    void reset_fields();
    void build_field(std::size_t si);
    bool field_reaches(std::size_t si, Vec2i t);
    struct IncState {   // what the last begin_step_incremental saw (raw walls over `box`, chunk set, reach bound)
        bool valid = false;
        std::vector<world::ChunkCoord> chunks;
        RectI box;
        int32_t reach = 0;
        std::vector<uint8_t> walls;
    };
    LightWorld world_;
    IncState inc_;
    std::map<Vec2i, LightSample> memo_;
    std::vector<Field> fields_;
};

// §10 fog of war (phase 2): one bit per tile of a chunk. Layout (pinned by a test; render reads the words directly):
// bit = y * 32 + x (chunk-local, row-major like ChunkLightMap::samples), word = bit / 64, LSB-first within the word
// (mask = uint64_t(1) << (bit & 63)). So words[0] holds rows 0-1, words[15] rows 30-31.
struct TileMask {
    std::array<uint64_t, 16> words{};
    static constexpr int bit_of(int lx, int ly) noexcept { return ly * world::kChunkSize + lx; }
    bool test(int lx, int ly) const noexcept {
        const int b = bit_of(lx, ly);
        return ((words[std::size_t(b >> 6)] >> (b & 63)) & 1u) != 0;
    }
    void set(int lx, int ly) noexcept {
        const int b = bit_of(lx, ly);
        words[std::size_t(b >> 6)] |= uint64_t(1) << (b & 63);
    }
    bool any() const noexcept {
        for (uint64_t w : words)
            if (w) return true;
        return false;
    }
    TileMask& operator|=(const TileMask& o) noexcept {
        for (std::size_t i = 0; i < words.size(); ++i) words[i] |= o.words[i];
        return *this;
    }
    bool operator==(const TileMask&) const = default;
};
// Team id = index into Combat::team_names() (rules5e::Combatant::team).
using TeamId = uint8_t;
struct ChunkVisibility {
    world::ChunkCoord coord;
    TileMask seen_now;   // perceived at the start of this step by some viewer of the team
    TileMask explored;   // perceived at any step so far (always a superset of seen_now)
    bool operator==(const ChunkVisibility&) const = default;
};
struct TeamVisibility {
    TeamId team = 0;
    std::vector<ChunkVisibility> chunks;   // ascending coord; frame chunks with explored.any() only (missing = all unseen)
    bool operator==(const TeamVisibility&) const = default;
};
// §2 a changed region of one chunk: chunk-local, inclusive bounds (0..31). One rect per chunk: the bounding box of the
// samples that differ from the previous frame (the whole chunk when it has no samples there).
struct DirtyRect {
    world::ChunkCoord coord;
    uint8_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool operator==(const DirtyRect&) const = default;
};

// §10 the immutable per-step frame render reads (never the cache).
struct ChunkLightMap {
    world::ChunkCoord coord;
    std::array<LightSample, world::kChunkSize * world::kChunkSize> samples{};   // row-major, index = y * 32 + x
};
struct LightFrame {
    uint64_t step = 0;
    std::vector<ChunkLightMap> chunks;          // ascending coord
    std::vector<LightSource> sources;           // ascending id, lit and unlit
    std::vector<world::ChunkCoord> dirty_chunks;   // chunks whose samples differ from the previous frame (or are new)
    std::vector<LightId> dirty_sources;         // sources added, removed or changed since the previous frame
    // Phase 2 (all ascending; derived, never hashed). "Tracked" = chunks + blocked_chunks.
    std::vector<DirtyRect> dirty_rects;         // one per dirty_chunks entry, same order
    std::vector<world::ChunkCoord> added_chunks;     // tracked now, not tracked in the previous frame
    std::vector<world::ChunkCoord> removed_chunks;   // tracked in the previous frame, not now (render frees them)
    std::vector<world::ChunkCoord> blocked_chunks;   // wanted, but the 3x3 halo is Pending or Failed: no samples here,
                                                     // render keeps the last light it had
    std::vector<TeamVisibility> visibility;          // ascending team (empty without a VisibilityState)
    std::vector<world::ChunkCoord> dirty_vis_chunks; // frame chunks whose seen_now / explored changed for some team
};
// Evaluate `chunks` through the cache and publish a frame. `prev` (may be null) is the previous frame for the dirty set.
std::shared_ptr<const LightFrame> publish_light_frame(LightCache& cache, std::span<const world::ChunkCoord> chunks,
                                                      uint64_t step, const LightFrame* prev);

class VisibilityState;   // sim/visibility.hpp
struct LightFrameInputs {
    std::span<const world::ChunkCoord> chunks;    // halo Complete: evaluated
    std::span<const world::ChunkCoord> blocked;   // halo Pending / Failed: listed only (split_by_halo)
    const VisibilityState* visibility = nullptr;  // its current masks are published (after VisibilityState::begin_step)
};
// Phase 2 form. The 4-argument form above is this one with no blocked chunks and no visibility.
std::shared_ptr<const LightFrame> publish_light_frame(LightCache& cache, const LightFrameInputs& in, uint64_t step,
                                                      const LightFrame* prev);

} // namespace eng::sim
