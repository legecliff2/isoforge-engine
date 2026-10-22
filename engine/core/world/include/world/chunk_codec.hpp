#pragma once
// chunk@1 encoding decisions (coordinated with schemas/chunk/v1, PR #2).
//
//  stable_id   Computed by the loader, never stored in files:
//              h = StableHash(i32 coord.x, i32 coord.y, i16 coord.level, u32 spawn_index)
//              (little-endian canonical bytes, XXH3-64 seed 0), stable_id = u32(h) ^ u32(h >> 32).
//              Chunk::Origin is deliberately NOT an input so the id is "the same in every
//              origin" (§4.5, §5.2); the doc's "origin" term is read as the chunk's coordinate
//              origin. spawn_index = position in the file's "spawns" array.
//  content_hash  StableHash over the canonical binary serialization of the in-memory Chunk
//              (everything except content_hash and origin), §4.5. Files may carry it as a
//              16-digit lowercase hex string; if present the loader checks it matches.
//  flags       bit0 blocks_move, bit1 blocks_sight, bit2 difficult, bit3 water, bit4 climbable, bit5 open_air; bits 6-7
//              reserved (must be 0 in chunk@1).
//  origin      "handcrafted" | "streamed" | "procedural".
//  palette     slot 0 reserved for the empty tile and must be "".
//  layers      4 objects (ground, detail, walls, roof), each with optional row-major arrays
//              "tiles" (u16), "elevation" (u8), "flags" (u8) of exactly 1024 entries
//              (index = y*32 + x). An omitted array means all zeros.
//  spawns      {"archetype", "tile": {x,y}, "overrides": {...} optional}. overrides is a JSON
//              object in the file; EntitySpawn::overrides_json holds its canonical compact
//              dump (sorted keys), "" when absent/empty.
//  triggers    JSON objects with required string "id" and "kind" (other fields free-form);
//              Chunk::triggers holds each one's canonical compact dump.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "assets/schema_registry.hpp"
#include "world/chunk.hpp"

namespace eng::world {

namespace tile_flags {
inline constexpr uint8_t kBlocksMove  = 1u << 0;
inline constexpr uint8_t kBlocksSight = 1u << 1;
inline constexpr uint8_t kDifficult   = 1u << 2;
inline constexpr uint8_t kWater       = 1u << 3;
inline constexpr uint8_t kClimbable   = 1u << 4;   // feat/move-modes: a surface to climb (walls, cliffs, trees); enterable by climbing even when blocks_move
inline constexpr uint8_t kOpenAir     = 1u << 5;   // feat/move-modes: open air / chasm: no footing; only flying creatures enter
inline constexpr uint8_t kKnownMask   = 0x3F;
} // namespace tile_flags

inline constexpr int cell_index(int x, int y) noexcept { return y * kChunkSize + x; }

uint32_t spawn_stable_id(ChunkCoord coord, uint32_t spawn_index);
// chunk@2 static light id: StableHash(i32 x, i32 y, i16 level, u32 index, u8 'L'); low ^ high 32 bits.
uint32_t chunk_light_stable_id(ChunkCoord coord, uint32_t light_index);
// format_version is hashed as 1 when the chunk has no lights and no ambient, so every chunk@1 hash is unchanged.
uint64_t compute_content_hash(const Chunk& c);
std::string_view to_string(Chunk::Origin o);
std::optional<Chunk::Origin> parse_origin(std::string_view s);
std::string hash_to_hex(uint64_t h);   // 16 lowercase hex digits

// Registers "chunk" at kChunkFormatVersion with the identity migration chunk@1 -> chunk@2 on reg.
void register_chunk_schema(assets::SchemaRegistry& reg);

// Decodes an already schema-loaded (migrated to current) chunk document.
// Returns status Ready with the chunk, or Error with a message.
ChunkResult chunk_from_json(const assets::Json& doc);

} // namespace eng::world
