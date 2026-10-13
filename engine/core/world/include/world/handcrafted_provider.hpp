#pragma once
// HandcraftedProvider v0: serves authored chunk@N JSON files, one file per coord.
// (The doc's final form slices map@N files into chunks; that arrives with map@1/tools/packer.)
#include <filesystem>
#include <map>
#include <string_view>

#include "assets/schema_registry.hpp"
#include "world/chunk.hpp"

namespace eng::world {

class HandcraftedProvider final : public IChunkProvider {
public:
    // Files are fixed at construction; load() only reads immutable state, so it is
    // thread-safe as IChunkProvider requires.
    explicit HandcraftedProvider(std::map<ChunkCoord, std::filesystem::path> files);

    std::string_view name() const override { return "handcrafted"; }
    bool covers(ChunkCoord c) const override { return files_.count(c) != 0; }
    // NotFound if not covered; Error on I/O/schema/decode failure or if the file's coord or
    // origin disagree with the request/provider. world_seed and gen_version are ignored.
    ChunkResult load(const ChunkRequest& req) override;

private:
    std::map<ChunkCoord, std::filesystem::path> files_;
    assets::SchemaRegistry schemas_;
};

} // namespace eng::world
