#include <doctest/doctest.h>

#include "presentation/equip_art.hpp"
#include "presentation/png.hpp"
#include "presentation/standin.hpp"

#include <cstdio>
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
    const StandinCanvas huge = standin_canvas("huge");
    CHECK(huge.w == 72);
    CHECK(huge.h == 96);
    CHECK(huge.pivot_x == 36);
    CHECK(huge.pivot_y == 95);
    const StandinCanvas gargantuan = standin_canvas("gargantuan");
    CHECK(gargantuan.w == 0);
    CHECK(gargantuan.h == 0);
    CHECK(gargantuan.pivot_x == 0);
    CHECK(gargantuan.pivot_y == 0);

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
    CHECK_FALSE(MonsterStandinKit::load(root(), "gargantuan", idle_only));

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

TEST_CASE("stand-in kit: per-monster directory, then the shared bin") {
    CHECK(monster_art_slug("srd51.monster.wolf") == "wolf");
    CHECK(monster_art_slug("srd51.monster.young_black_dragon") == "young_black_dragon");
    CHECK(monster_art_dir("srd51.monster.blink_dog") == "blink-dog");
    CHECK(monster_art_dir("srd51.monster.swarm_of_bats") == "swarm-of-bats");
    CHECK(monster_art_dir("srd51.monster.succubus") == "succubus-incubus");
    CHECK(monster_art_slug("wolf") == "wolf");
    CHECK(monster_art_slug("").empty());
    CHECK(monster_art_slug("srd51.monster.").empty());
    CHECK(monster_art_slug("..").empty());
    CHECK(monster_art_slug("srd51.monster.wolf/../x").empty());
    CHECK(monster_art_slug("srd51.monster.wolf\\x").empty());

    const auto base = std::filesystem::temp_directory_path() / "isoforge-standin-monster";
    std::filesystem::remove_all(base);
    const auto shared = base / "frames" / "medium" / "se";
    const auto wolf_se = base / "srd" / "wolf" / "medium" / "se";
    const auto wolf_sw = base / "srd" / "wolf" / "medium" / "sw";
    const auto ogre = base / "srd" / "ogre" / "huge" / "se";
    const auto dog = base / "srd" / "blink-dog" / "medium" / "se";
    std::filesystem::create_directories(shared);
    std::filesystem::create_directories(wolf_se);
    std::filesystem::create_directories(wolf_sw);
    std::filesystem::create_directories(ogre);
    std::filesystem::create_directories(dog);
    std::filesystem::copy_file(root() / "frames/medium/se/mon_medium_se_idle_00.png", shared / "mon_medium_se_idle_00.png");
    std::filesystem::copy_file(root() / "frames/medium/se/mon_medium_se_idle_01.png", wolf_se / "mon_wolf_medium_se_idle_00.png");
    // Authored SW is a different frame, not hflip_pivot(SE). clips.json matches the art manifest:
    // mirrored_from null, no strip key.
    std::filesystem::copy_file(root() / "frames/medium/se/mon_medium_se_idle_02.png", wolf_sw / "mon_wolf_medium_sw_idle_00.png");
    std::ofstream(base / "srd" / "clips.json") << R"({
  "clips": [{
    "slug": "wolf",
    "size": "medium",
    "facing": "sw",
    "clip": "idle",
    "frames": 1,
    "mirrored_from": null,
    "paths": ["content/art/monster_standins/srd/wolf/medium/sw/mon_wolf_medium_sw_idle_00.png"]
  }]
})";

    const StandinKitLoc wolf_loc = resolve_standin_kit(base, "srd51.monster.wolf", "medium");
    const StandinKitLoc gob_loc = resolve_standin_kit(base, "srd51.monster.goblin", "medium");
    const StandinKitLoc ogre_loc = resolve_standin_kit(base, "srd51.monster.ogre", "huge");
    const StandinKitLoc dog_loc = resolve_standin_kit(base, "srd51.monster.blink_dog", "medium");
    CHECK(wolf_loc.per_monster);
    CHECK(wolf_loc.slug == "wolf");
    CHECK(wolf_loc.root == base / "srd" / "wolf");
    CHECK_FALSE(gob_loc.per_monster);
    CHECK(gob_loc.root == base);
    CHECK(ogre_loc.per_monster);
    CHECK(ogre_loc.root == base / "srd" / "ogre");
    CHECK(dog_loc.per_monster);
    CHECK(dog_loc.slug == "blink-dog");
    CHECK_FALSE(resolve_standin_kit(base, "srd51.monster.ogre", "medium").per_monster);
    CHECK_FALSE(resolve_standin_kit(base, "", "medium").per_monster);
    CHECK_FALSE(resolve_standin_kit(base, "srd51.monster.wolf", "gargantuan").per_monster);
    CHECK_FALSE(resolve_standin_kit(root(), "srd51.monster.wolf", "medium").per_monster);
    CHECK_FALSE(resolve_standin_kit(root(), "srd51.monster.ogre", "huge").per_monster);

    const KitClip idle[] = {{"idle", 1, 6, ClipMode::Loop}};
    const auto wolf_kit = MonsterStandinKit::load(wolf_loc.root, "medium", idle, wolf_loc.slug);
    const auto gob_kit = MonsterStandinKit::load(gob_loc.root, "medium", idle, gob_loc.slug);
    const auto ogre_kit = MonsterStandinKit::load(ogre_loc.root, "huge", idle, ogre_loc.slug);
    REQUIRE(wolf_kit);
    REQUIRE(gob_kit);
    REQUIRE(ogre_kit);
    const StandinPose wolf_se_pose = wolf_kit->pose("se", "idle", 0);
    const StandinPose wolf_sw_pose = wolf_kit->pose("sw", "idle", 0);
    const StandinPose gob_se_pose = gob_kit->pose("se", "idle", 0);
    const StandinPose ogre_se_pose = ogre_kit->pose("se", "idle", 0);
    REQUIRE(wolf_se_pose.frame >= 0);
    REQUIRE(wolf_sw_pose.frame >= 0);
    REQUIRE(gob_se_pose.frame >= 0);
    REQUIRE(ogre_se_pose.frame >= 0);
    const ImageRgba* wolf_px = wolf_kit->frame_at(size_t(wolf_se_pose.frame)).image;
    const ImageRgba* wolf_sw_px = wolf_kit->frame_at(size_t(wolf_sw_pose.frame)).image;
    const ImageRgba* gob_px = gob_kit->frame_at(size_t(gob_se_pose.frame)).image;
    const ImageRgba* ogre_px = ogre_kit->frame_at(size_t(ogre_se_pose.frame)).image;
    REQUIRE(wolf_px);
    REQUIRE(wolf_sw_px);
    REQUIRE(gob_px);
    REQUIRE(ogre_px);
    CHECK_FALSE(wolf_kit->frame_at(size_t(wolf_se_pose.frame)).missing);
    CHECK_FALSE(wolf_kit->frame_at(size_t(wolf_sw_pose.frame)).missing);
    CHECK_FALSE(gob_kit->frame_at(size_t(gob_se_pose.frame)).missing);
    CHECK(ogre_kit->frame_at(size_t(ogre_se_pose.frame)).missing);
    CHECK(wolf_px->rgba != gob_px->rgba);
    const ImageRgba mirrored = hflip_pivot(*wolf_px);
    CHECK(wolf_sw_px->rgba != mirrored.rgba);
    const auto authored_sw = decode_png(read_bin(wolf_sw / "mon_wolf_medium_sw_idle_00.png"));
    REQUIRE(authored_sw);
    CHECK(wolf_sw_px->rgba == authored_sw->rgba);
    CHECK(ogre_px->w == 72);
    CHECK(ogre_px->h == 96);
    CHECK(ogre_kit->frame_at(size_t(ogre_se_pose.frame)).pivot_x == 36);
    CHECK(ogre_kit->frame_at(size_t(ogre_se_pose.frame)).pivot_y == 95);
    const StandinPose wolf_ne = wolf_kit->pose("ne", "idle", 0);
    REQUIRE(wolf_ne.frame >= 0);
    CHECK(wolf_kit->frame_at(size_t(wolf_ne.frame)).missing);
    std::filesystem::remove_all(base);
}

TEST_CASE("stand-in kit: shipped medium kits resolve, and west frames are the authored files") {
    struct Creature { const char* id; const char* dir; };
    const Creature roster[] = {
        {"srd51.monster.basilisk", "basilisk"},
        {"srd51.monster.blink_dog", "blink-dog"},
        {"srd51.monster.death_dog", "death-dog"},
        {"srd51.monster.dryad", "dryad"},
        {"srd51.monster.ettercap", "ettercap"},
        {"srd51.monster.gargoyle", "gargoyle"},
        {"srd51.monster.gray_ooze", "gray-ooze"},
        {"srd51.monster.green_hag", "green-hag"},
        {"srd51.monster.grick", "grick"},
        {"srd51.monster.harpy", "harpy"},
        {"srd51.monster.hell_hound", "hell-hound"},
        {"srd51.monster.invisible_stalker", "invisible-stalker"},
        {"srd51.monster.medusa", "medusa"},
        {"srd51.monster.rust_monster", "rust-monster"},
        {"srd51.monster.satyr", "satyr"},
        {"srd51.monster.sea_hag", "sea-hag"},
        {"srd51.monster.shrieker", "shrieker"},
        {"srd51.monster.succubus", "succubus-incubus"},
        {"srd51.monster.swarm_of_bats", "swarm-of-bats"},
        {"srd51.monster.swarm_of_insects", "swarm-of-insects"},
        {"srd51.monster.swarm_of_poisonous_snakes", "swarm-of-poisonous-snakes"},
        {"srd51.monster.swarm_of_quippers", "swarm-of-quippers"},
        {"srd51.monster.swarm_of_rats", "swarm-of-rats"},
        {"srd51.monster.swarm_of_ravens", "swarm-of-ravens"},
        {"srd51.monster.violet_fungus", "violet-fungus"},
        {"srd51.monster.xorn", "xorn"},
    };
    for (const Creature& c : roster) {
        CAPTURE(c.id);
        const StandinKitLoc loc = resolve_standin_kit(root(), c.id, "medium");
        CHECK(loc.per_monster);
        CHECK(loc.slug == c.dir);
        CHECK(loc.root == root() / "srd" / c.dir);
        CHECK(std::filesystem::is_directory(loc.root / "medium" / "sw"));
        CHECK(std::filesystem::is_directory(loc.root / "medium" / "nw"));
    }

    // Basilisk stands in for the pixel check. Every sw/nw frame must be the file
    // on disk. At least one of each west facing must differ from a flip of east.
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
    const StandinKitLoc basilisk = resolve_standin_kit(root(), "srd51.monster.basilisk", "medium");
    REQUIRE(basilisk.per_monster);
    const auto kit = MonsterStandinKit::load(basilisk.root, "medium", clips, basilisk.slug);
    REQUIRE(kit);
    CHECK(kit->missing_count() == 0);
    int sw_not_mirror = 0, nw_not_mirror = 0;
    auto frame_of = [&](std::string_view facing, std::string_view clip, int index) -> const ImageRgba* {
        for (std::size_t n = 0; n < kit->frame_count(); ++n) {
            const StandinFrameView v = kit->frame_at(n);
            if (v.facing == facing && v.clip == clip && v.index == index && !v.missing) return v.image;
        }
        return nullptr;
    };
    for (const KitClip& clip : clips) {
        for (int i = 0; i < clip.frames; ++i) {
            CAPTURE(clip.id);
            CAPTURE(i);
            char num[8];
            std::snprintf(num, sizeof num, "%02d", i);
            const auto file = [&](const char* facing) {
                return basilisk.root / "medium" / facing /
                       (std::string("mon_basilisk_medium_") + facing + "_" + clip.id + "_" + num + ".png");
            };
            const ImageRgba* se = frame_of("se", clip.id, i);
            const ImageRgba* sw = frame_of("sw", clip.id, i);
            const ImageRgba* ne = frame_of("ne", clip.id, i);
            const ImageRgba* nw = frame_of("nw", clip.id, i);
            REQUIRE(se);
            REQUIRE(sw);
            REQUIRE(ne);
            REQUIRE(nw);
            const auto sw_png = decode_png(read_bin(file("sw")));
            const auto nw_png = decode_png(read_bin(file("nw")));
            REQUIRE(sw_png);
            REQUIRE(nw_png);
            CHECK(sw->rgba == sw_png->rgba);
            CHECK(nw->rgba == nw_png->rgba);
            if (sw->rgba != hflip_pivot(*se).rgba) ++sw_not_mirror;
            if (nw->rgba != hflip_pivot(*ne).rgba) ++nw_not_mirror;
        }
    }
    CHECK(sw_not_mirror > 0);
    CHECK(nw_not_mirror > 0);
}

TEST_CASE("stand-in kit: the bin-only fallback still uses the mirrored west frame") {
    const KitClip idle[] = {{"idle", 1, 6, ClipMode::Loop}};
    const auto kit = MonsterStandinKit::load(root(), "medium", idle);
    REQUIRE(kit);
    const StandinPose se = kit->pose("se", "idle", 0);
    const StandinPose sw = kit->pose("sw", "idle", 0);
    REQUIRE(se.frame >= 0);
    REQUIRE(sw.frame >= 0);
    CHECK_FALSE(kit->frame_at(size_t(se.frame)).missing);
    CHECK_FALSE(kit->frame_at(size_t(sw.frame)).missing);
    const ImageRgba* se_px = kit->frame_at(size_t(se.frame)).image;
    const ImageRgba* sw_px = kit->frame_at(size_t(sw.frame)).image;
    REQUIRE(se_px);
    REQUIRE(sw_px);
    CHECK(sw_px->rgba == hflip_pivot(*se_px).rgba);
    CHECK(sw_px->rgba != pure_hflip(*se_px).rgba);
}

TEST_CASE("equipment art: content id, then medium, then kind, then nothing") {
    const auto kit = std::filesystem::temp_directory_path() / "isoforge-equip-art";
    std::filesystem::remove_all(kit);
    const auto quiver = kit / "srd51.item.quiver";
    const auto bolts = kit / "srd51.item.case_crossbow_bolt";
    const auto pack = kit / "srd51.item.backpack";
    const auto sword = kit / "srd51.item.longsword";
    std::filesystem::create_directories(quiver);
    std::filesystem::create_directories(bolts);
    std::filesystem::create_directories(pack / "large");
    std::filesystem::create_directories(pack / "medium");
    std::filesystem::create_directories(sword);
    std::filesystem::create_directories(kit / "_kind" / "weapon");
    std::filesystem::create_directories(kit / "_kind" / "other");
    std::filesystem::copy_file(root() / "frames/tiny/se/mon_tiny_se_idle_00.png", quiver / "back.se.png");
    std::filesystem::copy_file(root() / "frames/small/se/mon_small_se_idle_00.png", bolts / "back.se.png");
    std::filesystem::copy_file(root() / "frames/medium/se/mon_medium_se_idle_00.png", pack / "back.se.png");
    std::filesystem::copy_file(root() / "frames/medium/ne/mon_medium_ne_idle_00.png", pack / "back.ne.png");
    std::filesystem::copy_file(root() / "frames/large/se/mon_large_se_idle_00.png", pack / "large" / "back.se.png");
    std::filesystem::copy_file(root() / "frames/small/se/mon_small_se_idle_01.png", pack / "medium" / "back.se.png");
    // "off_hand" sorts after "medium", so a flat file must not beat the medium/ override.
    std::filesystem::copy_file(root() / "frames/tiny/se/mon_tiny_se_idle_02.png", pack / "off_hand.se.png");
    std::filesystem::copy_file(root() / "frames/small/ne/mon_small_ne_idle_00.png", pack / "medium" / "off_hand.se.png");
    std::filesystem::copy_file(root() / "frames/tiny/ne/mon_tiny_ne_idle_00.png", sword / "main_hand.se.png");
    std::filesystem::copy_file(root() / "frames/small/ne/mon_small_ne_idle_00.png", kit / "_kind" / "weapon" / "main_hand.ne.png");
    std::filesystem::copy_file(root() / "frames/large/ne/mon_large_ne_idle_00.png", kit / "_kind" / "other" / "hip.se.png");

    CHECK(EquipArtTable::load(kit / "no-such-root").size() == 0);
    const EquipArtTable table = EquipArtTable::load(kit);
    CHECK(table.size() > 0);

    EquipArtQuery q;
    q.part = EquipArtPart::Back;
    q.view = EquipArtView::Toward;
    q.content_id = "srd51.item.quiver";
    q.kind = "other";
    const EquipArtSprite* quiver_se = table.find(q);
    q.content_id = "srd51.item.case_crossbow_bolt";
    const EquipArtSprite* bolt_se = table.find(q);
    REQUIRE(quiver_se);
    REQUIRE(bolt_se);
    CHECK(quiver_se->image.w == 16);
    CHECK(bolt_se->image.w == 24);
    CHECK(quiver_se->image.rgba != bolt_se->image.rgba);
    CHECK(quiver_se->pivot_x == 8);
    CHECK(quiver_se->pivot_y == 15);

    q.content_id = "srd51.item.quiver";
    q.view = EquipArtView::Away;
    CHECK(table.find(q) == nullptr);

    q.content_id = "srd51.item.backpack";
    q.view = EquipArtView::Toward;
    q.size_bin = "large";
    const EquipArtSprite* large = table.find(q);
    REQUIRE(large);
    CHECK(large->image.w == 48);
    q.size_bin = "huge";
    const EquipArtSprite* huge_falls_to_medium = table.find(q);
    REQUIRE(huge_falls_to_medium);
    CHECK(huge_falls_to_medium->image.w == 24);
    q.size_bin = "medium";
    CHECK(table.find(q) == huge_falls_to_medium);
    q.size_bin = "";
    CHECK(table.find(q) == huge_falls_to_medium);
    q.part = EquipArtPart::OffHand;
    q.size_bin = "medium";
    const EquipArtSprite* off = table.find(q);
    REQUIRE(off);
    CHECK(off->image.w == 24);
    q.part = EquipArtPart::Back;
    q.view = EquipArtView::Away;
    q.size_bin = "huge";
    const EquipArtSprite* away = table.find(q);
    REQUIRE(away);
    CHECK(away->image.w == 32);
    CHECK(away != large);

    q.content_id = "srd51.item.longbow";
    q.kind = "weapon";
    q.part = EquipArtPart::MainHand;
    q.view = EquipArtView::Toward;
    q.size_bin = "medium";
    CHECK(table.find(q) == nullptr);
    q.content_id = "srd51.item.club";
    q.view = EquipArtView::Away;
    const EquipArtSprite* kind_weapon = table.find(q);
    REQUIRE(kind_weapon);
    CHECK(kind_weapon->image.w == 24);
    q.content_id = "srd51.item.longsword";
    q.view = EquipArtView::Toward;
    const EquipArtSprite* sword_se = table.find(q);
    REQUIRE(sword_se);
    CHECK(sword_se->image.w == 16);
    CHECK(sword_se != kind_weapon);

    q.content_id = "srd51.item.pouch";
    q.kind = "other";
    q.part = EquipArtPart::Hip;
    q.view = EquipArtView::Toward;
    const EquipArtSprite* pouch = table.find(q);
    REQUIRE(pouch);
    CHECK(pouch->image.w == 48);
    q.part = EquipArtPart::Back;
    CHECK(table.find(q) == nullptr);

    std::filesystem::create_directories(kit / "srd51.item.torch" / "small");
    std::filesystem::copy_file(root() / "frames/tiny/se/mon_tiny_se_idle_01.png",
                               kit / "srd51.item.torch" / "small" / "held_light.se.png");
    const EquipArtTable sized = EquipArtTable::load(kit);
    q.content_id = "srd51.item.torch";
    q.kind = "light";
    q.part = EquipArtPart::HeldLight;
    q.view = EquipArtView::Toward;
    q.size_bin = "small";
    const EquipArtSprite* torch_small = sized.find(q);
    REQUIRE(torch_small);
    CHECK(torch_small->image.w == 16);
    q.size_bin = "large";
    CHECK(sized.find(q) == nullptr);
    q.size_bin = "medium";
    CHECK(sized.find(q) == nullptr);

    const ImageRgba& src = quiver_se->image;
    const ImageRgba flipped = mirror_sprite_around_pivot(src, quiver_se->pivot_x);
    CHECK(flipped.w == src.w);
    CHECK(std::memcmp(flipped.px(uint32_t(quiver_se->pivot_x), uint32_t(quiver_se->pivot_y)),
                      src.px(uint32_t(quiver_se->pivot_x), uint32_t(quiver_se->pivot_y)), 4) == 0);
    const ImageRgba standin_flip = hflip_pivot(src);
    CHECK(flipped.rgba == standin_flip.rgba);
    std::filesystem::remove_all(kit);
}

TEST_CASE("equipment art: neck.se loads and neck.ne is rejected") {
    const auto kit = std::filesystem::temp_directory_path() / "isoforge-equip-neck";
    std::filesystem::remove_all(kit);
    const auto amulet = kit / "srd51.item.holy_symbol_amulet";
    std::filesystem::create_directories(amulet);
    std::filesystem::copy_file(root() / "frames/tiny/se/mon_tiny_se_idle_00.png", amulet / "neck.se.png");
    std::filesystem::copy_file(root() / "frames/small/ne/mon_small_ne_idle_00.png", amulet / "neck.ne.png");
    const EquipArtTable table = EquipArtTable::load(kit);
    CHECK(table.size() == 1);
    EquipArtQuery q;
    q.content_id = "srd51.item.holy_symbol_amulet";
    q.kind = "other";
    q.part = EquipArtPart::Neck;
    q.view = EquipArtView::Toward;
    const EquipArtSprite* toward = table.find(q);
    REQUIRE(toward);
    CHECK(toward->image.w == 16);
    CHECK(toward->pivot_x == 8);
    CHECK(toward->pivot_y == 15);
    q.view = EquipArtView::Away;
    CHECK(table.find(q) == nullptr);
    std::filesystem::remove_all(kit);
}

TEST_CASE("equipment art: manifest pivot, exact content ids, kind fallback, neck.se loads") {
    const auto kit = std::filesystem::temp_directory_path() / "isoforge-equip-kit-manifest";
    std::filesystem::remove_all(kit);
    const auto quiver = kit / "srd51.item.quiver";
    const auto pouch = kit / "srd51.item.pouch";
    const auto amulet = kit / "srd51.item.holy_symbol_amulet";
    std::filesystem::create_directories(quiver);
    std::filesystem::create_directories(pouch);
    std::filesystem::create_directories(amulet);
    std::filesystem::create_directories(kit / "_kind" / "weapon");
    const auto medium = root() / "frames/medium/se/mon_medium_se_idle_00.png";
    std::filesystem::copy_file(medium, quiver / "back.se.png");
    std::filesystem::copy_file(medium, pouch / "hip.se.png");
    std::filesystem::copy_file(medium, amulet / "neck.se.png");
    std::filesystem::copy_file(medium, kit / "_kind" / "weapon" / "main_hand.se.png");
    {
        std::ofstream out(kit / "kit.json");
        out << R"({
            "$schema": "equip_kit@1",
            "cell": [24, 24],
            "items": [
                {"content_id": "srd51.item.quiver", "back_role": "quiver",
                 "parts": [{"part": "back", "views": [{"view": "se", "pivot": [12, 20]}]}]},
                {"content_id": "srd51.item.pouch",
                 "parts": [{"part": "hip", "views": [{"view": "se"}]}]},
                {"content_id": "srd51.item.holy_symbol_amulet",
                 "parts": [{"part": "neck", "views": [{"view": "se", "pivot": [12, 20]}]}]}
            ],
            "kinds": [
                {"kind": "weapon",
                 "parts": [{"part": "main_hand", "views": [{"view": "se", "pivot": [12, 20]}]}]}
            ]
        })";
    }
    const EquipArtTable table = EquipArtTable::load(kit);
    // neck.se.png is on disk and in the manifest. neck.ne is not a file.
    CHECK(table.size() == 4);
    const auto quiver_role = table.back_role("srd51.item.quiver");
    REQUIRE(quiver_role);
    CHECK(*quiver_role == "quiver");
    CHECK_FALSE(table.back_role("srd51.item.pouch"));
    CHECK_FALSE(table.back_role("srd51.item.holy_symbol_amulet"));

    EquipArtQuery q;
    q.part = EquipArtPart::Back;
    q.view = EquipArtView::Toward;
    q.content_id = "srd51.item.quiver";
    q.kind = "other";
    const EquipArtSprite* quiver_se = table.find(q);
    REQUIRE(quiver_se);
    CHECK(quiver_se->pivot_x == 12);
    CHECK(quiver_se->pivot_y == 20);
    CHECK(quiver_se->pivot_x != int(quiver_se->image.w / 2));
    CHECK(quiver_se->pivot_y != int(quiver_se->image.h - 1));

    q.content_id = "srd51.item.case_crossbow_bolt";
    CHECK(table.find(q) == nullptr);
    q.content_id = "srd51.item.quiver_extra";
    CHECK(table.find(q) == nullptr);
    q.content_id = "srd51.item.qui";
    CHECK(table.find(q) == nullptr);

    q.content_id = "srd51.item.pouch";
    q.part = EquipArtPart::Hip;
    const EquipArtSprite* pouch_se = table.find(q);
    REQUIRE(pouch_se);
    CHECK(pouch_se->pivot_x == int(pouch_se->image.w / 2));
    CHECK(pouch_se->pivot_y == int(pouch_se->image.h - 1));
    CHECK(pouch_se != quiver_se);

    q.content_id = "srd51.item.club";
    q.kind = "weapon";
    q.part = EquipArtPart::MainHand;
    const EquipArtSprite* club = table.find(q);
    REQUIRE(club);
    CHECK(club->pivot_x == 12);
    CHECK(club->pivot_y == 20);
    CHECK(club != quiver_se);
    q.content_id = "srd51.item.longsword";
    CHECK(table.find(q) == club);

    q.content_id = "srd51.item.holy_symbol_amulet";
    q.kind = "other";
    q.part = EquipArtPart::Neck;
    q.view = EquipArtView::Toward;
    const EquipArtSprite* neck = table.find(q);
    REQUIRE(neck);
    CHECK(neck->pivot_x == 12);
    CHECK(neck->pivot_y == 20);
    q.view = EquipArtView::Away;
    CHECK(table.find(q) == nullptr);

    {
        std::ofstream out(kit / "kit.json");
        out << "{";
    }
    const EquipArtTable broken = EquipArtTable::load(kit);
    q.content_id = "srd51.item.quiver";
    q.kind = "other";
    q.part = EquipArtPart::Back;
    q.view = EquipArtView::Toward;
    const EquipArtSprite* still = broken.find(q);
    REQUIRE(still);
    CHECK(still->pivot_x == int(still->image.w / 2));
    CHECK(still->pivot_y == int(still->image.h - 1));
    CHECK(broken.size() == 4);
    CHECK_FALSE(broken.back_role("srd51.item.quiver"));
    std::filesystem::remove_all(kit);
}
