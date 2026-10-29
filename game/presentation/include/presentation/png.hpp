#pragma once
// SDL-free PNG → RGBA8. Presentation only (game/presentation). iso2d does not decode files.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace eng::presentation {

struct ImageRgba {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba;   // row-major, 4 bytes per pixel, straight alpha

    uint8_t* px(uint32_t x, uint32_t y) { return rgba.data() + (size_t(y) * w + x) * 4; }
    const uint8_t* px(uint32_t x, uint32_t y) const { return rgba.data() + (size_t(y) * w + x) * 4; }
    bool empty() const { return w == 0 || h == 0 || rgba.size() != size_t(w) * h * 4; }
};

// Non-interlaced 8-bit greyscale, RGB, greyscale+alpha, or RGBA. nullopt on anything else
// (indexed, interlaced, a bad zlib stream, a failed Adler-32).
std::optional<ImageRgba> decode_png(std::span<const std::byte> bytes);

// Missing-frame stand-in: 4px cells of magenta (#FF00FF) and outline grey (#1E2228).
ImageRgba magenta_checker(uint32_t w, uint32_t h);

// Art mirror for even widths (MONSTER_STANDIN_SPEC): flip, then +1px so column 0 is clear
// and pivot x = w/2 stays on itself. A plain UV swap does not do the 1px shift.
ImageRgba hflip_pivot(const ImageRgba& src);

} // namespace eng::presentation
