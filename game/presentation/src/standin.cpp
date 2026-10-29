#include "presentation/standin.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>

namespace eng::presentation {

namespace {

int64_t clip_step(const ClipClock& c, double t_sec) {
    if (!(t_sec > 0.0)) return 0;
    const int fps = c.fps < 1 ? 1 : c.fps;
    const double ms = std::floor(t_sec * 1000.0);
    if (!(ms > 0.0)) return 0;
    const double capped = ms > 1.0e12 ? 1.0e12 : ms;
    return int64_t(capped) * int64_t(fps) / 1000;
}

std::optional<ImageRgba> read_png(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return std::nullopt;
    std::vector<char> bytes(static_cast<size_t>(n));
    in.seekg(0);
    in.read(bytes.data(), n);
    if (!in) return std::nullopt;
    return decode_png(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
}

std::string lookup_key(std::string_view facing, std::string_view clip, int index) {
    std::string k;
    k.reserve(facing.size() + clip.size() + 8);
    k.append(facing);
    k.push_back('|');
    k.append(clip);
    k.push_back('|');
    k.append(std::to_string(index));
    return k;
}

const char* norm_facing(std::string_view facing) {
    if (facing == "se" || facing == "sw" || facing == "ne" || facing == "nw") return nullptr;
    return "se";
}

} // namespace

int clip_frame(const ClipClock& clock, double t_sec) {
    const int n = clock.frames < 1 ? 1 : clock.frames;
    const int64_t step = clip_step(clock, t_sec);
    if (clock.mode == ClipMode::Loop) return int(step % n);
    if (step >= n) return n - 1;
    return int(step);
}

bool clip_finished(const ClipClock& clock, double t_sec) {
    if (clock.mode == ClipMode::Loop) return false;
    const int n = clock.frames < 1 ? 1 : clock.frames;
    const int64_t step = clip_step(clock, t_sec);
    if (clock.mode == ClipMode::HoldLast) return step >= int64_t(n - 1);
    return step >= n;
}

StandinCanvas standin_canvas(std::string_view bin) {
    if (bin == "tiny") return {16, 16, 8, 15};
    if (bin == "small") return {24, 32, 12, 31};
    if (bin == "medium") return {32, 48, 16, 47};
    if (bin == "large") return {48, 64, 24, 63};
    return {};
}

std::size_t MonsterStandinKit::missing_count() const {
    std::size_t n = 0;
    for (const Frame& f : frames_) n += f.missing ? 1 : 0;
    return n;
}

bool MonsterStandinKit::has_clip(std::string_view name) const { return clock_of(name) != nullptr; }

const ClipClock* MonsterStandinKit::clock_of(std::string_view clip) const {
    for (const auto& [id, clock] : clocks_)
        if (id == clip) return &clock;
    return nullptr;
}

int MonsterStandinKit::lookup(std::string_view facing, std::string_view clip, int index) const {
    const std::string key = lookup_key(facing, clip, index);
    for (const auto& [k, i] : lookup_)
        if (k == key) return i;
    return -1;
}

StandinFrameView MonsterStandinKit::frame_at(std::size_t i) const {
    StandinFrameView v;
    if (i >= frames_.size()) return v;
    const Frame& f = frames_[i];
    v.image = &f.image;
    v.pivot_x = f.pivot_x;
    v.pivot_y = f.pivot_y;
    v.index = f.index;
    v.missing = f.missing;
    v.facing = f.facing;
    v.clip = f.clip;
    return v;
}

StandinPose MonsterStandinKit::pose(std::string_view facing, std::string_view clip, double t_sec) const {
    StandinPose pose;
    pose.clip = std::string(clip);
    const ClipClock* clock = clock_of(clip);
    if (!clock) return pose;
    pose.clock = *clock;
    pose.index = clip_frame(*clock, t_sec);
    const char* forced = norm_facing(facing);
    const std::string_view face = forced ? std::string_view(forced) : facing;
    int found = lookup(face, clip, pose.index);
    if (found < 0 && face != "se") found = lookup("se", clip, pose.index);
    pose.frame = found;
    return pose;
}

std::optional<MonsterStandinKit> MonsterStandinKit::load(const std::filesystem::path& root, std::string_view bin,
                                                          std::span<const KitClip> clips) {
    const StandinCanvas canvas = standin_canvas(bin);
    if (canvas.w == 0 || clips.empty()) return std::nullopt;
    static constexpr const char* kFacings[] = {"se", "sw", "ne", "nw"};
    MonsterStandinKit kit;
    kit.bin_ = std::string(bin);
    for (const KitClip& c : clips) {
        if (c.id.empty() || c.frames < 1 || c.fps < 1) return std::nullopt;
        if (const ClipClock* existing = kit.clock_of(c.id)) {
            if (existing->frames != c.frames || existing->fps != c.fps || existing->mode != c.mode) return std::nullopt;
            continue;
        }
        kit.clocks_.push_back({c.id, ClipClock{c.frames, c.fps, c.mode}});
        for (const char* facing : kFacings) {
            for (int fi = 0; fi < c.frames; ++fi) {
                Frame frame;
                frame.facing = facing;
                frame.clip = c.id;
                frame.index = fi;
                frame.pivot_x = canvas.pivot_x;
                frame.pivot_y = canvas.pivot_y;
                char name[96];
                std::snprintf(name, sizeof name, "mon_%s_%s_%s_%02d.png", kit.bin_.c_str(), facing, c.id.c_str(), fi);
                const auto path = root / "frames" / kit.bin_ / facing / name;
                if (auto img = read_png(path); img && img->w == canvas.w && img->h == canvas.h) {
                    frame.image = std::move(*img);
                } else {
                    frame.image = magenta_checker(canvas.w, canvas.h);
                    frame.missing = true;
                }
                kit.lookup_.push_back({lookup_key(frame.facing, frame.clip, fi), int(kit.frames_.size())});
                kit.frames_.push_back(std::move(frame));
            }
        }
    }
    if (kit.frames_.empty()) return std::nullopt;
    return kit;
}

} // namespace eng::presentation
