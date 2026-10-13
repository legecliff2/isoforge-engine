#pragma once
// The live sample set: roots {content/packs}, requested {sample.vertical_slice} (srd51.core comes
// in through depends). Resolved once per test binary.
#include <filesystem>
#include <string>

#include "assets/pack_set.hpp"

inline const eng::assets::PackSet& sample_set() {
    static const eng::assets::PackSet s = [] {
        const std::filesystem::path roots[] = {std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs"};
        const std::string req[] = {"sample.vertical_slice"};
        return eng::assets::PackSet::resolve(roots, req);
    }();
    return s;
}
