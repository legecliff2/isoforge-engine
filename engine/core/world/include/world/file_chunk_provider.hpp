#pragma once
// FileChunkProvider (formerly FileChunkProvider): serves chunk@N JSON files, one file per
// coord, whatever their origin (authored "handcrafted", or baked "procedural"/"streamed"
// chunks such as procgen_dump output). The file's origin is preserved in the loaded Chunk.
// (The doc's final form slices map@N files into chunks; that arrives with map@1/tools/packer.)
#include <filesystem>
#include <map>
#include <string_view>

#include "assets/schema_registry.hpp"
#include "world/chunk.hpp"

namespace eng::world {

class FileChunkProvider final : public IChunkProvider {
public:
    // Files are fixed at construction; load() only reads immutable state, so it is
    // thread-safe as IChunkProvider requires.
    explicit FileChunkProvider(std::map<ChunkCoord, std::filesystem::path> files);

    std::string_view name() const override { return "file"; }
    bool covers(ChunkCoord c) const override { return files_.count(c) != 0; }
    // NotFound if not covered; Error on I/O/schema/decode failure or if the file's coord
    // disagrees with the request. Any origin is accepted and kept. world_seed and
    // gen_version are ignored.
    ChunkResult load(const ChunkRequest& req) override;

private:
    std::map<ChunkCoord, std::filesystem::path> files_;
    assets::SchemaRegistry schemas_;
};

} // namespace eng::world
