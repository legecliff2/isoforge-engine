#pragma once
// Whole-body monster stand-ins (R-M0). One kit directory: clips.json + mon_* frames.
// Paper-doll layering is M1 and is not loaded here.
#include "presentation/png.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
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

// Frame index at t seconds. Time is truncated to milliseconds, then `ms * fps / 1000`,
// so 10 fps at 0.1s is frame 1. loop wraps. once and hold_last clamp to the last frame.
int clip_frame(const ClipClock& clock, double t_sec);
// loop: never. once: after the last frame's slot (`step >= frames`). hold_last: once the
// last frame is showing (`step >= frames - 1`).
bool clip_finished(const ClipClock& clock, double t_sec);

// M0 alias stub (Core owns the registry). hard-land and hard_land → prone_idle;
// climb and crawl → walk; hide → idle; melee → attack. Unknown names have no alias.
std::string_view clip_alias(std::string_view name);

// Missing clip → its alias when that clip exists → idle.
// `has` is "this kit contains a clip with that id".
std::string resolve_clip(std::string_view requested, const std::function<bool(std::string_view)>& has);

struct StandinFrameView {
    const ImageRgba* image = nullptr;
    int pivot_x = 0, pivot_y = 0;
    int index = 0;
    bool missing = false;
    std::string_view facing;
    std::string_view clip;
};

struct StandinPose {
    int frame = -1;   // index for frame_at, or -1 when the bin has no such facing
    int index = 0;    // index within the clip
    ClipClock clock{};
    std::string clip;   // resolved id
};

class MonsterStandinKit {
public:
    // `bin` is tiny|small|medium|large. nullopt when clips.json is missing or that bin
    // has no clips. A missing frame file is a magenta checker (missing_count() > 0), not a failure.
    static std::optional<MonsterStandinKit> load(const std::filesystem::path& root, std::string_view bin);

    const std::string& bin() const { return bin_; }
    std::size_t frame_count() const { return frames_.size(); }
    std::size_t missing_count() const;
    bool has_clip(std::string_view name) const;
    StandinFrameView frame_at(std::size_t i) const;
    // facing: se|sw|ne|nw (anything else uses se). Clip ids go through resolve_clip.
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
    std::vector<std::pair<std::string, ClipClock>> clocks_;   // clip id → clock (one per id)
    std::vector<std::pair<std::string, int>> lookup_;         // "facing|clip|index" → frames_ index
    const ClipClock* clock_of(std::string_view clip) const;
    int lookup(std::string_view facing, std::string_view clip, int index) const;
};

} // namespace eng::presentation
