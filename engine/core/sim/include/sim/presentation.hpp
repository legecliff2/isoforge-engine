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

// Resolved presentation for one creature. JSON shape is visual{kind,kit,standin_bin}
// plus optional art_root and size_short. `sprite` is the authored presentation.sprite.
// `kit` is what Render & Platform loads: visual.kit, else presentation.sprite, else
// "paperdoll" or "standin.<standin_bin>". Unbinned (huge / gargantuan with no authored
// bin) leaves kit empty.
struct CreatureVisual {
    VisualKind kind = VisualKind::Paperdoll;
    std::string standin_bin;   // tiny | small | medium | large; empty for paperdoll and unbinned
    std::string art_root;      // pack-relative directory; empty when the file names none
    std::string sprite;        // presentation.sprite as authored
    std::string kit;
    bool size_short = false;   // authored flag; ignored while kSizeShortRuntime is false
};

} // namespace eng::sim
