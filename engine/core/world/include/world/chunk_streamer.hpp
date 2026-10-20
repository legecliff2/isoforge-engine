#pragma once
// ChunkStreamer (ARCHITECTURE.md §5.2, §4 "Streaming"): keeps the chunks around a focus point
// resident, loading them through an IChunkProvider on worker threads.
//
// Determinism contract: a chunk's contents are a pure function of (provider, coord, seed,
// gen_version). Loads finish in any order on any thread, but they are only *integrated* on the
// caller's thread in update(), in ChunkCoord order, and the resident set is exactly the chunks
// within the focus radius. So after wait_idle() the resident set (and digest()) depends only on
// the focus, never on thread count, load timing, request order or the walk that led there.
// Evicted chunks go to a bounded LRU cache (a pure optimisation: reloading yields the same bytes).
// Sim never sees a partially loaded chunk: get()/resident() only return integrated chunks.
//
// Not yet: ChunkDelta overlays (save state), async texture upload (render side).
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "base/math.hpp"
#include "world/chunk.hpp"

namespace eng::world {

struct StreamerConfig {
    uint64_t world_seed = 0;
    uint16_t gen_version = 1;
    uint32_t cache_chunks = 64;      // LRU cache of evicted chunks (0 = none)
    // Test hook: shuffle the order in which wanted chunks are queued (0 = ring order around the
    // focus, nearest first). Must not change any result.
    uint64_t request_shuffle_seed = 0;
};

// Change of the resident set over one update() (or one wait_idle()): coords that became
// resident / stopped being resident, each in ChunkCoord order. Disjoint; a coord appears at
// most once. Applying successive deltas to an empty set reproduces resident(). (Not to be
// confused with ChunkDelta, the save-state overlay of ARCHITECTURE.md.)
struct StreamDelta {
    std::vector<ChunkCoord> added, removed;
    bool empty() const noexcept { return added.empty() && removed.empty(); }
};

// One interest point for set_foci: covered chunks within Chebyshev chunk distance radius_chunks
// of chunk_of_tile(world_tile, level) are wanted.
struct Focus {
    Vec2i   world_tile{};
    int16_t level = 0;
    int     radius_chunks = 0;
};

// Halo of a chunk = the chunk and its 8 neighbours on the same level (LIGHTING.md §7, P2).
// Complete: each of the 9 is resident or known-absent (not covered by the provider, or the provider
// answered Empty). Failed: at least one load failed (error state, never treated as empty).
// Pending: otherwise (some still loading or not wanted). Failed wins over Pending.
enum class HaloState : uint8_t { Complete, Pending, Failed };

// Chunk coord containing a world tile (floor division by kChunkSize).
ChunkCoord chunk_of_tile(Vec2i world_tile, int16_t level) noexcept;

class ChunkStreamer {
public:
    // worker_threads == 0: loads run synchronously inside update().
    explicit ChunkStreamer(IChunkProvider& root, uint32_t worker_threads = 2, StreamerConfig cfg = {});
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;

    // Wanted set: chunks (cx, cy, level) with Chebyshev chunk distance <= radius_chunks from the
    // focus chunk and covered by the provider. Out-of-set chunks are evicted at the next update().
    // One-element wrapper over set_foci.
    void set_focus(Vec2i world_tile, int16_t level, int radius_chunks);
    // Wanted set = union over foci (plus pinned halos). Pure function of the foci *set*: order and
    // duplicates don't change it (nor the settled resident set / digest()).
    void set_foci(std::span<const Focus> foci);
    // Combat pinning: `owner`'s chunks plus their 3x3 halo (covered chunks) are always wanted, so
    // they are never evicted whatever the foci. Replaces owner's previous pins; empty = unpin.
    void pin(uint32_t owner, std::span<const ChunkCoord> chunks);
    void unpin(uint32_t owner) { pin(owner, {}); }
    std::vector<ChunkCoord> pinned() const;   // pinned chunks (not halos), coord order, deduped
    // Halo gate (see HaloState). Reflects integrated state only (call after update/wait_idle).
    HaloState halo_state(ChunkCoord c) const;
    HaloState halo_state_of_tile(Vec2i world_tile, int16_t level) const {
        return halo_state(chunk_of_tile(world_tile, level));
    }
    // Failed loads stay failed (no silent auto-retry, which would make the resident set depend on
    // timing) until retried explicitly or the chunk stops being wanted. retry() clears the failure so
    // the next update() reloads it (halo: Failed -> Pending -> Complete on success). This is the way
    // out for pinned chunks, which are always wanted. Both are no-ops for coords that didn't fail.
    void retry(ChunkCoord c);
    void retry_failed();
    // Known-absent: not covered by the provider, or covered but the provider returned Empty.
    bool known_absent(ChunkCoord c) const;
    // Caller's thread: integrate finished loads (coord order), evict, queue missing chunks.
    // Returns the resident-set change it made (also readable via last_delta()).
    StreamDelta update();
    // update() until every wanted chunk is resident or failed. Returns the net change over
    // all those updates (last_delta() is set to it as well).
    StreamDelta wait_idle();
    const StreamDelta& last_delta() const noexcept { return last_delta_; }

    const Chunk* get(ChunkCoord) const;                       // nullptr if not resident
    std::vector<const Chunk*> resident(int16_t level) const;  // coord order
    std::vector<ChunkCoord> wanted() const;                   // coord order
    // Errors of wanted chunks whose load failed ("x,y,level: message"), coord order.
    std::vector<std::string> errors() const;
    bool idle() const;
    // Canonical hash of the resident set: per chunk (coord order) coord, origin, content hash
    // (recomputed), spawn count. Golden material for streaming determinism.
    uint64_t digest() const;
    uint64_t loads_started() const noexcept { return loads_started_; }

private:
    struct Done { ChunkCoord coord; ChunkResult result; };
    void worker();
    void enqueue_missing();
    void rebuild_wanted();
    void integrate(ChunkCoord c, ChunkResult r);
    std::set<ChunkCoord> resident_keys() const;
    StreamDelta diff_from(const std::set<ChunkCoord>& before) const;

    IChunkProvider& root_;
    StreamerConfig cfg_;
    std::vector<Focus> foci_;
    std::map<uint32_t, std::vector<ChunkCoord>> pins_;
    std::set<ChunkCoord> wanted_;
    std::vector<ChunkCoord> wanted_order_;   // queue order for wanted_ (ring or shuffled)
    std::map<ChunkCoord, Chunk> resident_;
    std::map<ChunkCoord, std::string> failed_;   // error message (never empty)
    std::set<ChunkCoord> empty_;                 // provider answered Empty (pure: kept when unwanted)
    std::set<ChunkCoord> in_flight_;
    std::map<ChunkCoord, Chunk> cache_;
    std::list<ChunkCoord> cache_lru_;        // front = most recent
    uint64_t loads_started_ = 0;
    StreamDelta last_delta_;

    // Shared with workers.
    mutable std::mutex mu_;
    std::condition_variable cv_work_, cv_done_;
    std::deque<ChunkCoord> queue_;
    std::vector<Done> done_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

} // namespace eng::world
