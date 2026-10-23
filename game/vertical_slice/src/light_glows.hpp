#pragma once
// LightView sources -> generic iso2d::GlowSource list (render side, presentation only).
// Every lit source becomes one glow: creature-carried sources are anchored to the carrier's token
// so the halo follows its drawn position; static, item and on-the-ground sources stay at the
// published tile. Nothing here knows where sources come from: the interim pc_torches, sim
// attached lights (lighting phase 3) and static chunk lights all arrive as LightSourceView.
#include <vector>

#include "iso2d/light_glow.hpp"
#include "view_model.hpp"

namespace slice {

std::vector<eng::iso2d::GlowSource> glows_from(const LightView& L, const eng::iso2d::GlowPalette& palette);
// Same, into `out` (cleared first; keeps its capacity across frames).
void glows_from(const LightView& L, const eng::iso2d::GlowPalette& palette, std::vector<eng::iso2d::GlowSource>& out);

} // namespace slice
