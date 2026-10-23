#pragma once
// R-G13 phase 2 fog of war (docs/proposals/LIGHTING.md §6, §10): per-team seen_now / explored bitmasks per chunk, computed
// at step start from the frozen LightCache and the viewers' senses, published with the LightFrame. Derived, never hashed,
// never logged; no rule reads them (a rule that does must claim a tag in SIM_EVENTS.md first). Integer math only.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "base/math.hpp"
#include "sim/light.hpp"
#include "world/chunk.hpp"

namespace eng::world { class ChunkStreamer; enum class HaloState : uint8_t; }
namespace rules5e { struct Combatant; }

namespace eng::sim {

// §6 senses in feet (0 = none). Sim's view of rules5e::Combatant::darkvision_ft ... truesight_ft; all zero = normal sight.
struct SenseRanges {
    uint16_t darkvision_ft = 0, blindsight_ft = 0, tremorsense_ft = 0, truesight_ft = 0;
    bool operator==(const SenseRanges&) const = default;
};
SenseRanges senses_of(const rules5e::Combatant& c) noexcept;

// §6 the light level a viewer perceives at a tile `dist_ft` away: darkvision within range raises Dim to Bright and Dark to
// Dim, except in magical darkness (as rules5e::derive_sight).
Light perceived_light(LightSample s, const SenseRanges& senses, int dist_ft) noexcept;

struct Viewer {
    uint32_t id = 0;        // CombatantId (ordering only)
    TeamId team = 0;
    Vec2i tile;             // the eye tile: carrier_centre(anchor, footprint)
    SenseRanges senses;
    bool blinded = false;   // Blinded: only blindsight / tremorsense remain
    // The viewer's own footprint (square, side `footprint`, min corner `anchor`) is always seen_now, even in darkness or
    // when blinded (R&P #238). footprint 0 = 1 x 1 on `tile`.
    Vec2i anchor;
    int footprint = 0;
    bool on_footprint(Vec2i t) const noexcept {
        if (footprint <= 0) return t == tile;
        return t.x >= anchor.x && t.y >= anchor.y && t.x < anchor.x + footprint && t.y < anchor.y + footprint;
    }
};

// Does `v` perceive tile `t`? Its own footprint always; otherwise the tile rule of rules5e::derive_sight with the subject a visible, grounded thing on `t` and no
// zone obscurement between: distance = Chebyshev tiles * 5 ft; sight needs not blinded, line of sight (light_reaches: the
// shared 16-segment trace, walls and opaque zones) and the perceived level not heavily obscured (Dark or a heavy zone);
// truesight in range sees heavy obscurement with line of sight; blindsight / tremorsense in range need neither.
// Pure reference (used by the tests); VisibilityState computes the same answer for whole chunks.
bool tile_visible(const LightWorld& w, LightCache& cache, const Viewer& v, Vec2i t);

class VisibilityState {
public:
    // Recompute seen_now for every team over `chunks` (deduped, sorted; tiles outside them are never marked) and fold it
    // into explored. Teams that have no viewer this step keep their explored masks and get an empty seen_now. The cache
    // must be frozen for this step (LightCache::begin_step). Viewers in any order.
    void begin_step(LightCache& cache, std::span<const Viewer> viewers, std::span<const world::ChunkCoord> chunks);
    // The published masks: every known team (ascending), each with the last begin_step's chunks whose explored is non-empty.
    const std::vector<TeamVisibility>& teams() const noexcept { return published_; }
    // explored of one chunk (all clear when unknown). Explored survives chunks leaving the frame (eviction).
    TileMask explored(TeamId team, world::ChunkCoord c) const;
    // Drop everything (a new world / session). explored is not saved (R&P default).
    void reset() { explored_.clear(); published_.clear(); los_.reset(); seen_.clear(); }

private:
    // Memoised line of sight (an optimisation only: answers equal light_reaches). 0 unknown, 1 clear, 2 blocked.
    struct LosMemo {
        Vec2i lo, hi;   // no default member initialisers: clang rejects optional<LosMemo>::emplace() in the enclosing class
        std::vector<uint8_t> wall;
        std::map<Vec2i, std::vector<uint8_t>> by_tile;
    };
    std::optional<LosMemo> los_;
    // Incremental seen_now (an optimisation only: equal to recomputing). A chunk's seen_now for a team depends only on
    // the team's viewers, the chunk's samples and the walls (line of sight), so it is reused while all three are equal.
    struct SeenMemo {
        std::vector<Viewer> viewers;   // the team's viewers, sorted by id
        std::array<LightSample, world::kChunkSize * world::kChunkSize> samples{};
        TileMask seen;
    };
    std::map<TeamId, std::map<world::ChunkCoord, SeenMemo>> seen_;
    std::map<TeamId, std::map<world::ChunkCoord, TileMask>> explored_;
    std::vector<TeamVisibility> published_;
};

// The §7 halo gate for a frame: `wanted` (any order, deduped here) split into chunks whose 3x3 halo is Complete (ready)
// and those Pending or Failed (blocked), both ascending.
void split_by_halo(std::span<const world::ChunkCoord> wanted, const std::function<world::HaloState(world::ChunkCoord)>& state,
                   std::vector<world::ChunkCoord>& ready, std::vector<world::ChunkCoord>& blocked);
void split_by_halo(const world::ChunkStreamer& streamer, std::span<const world::ChunkCoord> wanted,
                   std::vector<world::ChunkCoord>& ready, std::vector<world::ChunkCoord>& blocked);

} // namespace eng::sim
