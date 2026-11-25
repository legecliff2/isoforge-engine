#include "equip_visuals.hpp"

#include "presentation/equip_art.hpp"
#include "presentation/standin.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace slice {

namespace {

void mirror_x(AttachPoints& p) {
    p.main_hand.x = -p.main_hand.x;
    p.off_hand.x = -p.off_hand.x;
    p.back.x = -p.back.x;
    p.hip_main.x = -p.hip_main.x;
    p.hip_off.x = -p.hip_off.x;
    p.neck.x = -p.neck.x;
}

bool read_point(const nlohmann::json& obj, const char* key, eng::Vec2f& dst) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() != 2) return false;
    if (!(*it)[0].is_number() || !(*it)[1].is_number()) return false;
    dst.x = (*it)[0].get<float>();
    dst.y = (*it)[1].get<float>();
    return true;
}

void apply_facing(const nlohmann::json& root, const char* key, AttachPoints& pts) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_object()) return;
    read_point(*it, "main", pts.main_hand);
    read_point(*it, "off", pts.off_hand);
    read_point(*it, "back", pts.back);
    read_point(*it, "hip-main", pts.hip_main);
    read_point(*it, "hip-off", pts.hip_off);
    read_point(*it, "neck", pts.neck);
}

// A readable object wins, even when it sets no points (those stay shared). Anything else is "no file".
std::optional<BodyAttach> read_attach_file(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
    std::ifstream in(file);
    if (!in) return std::nullopt;
    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!root.is_object()) return std::nullopt;
    BodyAttach body = shared_body_attach();
    apply_facing(root, "se", body.se);
    apply_facing(root, "ne", body.ne);
    body.loaded = true;
    return body;
}

std::string_view item_slug(std::string_view id) {
    const auto dot = id.rfind('.');
    return dot == std::string_view::npos ? id : id.substr(dot + 1);
}

// Seam: equip_kit@1 items[].back_role, whole content id. Unknown or absent is not a role.
std::optional<BackRole> manifest_back_role(const eng::presentation::EquipArtTable* table, std::string_view content_id) {
    if (!table) return std::nullopt;
    const std::optional<std::string_view> raw = table->back_role(content_id);
    if (!raw) return std::nullopt;
    if (*raw == "quiver") return BackRole::Quiver;
    if (*raw == "bolt_case") return BackRole::BoltCase;
    if (*raw == "pack") return BackRole::Pack;
    if (*raw == "other") return BackRole::Other;
    return std::nullopt;
}

bool is_crossbow_id(std::string_view id) { return id.find("crossbow") != std::string_view::npos; }
bool is_bow_id(std::string_view id) { return !is_crossbow_id(id) && id.find("bow") != std::string_view::npos; }

enum class PouchKind : uint8_t { None, Belt, Component };

PouchKind pouch_kind(std::string_view id) {
    const std::string_view slug = item_slug(id);
    if (slug == "component_pouch") return PouchKind::Component;
    if (slug == "pouch") return PouchKind::Belt;
    return PouchKind::None;
}

bool is_covered(const ActorView& a, uint32_t item) {
    return std::find(a.covered_equipment.begin(), a.covered_equipment.end(), item) != a.covered_equipment.end();
}

int back_rank(const EquipVisual& e, bool bow_wielded, bool crossbow_wielded,
              const eng::presentation::EquipArtTable* manifest) {
    const BackRole role = container_back_role(e.content_id, manifest);
    if ((role == BackRole::Quiver && bow_wielded) || (role == BackRole::BoltCase && crossbow_wielded)) return 1;
    if (e.kind == EquipKind::Shield) return 2;   // slot is already Back: the sim said stowed
    if (e.kind == EquipKind::Weapon && (e.hands >= 2 || e.versatile)) return 3;
    if (role == BackRole::Pack) return 4;
    if (role == BackRole::Quiver || role == BackRole::BoltCase) return 5;
    return 6;
}

const EquipVisual* choose_back(const ActorView& a, const eng::presentation::EquipArtTable* manifest) {
    bool bow = false, crossbow = false;
    for (const EquipVisual& e : a.equipment) {
        if (is_covered(a, e.item)) continue;
        if (e.slot != AttachSlot::MainHand && e.slot != AttachSlot::OffHand) continue;
        if (e.kind != EquipKind::Weapon) continue;
        if (is_crossbow_id(e.content_id)) crossbow = true;
        else if (is_bow_id(e.content_id)) bow = true;
    }
    const EquipVisual* best = nullptr;
    int rank = 0;
    for (const EquipVisual& e : a.equipment) {
        if (is_covered(a, e.item) || e.slot != AttachSlot::Back) continue;
        const int r = back_rank(e, bow, crossbow, manifest);
        if (!best || r < rank || (r == rank && e.item < best->item)) {
            best = &e;
            rank = r;
        }
    }
    return best;
}

struct HipAssign {
    const EquipVisual* sheath_main = nullptr;
    const EquipVisual* sheath_off = nullptr;
    const EquipVisual* pouch = nullptr;
    bool pouch_on_main = false;
};

const EquipVisual* lower_id(const EquipVisual* cur, const EquipVisual& e) {
    return (!cur || e.item < cur->item) ? &e : cur;
}

HipAssign assign_hips(const ActorView& a) {
    const EquipVisual* main_w = nullptr;
    const EquipVisual* off_w = nullptr;
    const EquipVisual* component = nullptr;
    const EquipVisual* belt = nullptr;
    for (const EquipVisual& e : a.equipment) {
        if (is_covered(a, e.item)) continue;
        if (e.kind == EquipKind::Weapon && e.slot == AttachSlot::MainHand) main_w = lower_id(main_w, e);
        else if (e.kind == EquipKind::Weapon && e.slot == AttachSlot::OffHand) off_w = lower_id(off_w, e);
        else if (e.slot == AttachSlot::Hip) {
            if (pouch_kind(e.content_id) == PouchKind::Component) component = lower_id(component, e);
            else if (pouch_kind(e.content_id) == PouchKind::Belt) belt = lower_id(belt, e);
        }
    }
    if (main_w && off_w && main_w->item == off_w->item) off_w = nullptr;
    const bool twf = main_w && off_w;
    HipAssign h;
    h.sheath_main = main_w;
    h.sheath_off = twf ? off_w : nullptr;
    // Two sheaths hide the pouch. No sheath: the pouch takes hip-main. One sheath: hip-off.
    // component_pouch is the component pouch. The belt pouch fills that anchor only when no component pouch is carried.
    if (!twf) {
        h.pouch = component ? component : belt;
        h.pouch_on_main = h.sheath_main == nullptr;
    }
    return h;
}

const EquipVisual* choose_neck(const ActorView& a) {
    const EquipVisual* best = nullptr;
    for (const EquipVisual& e : a.equipment) {
        if (is_covered(a, e.item) || e.slot != AttachSlot::Neck) continue;
        best = lower_id(best, e);
    }
    return best;
}

void push_hip(std::vector<EquipLayer>& out, const EquipVisual& e, EquipPart part, Facing facing, eng::Vec2f at) {
    EquipLayer l;
    l.item = &e;
    l.part = part;
    l.z = worn_z(AttachSlot::Hip, facing);   // both hips; far-side hip-off on profile is the TODO on worn_z
    l.at = at;
    out.push_back(l);
}

} // namespace

AttachPoints attach_points(const BodyAttach& body, Facing facing) {
    AttachPoints p = facing_shows_back(facing) ? body.ne : body.se;
    if (facing == Facing::SouthWest || facing == Facing::NorthWest) mirror_x(p);
    return p;
}

BodyAttach load_body_attach(const std::filesystem::path& standin_root, std::string_view monster_id, std::string_view bin) {
    const bool known = eng::presentation::standin_canvas(bin).w != 0;
    if (known) {
        const std::string dir = eng::presentation::monster_art_dir(monster_id);
        if (!dir.empty()) {
            if (auto body = read_attach_file(standin_root / "srd" / dir / std::string(bin) / "attach.json")) return *body;
        }
        if (auto body = read_attach_file(standin_root / "frames" / std::string(bin) / "attach.json")) return *body;
    }
    return shared_body_attach();
}

BackRole container_back_role(std::string_view content_id, const eng::presentation::EquipArtTable* manifest) {
    if (const std::optional<BackRole> from_manifest = manifest_back_role(manifest, content_id)) return *from_manifest;
    const std::string_view slug = item_slug(content_id);
    if (slug == "quiver") return BackRole::Quiver;
    if (slug == "case_crossbow_bolt") return BackRole::BoltCase;
    if (slug == "backpack") return BackRole::Pack;
    return BackRole::None;
}

std::vector<EquipLayer> equip_layers(const ActorView& a, const BodyAttach& body,
                                  const eng::presentation::EquipArtTable* manifest) {
    const bool west = a.facing == Facing::SouthWest || a.facing == Facing::NorthWest;
    const AttachPoints ap = attach_points(body, a.facing);
    const EquipVisual* back = choose_back(a, manifest);
    const HipAssign hip = assign_hips(a);
    const EquipVisual* neck = facing_shows_neck(a.facing) ? choose_neck(a) : nullptr;
    std::vector<EquipLayer> out;
    out.push_back({EquipPart::Body, kZBody, nullptr, {0, 0}});
    for (const EquipVisual& e : a.equipment) {
        if (is_covered(a, e.item)) continue;
        EquipLayer l;
        l.item = &e;
        if (&e == back) {
            l.part = EquipPart::Back;
            l.z = worn_z(AttachSlot::Back, a.facing);
            l.at = ap.back;
        } else if (&e == hip.pouch) {
            l.part = hip.pouch_on_main ? EquipPart::HipMain : EquipPart::HipOff;
            l.z = worn_z(AttachSlot::Hip, a.facing);
            l.at = hip.pouch_on_main ? ap.hip_main : ap.hip_off;
        } else if (&e == neck) {
            l.part = EquipPart::Neck;
            l.z = kZWornFront;   // in front of the body, behind both hands
            l.at = ap.neck;
        } else if (e.slot == AttachSlot::MainHand || e.slot == AttachSlot::OffHand) {
            const bool main = e.slot == AttachSlot::MainHand;
            l.at = main ? ap.main_hand : ap.off_hand;
            if (e.kind == EquipKind::Light) { l.part = EquipPart::HeldLight; l.z = kZLight; }
            else {
                l.part = main ? EquipPart::MainHand : EquipPart::OffHand;
                l.z = (main != west) ? kZNearHand : kZFarHand;
            }
        } else {
            continue;   // the back, hip, or neck item that lost its anchor
        }
        out.push_back(l);
    }
    if (hip.sheath_main) push_hip(out, *hip.sheath_main, EquipPart::HipMain, a.facing, ap.hip_main);
    if (hip.sheath_off) push_hip(out, *hip.sheath_off, EquipPart::HipOff, a.facing, ap.hip_off);
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
                     kGrey{0.6f, 0.6f, 0.6f, 1.f}, kFlame{1.f, 0.92f, 0.55f, 1.f}, kLeather{0.45f, 0.32f, 0.22f, 1.f};
} // namespace

std::vector<EquipQuad> equip_quads(const EquipVisual& e, EquipPart part, eng::Vec2f at) {
    std::vector<EquipQuad> q;
    const bool wooden = has(e.content_id, "bow") || has(e.content_id, "club") || has(e.content_id, "staff");
    if (part == EquipPart::Back) {
        // A shield shows its rim, a weapon its hilt / shaft. Rear gear (pack, quiver, bolt case) is a bag.
        if (e.kind == EquipKind::Shield) q.push_back({{0.f, at.y + 6.f}, {24.f, 18.f}, kWood});
        else if (e.kind == EquipKind::Other) q.push_back({{at.x, at.y + 4.f}, {14.f, 16.f}, kLeather});
        else q.push_back({at, {3.f, 24.f}, wooden ? kWood : kSteel});
        return q;
    }
    if (part == EquipPart::HipMain || part == EquipPart::HipOff) {
        // Sheath art is the weapon's hip sprite. No sprite, no quad: the hand is the unmapped weapon.
        if (e.kind == EquipKind::Weapon) return q;
        q.push_back({{at.x, at.y + 2.f}, {8.f, 7.f}, kLeather});   // belt pouch
        return q;
    }
    if (part == EquipPart::Neck) {
        q.push_back({{at.x, at.y + 2.f}, {5.f, 6.f}, kBrass});
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

namespace {
const char* kind_name(EquipKind k) {
    switch (k) {
    case EquipKind::Weapon: return "weapon";
    case EquipKind::Shield: return "shield";
    case EquipKind::Light: return "light";
    case EquipKind::Other: return "other";
    }
    return "other";
}
eng::presentation::EquipArtPart art_part(EquipPart part) {
    switch (part) {
    case EquipPart::Back: return eng::presentation::EquipArtPart::Back;
    case EquipPart::HipMain: return eng::presentation::EquipArtPart::Hip;
    case EquipPart::HipOff: return eng::presentation::EquipArtPart::Hip;
    case EquipPart::OffHand: return eng::presentation::EquipArtPart::OffHand;
    case EquipPart::HeldLight: return eng::presentation::EquipArtPart::HeldLight;
    case EquipPart::MainHand: return eng::presentation::EquipArtPart::MainHand;
    case EquipPart::Neck: return eng::presentation::EquipArtPart::Neck;
    case EquipPart::Body: return eng::presentation::EquipArtPart::MainHand;
    }
    return eng::presentation::EquipArtPart::MainHand;
}
} // namespace

EquipArtChoice equip_art_choice(const eng::presentation::EquipArtTable* table, const EquipVisual& e, EquipPart part,
                                Facing facing, std::string_view size_bin) {
    EquipArtChoice choice;
    choice.mirror_x = facing == Facing::SouthWest || facing == Facing::NorthWest;
    if (!table || part == EquipPart::Body) return choice;
    if (part == EquipPart::Neck && !facing_shows_neck(facing)) return choice;   // away: hidden, no neck.ne lookup
    eng::presentation::EquipArtQuery query;
    query.content_id = e.content_id;
    query.kind = kind_name(e.kind);
    query.part = art_part(part);
    query.view = facing_shows_back(facing) ? eng::presentation::EquipArtView::Away : eng::presentation::EquipArtView::Toward;
    query.size_bin = size_bin;
    choice.sprite = table->find(query);
    return choice;
}

} // namespace slice
