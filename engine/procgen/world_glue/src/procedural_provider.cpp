#include "procgen_world/procedural_provider.hpp"

#include <string>

#include "world/chunk_codec.hpp"

namespace eng::world {

ChunkResult to_chunk(const procgen::GenChunk& g) {
    Chunk c{};
    c.format_version = kChunkFormatVersion;
    c.coord = ChunkCoord{g.x, g.y, g.level};
    c.palette = g.palette;
    for (std::size_t l = 0; l < c.layers.size(); ++l)
        for (std::size_t i = 0; i < c.layers[l].size(); ++i) {
            const auto& s = g.layers[l][i];
            c.layers[l][i] = TileCell{s.tile, s.elevation, s.flags};
        }
    for (std::size_t i = 0; i < g.spawns.size(); ++i) {
        const auto& s = g.spawns[i];
        c.spawns.push_back(EntitySpawn{s.archetype, Vec2i{s.x, s.y}, spawn_stable_id(c.coord, uint32_t(i)),
                                       s.overrides_json});
    }
    c.triggers = g.triggers;
    c.origin = Chunk::Origin::Procedural;
    c.content_hash = compute_content_hash(c);
    if (c.content_hash != procgen::content_hash(g))
        return ChunkResult{ChunkStatus::Error, std::nullopt,
                           "procgen: content_hash mismatch between procgen and world (layout drift)"};
    return ChunkResult{ChunkStatus::Ready, std::move(c), {}};
}

ChunkResult ProceduralProvider::load(const ChunkRequest& req) {
    if (!covers(req.coord)) return ChunkResult{ChunkStatus::NotFound, std::nullopt, {}};
    auto g = procgen::generate(procgen::WorldSeed{req.gen_version, req.world_seed}, req.coord.x, req.coord.y,
                               req.coord.level);
    if (!g)
        return ChunkResult{ChunkStatus::Error, std::nullopt,
                           "procgen: unknown gen_version " + std::to_string(req.gen_version)};
    return to_chunk(*g);
}

} // namespace eng::world
