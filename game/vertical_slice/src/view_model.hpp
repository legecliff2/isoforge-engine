#pragma once
// Plain-data snapshot of what the vertical slice shows. The renderer (SliceView) only ever
// sees this: no sim/world/rules types, no game state lives on the render side.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "base/math.hpp"

namespace slice {

enum class TileKind : uint8_t { Floor, Wall, Rubble, Raised, Door, Water, Unknown };

struct TileView {
    eng::Vec2i tile;
    uint8_t    layer = 0;      // chunk layer: 0 ground, 1 detail, 2 walls, 3 roof
    uint8_t    elevation = 0;
    TileKind   kind = TileKind::Unknown;
};

struct ActorView {
    uint32_t    id = 0;        // stable id (sort key tie-break)
    std::string name;          // "FIGHTER", "GOBLIN"
    eng::Vec2i  tile;
    int         hp = 0, hp_max = 0;
    uint8_t     team = 0;
    bool        is_pc = false;
    bool        current = false;
    bool        down = false;  // 0 HP / unconscious
    bool        dead = false;
};

// ---- Light (LIGHTING.md §10): a plain-data copy of the sim's published LightFrame. ----
// Samples are packed bytes (iso2d::pack_light layout: bits 0-1 level Dark|Dim|Bright,
// bits 2-5 sim LightFlags). Render never computes gameplay light; it only shades these.
struct LightChunkView {
    int32_t cx = 0, cy = 0;
    int16_t level = 0;
    std::array<uint8_t, 32 * 32>  samples{};
    // Fog for the shown team: 1024-bit masks, bit i = tile (i % 32, i / 32). has_fog=false -> all visible.
    bool has_fog = false;
    std::array<uint64_t, 16> seen_now{}, explored{};
    bool seen(int lx, int ly) const { return !has_fog || (seen_now[size_t((ly * 32 + lx) / 64)] >> ((ly * 32 + lx) % 64)) & 1u; }
    bool known(int lx, int ly) const { return !has_fog || (explored[size_t((ly * 32 + lx) / 64)] >> ((ly * 32 + lx) % 64)) & 1u; }
};
struct LightChunkRef { int32_t cx = 0, cy = 0; int16_t level = 0; };
struct LightDirtyView { LightChunkRef chunk; eng::RectI rect; };   // chunk-local tiles
struct LightSourceView {
    uint64_t    id = 0;            // LightId: stable, unique across static / attached
    bool        attached = false;  // false = static (chunk) source
    eng::Vec2i  tile;              // world tile
    uint8_t     carrier = 0;       // 0 none/tile, 1 creature, 2 item
    uint32_t    carrier_id = 0;    // so the glow can follow the token's drawn position
    uint16_t    bright_ft = 0, dim_ft = 0;
    bool        lit = true, magical = false, sunlight = false;
    std::string preset;            // content key -> render-side visual (colour, flicker)
};
struct LightView {
    bool present = false;          // false: no LightFrame (old content / sim without light): unlit view
    uint64_t frame = 0;            // sim step the frame belongs to
    std::vector<LightChunkView>  chunks;    // resident visible chunks
    std::vector<LightChunkRef>   added, removed;   // StreamDelta since the previous frame
    std::vector<LightDirtyView>  dirty;     // rects whose samples changed this frame
    std::vector<LightSourceView> sources;
    bool        fog = false;       // any chunk carries fog masks
    const LightChunkView* chunk_of_tile(eng::Vec2i t) const {
        const int32_t cx = t.x >= 0 ? t.x / 32 : -((-t.x + 31) / 32), cy = t.y >= 0 ? t.y / 32 : -((-t.y + 31) / 32);
        for (const auto& c : chunks) if (c.cx == cx && c.cy == cy) return &c;
        return nullptr;
    }
};

struct ViewModel {
    std::vector<TileView>    tiles;   // static: from the chunk
    std::vector<ActorView>   actors;
    bool        sim_live = false;     // false = static chunk view (no encounter)
    int         round = 0;
    std::string turn;                 // current actor name
    bool        ended = false;
    std::string pending;              // reaction prompt while a request is paused (not a fact)
    std::string status;               // e.g. "SPACE: STEP  A: AUTO"
    std::vector<std::string> log;     // newest last
    LightView   light;
};

} // namespace slice
