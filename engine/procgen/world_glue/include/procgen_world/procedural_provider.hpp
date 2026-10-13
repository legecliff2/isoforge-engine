#pragma once
// ProceduralProvider: IChunkProvider over procgen::generate (ARCHITECTURE.md §5.2, §4).
// Pure in (world_seed, gen_version, coord): load() reads no mutable state, so it is thread-safe.
#include <optional>
#include <string_view>

#include "procgen/dungeon.hpp"
#include "world/chunk.hpp"

namespace eng::world {

// Converts a GenChunk to a Chunk: stable_ids via spawn_stable_id, content_hash via
// compute_content_hash, origin Procedural. Error if procgen's hash disagrees with world's.
ChunkResult to_chunk(const procgen::GenChunk& g);

class ProceduralProvider final : public IChunkProvider {
public:
    // Covers every coord whose level is in [min_level, max_level].
    explicit ProceduralProvider(int16_t min_level = -32768, int16_t max_level = 32767)
        : min_level_(min_level), max_level_(max_level) {}
    std::string_view name() const override { return "procedural"; }
    bool covers(ChunkCoord c) const override { return c.level >= min_level_ && c.level <= max_level_; }
    // Uses req.world_seed and req.gen_version; Error for an unknown gen_version.
    ChunkResult load(const ChunkRequest& req) override;

private:
    int16_t min_level_, max_level_;
};

} // namespace eng::world
