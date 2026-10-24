#pragma once
#include <cstdint>
#include <tuple>
#include "base/present_math.hpp"

namespace eng::iso2d {

// 2:1 dimetric projection; one tile = 5 ft square, default 64x32 px diamond (§5.1).
struct IsoProjection {
    float tile_w = 64.f;
    float tile_h = 32.f;

    // Iso (tile) coords -> world pixel coords of the diamond's top vertex... centre-top
    // convention: tile (x, y) has its top corner at to_screen(x, y).
    Vec2f to_screen(float ix, float iy) const {
        return {(ix - iy) * tile_w * 0.5f, (ix + iy) * tile_h * 0.5f};
    }
    // Inverse of to_screen (used for picking).
    Vec2f to_iso(float sx, float sy) const {
        const float a = sx / (tile_w * 0.5f), b = sy / (tile_h * 0.5f);
        return {(a + b) * 0.5f, (b - a) * 0.5f};
    }
};

// Depth sort key = (layer, depth, z, entity_id): deterministic, total order (§5.1).
// `depth` is the sort sample's iso_x + iso_y. The sample does not have to be the point the
// sprite is projected from; SpriteBatch::draw takes those positions separately.
// Absolute height is not applied. One approach under consideration, not a commitment, is to
// subtract height in tiles from `depth` inside make_sort_key, so a flying actor would sort
// further back without moving the sprite and without a fifth component. The altitude design
// is not settled. `z` stays the stack on one sample (floor, body, outline), not altitude.
struct SortKey {
    int32_t  layer = 0;
    float    depth = 0.f;   // iso_x + iso_y of the sort sample
    int32_t  z = 0;
    uint32_t entity_id = 0;

    friend bool operator<(const SortKey& a, const SortKey& b) {
        return std::tie(a.layer, a.depth, a.z, a.entity_id) < std::tie(b.layer, b.depth, b.z, b.entity_id);
    }
};

// `sort_iso` is the depth sample, not necessarily the projected anchor.
inline SortKey make_sort_key(Vec2f sort_iso, int32_t layer, int32_t z, uint32_t entity_id) {
    return {layer, sort_iso.x + sort_iso.y, z, entity_id};
}

} // namespace eng::iso2d
