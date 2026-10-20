// R-G13 phase 1 light model (docs/proposals/LIGHTING.md §3-§6, §10): composition, SRD radii, occlusion matching cover,
// magical darkness with the R1 level rule, LightCache / LightFrame, determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "rules5e/spells.hpp"
#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/light.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::Cover;
using rules5e::CoverQuery;

namespace {
// SRD 5.1 radii (bright / additional dim).
constexpr uint16_t kTorchB = 20, kTorchD = 20, kLanternB = 30, kLanternD = 30, kLightB = 20, kLightD = 20,
                   kDaylightB = 60, kDaylightD = 60;

world::Chunk room(const std::vector<Vec2i>& walls, std::vector<std::pair<std::string, Vec2i>> spawns = {}) {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls)
        c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
LightSource src(uint32_t id, Vec2i at, uint16_t b, uint16_t d, uint8_t flags = 0, uint8_t level = 0) {
    AttachedLight a;
    a.id = id; a.carrier = Carrier::Tile; a.tile = at; a.bright_ft = b; a.dim_ft = d;
    a.flags = uint8_t(flags | light_src::kLit); a.spell_level = level;
    return attached_source(a, at);
}
LightZone zone(uint32_t id, uint8_t flags, uint8_t level, Vec2i c, int r_tiles) {
    LightZone z;
    z.zone_id = id; z.flags = flags; z.spell_level = level;
    for (int y = c.y - r_tiles; y <= c.y + r_tiles; ++y)
        for (int x = c.x - r_tiles; x <= c.x + r_tiles; ++x) z.tiles.push_back({x, y});
    return z;
}
LightWorld world_of(const Grid* g, std::vector<LightSource> s, std::vector<LightZone> z = {}, Ambient a = {}) {
    LightWorld w;
    w.grid = g; w.sources = std::move(s); w.zones = std::move(z); w.ambient = a;
    w.normalise();
    return w;
}
Light at(const LightWorld& w, int x, int y) { return light_at(w, Vec2i{x, y}).level; }
} // namespace

TEST_CASE("light: defaults are Dark everywhere (no sources, underground ambient)") {
    LightWorld w;
    CHECK(light_at(w, {3, 3}) == LightSample{});
    CHECK(light_at(w, {-40, 7}) == LightSample{});
}

TEST_CASE("light: SRD radii - torch 20/20, lantern 30/30, Light 20/20, Daylight 60/60 (Chebyshev, 5 ft tiles)") {
    struct Case { uint16_t b, d; };
    for (Case c : {Case{kTorchB, kTorchD}, Case{kLanternB, kLanternD}, Case{kLightB, kLightD}, Case{kDaylightB, kDaylightD}}) {
        const auto w = world_of(nullptr, {src(1, {0, 0}, c.b, c.d)});
        const int bt = c.b / 5, dt = (c.b + c.d) / 5;
        CHECK(at(w, 0, 0) == Light::Bright);
        CHECK(at(w, bt, 0) == Light::Bright);
        CHECK(at(w, bt, bt) == Light::Bright);        // diagonal counts 5 ft per tile
        CHECK(at(w, -bt, 0) == Light::Bright);
        CHECK(at(w, bt + 1, 0) == Light::Dim);
        CHECK(at(w, dt, -dt) == Light::Dim);
        CHECK(at(w, dt + 1, 0) == Light::Dark);
        CHECK(at(w, 0, -(dt + 1)) == Light::Dark);
    }
    // Hooded lantern state (dim 5 only).
    const auto h = world_of(nullptr, {src(1, {0, 0}, 0, 5)});
    CHECK(at(h, 0, 0) == Light::Dim);
    CHECK(at(h, 1, 1) == Light::Dim);
    CHECK(at(h, 2, 0) == Light::Dark);
}

TEST_CASE("light: composition is the max over sources, then the ambient floor; unlit sources don't count") {
    auto unlit = src(3, {10, 0}, 60, 60);
    unlit.lit = false;
    const auto w = world_of(nullptr, {src(1, {0, 0}, kTorchB, kTorchD), src(2, {12, 0}, kTorchB, kTorchD), unlit});
    CHECK(at(w, 6, 0) == Light::Dim);      // 30 ft from both torches: dim + dim = dim (max, not a sum)
    CHECK(at(w, 4, 0) == Light::Bright);   // bright from source 1
    CHECK(at(w, 8, 0) == Light::Bright);   // bright from source 2
    CHECK(at(w, 30, 0) == Light::Dark);    // the unlit source would have lit it
    // Ambient floor: dim moonlight raises Dark to Dim, never lowers Bright.
    auto moon = w;
    moon.ambient = Ambient{AmbientKind::Moon, Light::Dim};
    CHECK(at(moon, 30, 0) == Light::Dim);
    CHECK(at(moon, 0, 0) == Light::Bright);
    CHECK((light_at(moon, {30, 0}).flags & light_flags::kSunlight) == 0);
    // Sunlight flag: from Sun ambient, or a sunlight source at the winning level.
    LightWorld day; day.ambient = Ambient{AmbientKind::Sun, Light::Bright};
    CHECK(light_at(day, {5, 5}) == LightSample{Light::Bright, light_flags::kSunlight});
    const auto dl = world_of(nullptr, {src(1, {0, 0}, kTorchB, kTorchD), src(2, {0, 1}, kDaylightB, kDaylightD,
                                                                           light_src::kMagical | light_src::kSunlight, 3)});
    CHECK(light_at(dl, {0, 0}) == LightSample{Light::Bright, light_flags::kSunlight});
    const auto tor = world_of(nullptr, {src(1, {0, 0}, kTorchB, kTorchD)});
    CHECK(light_at(tor, {0, 0}).flags == 0);
}

TEST_CASE("light: walls occlude; the lit side of a wall is lit, the far side and tiles behind are not") {
    std::vector<Vec2i> walls;
    for (int y = 0; y < 32; ++y) walls.push_back({10, y});   // a full-height wall at x = 10
    const auto ch = room(walls);
    const Grid g(ch);
    const auto w = world_of(&g, {src(1, {8, 8}, kLanternB, kLanternD)});
    CHECK(at(w, 9, 8) == Light::Bright);
    CHECK(at(w, 10, 8) == Light::Bright);     // the wall face toward the lantern
    CHECK(at(w, 11, 8) == Light::Dark);
    CHECK(at(w, 12, 3) == Light::Dark);
    // A lone pillar: a line grazing its corner is not blocked (same corner rule as cover).
    const auto pc = room({{5, 5}});
    const Grid pg(pc);
    const auto pw = world_of(&pg, {src(1, {4, 4}, kTorchB, kTorchD)});
    CHECK(at(pw, 6, 6) == Light::Bright);
    CHECK(at(pw, 5, 5) == Light::Bright);
}

TEST_CASE("light: occlusion matches cover - lit exactly when cover_of from the source tile is not Total") {
    // Walls in a few shapes; the target is a goblin; the cover origin is the light's tile (1 x 1).
    const std::vector<Vec2i> walls = {{6, 4}, {6, 5}, {6, 6}, {9, 9}, {10, 10}, {3, 8}, {4, 8}, {12, 4}, {12, 6}};
    const Vec2i s{4, 5};
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    int lit = 0, unlit = 0;
    for (int y = 1; y < 13; y += 1)
        for (int x = 1; x < 15; x += 2) {
            const Vec2i t{x, y};
            if (t == s || std::find(walls.begin(), walls.end(), t) != walls.end()) continue;
            const auto ch = room(walls, {{"srd51.character.fighter_1", {30, 30}}, {"srd51.monster.goblin", t}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, 1);
            REQUIRE(c);
            CoverQuery q{ch.spawns[0].stable_id, ch.spawns[1].stable_id};
            q.has_origin = true; q.origin_x = s.x; q.origin_y = s.y;
            const bool total = c->cover_of(q) == Cover::Total;
            const Grid g(ch);
            const auto w = world_of(&g, {});
            CHECK_MESSAGE(light_reaches(w, s, t) == !total, "t = (" << x << ", " << y << ")");
            (total ? unlit : lit) += 1;
        }
    CHECK(lit > 10);
    CHECK(unlit > 3);
}

TEST_CASE("light: opaque zones block light; fog only sets obscurement flags (R4)") {
    const auto ch = room({});
    const Grid g(ch);
    auto wall_of_fire = zone(1, rules5e::kZoneOpaque, 4, {10, 8}, 0);
    for (int y = 0; y < 20; ++y) wall_of_fire.tiles.push_back({10, y});
    const auto fog = zone(2, rules5e::kZoneHeavyObscured, 1, {6, 8}, 1);
    const auto w = world_of(&g, {src(1, {8, 8}, kLanternB, kLanternD)}, {wall_of_fire, fog});
    CHECK(at(w, 11, 8) == Light::Dark);
    CHECK(light_at(w, {6, 8}) == LightSample{Light::Bright, light_flags::kObscuredHeavy});
    CHECK(at(w, 4, 8) == Light::Bright);     // light passes through the fog
    const auto web = world_of(&g, {}, {zone(3, rules5e::kZoneLightObscured, 2, {0, 0}, 0)});
    CHECK(light_at(web, {0, 0}).flags == light_flags::kObscuredLight);
}

TEST_CASE("light: magical darkness - R1 level rule (only magical light of a higher spell level), no ambient floor") {
    const auto darkness = zone(1, rules5e::kZoneMagicalDarkness, 2, {10, 10}, 3);   // Darkness, 2nd level, 15 ft
    const LightSource torch = src(1, {10, 10}, kTorchB, kTorchD);
    const LightSource light_cantrip = src(2, {10, 10}, kLightB, kLightD, light_src::kMagical, 0);
    const LightSource second = src(3, {10, 10}, kLightB, kLightD, light_src::kMagical, 2);
    const LightSource daylight_src = src(4, {10, 10}, kDaylightB, kDaylightD, light_src::kMagical | light_src::kSunlight, 3);
    const Ambient sun{AmbientKind::Sun, Light::Bright};
    for (const LightSource& s : {torch, light_cantrip, second}) {
        const auto w = world_of(nullptr, {s}, {darkness}, sun);
        CHECK(light_at(w, {10, 10}) == LightSample{Light::Dark, light_flags::kMagicalDark});
        CHECK(light_at(w, {13, 13}) == LightSample{Light::Dark, light_flags::kMagicalDark});
        CHECK(light_at(w, {14, 10}).level == Light::Bright);   // outside the darkness
    }
    const auto dw = world_of(nullptr, {daylight_src}, {darkness});
    CHECK(light_at(dw, {10, 10}) == LightSample{Light::Bright, uint8_t(light_flags::kMagicalDark | light_flags::kSunlight)});
    // Two overlapping darknesses: the higher level decides.
    const auto d3 = zone(2, rules5e::kZoneMagicalDarkness, 3, {10, 10}, 0);
    const auto both = world_of(nullptr, {daylight_src}, {darkness, d3});
    CHECK(at(both, 10, 10) == Light::Dark);
    CHECK(at(both, 11, 10) == Light::Bright);
}

TEST_CASE("light: LightCache equals light_at everywhere; frozen per step; incremental invalidation equals a rebuild") {
    const auto ch = room({{6, 4}, {6, 5}, {6, 6}, {20, 20}});
    const Grid g(ch);
    LightWorld w = world_of(&g, {src(1, {4, 5}, kTorchB, kTorchD), src(2, {25, 25}, kLanternB, kLanternD)},
                            {zone(1, rules5e::kZoneMagicalDarkness, 2, {12, 12}, 2)}, Ambient{AmbientKind::Moon, Light::Dim});
    LightCache cache(w);
    for (int y = -2; y < 34; ++y)
        for (int x = -2; x < 34; ++x) REQUIRE(cache.at({x, y}) == light_at(w, {x, y}));
    // Frozen: changing the caller's world does not affect this step.
    w.sources[0].tile = {0, 0};
    CHECK(cache.at({4, 5}) == light_at(cache.world(), {4, 5}));
    CHECK(cache.at({4, 5}).level == Light::Bright);
    // Next step: the torch moved from (4,5) to (0,0); dirty = old and new halo (radius 8 tiles).
    const RectI dirty[] = {{4 - 8, 5 - 8, 17, 17}, {0 - 8, 0 - 8, 17, 17}};
    LightWorld moved = w;
    cache.begin_step(moved, dirty);
    LightCache fresh(moved);
    for (int y = -2; y < 34; ++y)
        for (int x = -2; x < 34; ++x) REQUIRE(cache.at({x, y}) == fresh.at({x, y}));
}

TEST_CASE("light: LightFrame - per-chunk samples, sorted sources, dirty chunks and sources against the previous frame") {
    LightWorld w = world_of(nullptr, {src(7, {5, 5}, kTorchB, kTorchD), static_source({1, 0, 0}, 0, StaticLight{{2, 2}, 30, 30, 0, 0}, "lantern")});
    LightCache cache(w);
    const world::ChunkCoord coords[] = {{1, 0, 0}, {0, 0, 0}, {1, 0, 0}};
    auto f1 = publish_light_frame(cache, coords, 1, nullptr);
    REQUIRE(f1->chunks.size() == 2);
    CHECK(f1->chunks[0].coord == world::ChunkCoord{0, 0, 0});
    CHECK(f1->chunks[0].samples[5 * 32 + 5].level == Light::Bright);
    CHECK(f1->chunks[1].samples[2 * 32 + 2].level == Light::Bright);   // world (34, 2)
    REQUIRE(f1->sources.size() == 2);
    CHECK(f1->sources[0].id.kind == LightKind::Static);                // static ids sort before attached
    CHECK(f1->sources[0].id == LightId{LightKind::Static, 1, 0, 0, 0});
    CHECK(f1->sources[0].preset == "lantern");
    CHECK(f1->dirty_chunks.size() == 2);
    CHECK(f1->dirty_sources.size() == 2);
    // Same world next step: nothing dirty.
    cache.begin_step(w);
    auto f2 = publish_light_frame(cache, coords, 2, f1.get());
    CHECK(f2->dirty_chunks.empty());
    CHECK(f2->dirty_sources.empty());
    // Douse the torch: chunk (0,0) and source 7 are dirty, chunk (1,0) is not.
    w.sources[1].lit = false;
    cache.begin_step(w);
    auto f3 = publish_light_frame(cache, coords, 3, f2.get());
    CHECK(f3->dirty_chunks == std::vector<world::ChunkCoord>{{0, 0, 0}});
    CHECK(f3->dirty_sources == std::vector<LightId>{LightId{LightKind::Attached, 0, 0, 0, 7}});
}

TEST_CASE("light: deterministic - input order doesn't matter, and a fixed scene has a pinned digest") {
    const auto ch = room({{6, 4}, {6, 5}, {6, 6}, {15, 3}, {16, 3}, {17, 3}});
    const Grid g(ch);
    std::vector<LightSource> s = {src(1, {4, 5}, kTorchB, kTorchD), src(2, {20, 10}, kLanternB, kLanternD),
                                  src(3, {10, 20}, kDaylightB, kDaylightD, light_src::kMagical | light_src::kSunlight, 3),
                                  static_source({0, 0, 0}, 0, StaticLight{{28, 28}, 20, 20, 0, 0})};
    std::vector<LightZone> z = {zone(1, rules5e::kZoneMagicalDarkness, 2, {16, 16}, 3), zone(2, rules5e::kZoneHeavyObscured, 1, {4, 20}, 4)};
    auto digest = [&](std::vector<LightSource> ss, std::vector<LightZone> zz) {
        LightCache c(world_of(&g, std::move(ss), std::move(zz), Ambient{AmbientKind::Moon, Light::Dim}));
        uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const LightSample v = c.at({x, y});
                for (uint8_t b : {uint8_t(v.level), v.flags}) { h ^= b; h *= 1099511628211ull; }
            }
        return h;
    };
    const uint64_t a = digest(s, z);
    std::reverse(s.begin(), s.end());
    std::reverse(z.begin(), z.end());
    CHECK(digest(s, z) == a);
    CHECK(a == 0x85ca0b49f3232ab7ull);   // pinned: any change to composition or the trace moves this
}
