#include "iso2d/zone_look.hpp"

#include <algorithm>

namespace eng::iso2d {

namespace {

int abs_i(int v) noexcept { return v < 0 ? -v : v; }

// Authored looks. Alpha is the fill strength before the pattern gain: None stays see-through,
// Light is a veil, Heavy hides the ground. Darkness is heavier than Fog Cloud on purpose.
constexpr ZoneLook kLooks[] = {
    {ZoneKind::Entangle, "srd51.spell.entangle", {0.18f, 0.62f, 0.22f, 0.36f}, ZonePattern::Stripes, ZoneEdge::Jagged,
     ZoneObscure::None, false},
    {ZoneKind::Grease, "srd51.spell.grease", {0.86f, 0.68f, 0.16f, 0.34f}, ZonePattern::Stripes, ZoneEdge::Soft,
     ZoneObscure::None, true},
    // SRD: Spike Growth is camouflaged, so the visible specks are a deliberate visual tell.
    {ZoneKind::SpikeGrowth, "srd51.spell.spike_growth", {0.72f, 0.28f, 0.14f, 0.40f}, ZonePattern::Specks,
     ZoneEdge::Jagged, ZoneObscure::None, false},
    {ZoneKind::SpiritGuardians, "srd51.spell.spirit_guardians", {0.72f, 0.62f, 0.98f, 0.38f}, ZonePattern::Pulse,
     ZoneEdge::Bright, ZoneObscure::None, true},
    {ZoneKind::StinkingCloud, "srd51.spell.stinking_cloud", {0.55f, 0.78f, 0.12f, 0.72f}, ZonePattern::Mottled,
     ZoneEdge::Soft, ZoneObscure::Heavy, true},
    {ZoneKind::Silence, "srd51.spell.silence", {0.42f, 0.52f, 0.70f, 0.32f}, ZonePattern::Wash, ZoneEdge::Dashed,
     ZoneObscure::None, false},
    {ZoneKind::Web, "srd51.spell.web", {0.92f, 0.92f, 0.96f, 0.46f}, ZonePattern::Lattice, ZoneEdge::Dashed,
     ZoneObscure::Light, false},
    {ZoneKind::FogCloud, "srd51.spell.fog_cloud", {0.55f, 0.64f, 0.82f, 0.68f}, ZonePattern::Wash, ZoneEdge::Soft,
     ZoneObscure::Heavy, false},
    {ZoneKind::Darkness, "srd51.spell.darkness", {0.12f, 0.05f, 0.22f, 0.90f}, ZonePattern::Wash, ZoneEdge::Solid,
     ZoneObscure::Heavy, false},
};

float pattern_gain(const ZoneLook& look, int x, int y, uint32_t phase) noexcept {
    const int step = int(look.animates ? (phase & 255u) : 0u);
    switch (look.pattern) {
    case ZonePattern::Wash: return 1.f;
    case ZonePattern::Stripes: return ((x + y + step) & 1) ? 1.f : 0.35f;
    case ZonePattern::Lattice: return ((x & 1) == 0 || (y & 1) == 0) ? 1.f : 0.22f;
    case ZonePattern::Specks: return ((abs_i(x) * 3 + abs_i(y) * 5 + step) % 3) == 0 ? 1.f : 0.28f;
    case ZonePattern::Pulse: {
        if (!look.animates) return 1.f;
        constexpr float k[4] = {0.45f, 0.70f, 1.f, 0.70f};
        return k[unsigned(step) & 3u];
    }
    case ZonePattern::Mottled: return ((abs_i(x) * 7 + abs_i(y) * 11 + step) % 4) == 0 ? 0.40f : 1.f;
    }
    return 1.f;
}

} // namespace

std::span<const ZoneLook> zone_looks() noexcept { return kLooks; }

const ZoneLook* zone_look_for(std::string_view content_id) noexcept {
    for (const ZoneLook& look : kLooks)
        if (look.content_id == content_id) return &look;
    return nullptr;
}

Color zone_fill_tint(const ZoneLook& look, int tile_x, int tile_y, uint32_t phase) noexcept {
    Color c = look.tint;
    c.a *= pattern_gain(look, tile_x, tile_y, phase);
    return c;
}

Color zone_edge_tint(const ZoneLook& look, int tile_x, int tile_y) noexcept {
    Color c = look.tint;
    switch (look.edge) {
    case ZoneEdge::None: return {0.f, 0.f, 0.f, 0.f};
    case ZoneEdge::Soft:
        c.r = std::min(1.f, c.r + 0.15f);
        c.g = std::min(1.f, c.g + 0.15f);
        c.b = std::min(1.f, c.b + 0.15f);
        c.a = 0.40f;
        return c;
    case ZoneEdge::Solid:
        c.r *= 0.75f;
        c.g *= 0.75f;
        c.b *= 0.75f;
        c.a = 0.95f;
        return c;
    case ZoneEdge::Dashed:
        if (((tile_x + tile_y) & 1) != 0) return {0.f, 0.f, 0.f, 0.f};
        c.a = 0.88f;
        return c;
    case ZoneEdge::Bright:
        c.r = std::min(1.f, c.r + 0.35f);
        c.g = std::min(1.f, c.g + 0.30f);
        c.b = std::min(1.f, c.b + 0.15f);
        c.a = 0.92f;
        return c;
    case ZoneEdge::Jagged:
        if (((abs_i(tile_x) * 3 + abs_i(tile_y)) % 3) == 0) return {0.f, 0.f, 0.f, 0.f};
        c.r *= 0.65f;
        c.g *= 0.65f;
        c.b *= 0.65f;
        c.a = 0.90f;
        return c;
    }
    return {0.f, 0.f, 0.f, 0.f};
}

bool zone_draws_edge(const ZoneLook& look, Vec2i tile) noexcept {
    return look.edge != ZoneEdge::None && zone_edge_tint(look, tile.x, tile.y).a > 0.f;
}

bool zone_tile_is_edge(std::span<const Vec2i> tiles, Vec2i tile) noexcept {
    auto has = [&](int x, int y) {
        return std::find(tiles.begin(), tiles.end(), Vec2i{x, y}) != tiles.end();
    };
    return !has(tile.x - 1, tile.y) || !has(tile.x + 1, tile.y) || !has(tile.x, tile.y - 1) || !has(tile.x, tile.y + 1);
}

} // namespace eng::iso2d
