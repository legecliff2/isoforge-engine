#include <doctest/doctest.h>

#include <vector>

#include "iso2d/light_glow.hpp"

using namespace eng;
using namespace eng::iso2d;

TEST_CASE("glow: entity-anchored sources follow the anchor; unanchored and missing anchors keep their tile") {
    std::vector<GlowSource> g(3);
    g[0].anchor = GlowAnchorKind::Entity; g[0].anchor_id = 7; g[0].tile = {1, 1};
    g[1].anchor = GlowAnchorKind::None;   g[1].anchor_id = 7; g[1].tile = {2, 2};
    g[2].anchor = GlowAnchorKind::Entity; g[2].anchor_id = 9; g[2].tile = {3, 3};
    const GlowAnchor a[] = {{7, {4.5f, 5.25f}}, {8, {0, 0}}};
    follow_anchors(g, a);
    CHECK(g[0].tile.x == 4.5f);
    CHECK(g[0].tile.y == 5.25f);
    CHECK(g[1].tile.x == 2.f);
    CHECK(g[2].tile.x == 3.f);
}

TEST_CASE("glow: radius from SRD feet, palette by preset, tint capped by intensity") {
    CHECK(glow_radius_tiles(20, 20) == doctest::Approx(6.f));
    CHECK(glow_radius_tiles(0, 0) == doctest::Approx(0.5f));
    CHECK(glow_radius_tiles(0, 5) == doctest::Approx(0.5f));
    const GlowPalette p;
    CHECK(p.style_for("srd51.core.light.torch", false).color.r == doctest::Approx(1.f));
    CHECK(p.style_for("srd51.core.light.lantern_hooded", false).intensity == doctest::Approx(0.9f));
    const GlowStyle mag = p.style_for("x.unknown", true), mun = p.style_for("x.unknown", false);
    CHECK(mag.color.b > mag.color.r);
    CHECK(mun.color.r >= mun.color.b);
    GlowSource s;
    s.color = {1, 1, 1, 1};
    s.intensity = 2.f;
    CHECK(glow_tint(s).a == doctest::Approx(kGlowMaxAlpha));
    s.intensity = 0.5f;
    s.color.a = 0.6f;
    CHECK(glow_tint(s).a == doctest::Approx(0.3f));
    s.intensity = -1.f;
    CHECK(glow_tint(s).a == doctest::Approx(0.f));
}

TEST_CASE("glow: content visual integers convert to float colour / intensity and override the defaults") {
    GlowVisual v;
    v.r = 255; v.g = 0; v.b = 51;
    v.intensity_permille = 250;
    v.flicker = GlowFlicker::Candle;
    const GlowStyle s = glow_style_from_visual(v);
    CHECK(s.color.r == doctest::Approx(1.f));
    CHECK(s.color.g == doctest::Approx(0.f));
    CHECK(s.color.b == doctest::Approx(0.2f));
    CHECK(s.color.a == doctest::Approx(kGlowDefaultAlpha));
    CHECK(s.intensity == doctest::Approx(0.25f));
    CHECK(s.flicker == GlowFlicker::Candle);
    v.intensity_permille = 5000;
    CHECK(glow_style_from_visual(v).intensity == doctest::Approx(1.f));
    v.intensity_permille = 0;
    CHECK(glow_style_from_visual(v).intensity == doctest::Approx(0.f));

    GlowPalette p;
    const GlowStyle torch_default = p.style_for("srd51.core.light.torch", false);
    p.set_override("test.item.torch_blue", s);
    CHECK(p.override_count() == 1);
    CHECK(p.style_for("test.item.torch_blue", false).color.b == doctest::Approx(0.2f));   // exact id wins over "torch"
    CHECK(p.style_for("srd51.core.light.torch", false).color.g == doctest::Approx(torch_default.color.g));   // fallback kept
}
