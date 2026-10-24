#pragma once
#include <map>
#include "iso2d/light_glow.hpp"
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

// ---- Equipment visuals: a plain-data copy of what the sim's loadout shows on a creature (sim::Combat::loadout). ----
// Where an item is drawn on its holder's sprite. MainHand / OffHand: the item is held in that hand (a two-handed item is
// listed once, MainHand). Back: a stowed (carried) weapon or shield, slung on the back. Worn armor is not drawn.
enum class AttachSlot : uint8_t { Back, MainHand, OffHand };
enum class EquipKind : uint8_t { Weapon, Shield, Light, Other };
struct EquipVisual {
    uint32_t    item = 0;            // item instance id (stable; tie-break inside a slot)
    AttachSlot  slot = AttachSlot::MainHand;
    EquipKind   kind = EquipKind::Other;
    uint8_t     hands = 1;           // 2: two-handed (held in both hands, drawn at the main hand)
    bool        lit = false;         // kHeldLit (lights / a torch)
    uint8_t     light_state = 0;     // rules5e::light_state_index
    uint16_t    bright_ft = 0, dim_ft = 0;   // rules5e::light_radii now (0 / 0 while unlit): the same numbers the glow uses
    std::string content_id;          // "srd51.item.longsword": picks placeholder art (render side)
};
// Presentation facing of a sprite (iso screen directions). East facings hold the main hand on the camera side.
enum class Facing : uint8_t { SouthEast, SouthWest, NorthEast, NorthWest };

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
    bool        outlined = false;   // Faerie Fire / ModStat::Outlined (presentation ring)
    bool        invisible = false;  // Condition::Invisible (ghost when still drawn)
    Facing      facing = Facing::SouthEast;
    std::vector<EquipVisual> equipment;   // held + slung items, slot order then instance order (empty: nothing drawn)
    // Sight cues for the fog-team camera combatant (presentation only; never written back into sim).
    // Defaults match an empty EncounterHooks::sight_facts: everyone sees everyone, so no cue is drawn.
    // seen_by_viewer false = unseen target (attacking-visible: !can_see, or hidden from the viewer).
    // sees_viewer false = this actor cannot see the camera combatant.
    // unseen_attacker = a hostile conscious creature that perceives the viewer while the viewer cannot see them.
    bool seen_by_viewer = true;
    bool sees_viewer = true;
    bool unseen_attacker = false;
};

// Debug cover readout. Numeric values match rules5e::Cover so the session can copy
// Combat::cover_between without translating. SliceView never includes rules or sim.
enum class CoverLevel : uint8_t { None = 0, Half = 1, ThreeQuarters = 2, Total = 3 };

// One attacker→target probe (or an aim-tile→target probe). present=false: overlay off, nothing drawn.
// The line is screen-space, so it does not enter the iso depth sort.
struct CoverDebugView {
    bool present = false;
    bool aim = false;                  // origin is a 1-tile aim point, not the attacker's footprint
    uint32_t attacker = 0;
    uint32_t target = 0;
    eng::Vec2i origin{};               // footprint anchor, or the aim tile
    int origin_n = 1;                  // footprint side (1 = Medium). Aim uses 1.
    eng::Vec2i target_tile{};
    int target_n = 1;
    CoverLevel level = CoverLevel::None;
    std::string note;                  // "NO AIM" / "NO PAIR" when the overlay is up but the query is not
};

// ---- Light (LIGHTING.md §10): a plain-data copy of the sim's published sim::LightFrame. ----
// Samples are packed bytes (iso2d::pack_light layout: bits 0-1 level Dark|Dim|Bright,
// bits 2-5 sim LightFlags). Render never computes gameplay light; it only shades these.
struct LightChunkView {
    int32_t cx = 0, cy = 0;
    int16_t level = 0;
    std::array<uint8_t, 32 * 32>  samples{};
    // Fog for the shown team (sim::TileMask layout: bit = y * 32 + x, word = bit / 64, LSB-first).
    // has_fog=false -> all visible (fog off, or the frame has no visibility). With fog on, a chunk
    // the team has no entry for carries all-zero masks (entirely unseen).
    bool has_fog = false;
    std::array<uint64_t, 16> seen_now{}, explored{};
    bool seen(int lx, int ly) const { return !has_fog || (seen_now[size_t((ly * 32 + lx) / 64)] >> ((ly * 32 + lx) % 64)) & 1u; }
    bool known(int lx, int ly) const { return !has_fog || (explored[size_t((ly * 32 + lx) / 64)] >> ((ly * 32 + lx) % 64)) & 1u; }
};
struct LightChunkRef { int32_t cx = 0, cy = 0; int16_t level = 0; };
struct LightDirtyView { LightChunkRef chunk; eng::RectI rect; };   // chunk-local tiles
struct LightSourceView {
    // sim::LightId, flattened: stable and unique across kinds (static: chunk + index; attached: sim id).
    bool        attached = false;  // false = static (chunk) source
    int32_t     cx = 0, cy = 0;    // static only
    int16_t     level = 0;         // static only
    uint32_t    index = 0;
    uint32_t sort_key() const { return (attached ? 0x80000000u : 0u) ^ index ^ uint32_t(cx * 73856093) ^ uint32_t(cy * 19349663); }
    eng::Vec2i  tile;              // world tile
    uint8_t     carrier = 0;       // 0 tile, 1 creature, 2 item
    uint32_t    carrier_id = 0;    // so the glow can follow the token's drawn position
    uint16_t    bright_ft = 0, dim_ft = 0;
    bool        lit = true, magical = false, sunlight = false;
    std::string preset;            // content key -> render-side visual (colour, flicker)
};
struct LightView {
    bool present = false;          // false: no LightFrame (old content / sim without light): unlit view
    uint64_t frame = 0;            // sim step the frame belongs to
    std::vector<LightChunkView>  chunks;    // resident visible chunks
    std::vector<LightChunkRef>   added, removed;   // tracked-set change vs the previous frame (sim added/removed_chunks)
    std::vector<LightChunkRef>   blocked;   // tracked but unsampled (halo pending/failed): render holds last light + fog
    std::vector<LightDirtyView>  dirty;     // sim dirty_rects (bounding rect of changed samples per chunk)
    std::vector<LightChunkRef>   vis_dirty; // sim dirty_vis_chunks: masks changed for some team
    std::vector<LightSourceView> sources;
    bool        fog = false;       // fog of war shown (option on and the frame carries visibility)
    uint8_t     fog_team = 0;      // team whose masks are shown (game option)
    // Content `visual` blocks (item@1 / light presets), converted to render floats at load, keyed by
    // light preset (an item's `light.preset`, == a source's preset). Presentation only: not part of the LightFrame or any hash.
    std::map<std::string, eng::iso2d::GlowStyle, std::less<>> visuals;
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
    // Debug overlay panel (debug equip view, F3): pre-formatted lines, drawn top-right; empty = no panel.
    std::vector<std::string> debug_panel;
    // Debug cover overlay (F4). Empty (present=false) draws nothing.
    CoverDebugView cover;
};

} // namespace slice
