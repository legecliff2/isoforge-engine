#pragma once
// Whole-body monster stand-ins (R-M0). Frames live under the packed kit directory.
// Clip ids, frame counts, fps, modes, and aliases are Engine Core's registry
// (content/packs/srd51.core/art/clips.json). This module does not resolve aliases.
#include "presentation/png.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::presentation {

enum class ClipMode : uint8_t { Loop, Once, HoldLast };

struct ClipClock {
    int frames = 1;
    int fps = 1;
    ClipMode mode = ClipMode::Loop;
};

// Frame index at t seconds. Time is truncated to milliseconds, then `ms * fps / 1000`.
// loop wraps. once and hold_last clamp to the last frame.
int clip_frame(const ClipClock& clock, double t_sec);
// loop: never. once: after the last frame's slot (`step >= frames`). hold_last: once the
// last frame is showing (`step >= frames - 1`).
bool clip_finished(const ClipClock& clock, double t_sec);

// Locked canvases (MONSTER_STANDIN_SPEC). Zero size when `bin` is not tiny|small|medium|large.
struct StandinCanvas {
    uint32_t w = 0, h = 0;
    int pivot_x = 0, pivot_y = 0;
};
StandinCanvas standin_canvas(std::string_view bin);

// One clip the kit should load. `id` is already resolved (Core's ClipRegistry::resolve).
struct KitClip {
    std::string id;
    int frames = 1;
    int fps = 1;
    ClipMode mode = ClipMode::Loop;
};

struct StandinFrameView {
    const ImageRgba* image = nullptr;
    int pivot_x = 0, pivot_y = 0;
    int index = 0;
    bool missing = false;
    std::string_view facing;
    std::string_view clip;
};

struct StandinPose {
    int frame = -1;   // index for frame_at, or -1 when that facing/clip was not loaded
    int index = 0;
    ClipClock clock{};
    std::string clip;
};

class MonsterStandinKit {
public:
    // `bin` is tiny|small|medium|large. PNGs are
    // frames/{bin}/{facing}/mon_{bin}_{facing}_{clip}_{ff}.png.
    // A missing file is a magenta checker. nullopt when `bin` is unknown or `clips` is empty.
    static std::optional<MonsterStandinKit> load(const std::filesystem::path& root, std::string_view bin,
                                                  std::span<const KitClip> clips);

    const std::string& bin() const { return bin_; }
    std::size_t frame_count() const { return frames_.size(); }
    std::size_t missing_count() const;
    bool has_clip(std::string_view name) const;
    StandinFrameView frame_at(std::size_t i) const;
    // facing: se|sw|ne|nw (anything else uses se). `clip` must already be resolved.
    StandinPose pose(std::string_view facing, std::string_view clip, double t_sec) const;

private:
    struct Frame {
        std::string facing, clip;
        int index = 0;
        ImageRgba image;
        int pivot_x = 0, pivot_y = 0;
        bool missing = false;
    };
    std::string bin_;
    std::vector<Frame> frames_;
    std::vector<std::pair<std::string, ClipClock>> clocks_;
    std::vector<std::pair<std::string, int>> lookup_;
    const ClipClock* clock_of(std::string_view clip) const;
    int lookup(std::string_view facing, std::string_view clip, int index) const;
};

} // namespace eng::presentation
