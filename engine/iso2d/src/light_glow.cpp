#include "iso2d/light_glow.hpp"

#include <algorithm>
#include <map>

namespace eng::iso2d {

GlowPalette::GlowPalette()
    : GlowPalette({{"torch", {Color::rgba8(255, 170, 70, 170), 1.f}},
                   {"brazier", {Color::rgba8(255, 150, 60, 180), 1.f}},
                   {"lantern", {Color::rgba8(255, 215, 140, 160), 0.9f}},
                   {"candle", {Color::rgba8(255, 200, 120, 140), 0.6f}}},
                  {Color::rgba8(255, 230, 160, 150), 1.f}, {Color::rgba8(190, 210, 255, 170), 1.f}) {}

GlowPalette::GlowPalette(std::vector<Entry> entries, GlowStyle fallback, GlowStyle magical_fallback)
    : entries_(std::move(entries)), fallback_(fallback), magical_(magical_fallback) {}

GlowStyle GlowPalette::style_for(std::string_view preset, bool magical) const {
    const auto dot = preset.rfind('.');
    const std::string_view last = dot == std::string_view::npos ? preset : preset.substr(dot + 1);
    for (const Entry& e : entries_)
        if (!e.match.empty() && last.find(e.match) != std::string_view::npos) return e.style;
    return magical ? magical_ : fallback_;
}

float glow_radius_tiles(uint16_t bright_ft, uint16_t dim_ft) noexcept {
    const float r = (float(bright_ft) + 0.5f * float(dim_ft)) / 5.f;
    return std::max(r, 0.5f);
}

void follow_anchors(std::span<GlowSource> glows, std::span<const GlowAnchor> anchors) {
    if (glows.empty() || anchors.empty()) return;
    std::map<uint32_t, Vec2f> at;
    for (const GlowAnchor& a : anchors) at.emplace(a.id, a.tile);   // first wins on duplicates
    for (GlowSource& g : glows) {
        if (g.anchor != GlowAnchorKind::Entity) continue;
        if (auto it = at.find(g.anchor_id); it != at.end()) g.tile = it->second;
    }
}

Color glow_tint(const GlowSource& g) noexcept {
    Color c = g.color;
    c.a = std::clamp(c.a * std::clamp(g.intensity, 0.f, 1.f), 0.f, kGlowMaxAlpha);
    return c;
}

} // namespace eng::iso2d
