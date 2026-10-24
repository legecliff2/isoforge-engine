#pragma once
// Presentation for persistent spell zones (Entangle, Grease, Spike Growth, Spirit Guardians,
// Stinking Cloud, Silence, Web, Fog Cloud, Darkness). The sim still owns obscurement; this table
// only decides how a zone reads. Keyed by the spell's content id, which the game copies onto the
// view next to the rules spell id. No gameplay state is read back from these colours.
//
// Overlays occupy the same depth slot the heavy-obscurement haze already used: layer 0, z = 1,
// entity key = tile.y * 32 + tile.x. That keeps them between the ground (z = the tile layer) and
// token glows (z = 8). Do not retune the slot or the SortKey order.
#include <cstdint>
#include <span>
#include <string_view>

#include "base/math.hpp"
#include "base/present_math.hpp"

namespace eng::iso2d {

inline constexpr int32_t kZoneOverlayLayer = 0;
inline constexpr int32_t kZoneOverlayZ = 1;

// Same entity id the ground sprite and the heavy-obscurement haze use for this tile.
constexpr uint32_t zone_overlay_key(int tile_x, int tile_y) noexcept {
    return uint32_t(tile_y * 32 + tile_x);
}

enum class ZoneKind : uint8_t {
    None = 0,
    Entangle,
    Grease,
    SpikeGrowth,
    SpiritGuardians,
    StinkingCloud,
    Silence,
    Web,
    FogCloud,
    Darkness,
};

// How the fill alpha varies across tiles. Wash is flat; the others are a tile pattern.
// Animated looks shift the pattern with `phase` (a presentation clock, typically the light-frame step).
enum class ZonePattern : uint8_t { Wash = 0, Stripes, Lattice, Specks, Pulse, Mottled };

// Rim on boundary tiles. Dashed and Jagged skip some of those tiles; the others mark every boundary tile.
enum class ZoneEdge : uint8_t { None = 0, Soft, Solid, Dashed, Bright, Jagged };

// Whether the fill hides the ground. None: a marking you can see through. Light: a veil (Web).
// Heavy: the ground is hard to read (Fog Cloud, Stinking Cloud, Darkness).
enum class ZoneObscure : uint8_t { None = 0, Light, Heavy };

struct ZoneLook {
    ZoneKind kind = ZoneKind::None;
    std::string_view content_id;   // "srd51.spell.fog_cloud"
    Color tint{1.f, 1.f, 1.f, 1.f};   // rgb and the fill alpha before the pattern gain
    ZonePattern pattern = ZonePattern::Wash;
    ZoneEdge edge = ZoneEdge::None;
    ZoneObscure obscure = ZoneObscure::None;
    bool animates = false;
};

// The built-in table (one row per spell above). Unknown content ids are not in it.
std::span<const ZoneLook> zone_looks() noexcept;

// nullptr when `content_id` is not one of the rows. Match is exact (the full content id).
const ZoneLook* zone_look_for(std::string_view content_id) noexcept;

// Fill colour for one tile. `phase` moves the pattern only when `look.animates`.
Color zone_fill_tint(const ZoneLook& look, int tile_x, int tile_y, uint32_t phase) noexcept;

// Rim colour. Alpha 0 means this boundary tile draws no rim (dashed / jagged cadence).
Color zone_edge_tint(const ZoneLook& look, int tile_x, int tile_y) noexcept;

// True when the edge style wants a rim on this tile. The caller also requires a boundary tile.
bool zone_draws_edge(const ZoneLook& look, Vec2i tile) noexcept;

// A tile in `tiles` (any order) whose 4-neighbour is missing from the set. A one-tile zone is all edge.
bool zone_tile_is_edge(std::span<const Vec2i> tiles, Vec2i tile) noexcept;

} // namespace eng::iso2d
