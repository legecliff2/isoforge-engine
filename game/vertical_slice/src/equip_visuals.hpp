#pragma once
// Equipment on character sprites (render side, presentation only). Pure layout: ActorView::equipment -> an ordered list
// of layers around the body, each with a fixed z inside the actor's batcher key (layer 1, x+y, z, actor id), so the
// order relative to the body never depends on submission order, frame or platform.
//
// Draw order, back to front (z). The batcher key is (layer 1, depth, z, actor id) and does not carry the item
// instance, so two layers that must stay ordered need different z. Ties inside one z: item instance id.
// Walls share layer 1 at z 0, so they still sort under every actor layer.
//
//   kZBack (2)       behind the body. Back on toward/profile facings (se, sw; and s, e, w when those exist).
//                    Hip, the front belt, on away facings (ne, nw; and n).
//   kZBody (4)       the body (pawn)
//   kZWornFront (5)  in front of the body, behind both hands. Back on away facings; Hip on toward/profile.
//                    There was no integer between the body and the far hand, so the hand, light and outline
//                    constants moved up by one. Their order relative to the body is the same.
//   6, 7             the hands: facing East (SE / NE) off hand 6, main hand 7 (main hand on the camera side);
//                    facing West (SW / NW) mirrored: main hand 6, off hand 7, and every attachment x is mirrored
//   kZLight (8)      held lights (torch, lantern) in either hand, at that hand's attachment point; then its flame
//   kZOutline (9)    Faerie Fire outline ring (after body / equip)
#include <cstdint>
#include <vector>

#include "base/math.hpp"
#include "base/present_math.hpp"
#include "view_model.hpp"

namespace slice {

inline constexpr int32_t kZBack = 2, kZBody = 4, kZWornFront = 5, kZFarHand = 6, kZNearHand = 7, kZLight = 8,
                         kZOutline = 9;   // Faerie Fire outline ring (after body / equip)

// Camera sees the character's back. The eight-facing rule is: away = n / ne / nw; toward or profile = s / se / sw / e / w.
// M0 has the four diagonals. Adding n is the only change this function needs.
inline bool facing_shows_back(Facing f) {
    return f == Facing::NorthEast || f == Facing::NorthWest;
}
// Back: in front of the body on an away facing, behind it otherwise (the rear rule).
// Hip: the front-belt rule, the opposite. Call this only for Back and Hip.
inline int32_t worn_z(AttachSlot slot, Facing f) {
    const bool away = facing_shows_back(f);
    const bool in_front = slot == AttachSlot::Back ? away : !away;
    return in_front ? kZWornFront : kZBack;
}

enum class EquipPart : uint8_t { Back, Body, OffHand, MainHand, HeldLight, Hip };

// Attachment points in screen px relative to the actor's iso point (the token's anchor), facing SouthEast; West facings
// negate x. Same for every pawn sprite (one placeholder body); per-sprite tables slot in here when art arrives.
// hip sits on the belt, opposite the back sling, so the two placeholders do not share a point.
struct AttachPoints {
    eng::Vec2f main_hand{9.f, 4.f}, off_hand{-9.f, 4.f}, back{-7.f, 2.f}, hip{6.f, 8.f};
};
AttachPoints attach_points(Facing);

struct EquipLayer {
    EquipPart part = EquipPart::Body;
    int32_t z = kZBody;
    const EquipVisual* item = nullptr;   // nullptr for Body
    eng::Vec2f at{};                     // attachment point (px from the token anchor)
};
// Body plus one layer per drawable item, in draw order (z, then item instance). Deterministic in the item list order.
std::vector<EquipLayer> equip_layers(const ActorView&);
// Placeholder art: simple colored quads (no item art in content yet), back to front within the layer. `at` is the quad's
// bottom-centre in px from the token anchor. Emissive quads (a lit flame) skip the tile light tint.
struct EquipQuad { eng::Vec2f at{}, size{}; eng::Color color{1, 1, 1, 1}; bool emissive = false; };
std::vector<EquipQuad> equip_quads(const EquipVisual&, EquipPart, eng::Vec2f at);
// The flame is drawn (and the sim publishes a glow) only when the light is lit and sheds light: the same test as glows_from.
inline bool sheds_light(const EquipVisual& e) { return e.lit && (e.bright_ft > 0 || e.dim_ft > 0); }

} // namespace slice
