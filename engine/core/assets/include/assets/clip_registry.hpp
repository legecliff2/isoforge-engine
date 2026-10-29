#pragma once
// Clip registry + alias table (Engine Core M0). Kit JSON under a pack's art/ directory.
// Membership: every alias target is a clip id in that kit. Not a migrated content schema
// and not part of any sim hash. Drawing stays with Render & Platform.
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/schema_registry.hpp"

namespace eng::assets {

struct Clip {
    std::string id;
    int frames = 0;     // 0 = not authored
    int fps = 0;
    std::string mode;   // "" | "loop" | "once" | "hold_last"
};

struct ClipRegistry {
    std::string id;      // "paperdoll" or "standin.<bin>"
    std::string kind;    // "paperdoll" | "standin"
    std::string bin;     // stand-in size bin; empty for paperdoll
    std::vector<Clip> clips;
    // Logical name -> draw clip. Keys are stored with '-' turned into '_'.
    std::map<std::string, std::string, std::less<>> aliases;
    std::string fallback;   // clip id used when the name is neither a clip nor an alias; empty = none

    bool has_clip(std::string_view id) const;
    // A clip id, an alias (hyphens match underscores), or `fallback`. nullopt when none of those hit.
    std::optional<std::string> resolve(std::string_view name) const;
};

struct ClipFile {
    std::vector<ClipRegistry> kits;
};

// Error string, or nullopt. `out` is replaced only on success.
std::optional<std::string> load_clip_file(const Json& doc, ClipFile& out);
std::optional<std::string> load_clip_file(const std::filesystem::path& path, ClipFile& out);

} // namespace eng::assets
