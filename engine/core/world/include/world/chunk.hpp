#pragma once
// Verbatim from ARCHITECTURE.md §5.2 (contract header), plus the chunk@2 light fields appended at the end of Chunk
// (R-G13, docs/proposals/LIGHTING.md §7). Encoding decisions live in world/chunk_codec.hpp; ChunkStreamer lives in
// world/chunk_streamer.hpp.
#include <cstdint>
#include <array>
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include "base/math.hpp"

namespace eng::world {

inline constexpr int kChunkSize = 32;          // 32x32 tiles, 1 tile = 5 ft
inline constexpr int kChunkLayers = 4;         // ground, detail, walls, roof
inline constexpr uint16_t kChunkFormatVersion = 2;   // chunk@2: lights + ambient (R-G13)

struct ChunkCoord {
    int32_t x, y; int16_t level;               // level = floor / dungeon depth
    auto operator<=>(const ChunkCoord&) const = default;
};

using TileId = uint16_t;                        // index into chunk palette

struct TileCell {
    TileId   tile;                              // 0 = empty
    uint8_t  elevation;                         // height steps for iso stacking
    uint8_t  flags;                             // blocks_move | blocks_sight | difficult | water ...
};

struct EntitySpawn {
    std::string  archetype;                     // content id, e.g. "srd51.monster.goblin"
    Vec2i        tile;                          // local to chunk
    uint32_t     stable_id;                     // StableHash(coord, origin, index); same in every origin
    std::string  overrides_json;                // optional per-instance data (schema-validated)
};

struct Chunk {
    uint16_t   format_version = kChunkFormatVersion;
    ChunkCoord coord;
    uint64_t   content_hash;                    // for cache / save diffs
    std::vector<std::string> palette;           // TileId -> tile asset id ("tiles.dungeon.floor_01")
    std::array<std::array<TileCell, kChunkSize * kChunkSize>, kChunkLayers> layers;
    std::vector<EntitySpawn> spawns;
    std::vector<std::string> triggers;          // script hooks / regions (serialized)
    enum class Origin : uint8_t { Handcrafted, Streamed, Procedural } origin;
    // chunk@2 (R-G13 LIGHTING.md §3.3 / §7), appended. Presentation (colour, flicker, glow) is never stored here.
    struct Light {
        Vec2i       tile;                       // local to chunk
        uint16_t    bright_ft = 0, dim_ft = 0;  // multiples of 5; bright + dim <= kMaxLightFt
        uint8_t     flags = 0;                  // 1 magical, 2 sunlight, 4 starts unlit (sim::light_src values)
        std::string preset;                     // content key render maps to visuals ("" = none)
        uint32_t    stable_id = 0;              // computed by the loader (chunk_light_stable_id); never authored
    };
    struct Ambient {
        uint8_t kind = 0;                       // 0 none (underground), 1 sun, 2 moon (sim::AmbientKind)
        uint8_t level = 0;                      // 0 dark, 1 dim, 2 bright (sim::Light)
        auto operator<=>(const Ambient&) const = default;
    };
    std::vector<Light>     lights;
    std::optional<Ambient> ambient;
};
inline constexpr uint16_t kMaxLightFt = 120;    // 24 tiles < kChunkSize: a 1-chunk halo suffices (LIGHTING.md §7)

struct ChunkRequest {
    ChunkCoord coord;
    uint64_t   world_seed;                      // procedural providers MUST be pure in (seed, coord)
    uint16_t   gen_version;                     // generator algorithm set (§4.6)
};

enum class ChunkStatus : uint8_t { Ready, Empty, NotFound, Error };
struct ChunkResult { ChunkStatus status; std::optional<Chunk> chunk; std::string error; };

class IChunkProvider {
public:
    virtual ~IChunkProvider() = default;
    virtual std::string_view name() const = 0;
    virtual bool covers(ChunkCoord) const = 0;               // cheap bounds test
    virtual ChunkResult load(const ChunkRequest&) = 0;       // called on worker threads; must be thread-safe
};

} // namespace eng::world
