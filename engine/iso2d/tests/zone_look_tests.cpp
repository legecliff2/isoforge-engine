#include <doctest/doctest.h>

#include <cmath>
#include <iterator>
#include <vector>

#include "iso2d/iso.hpp"
#include "iso2d/zone_look.hpp"

using namespace eng;
using namespace eng::iso2d;

namespace {
const ZoneLook& look(const char* id) {
    const ZoneLook* p = zone_look_for(id);
    REQUIRE(p);
    return *p;
}
bool alpha_moved(const ZoneLook& L) {
    const Color a = zone_fill_tint(L, 0, 0, 0);
    const Color b = zone_fill_tint(L, 0, 0, 1);
    return a.a != b.a;
}
} // namespace

TEST_CASE("zone looks: each persistent spell has one row, and unknown ids do not") {
    const char* ids[] = {"srd51.spell.entangle",        "srd51.spell.grease",     "srd51.spell.spike_growth",
                         "srd51.spell.spirit_guardians", "srd51.spell.stinking_cloud", "srd51.spell.silence",
                         "srd51.spell.web",              "srd51.spell.fog_cloud",  "srd51.spell.darkness"};
    REQUIRE(zone_looks().size() == std::size(ids));
    for (size_t i = 0; i < std::size(ids); ++i) {
        const ZoneLook& a = look(ids[i]);
        CHECK(a.content_id == ids[i]);
        CHECK(a.kind != ZoneKind::None);
        for (size_t j = i + 1; j < std::size(ids); ++j) {
            const ZoneLook& b = look(ids[j]);
            const bool distinct = a.pattern != b.pattern || a.edge != b.edge || a.obscure != b.obscure ||
                                  a.animates != b.animates || a.tint.r != b.tint.r || a.tint.g != b.tint.g ||
                                  a.tint.b != b.tint.b || a.tint.a != b.tint.a;
            CHECK(distinct);
        }
    }
    CHECK(zone_look_for("") == nullptr);
    CHECK(zone_look_for("srd51.spell.fire_bolt") == nullptr);
    CHECK(zone_look_for("fog_cloud") == nullptr);
}

TEST_CASE("zone looks: Web reads apart from Fog Cloud and Darkness, and the ground spells do not obscure") {
    const ZoneLook& web = look("srd51.spell.web");
    const ZoneLook& fog = look("srd51.spell.fog_cloud");
    const ZoneLook& dark = look("srd51.spell.darkness");
    const ZoneLook& stink = look("srd51.spell.stinking_cloud");
    CHECK(web.obscure == ZoneObscure::Light);
    CHECK(fog.obscure == ZoneObscure::Heavy);
    CHECK(dark.obscure == ZoneObscure::Heavy);
    CHECK(stink.obscure == ZoneObscure::Heavy);
    CHECK(web.pattern == ZonePattern::Lattice);
    CHECK(fog.pattern == ZonePattern::Wash);
    CHECK(dark.pattern == ZonePattern::Wash);
    CHECK(stink.pattern == ZonePattern::Mottled);
    CHECK(web.edge == ZoneEdge::Dashed);
    CHECK(fog.edge == ZoneEdge::Soft);
    CHECK(dark.edge == ZoneEdge::Solid);
    CHECK(web.tint.r > 0.8f);
    CHECK(std::fabs(web.tint.r - web.tint.b) < 0.1f);
    CHECK(fog.tint.b > fog.tint.r);
    CHECK(dark.tint.b > dark.tint.r);
    CHECK(dark.tint.r + dark.tint.g + dark.tint.b < fog.tint.r + fog.tint.g + fog.tint.b);
    CHECK(stink.tint.g > stink.tint.r);
    CHECK(stink.tint.g > stink.tint.b);
    // Gain-1 tiles: veil < fog < darkness, and a see-through marking stays under the veil.
    const float ent = zone_fill_tint(look("srd51.spell.entangle"), 1, 0, 0).a;
    const float web_a = zone_fill_tint(web, 0, 0, 0).a;
    const float fog_a = zone_fill_tint(fog, 0, 0, 0).a;
    const float dark_a = zone_fill_tint(dark, 0, 0, 0).a;
    CHECK(ent < web_a);
    CHECK(web_a < fog_a);
    CHECK(fog_a < dark_a);
    for (const char* id : {"srd51.spell.entangle", "srd51.spell.grease", "srd51.spell.spike_growth",
                           "srd51.spell.spirit_guardians", "srd51.spell.silence"})
        CHECK(look(id).obscure == ZoneObscure::None);
}

TEST_CASE("zone looks: pattern and animation stay on the looks that asked for them") {
    const ZoneLook& web = look("srd51.spell.web");
    const ZoneLook& fog = look("srd51.spell.fog_cloud");
    CHECK(zone_fill_tint(web, 0, 0, 0).a > zone_fill_tint(web, 1, 1, 0).a);   // lattice gaps
    CHECK(zone_fill_tint(fog, 0, 0, 0).a == zone_fill_tint(fog, 1, 1, 0).a);  // flat wash
    CHECK(alpha_moved(look("srd51.spell.grease")));
    CHECK(alpha_moved(look("srd51.spell.spirit_guardians")));
    CHECK(alpha_moved(look("srd51.spell.stinking_cloud")));
    for (const char* id : {"srd51.spell.entangle", "srd51.spell.spike_growth", "srd51.spell.silence", "srd51.spell.web",
                           "srd51.spell.fog_cloud", "srd51.spell.darkness"})
        CHECK_FALSE(alpha_moved(look(id)));
    // Same inputs, same tint.
    const Color a = zone_fill_tint(look("srd51.spell.grease"), 4, -2, 3);
    const Color b = zone_fill_tint(look("srd51.spell.grease"), 4, -2, 3);
    CHECK(a.r == b.r);
    CHECK(a.g == b.g);
    CHECK(a.b == b.b);
    CHECK(a.a == b.a);
}

TEST_CASE("zone looks: edge cadence and the overlay depth slot") {
    std::vector<Vec2i> sq;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) sq.push_back({x, y});
    CHECK_FALSE(zone_tile_is_edge(sq, {1, 1}));
    CHECK(zone_tile_is_edge(sq, {0, 0}));
    CHECK(zone_tile_is_edge(sq, {2, 1}));
    CHECK(zone_tile_is_edge(std::vector<Vec2i>{{4, 4}}, {4, 4}));

    const ZoneLook& silence = look("srd51.spell.silence");
    const ZoneLook& fog = look("srd51.spell.fog_cloud");
    const ZoneLook& dark = look("srd51.spell.darkness");
    CHECK(zone_draws_edge(silence, {0, 0}));
    CHECK_FALSE(zone_draws_edge(silence, {1, 0}));   // dashed: odd x+y
    CHECK(zone_draws_edge(fog, {1, 0}));
    CHECK(zone_draws_edge(dark, {1, 0}));
    CHECK(zone_edge_tint(fog, 1, 0).a < zone_edge_tint(dark, 1, 0).a);
    CHECK_FALSE(zone_draws_edge(look("srd51.spell.entangle"), {0, 0}));   // jagged skips multiples of 3
    CHECK(zone_draws_edge(look("srd51.spell.entangle"), {1, 1}));

    CHECK(kZoneOverlayLayer == 0);
    CHECK(kZoneOverlayZ == 1);
    CHECK(zone_overlay_key(2, 3) == uint32_t(3 * 32 + 2));
    const SortKey slot = make_sort_key({2.f, 3.f}, kZoneOverlayLayer, kZoneOverlayZ, zone_overlay_key(2, 3));
    const SortKey haze = make_sort_key({2.f, 3.f}, 0, 1, uint32_t(3 * 32 + 2));
    CHECK_FALSE(slot < haze);
    CHECK_FALSE(haze < slot);
}
