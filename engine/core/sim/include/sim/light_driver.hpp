#pragma once
// R-G13 phase 2 (docs/proposals/LIGHTING.md §7, §10; lighting-p2 API §4): the sim publishes LightFrames on its own. One
// LightDriver per session: Combat calls step() once when attached (create time) and after every committed step. It
// builds the LightWorld (grid, ambient, static chunk@2 lights of resident chunks, game-supplied attached sources,
// R-G11 ActiveZones as LightZones via zone_covers),
// gates chunks on their 3x3 halo, computes the per-team visibility masks and publishes. Nothing here is hashed.
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "base/math.hpp"
#include "sim/light.hpp"
#include "sim/visibility.hpp"
#include "world/chunk.hpp"

namespace eng::world { class ChunkStreamer; }

namespace eng::sim {

class Combat;

struct LightProviderInputs {
    // Streamed worlds: halo gate (halo_state), static lights (get), foci (set_foci) and pins. Null: every frame chunk is
    // treated as Complete and static lights come from `chunks`.
    world::ChunkStreamer* streamer = nullptr;
    std::vector<const world::Chunk*> chunks;   // no streamer: the loaded chunks, in WORLD coords (any order)
    Ambient ambient;                           // default ambient; a chunk's authored chunk@2 ambient wins (see step)
    std::optional<Vec2i> camera_tile;          // world tile; adds Focus{camera, level, 1} after the creatures
    uint32_t pin_owner = 0x4C494748u;          // ChunkStreamer::pin owner id for the combat's chunks ("LIGH")
    // PC torches and other attached sources (phase 3 commands come later): appended to the world each step. Tiles are in
    // the combat's frame. Must be deterministic in the combat state.
    std::function<void(const Combat&, std::vector<LightSource>&)> attached_sources;
    // Rebuild only the tiles a changed source, wall or zone can touch (LightCache::begin_step_incremental). Frames are
    // bit-identical either way; false forces a full rebuild every step (the reference the tests compare against).
    bool incremental = true;
};

// altitude PR5 (docs/SRD_COMBAT_COVERAGE.md §11.8): lighting stays per-tile 2D. Walls block light at any height and
// light is not attenuated by height; an airborne creature is lit by the tile under it (sight_facts_of samples pos()).
class LightDriver {
public:
    explicit LightDriver(LightProviderInputs in = {});
    ~LightDriver();   // unpins
    LightDriver(const LightDriver&) = delete;
    LightDriver& operator=(const LightDriver&) = delete;

    LightProviderInputs& inputs() noexcept { return in_; }
    void set_camera(std::optional<Vec2i> world_tile) { in_.camera_tile = world_tile; }

    // Build and publish the frame for `combat`'s committed state. Pins the combat's chunks while it runs and unpins once
    // it has ended. Returns the published frame.
    std::shared_ptr<const LightFrame> step(const Combat& combat);
    // The last published frame (thread-safe; the render thread reads only this). Null before the first step.
    std::shared_ptr<const LightFrame> latest() const;
    // True when the last step left a wanted chunk's halo Pending (the combat refuses to step).
    bool blocked() const noexcept { return pending_; }
    // Wanted chunks (world coords) whose halo Failed: an error state, never empty light. Call ChunkStreamer::retry.
    std::span<const world::ChunkCoord> failed() const noexcept { return failed_; }
    bool halted() const noexcept { return pending_ || !failed_.empty(); }
    const VisibilityState& visibility() const noexcept { return vis_; }
    // What the last step's light rebuild did (full or the dirty rects it invalidated).
    const LightCache::StepStats& last_stats() const noexcept { return stats_; }

private:
    LightProviderInputs in_;
    LightCache cache_;
    LightCache::StepStats stats_;
    VisibilityState vis_;
    uint64_t step_ = 0;
    bool pending_ = false;
    bool pinned_ = false;
    std::vector<world::ChunkCoord> failed_;
    mutable std::mutex mu_;
    std::shared_ptr<const LightFrame> latest_;
};

// Viewers for the combat's committed state: every combatant that is not dead, eye = carrier_centre(pos, footprint),
// senses from the combatant, blinded = has(Blinded). Ascending id.
std::vector<Viewer> combat_viewers(const Combat& combat);

} // namespace eng::sim
