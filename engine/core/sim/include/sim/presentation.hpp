#pragma once
// Presentation facts copied off a stat block for Render & Platform. Not part of Combat::hash,
// log_hash, or chunk content_hash. The renderer reads them; it does not write sim state.
#include <cstdint>
#include <string>

namespace eng::sim {

// M0 does not apply size_short. M1: prefer doll_torso / doll_head `_<facing>_short` plates
// (shared pose keys); scale 0.75–0.85 (default 0.8) about the feet pivot is the fallback only.
// Huge and gargantuan stand-in bins are also deferred (no M0 art bin).
inline constexpr bool kSizeShortRuntime = false;

enum class VisualKind : uint8_t { Paperdoll, Standin, Unbinned };

// Resolved presentation for one creature. `sprite` is the authored presentation.sprite.
// `kit_id` is what Render & Platform looks up: sprite when set, otherwise "paperdoll" or
// "standin.<bin>". Unbinned (huge / gargantuan with no authored bin) leaves kit_id empty.
struct CreatureVisual {
    VisualKind kind = VisualKind::Paperdoll;
    std::string bin;        // tiny | small | medium | large; empty for paperdoll and unbinned
    std::string art_root;   // pack-relative directory; empty when the file names none
    std::string sprite;
    std::string kit_id;
    bool size_short = false;   // authored flag; ignored while kSizeShortRuntime is false
};

} // namespace eng::sim
