#include "iso2d/light_glow.hpp"

#include <algorithm>
#include <map>

namespace eng::iso2d {

GlowPalette::GlowPalette()
    // Matched in order against the preset's last segment (srd51.core.light.<name>): specific names first.
    : GlowPalette({{"torch", {Color::rgba8(255, 170, 70, 170), 1.f, GlowFlicker::Torch}},
                   {"brazier", {Color::rgba8(255, 150, 60, 180), 1.f, GlowFlicker::Torch}},
                   {"lantern_hooded", {Color::rgba8(255, 210, 130, 160), 0.9f, GlowFlicker::None}},
                   {"lantern_bullseye", {Color::rgba8(255, 225, 160, 170), 1.f, GlowFlicker::None}},
                   {"lantern", {Color::rgba8(255, 215, 140, 160), 0.9f, GlowFlicker::None}},
                   {"lamp", {Color::rgba8(255, 195, 110, 150), 0.75f, GlowFlicker::Candle}},
                   {"candle", {Color::rgba8(255, 200, 120, 140), 0.6f, GlowFlicker::Candle}}},
                  {Color::rgba8(255, 230, 160, 150), 1.f}, {Color::rgba8(190, 210, 255, 170), 1.f}) {}

GlowPalette::GlowPalette(std::vector<Entry> entries, GlowStyle fallback, GlowStyle magical_fallback)
    : entries_(std::move(entries)), fallback_(fallback), magical_(magical_fallback) {}

void GlowPalette::set_override(std::string id, GlowStyle style) { overrides_[std::move(id)] = style; }

GlowStyle glow_style_from_visual(const GlowVisual& v) noexcept {
    GlowStyle s;
    s.color = Color{v.r / 255.f, v.g / 255.f, v.b / 255.f, kGlowDefaultAlpha};
    s.intensity = float(std::min<uint16_t>(v.intensity_permille, 1000)) / 1000.f;
    s.flicker = v.flicker;
    return s;
}

GlowStyle GlowPalette::style_for(std::string_view preset, bool magical) const {
    if (auto it = overrides_.find(preset); it != overrides_.end()) return it->second;
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
