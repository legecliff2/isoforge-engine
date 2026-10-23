#pragma once
// Render-side glows for light sources attached to entities (LIGHTING.md §10, phase 3 prep).
// Presentation only: the sim decides what is lit (LightFrame samples); a glow is the visual halo
// drawn under a lit source and never brightens a tile the sim says is Dark (it is drawn with the
// tile tints already applied, and its alpha is capped).
//
// Producers fill a generic list of GlowSource (one per lit source): today the game copies them
// from the LightFrame's source list (the interim pc_torches are one producer of attached sources);
// sim attached lights (phase 3) and static chunk lights flow through the same list unchanged.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include "base/math.hpp"
#include "base/present_math.hpp"

namespace eng::iso2d {

// What a glow is attached to. None: fixed world position (static light, item on the ground).
enum class GlowAnchorKind : uint8_t { None = 0, Entity = 1 };

struct GlowSource {
    uint32_t       key = 0;            // stable per source (sort tie-break; e.g. flattened LightId)
    GlowAnchorKind anchor = GlowAnchorKind::None;
    uint32_t       anchor_id = 0;      // entity id when anchor == Entity
    Vec2f          tile{};             // world tile position (fallback when the anchor isn't drawn)
    float          radius_tiles = 1.f; // visual radius in tiles (>= 0.5)
    Color          color{1.f, 1.f, 1.f, 1.f};
    float          intensity = 1.f;    // 0..1, scales alpha
};

// Drawn position of an entity this frame (e.g. the token's interpolated tile).
struct GlowAnchor {
    uint32_t id = 0;
    Vec2f    tile{};
};

// Presentation for a light preset (content key). Lives only in render/content, never hashed.
enum class GlowFlicker : uint8_t { None = 0, Torch = 1, Candle = 2 };
struct GlowStyle {
    Color color{1.f, 0.9f, 0.63f, 0.6f};
    float intensity = 1.f;
    GlowFlicker flicker = GlowFlicker::None;
};

// Content `visual` block (item@1, later light presets; LIGHTING.md 10.1), integers as authored:
// color channels 0..255, intensity in permille 0..1000. Presentation only: never hashed, never read by sim.
struct GlowVisual {
    uint8_t     r = 255, g = 255, b = 255;
    uint16_t    intensity_permille = 1000;
    GlowFlicker flicker = GlowFlicker::None;
};
// Integers -> render-side style (done once at load). Alpha of `color` is the palette default glow alpha
// (kGlowDefaultAlpha); intensity scales it at draw time (glow_tint). Out-of-range intensity is clamped.
GlowStyle glow_style_from_visual(const GlowVisual& v) noexcept;
inline constexpr float kGlowDefaultAlpha = 170.f / 255.f;

// Preset key -> style. Prefix/substring match on the key's last segment ("…light.torch" -> torch).
// Unknown presets get `fallback` (magical sources get the cool fallback).
class GlowPalette {
public:
    struct Entry { std::string_view match; GlowStyle style; };
    GlowPalette();   // built-in defaults: torch, lantern, candle, brazier
    explicit GlowPalette(std::vector<Entry> entries, GlowStyle fallback, GlowStyle magical_fallback);
    // Exact preset / item id -> style from content `visual` blocks; wins over the built-in entries.
    void set_override(std::string id, GlowStyle style);
    std::size_t override_count() const noexcept { return overrides_.size(); }
    const GlowStyle* override_for(std::string_view id) const;   // nullptr if none
    GlowStyle style_for(std::string_view preset, bool magical) const;

private:
    std::map<std::string, GlowStyle, std::less<>> overrides_;
    std::vector<Entry> entries_;
    GlowStyle fallback_, magical_;
};

// Visual radius in tiles for an SRD bright/dim radius in feet (5 ft per tile). Bright + half the
// dim ring, so the halo reads as the bright area with a soft edge. At least half a tile.
float glow_radius_tiles(uint16_t bright_ft, uint16_t dim_ft) noexcept;

// Moves entity-anchored glows to their anchor's drawn position. `anchors` need not be sorted;
// glows whose anchor isn't present keep their `tile` (the sim-published position).
void follow_anchors(std::span<GlowSource> glows, std::span<const GlowAnchor> anchors);

// Final tint for a glow: color with alpha scaled by intensity, capped so a glow alone can never
// make a tile read as fully lit.
Color glow_tint(const GlowSource& g) noexcept;

inline constexpr float kGlowMaxAlpha = 0.75f;

} // namespace eng::iso2d
