#include <doctest/doctest.h>

#include "presentation/png.hpp"
#include "presentation/standin.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

using namespace eng::presentation;

#ifndef STANDIN_ROOT
#define STANDIN_ROOT ""
#endif

namespace {

std::filesystem::path root() { return STANDIN_ROOT; }

std::vector<std::byte> read_bin(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    REQUIRE(n > 0);
    std::vector<std::byte> b(static_cast<size_t>(n));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(b.data()), n);
    REQUIRE(in);
    return b;
}

ImageRgba pure_hflip(const ImageRgba& src) {
    ImageRgba d;
    d.w = src.w;
    d.h = src.h;
    d.rgba.assign(size_t(src.w) * src.h * 4, 0);
    for (uint32_t y = 0; y < src.h; ++y)
        for (uint32_t x = 0; x < src.w; ++x)
            std::memcpy(d.px(x, y), src.px(src.w - 1 - x, y), 4);
    return d;
}

} // namespace

TEST_CASE("png: real stand-in frame decodes to the canvas, pivot pixel, and hard alpha") {
    const auto bytes = read_bin(root() / "frames/medium/se/mon_medium_se_idle_00.png");
    const auto img = decode_png(bytes);
    REQUIRE(img);
    CHECK(img->w == 32);
    CHECK(img->h == 48);
    const uint8_t* pivot = img->px(16, 47);
    CHECK(pivot[0] == 30);
    CHECK(pivot[1] == 34);
    CHECK(pivot[2] == 40);
    CHECK(pivot[3] == 255);
    CHECK(img->px(0, 0)[3] == 0);
    for (size_t i = 3; i < img->rgba.size(); i += 4) CHECK((img->rgba[i] == 0 || img->rgba[i] == 255));
}

TEST_CASE("png: a truncated buffer is a failure, and a missing frame is a magenta checker") {
    const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK_FALSE(decode_png(std::as_bytes(std::span<const uint8_t>(junk, sizeof junk))));
    const ImageRgba c = magenta_checker(8, 4);
    CHECK(c.w == 8);
    CHECK(c.h == 4);
    CHECK(c.px(0, 0)[0] == 0x1E);
    CHECK(c.px(4, 0)[0] == 255);
    CHECK(c.px(4, 0)[1] == 0);
    CHECK(c.px(4, 0)[2] == 255);
}

TEST_CASE("png: authored west frames are flip-plus-one, not a plain UV mirror") {
    const auto se = decode_png(read_bin(root() / "frames/medium/se/mon_medium_se_attack_02.png"));
    const auto sw = decode_png(read_bin(root() / "frames/medium/sw/mon_medium_sw_attack_02.png"));
    REQUIRE(se);
    REQUIRE(sw);
    const ImageRgba shifted = hflip_pivot(*se);
    const ImageRgba plain = pure_hflip(*se);
    CHECK(shifted.rgba == sw->rgba);
    CHECK(plain.rgba != sw->rgba);
    // Pivot x = w/2 stays put. A UV swap of the whole quad cannot insert the clear column.
    CHECK(std::memcmp(shifted.px(16, 47), se->px(16, 47), 4) == 0);
}

TEST_CASE("clip clock: loop wraps, once finishes after the last slot, hold_last sits on the last frame") {
    const ClipClock idle{4, 6, ClipMode::Loop};
    CHECK(clip_frame(idle, 0) == 0);
    CHECK_FALSE(clip_finished(idle, 100));
    CHECK(clip_frame(idle, 1.0) == 2);   // 1000ms * 6 / 1000 = 6, 6 % 4 = 2

    const ClipClock walk{6, 10, ClipMode::Loop};
    CHECK(clip_frame(walk, 0.1) == 1);
    CHECK(clip_frame(walk, 0.6) == 0);

    const ClipClock attack{6, 12, ClipMode::Once};
    CHECK(clip_frame(attack, 0) == 0);
    CHECK_FALSE(clip_finished(attack, 0));
    CHECK(clip_frame(attack, 0.5) == 5);
    CHECK(clip_finished(attack, 0.5));   // 500ms * 12 / 1000 = 6 >= 6

    const ClipClock death{5, 8, ClipMode::HoldLast};
    CHECK(clip_frame(death, 0) == 0);
    CHECK_FALSE(clip_finished(death, 0));
    CHECK(clip_frame(death, 0.5) == 4);   // 500ms * 8 / 1000 = 4
    CHECK(clip_finished(death, 0.5));
    CHECK(clip_frame(death, 10) == 4);
}

TEST_CASE("stand-in kit: loads authored frames and a missing file is a checker") {
    const StandinCanvas medium = standin_canvas("medium");
    CHECK(medium.w == 32);
    CHECK(medium.h == 48);
    CHECK(medium.pivot_x == 16);
    CHECK(medium.pivot_y == 47);
    CHECK(standin_canvas("huge").w == 0);

    // Frame counts match Core's stand-in registry. Aliases are not applied here.
    const KitClip clips[] = {
        {"idle", 4, 6, ClipMode::Loop},
        {"walk", 6, 10, ClipMode::Loop},
        {"react", 3, 12, ClipMode::Once},
        {"attack", 6, 12, ClipMode::Once},
        {"cast", 4, 10, ClipMode::Once},
        {"stun", 4, 8, ClipMode::Loop},
        {"death", 5, 8, ClipMode::HoldLast},
        {"fall", 3, 12, ClipMode::Loop},
        {"fall_impact", 3, 12, ClipMode::Once},
    };
    const auto kit = MonsterStandinKit::load(root(), "medium", clips);
    REQUIRE(kit);
    CHECK(kit->frame_count() == 152);
    CHECK(kit->missing_count() == 0);
    CHECK(kit->has_clip("idle"));
    CHECK_FALSE(kit->has_clip("prone_idle"));
    CHECK(kit->pose("se", "climb", 0).frame < 0);   // unresolved names are not a second alias table

    const StandinPose dead0 = kit->pose("se", "death", 0);
    const StandinPose dead1 = kit->pose("se", "death", 10);
    REQUIRE(dead0.frame >= 0);
    REQUIRE(dead1.frame >= 0);
    CHECK(dead0.frame != dead1.frame);
    CHECK(dead1.index == 4);
    CHECK(kit->frame_at(size_t(dead1.frame)).image->px(16, 47)[3] == 255);

    const KitClip idle_only[] = {{"idle", 4, 6, ClipMode::Loop}};
    const auto tiny = MonsterStandinKit::load(root(), "tiny", idle_only);
    REQUIRE(tiny);
    CHECK(tiny->frame_at(0).image->w == 16);
    CHECK(tiny->frame_at(0).pivot_x == 8);
    CHECK(tiny->frame_at(0).pivot_y == 15);
    CHECK_FALSE(MonsterStandinKit::load(root(), "huge", idle_only));

    const auto dir = std::filesystem::temp_directory_path() / "isoforge-standin-missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto bare = MonsterStandinKit::load(dir, "small", idle_only);
    REQUIRE(bare);
    CHECK(bare->missing_count() == bare->frame_count());
    const StandinPose gap = bare->pose("se", "idle", 0);
    REQUIRE(gap.frame >= 0);
    CHECK(bare->frame_at(size_t(gap.frame)).missing);
    CHECK(bare->frame_at(size_t(gap.frame)).image->px(4, 0)[0] == 255);
    std::filesystem::remove_all(dir);
}
