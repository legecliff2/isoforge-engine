#pragma once
// Multi-tile creature presentation (Large 2x2, Huge 3x3, Gargantuan 4x4, and any larger side
// the sim reports). Pure view math: no sim types, no RNG, no SDL. SliceView reads
// ActorView::tile (the anchor, min-x / min-y) and ActorView::footprint (the side). It does not
// consume SimEvent::Kind::Footprint; that event is a log, and the snapshot copies the live side.
//
// Depth. SpriteBatch::draw passes one iso position to make_sort_key, and that key's depth is
// iso.x + iso.y. This file does not change the key. sort_iso is the front-most footprint tile
// (maximum x+y), so a body or a base ring drawn there paints after every tile of its own
// footprint and before anything strictly in front of that corner. A sprite authored on the
// anchor tile's top is shifted by centre_anchor_y — screen y only, because the centre and the
// front tile share x-y — so the art still sits on the footprint centre.
//
// One sprite still has one depth. A neighbour whose tile sums to less than the front corner,
// but who stands in front of the visual centre, sorts behind the whole quad. Giving draw
// position and sort position separate fields would be an iso2d change (make_sort_key /
// sprite_batch) and is intentionally not done here.

#include <algorithm>

#include "base/math.hpp"
#include "base/present_math.hpp"

namespace slice {

inline int footprint_side(int n) noexcept { return n < 1 ? 1 : n; }

struct FootprintGeom {
    int n = 1;
    eng::Vec2f sort_iso{};        // front tile; make_sort_key depth = x + y
    eng::Vec2f centre_iso{};      // iso point the 1x1 pawn anchor treats as the tile top
    float centre_anchor_y = 0.f;  // added to anchor_y when the sprite is drawn at sort_iso
    eng::Vec2i centre_tile{};     // integer light sample: anchor + (n - 1) / 2
    float pawn_scale = 1.f;       // placeholder body grows with the side; 1 leaves a 1x1 pawn
    // Added to anchor_y of a scaled sprite (place_on_footprint grow=true) so the body grows up
    // from the feet. The feet stay on the footprint centre; side 1 is 0.
    float grow_lift = 0.f;
    float ring_w = 0.f, ring_h = 0.f;
    float ring_anchor_x = 0.f, ring_anchor_y = 0.f;   // diamond top on the anchor tile's top
};

inline FootprintGeom footprint_geom(eng::Vec2i anchor, int side, float tile_w, float tile_h) noexcept {
    FootprintGeom g;
    g.n = footprint_side(side);
    const float back = float(g.n - 1);
    g.sort_iso = {float(anchor.x) + back, float(anchor.y) + back};
    g.centre_iso = {float(anchor.x) + back * 0.5f, float(anchor.y) + back * 0.5f};
    g.centre_anchor_y = back * tile_h * 0.5f;
    g.centre_tile = {anchor.x + (g.n - 1) / 2, anchor.y + (g.n - 1) / 2};
    g.pawn_scale = float(g.n);
    g.grow_lift = tile_h * 0.5f * (g.pawn_scale - 1.f);
    g.ring_w = tile_w * float(g.n);
    g.ring_h = tile_h * float(g.n);
    g.ring_anchor_x = g.ring_w * 0.5f;
    g.ring_anchor_y = back * tile_h;
    return g;
}

// A sprite authored for a 1x1 token (its iso point is the anchor tile's top vertex). grow
// scales it with the body, about the feet, so the ground contact stays on the footprint
// centre; otherwise only the centre slide is applied (a cue mark). Side 1 leaves w, h and
// both anchors unchanged: centre_anchor_y, pawn_scale's effect and grow_lift are all 0.
inline void place_on_footprint(float& w, float& h, float& ax, float& ay, const FootprintGeom& g, bool grow) noexcept {
    const float s = grow ? g.pawn_scale : 1.f;
    w *= s;
    h *= s;
    ax *= s;
    ay = ay * s + g.centre_anchor_y + (grow ? g.grow_lift : 0.f);
}

// World px of the name / HP anchor: the footprint centre, one body-height above the feet.
// pawn_h is the unscaled placeholder height. Side 1 is centre.y - (pawn_h - 4).
inline eng::Vec2f footprint_head_px(eng::Vec2f centre_px, const FootprintGeom& g, float pawn_h) noexcept {
    return {centre_px.x, centre_px.y - (pawn_h - 4.f) * g.pawn_scale};
}

// Union a creature's screen bounds into the focus box. centre_px is the footprint centre
// (tile centre of centre_iso). Side 1 is one tile wide and pawn_h above that centre.
inline void include_actor_bounds(float& x0, float& y0, float& x1, float& y1, eng::Vec2f centre_px,
                                 const FootprintGeom& g, float pawn_h) noexcept {
    const float hw = g.ring_w * 0.5f, hh = g.ring_h * 0.5f;
    x0 = std::min(x0, centre_px.x - hw);
    x1 = std::max(x1, centre_px.x + hw);
    y0 = std::min(y0, centre_px.y - pawn_h * g.pawn_scale);
    y1 = std::max(y1, centre_px.y + hh);
}

} // namespace slice
