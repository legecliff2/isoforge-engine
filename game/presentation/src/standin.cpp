#include "presentation/standin.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
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

struct JVal {
    enum class Kind { Null, Bool, Num, Str, Arr, Obj };
    Kind kind = Kind::Null;
    bool b = false;
    int64_t num = 0;
    std::string str;
    std::vector<JVal> arr;
    // Parallel, not pair<string, JVal>: pair needs a complete JVal, and JVal is still open here.
    std::vector<std::string> keys;
    std::vector<JVal> vals;

    const JVal* field(std::string_view key) const {
        if (kind != Kind::Obj) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &vals[i];
        return nullptr;
    }
};

struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    bool ok = true;

    void skip() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }
    bool eat(char c) {
        skip();
        if (p < end && *p == c) { ++p; return true; }
        ok = false;
        return false;
    }
    JVal parse() {
        skip();
        if (p >= end) { ok = false; return {}; }
        if (*p == '{') return parse_obj();
        if (*p == '[') return parse_arr();
        if (*p == '"') return JVal{JVal::Kind::Str, false, 0, parse_str(), {}, {}, {}};
        if (*p == 't' || *p == 'f') return parse_bool();
        if (*p == 'n') return parse_null();
        if (*p == '-' || std::isdigit(static_cast<unsigned char>(*p))) return parse_num();
        ok = false;
        return {};
    }
    JVal parse_obj() {
        JVal v;
        v.kind = JVal::Kind::Obj;
        if (!eat('{')) return {};
        skip();
        if (p < end && *p == '}') { ++p; return v; }
        while (ok && p < end) {
            if (*p != '"') { ok = false; break; }
            std::string key = parse_str();
            if (!eat(':')) break;
            v.keys.push_back(std::move(key));
            v.vals.push_back(parse());
            skip();
            if (p < end && *p == ',') { ++p; skip(); continue; }
            if (p < end && *p == '}') { ++p; break; }
            ok = false;
            break;
        }
        return v;
    }
    JVal parse_arr() {
        JVal v;
        v.kind = JVal::Kind::Arr;
        if (!eat('[')) return {};
        skip();
        if (p < end && *p == ']') { ++p; return v; }
        while (ok && p < end) {
            v.arr.push_back(parse());
            skip();
            if (p < end && *p == ',') { ++p; skip(); continue; }
            if (p < end && *p == ']') { ++p; break; }
            ok = false;
            break;
        }
        return v;
    }
    std::string parse_str() {
        if (!eat('"')) return {};
        std::string s;
        while (ok && p < end && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (p >= end) { ok = false; break; }
                const char e = *p++;
                if (e == '"' || e == '\\' || e == '/') s.push_back(e);
                else if (e == 'n') s.push_back('\n');
                else if (e == 't') s.push_back('\t');
                else if (e == 'r') s.push_back('\r');
                else { ok = false; break; }
            } else {
                s.push_back(*p++);
            }
        }
        if (!eat('"')) ok = false;
        return s;
    }
    JVal parse_bool() {
        if (end - p >= 4 && std::memcmp(p, "true", 4) == 0) { p += 4; return JVal{JVal::Kind::Bool, true, 0, {}, {}, {}, {}}; }
        if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) { p += 5; return JVal{JVal::Kind::Bool, false, 0, {}, {}, {}, {}}; }
        ok = false;
        return {};
    }
    JVal parse_null() {
        if (end - p >= 4 && std::memcmp(p, "null", 4) == 0) { p += 4; return {}; }
        ok = false;
        return {};
    }
    JVal parse_num() {
        const char* s = p;
        if (*p == '-') ++p;
        if (p >= end || !std::isdigit(static_cast<unsigned char>(*p))) { ok = false; return {}; }
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) { ok = false; return {}; }
        int64_t acc = 0;
        const bool neg = *s == '-';
        for (const char* q = s + (neg ? 1 : 0); q < p; ++q) {
            const int digit = *q - '0';
            if (acc > (std::numeric_limits<int64_t>::max() - digit) / 10) { ok = false; return {}; }
            acc = acc * 10 + digit;
        }
        JVal v;
        v.kind = JVal::Kind::Num;
        v.num = neg ? -acc : acc;
        return v;
    }
};

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string s;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n < 0) return std::nullopt;
    s.resize(size_t(n));
    in.seekg(0);
    if (n > 0) in.read(s.data(), n);
    if (!in) return std::nullopt;
    return s;
}

std::optional<ImageRgba> read_png(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (!bytes) return std::nullopt;
    return decode_png(std::as_bytes(std::span<const char>(bytes->data(), bytes->size())));
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

void add_default_aliases(std::map<std::string, std::string, std::less<>>& m) {
    m.insert_or_assign("climb", "walk");
    m.insert_or_assign("crawl", "walk");
    m.insert_or_assign("hide", "idle");
    m.insert_or_assign("melee", "attack");
    m.insert_or_assign("hard-land", "prone_idle");
    m.insert_or_assign("hard_land", "prone_idle");
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

std::string_view clip_alias(std::string_view name) {
    if (name == "climb" || name == "crawl") return "walk";
    if (name == "hide") return "idle";
    if (name == "melee") return "attack";
    if (name == "hard-land" || name == "hard_land") return "prone_idle";
    return {};
}

std::string resolve_clip(std::string_view requested, const std::function<bool(std::string_view)>& has) {
    const std::string req = requested.empty() ? std::string("idle") : std::string(requested);
    if (has(req)) return req;
    const std::string_view alias = clip_alias(req);
    if (!alias.empty() && has(alias)) return std::string(alias);
    return "idle";
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
    pose.clip = resolve_clip(clip, [this](std::string_view id) { return has_clip(id); });
    const ClipClock* clock = clock_of(pose.clip);
    if (!clock) return pose;
    pose.clock = *clock;
    pose.index = clip_frame(*clock, t_sec);
    const char* forced = norm_facing(facing);
    const std::string_view face = forced ? std::string_view(forced) : facing;
    int found = lookup(face, pose.clip, pose.index);
    if (found < 0 && face != "se") found = lookup("se", pose.clip, pose.index);
    pose.frame = found;
    return pose;
}

std::optional<MonsterStandinKit> MonsterStandinKit::load(const std::filesystem::path& root, std::string_view bin) {
    if (bin != "tiny" && bin != "small" && bin != "medium" && bin != "large") return std::nullopt;
    const auto text = read_file(root / "clips.json");
    if (!text) return std::nullopt;
    Parser parser;
    parser.p = text->data();
    parser.end = text->data() + text->size();
    const JVal doc = parser.parse();
    if (!parser.ok) return std::nullopt;
    const JVal* clips = doc.field("clips");
    if (!clips || clips->kind != JVal::Kind::Arr) return std::nullopt;

    MonsterStandinKit kit;
    kit.bin_ = std::string(bin);
    std::map<std::string, std::string, std::less<>> aliases;
    add_default_aliases(aliases);
    if (const JVal* engine = doc.field("engine")) {
        if (const JVal* al = engine->field("aliases")) {
            if (al->kind == JVal::Kind::Obj) {
                for (size_t i = 0; i < al->keys.size(); ++i)
                    if (al->vals[i].kind == JVal::Kind::Str) aliases.insert_or_assign(al->keys[i], al->vals[i].str);
            }
        }
    }
    // The kit resolves through clip_alias (the M0 stub). File aliases that match the stub
    // are checked below so a drift in clips.json fails the load instead of being ignored.
    for (const auto& [k, v] : aliases) {
        const std::string_view stub = clip_alias(k);
        if (!stub.empty() && stub != v) return std::nullopt;
    }

    for (const JVal& c : clips->arr) {
        if (c.kind != JVal::Kind::Obj) return std::nullopt;
        const JVal* size = c.field("size");
        const JVal* facing = c.field("facing");
        const JVal* clip = c.field("clip");
        const JVal* frames = c.field("frames");
        const JVal* fps = c.field("fps");
        const JVal* mode = c.field("mode");
        const JVal* paths = c.field("paths");
        const JVal* canvas = c.field("canvas");
        const JVal* pivot = c.field("pivot");
        if (!size || size->kind != JVal::Kind::Str || size->str != bin) continue;
        if (!facing || facing->kind != JVal::Kind::Str || !clip || clip->kind != JVal::Kind::Str) return std::nullopt;
        if (!frames || frames->kind != JVal::Kind::Num || !fps || fps->kind != JVal::Kind::Num) return std::nullopt;
        if (!mode || mode->kind != JVal::Kind::Str || !paths || paths->kind != JVal::Kind::Arr) return std::nullopt;
        if (!canvas || canvas->kind != JVal::Kind::Arr || canvas->arr.size() != 2) return std::nullopt;
        if (!pivot || pivot->kind != JVal::Kind::Arr || pivot->arr.size() != 2) return std::nullopt;
        if (canvas->arr[0].kind != JVal::Kind::Num || canvas->arr[1].kind != JVal::Kind::Num) return std::nullopt;
        if (pivot->arr[0].kind != JVal::Kind::Num || pivot->arr[1].kind != JVal::Kind::Num) return std::nullopt;
        ClipMode cm = ClipMode::Loop;
        if (mode->str == "loop") cm = ClipMode::Loop;
        else if (mode->str == "once") cm = ClipMode::Once;
        else if (mode->str == "hold_last") cm = ClipMode::HoldLast;
        else return std::nullopt;
        const int nframes = int(frames->num);
        const int nfps = int(fps->num);
        if (nframes < 1 || nfps < 1) return std::nullopt;
        const uint32_t cw = uint32_t(canvas->arr[0].num);
        const uint32_t ch = uint32_t(canvas->arr[1].num);
        if (cw == 0 || ch == 0 || cw > 4096 || ch > 4096) return std::nullopt;
        const int px = int(pivot->arr[0].num);
        const int py = int(pivot->arr[1].num);

        if (const ClipClock* existing = kit.clock_of(clip->str)) {
            if (existing->frames != nframes || existing->fps != nfps || existing->mode != cm) return std::nullopt;
        } else {
            kit.clocks_.push_back({clip->str, ClipClock{nframes, nfps, cm}});
        }
        for (int fi = 0; fi < nframes; ++fi) {
            Frame frame;
            frame.facing = facing->str;
            frame.clip = clip->str;
            frame.index = fi;
            frame.pivot_x = px;
            frame.pivot_y = py;
            bool got = false;
            if (fi < int(paths->arr.size()) && paths->arr[size_t(fi)].kind == JVal::Kind::Str) {
                if (auto img = read_png(root / paths->arr[size_t(fi)].str)) {
                    if (img->w == cw && img->h == ch) {
                        frame.image = std::move(*img);
                        got = true;
                    }
                }
            }
            if (!got) {
                frame.image = magenta_checker(cw, ch);
                frame.missing = true;
            }
            kit.lookup_.push_back({lookup_key(frame.facing, frame.clip, fi), int(kit.frames_.size())});
            kit.frames_.push_back(std::move(frame));
        }
    }
    if (kit.frames_.empty() || kit.clock_of("idle") == nullptr) return std::nullopt;
    return kit;
}

} // namespace eng::presentation
