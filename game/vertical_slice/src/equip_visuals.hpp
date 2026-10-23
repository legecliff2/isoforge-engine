#pragma once
// Equipment on character sprites (render side, presentation only). Pure layout: ActorView::equipment -> an ordered list
// of layers around the body, each with a fixed z inside the actor's batcher key (layer 1, x+y, z, actor id), so the
// order relative to the body never depends on submission order, frame or platform.
//
// Draw order, back to front (z):
//   kZBack (2)  Back items: stowed weapons / shields slung on the back
//   kZBody (4)  the body (pawn)
//   5, 6        the hands: facing East (SE / NE) off hand 5, main hand 6 (main hand on the camera side);
//               facing West (SW / NW) mirrored: main hand 5, off hand 6, and every attachment x is mirrored
//   kZLight (7) held lights (torch, lantern) in either hand, at that hand's attachment point; then its flame
//   kZOutline (8) Faerie Fire outline ring (after body / equip)
// Ties inside one z: item instance id. Walls share layer 1 at z 0, so they still sort under every actor layer.
#include <cstdint>
#include <vector>

#include "base/math.hpp"
#include "base/present_math.hpp"
#include "view_model.hpp"

namespace slice {

inline constexpr int32_t kZBack = 2, kZBody = 4, kZFarHand = 5, kZNearHand = 6, kZLight = 7,
                         kZOutline = 8;   // Faerie Fire outline ring (after body / equip)

enum class EquipPart : uint8_t { Back, Body, OffHand, MainHand, HeldLight };

// Attachment points in screen px relative to the actor's iso point (the token's anchor), facing SouthEast; West facings
// negate x. Same for every pawn sprite (one placeholder body); per-sprite tables slot in here when art arrives.
struct AttachPoints { eng::Vec2f main_hand{9.f, 4.f}, off_hand{-9.f, 4.f}, back{-7.f, 2.f}; };
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
