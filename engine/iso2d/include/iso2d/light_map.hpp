#pragma once
// Render-side light maps (LIGHTING.md §10). View-only: consumes the per-tile results the sim
// publishes in its LightFrame (copied into the game's ViewModel); never computes gameplay light.
//
// One R8_UNORM 32x32 texture per resident chunk, created / destroyed from StreamDelta-style
// added / removed lists, updated by dirty rect. v0 shades through sprite tints (light_tint);
// the textures are kept current for the later shader path.
#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <span>

#include "base/math.hpp"
#include "base/present_math.hpp"
#include "render/device.hpp"

namespace eng::iso2d {

inline constexpr int kLightChunk = 32;   // == world::kChunkSize (iso2d doesn't depend on world)
inline constexpr int kLightTiles = kLightChunk * kLightChunk;

struct LightChunkKey {
    int32_t x = 0, y = 0;
    int16_t level = 0;
    auto operator<=>(const LightChunkKey&) const = default;
};

enum class LightLevel : uint8_t { Dark = 0, Dim = 1, Bright = 2 };

// Packed sample byte (R8 texel): bits 0-1 level, bit 2 magical_dark, bit 3 sunlight,
// bit 4 obscured_light, bit 5 obscured_heavy (sim LightFlags bits 0-3 shifted by 2).
namespace light_bits {
inline constexpr uint8_t kMagicalDark = 1u << 2, kSunlight = 1u << 3, kObscuredLight = 1u << 4,
                         kObscuredHeavy = 1u << 5;
}
constexpr uint8_t pack_light(LightLevel l, uint8_t sim_flags) noexcept {
    return uint8_t(uint8_t(l) & 3u) | uint8_t((sim_flags & 0x0Fu) << 2);
}
constexpr LightLevel light_level(uint8_t packed) noexcept {
    const uint8_t l = packed & 3u;
    return l >= 2 ? LightLevel::Bright : LightLevel(l);
}

// Fog of war as shown to one team (from the sim's seen_now / explored bitmasks).
enum class Fog : uint8_t { Visible, Remembered, Unexplored };
constexpr Fog fog_of(bool seen_now, bool explored) noexcept {
    return seen_now ? Fog::Visible : explored ? Fog::Remembered : Fog::Unexplored;
}

// Sprite tint for a tile. Presentation only. Guarantees (LIGHTING.md §10): a Dark tile is never
// shown lit (its multiplier is the darkest), and non-visible tiles show no current light at all.
Color light_tint(uint8_t packed, Fog fog) noexcept;

class LightMaps {
public:
    explicit LightMaps(render::IRenderDevice&);
    ~LightMaps();
    LightMaps(const LightMaps&) = delete;
    LightMaps& operator=(const LightMaps&) = delete;

    // Mirror the resident set: create a Dark-initialised map per added chunk, destroy per removed.
    void on_stream_delta(std::span<const LightChunkKey> added, std::span<const LightChunkKey> removed);
    // Copy `samples` (row-major 32x32 packed bytes) into chunk `k` and upload `dirty` (chunk-local
    // tiles, clipped to the chunk). Unknown chunks are ignored (returns false). Empty rect = no-op.
    bool update(LightChunkKey k, std::span<const uint8_t, kLightTiles> samples, RectI dirty);

    render::TextureId texture(LightChunkKey) const;   // invalid handle if not resident
    uint8_t sample(LightChunkKey, Vec2i local) const;  // 0 (Dark) if not resident
    size_t chunks() const noexcept { return maps_.size(); }
    uint64_t bytes_uploaded() const noexcept { return bytes_uploaded_; }
    uint32_t uploads() const noexcept { return uploads_; }

private:
    struct Map { render::TextureId tex; std::array<uint8_t, kLightTiles> cpu{}; };
    render::IRenderDevice& dev_;
    std::map<LightChunkKey, Map> maps_;
    uint64_t bytes_uploaded_ = 0;
    uint32_t uploads_ = 0;
};

} // namespace eng::iso2d
