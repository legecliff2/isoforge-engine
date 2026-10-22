#include "equip_visuals.hpp"

#include <algorithm>
#include <string_view>

namespace slice {

AttachPoints attach_points(Facing f) {
    AttachPoints p;
    if (f == Facing::SouthWest || f == Facing::NorthWest) {
        p.main_hand.x = -p.main_hand.x;
        p.off_hand.x = -p.off_hand.x;
        p.back.x = -p.back.x;
    }
    return p;
}

std::vector<EquipLayer> equip_layers(const ActorView& a) {
    const bool west = a.facing == Facing::SouthWest || a.facing == Facing::NorthWest;
    const AttachPoints ap = attach_points(a.facing);
    std::vector<EquipLayer> out;
    out.push_back({EquipPart::Body, kZBody, nullptr, {0, 0}});
    for (const EquipVisual& e : a.equipment) {
        EquipLayer l;
        l.item = &e;
        if (e.slot == AttachSlot::Back) {
            l.part = EquipPart::Back; l.z = kZBack; l.at = ap.back;
        } else {
            const bool main = e.slot == AttachSlot::MainHand;
            l.at = main ? ap.main_hand : ap.off_hand;
            if (e.kind == EquipKind::Light) { l.part = EquipPart::HeldLight; l.z = kZLight; }
            else {
                l.part = main ? EquipPart::MainHand : EquipPart::OffHand;
                l.z = (main != west) ? kZNearHand : kZFarHand;
            }
        }
        out.push_back(l);
    }
    std::stable_sort(out.begin(), out.end(), [](const EquipLayer& x, const EquipLayer& y) {
        if (x.z != y.z) return x.z < y.z;
        const uint32_t xi = x.item ? x.item->item : 0, yi = y.item ? y.item->item : 0;
        return xi < yi;
    });
    return out;
}


namespace {
bool has(std::string_view id, std::string_view w) { return id.find(w) != std::string_view::npos; }
constexpr eng::Color kSteel{0.78f, 0.80f, 0.85f, 1.f}, kWood{0.50f, 0.33f, 0.18f, 1.f}, kBrass{0.80f, 0.63f, 0.25f, 1.f},
                     kGrey{0.6f, 0.6f, 0.6f, 1.f}, kFlame{1.f, 0.92f, 0.55f, 1.f};
} // namespace

std::vector<EquipQuad> equip_quads(const EquipVisual& e, EquipPart part, eng::Vec2f at) {
    std::vector<EquipQuad> q;
    const bool wooden = has(e.content_id, "bow") || has(e.content_id, "club") || has(e.content_id, "staff");
    if (part == EquipPart::Back) {
        // Slung behind the body: a shield shows its rim either side, a weapon its hilt / shaft over the shoulder.
        if (e.kind == EquipKind::Shield) q.push_back({{0.f, at.y + 6.f}, {24.f, 18.f}, kWood});
        else q.push_back({at, {3.f, 24.f}, wooden ? kWood : kSteel});
        return q;
    }
    switch (e.kind) {
    case EquipKind::Weapon:
        q.push_back({{at.x, at.y - 1.f}, {3.f, e.hands > 1 ? 22.f : 16.f}, wooden ? kWood : kSteel});   // blade / shaft, up
        q.push_back({{at.x, at.y}, {7.f, 2.f}, kWood});                                                   // grip / guard
        break;
    case EquipKind::Shield:
        q.push_back({{at.x, at.y + 6.f}, {11.f, 13.f}, kWood});
        q.push_back({{at.x, at.y + 1.f}, {3.f, 3.f}, kSteel});   // boss
        break;
    case EquipKind::Light: {
        const bool torch = has(e.content_id, "torch");
        const eng::Vec2f body = torch ? eng::Vec2f{3.f, 10.f} : eng::Vec2f{6.f, 7.f};
        q.push_back({{at.x, at.y + 2.f}, body, torch ? kWood : kBrass});
        if (sheds_light(e)) {   // lit flame, consistent with the sim glow: dim-only (hooded) is a small faint ember
            const bool dim_only = e.bright_ft == 0;
            const float f = dim_only ? 2.f : torch ? 5.f : 4.f;
            eng::Color c = kFlame;
            if (dim_only) c.a = 0.6f;
            q.push_back({{at.x, at.y + 2.f - body.y}, {f, f}, c, true});   // on top of the torch / lantern
        }
        break;
    }
    case EquipKind::Other: q.push_back({at, {5.f, 5.f}, kGrey}); break;
    }
    return q;
}

} // namespace slice
