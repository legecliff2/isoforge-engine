#pragma once
// R-M0 creature visual on ActorView. Core M0's document field, when present:
//   "visual": { "kind": "paperdoll"|"standin", "kit": "<id>", "standin_bin": "tiny"|"small"|"medium"|"large" }
// Until that schema lands, presentation.sprite is the kit id (fallback standin.<bin>).
// Layered paper-doll drawing is M1: kind paperdoll is recorded and the pawn stays.
#include "view_model.hpp"

#include <string>
#include <string_view>

namespace slice {

struct VisualSource {
    std::string sprite;        // presentation.sprite
    std::string kind;          // "paperdoll" | "standin" | ""
    std::string kit;
    std::string standin_bin;
};

// huge and gargantuan share the large canvas. Anything else unknown is medium.
inline std::string_view standin_bin_for_size(std::string_view size) noexcept {
    if (size == "tiny" || size == "small" || size == "medium" || size == "large") return size;
    if (size == "huge" || size == "gargantuan") return "large";
    return "medium";
}

inline bool known_standin_bin(std::string_view bin) noexcept {
    return bin == "tiny" || bin == "small" || bin == "medium" || bin == "large";
}

// `size_name` is the stat-block size (tiny..gargantuan). A monster with no source becomes
// a stand-in. A character with no source stays VisualKind::None (procedural pawn).
inline void apply_actor_visual(ActorView& a, bool monster, std::string_view size_name, const VisualSource* src) {
    const std::string size_bin(standin_bin_for_size(size_name));
    const std::string kind = src ? src->kind : std::string();
    std::string kit = src ? src->kit : std::string();
    std::string bin = src && !src->standin_bin.empty() ? src->standin_bin : size_bin;
    if (!known_standin_bin(bin)) bin = size_bin;
    if (kit.empty() && src && !src->sprite.empty()) kit = src->sprite;
    // presentation.sprite "standin.<bin>" names the canvas when visual.standin_bin is absent.
    if (src && src->standin_bin.empty() && kit.rfind("standin.", 0) == 0) {
        const std::string named = kit.substr(8);
        if (known_standin_bin(named)) bin = named;
    }
    if (kit.empty()) kit = "standin." + bin;

    if (kind == "paperdoll") {
        a.visual_kind = VisualKind::Paperdoll;
        a.kit = std::move(kit);
        a.standin_bin.clear();
        return;
    }
    if (kind == "standin" || (monster && kind.empty())) {
        a.visual_kind = VisualKind::Standin;
        a.kit = std::move(kit);
        a.standin_bin = std::move(bin);
    }
}

} // namespace slice
