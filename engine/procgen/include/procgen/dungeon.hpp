#pragma once
// Room/corridor dungeon generator v0 (gen_version 1). Emits chunk@1 data exactly as
// engine/core/world/chunk_codec decodes it, without depending on world (procgen may only use
// base + detmath/rng, §2.1). GenChunk mirrors eng::world::Chunk field for field; see
// docs/PROCGEN.md for the ProceduralProvider adapter that converts it.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "procgen/seed.hpp"

namespace procgen {

inline constexpr int kChunkSize = 32;     // == eng::world::kChunkSize
inline constexpr int kChunkLayers = 4;    // ground, detail, walls, roof
inline constexpr int kCells = kChunkSize * kChunkSize;
inline constexpr uint16_t kChunkFormatVersion = 1;

namespace flags {   // == eng::world::tile_flags
inline constexpr uint8_t kBlocksMove = 1u << 0, kBlocksSight = 1u << 1, kDifficult = 1u << 2, kWater = 1u << 3;
}
namespace tiles {   // palette indices used by gen_version 1
inline constexpr uint16_t kEmpty = 0, kFloor = 1, kWall = 2, kRubble = 3, kWater = 4;
}
enum Layer : int { kGround = 0, kDetail = 1, kWalls = 2, kRoof = 3 };

struct GenCell { uint16_t tile = 0; uint8_t elevation = 0; uint8_t flags = 0; };

struct GenSpawn {
    std::string archetype;
    int32_t x = 0, y = 0;
    std::string overrides_json;   // canonical compact JSON or "" (always "" in gen 1)
};

struct GenChunk {
    int32_t x = 0, y = 0; int16_t level = 0;
    std::vector<std::string> palette;               // [0] == ""
    std::array<std::array<GenCell, kCells>, kChunkLayers> layers{};
    std::vector<GenSpawn> spawns;                   // stable_id is derived, never stored
    std::vector<std::string> triggers;              // canonical compact JSON (none in gen 1)
    // origin is always Procedural
};

// Generator registry (D-007): append-only, keyed by gen_version, contiguous from 1. Every
// shipped version stays callable forever; each has never-changing goldens in procgen_tests.
using GeneratorFn = std::optional<GenChunk> (*)(WorldSeed, int32_t, int32_t, int16_t);
struct GeneratorVersion { uint16_t gen_version; GeneratorFn fn; const char* summary; };
std::span<const GeneratorVersion> generator_versions() noexcept;
uint16_t latest_gen_version() noexcept;

// Generates the chunk at (x, y, level) for world seed w. nullopt if w.gen_version is unknown.
// Pure: same inputs -> identical chunk on every platform/thread/order.
std::optional<GenChunk> generate(WorldSeed w, int32_t x, int32_t y, int16_t level);

// eng::world::spawn_stable_id, reimplemented (xxHash3-64 of i32 x, i32 y, i16 level,
// u32 index; low32 ^ high32). Cross-checked against world in procgen_world_tests.
uint32_t spawn_stable_id(int32_t x, int32_t y, int16_t level, uint32_t index);

// eng::world::compute_content_hash over the same canonical bytes (origin excluded).
uint64_t content_hash(const GenChunk& c);
std::string hash_hex(uint64_t h);

// chunk@1 JSON (compact layers, all-zero arrays omitted, origin "procedural", content_hash).
std::string to_json(const GenChunk& c);

// Every cell with ground != empty and no blocks_move in any layer, 4-connected?
bool walkable_connected(const GenChunk& c);

} // namespace procgen
