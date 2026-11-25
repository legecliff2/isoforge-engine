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
//                    Neck sits here too, and only on toward facings (se, sw). Away facings draw no neck.
//                    There was no integer between the body and the far hand, so the hand, light and outline
//                    constants moved up by one. Their order relative to the body is the same.
//   6, 7             the hands: facing East (SE / NE) off hand 6, main hand 7 (main hand on the camera side);
//                    facing West (SW / NW) mirrored: main hand 6, off hand 7, and every attachment x is mirrored
//   kZLight (8)      held lights (torch, lantern) in either hand, at that hand's attachment point; then its flame
//   kZOutline (9)    Faerie Fire outline ring (after body / equip)
//
// One back item is drawn. Hip-main and hip-off are separate anchors and share the hip z rule. A weapon's sheath
// is that weapon's `hip` art part (no side field on the sprite); this layout assigns the anchor.
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "base/math.hpp"
#include "base/present_math.hpp"
#include "view_model.hpp"

namespace eng::presentation {
class EquipArtTable;
struct EquipArtSprite;
} // namespace eng::presentation

namespace slice {

inline constexpr int32_t kZBack = 2, kZBody = 4, kZWornFront = 5, kZFarHand = 6, kZNearHand = 7, kZLight = 8,
                         kZOutline = 9;   // Faerie Fire outline ring (after body / equip)

// Camera sees the character's back. The eight-facing rule is: away = n / ne / nw; toward or profile = s / se / sw / e / w.
// M0 has the four diagonals. Adding n is the only change this function needs.
inline bool facing_shows_back(Facing f) {
    return f == Facing::NorthEast || f == Facing::NorthWest;
}
// Neck is toward-only (se, sw). Away hides it. Profile e/w are not toward; M0 has none, and this does not treat them as se/sw.
inline bool facing_shows_neck(Facing f) {
    return f == Facing::SouthEast || f == Facing::SouthWest;
}
// Back: in front of the body on an away facing, behind it otherwise (the rear rule).
// Hip: the front-belt rule, the opposite. Call this only for Back and Hip (both hip anchors).
// TODO(art): Art decided hip-off stays z5 for M0, including the far side. Move the far side to z2 only when true E/W facings ship.
inline int32_t worn_z(AttachSlot slot, Facing f) {
    const bool away = facing_shows_back(f);
    const bool in_front = slot == AttachSlot::Back ? away : !away;
    return in_front ? kZWornFront : kZBack;
}

enum class EquipPart : uint8_t { Back, Body, OffHand, MainHand, HeldLight, HipMain, HipOff, Neck };

// Attachment points in screen px relative to the actor's iso point (the token's anchor), for one authored facing.
// West facings negate x. hip_main is today's hip, on the main-hand side. hip_off is the off-hand side of the belt.
// neck (0, 2) is the shared fallback. The paperdoll medium kit names its own neck.
struct AttachPoints {
    eng::Vec2f main_hand{9.f, 4.f}, off_hand{-9.f, 4.f}, back{-7.f, 2.f};
    eng::Vec2f hip_main{6.f, 8.f}, hip_off{-6.f, 8.f}, neck{0.f, 2.f};
};

// One body's authored points. se and ne are stored; sw mirrors se and nw mirrors ne. `loaded` is false when no
// attach.json was found and both facings are the shared table above.
struct BodyAttach {
    AttachPoints se{};
    AttachPoints ne{};
    bool loaded = false;
};
// The shared table every body uses when it has no attach.json.
inline BodyAttach shared_body_attach() { return {}; }

// Optional attach.json beside a body kit. Authored facings only:
//   { "se": { "main": [x,y], "off": [x,y], "back": [x,y], "hip-main": [x,y], "hip-off": [x,y], "neck": [x,y] },
//     "ne": { ... } }
// A missing key keeps that shared point. Offsets are screen px from the token anchor.
// visual_kind "paperdoll" reads {standin_root}/../paperdoll/{bin}/attach.json first (the humanoid
// doll, not the generic blob). Anything else reads the per-monster kit srd/{dir}/{bin}/attach.json.
// Both then fall through to frames/{bin}/attach.json, then the shared table. frames/{bin} stays the
// generic medium blob and does not carry the humanoid points.
BodyAttach load_body_attach(const std::filesystem::path& standin_root, std::string_view monster_id, std::string_view bin,
                            std::string_view visual_kind = {});
AttachPoints attach_points(const BodyAttach& body, Facing facing);

struct EquipLayer {
    EquipPart part = EquipPart::Body;
    int32_t z = kZBody;
    const EquipVisual* item = nullptr;   // nullptr for Body
    eng::Vec2f at{};                     // attachment point (px from the token anchor)
};
// Body plus the drawn items, in draw order (z, then item instance). An empty body is the shared table.
// Sheaths of wielded weapons are extra layers on the weapon's own item (its hip art part).
// `manifest` is the loaded equip_kit@1 table. Null keeps the content-id fallback for back_role.
std::vector<EquipLayer> equip_layers(const ActorView& actor, const BodyAttach& body = {},
                                    const eng::presentation::EquipArtTable* manifest = nullptr);
// Placeholder art: simple colored quads (no item art in content yet), back to front within the layer. `at` is the quad's
// bottom-centre in px from the token anchor. Emissive quads (a lit flame) skip the tile light tint.
// A weapon drawn as HipMain or HipOff is a sheath. Unmapped, it has no quad: the hand quad is the weapon.
struct EquipQuad { eng::Vec2f at{}, size{}; eng::Color color{1, 1, 1, 1}; bool emissive = false; };
std::vector<EquipQuad> equip_quads(const EquipVisual&, EquipPart, eng::Vec2f at);

// Rear containers only. Shields and stowed two-handed or versatile weapons are not containers;
// back priority reads those from kind, hands, and versatile.
// Seam (container_back_role): one call to manifest_back_role, which reads equip_kit@1
// items[].back_role (quiver | bolt_case | pack | other). A missing field falls back to the content id.
enum class BackRole : uint8_t { None, Quiver, BoltCase, Pack, Other };
BackRole container_back_role(std::string_view content_id, const eng::presentation::EquipArtTable* manifest = nullptr);

// Equipment art is chosen beside the quads, not instead of the attach table.
// West facings mirror x (attach_points) and, when a sprite exists, mirror the
// sprite about its pivot. se is toward; ne is away. A null sprite means the
// caller draws equip_quads, which is today's picture for an unmapped item.
// HipMain and HipOff both look up the `hip` part. Neck looks up `neck`, toward only.
struct EquipArtChoice {
    const eng::presentation::EquipArtSprite* sprite = nullptr;
    bool mirror_x = false;
};
// `size_bin` empty is medium. A non-medium bin falls back to medium inside the table,
// then to the kind default, then to no sprite.
EquipArtChoice equip_art_choice(const eng::presentation::EquipArtTable* table, const EquipVisual&, EquipPart, Facing,
                                std::string_view size_bin);
// Pivot pixel, in sprite px from the top-left, placed on the token anchor + `at`.
inline eng::Vec2f equip_sprite_anchor(int pivot_x, int pivot_y, eng::Vec2f at) {
    return {float(pivot_x) - at.x, float(pivot_y) - at.y};
}
// The flame is drawn (and the sim publishes a glow) only when the light is lit and sheds light: the same test as glows_from.
inline bool sheds_light(const EquipVisual& e) { return e.lit && (e.bright_ft > 0 || e.dim_ft > 0); }

} // namespace slice
