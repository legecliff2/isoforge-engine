#include "world/handcrafted_provider.hpp"

#include <string>

#include "world/chunk_codec.hpp"

namespace eng::world {

HandcraftedProvider::HandcraftedProvider(std::map<ChunkCoord, std::filesystem::path> files)
    : files_(std::move(files)) {
    register_chunk_schema(schemas_);
}

ChunkResult HandcraftedProvider::load(const ChunkRequest& req) {
    auto it = files_.find(req.coord);
    if (it == files_.end()) return ChunkResult{ChunkStatus::NotFound, std::nullopt, {}};
    assets::LoadResult lr = schemas_.load_file(it->second);
    if (!lr.ok) return ChunkResult{ChunkStatus::Error, std::nullopt, lr.error};
    ChunkResult r = chunk_from_json(lr.data);
    if (r.status != ChunkStatus::Ready) {
        r.error = it->second.generic_string() + ": " + r.error;
        return r;
    }
    if (r.chunk->coord != req.coord)
        return ChunkResult{ChunkStatus::Error, std::nullopt, it->second.generic_string() + ": coord does not match request"};
    if (r.chunk->origin != Chunk::Origin::Handcrafted)
        return ChunkResult{ChunkStatus::Error, std::nullopt, it->second.generic_string() + ": origin must be \"handcrafted\""};
    return r;
}

} // namespace eng::world
