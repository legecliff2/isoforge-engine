// Incremental light rebuild (LightCache::begin_step_incremental, VisibilityState seen_now reuse, LightDriver::incremental):
// randomised mutation of lights, walls, zones, viewers and the chunk set over many steps, driven by the engine PRNG
// (rng::Pcg32, fixed seed), asserting every incremental frame equals the full-rebuild frame bit for bit.
#include <doctest/doctest.h>

#include <memory>
#include <vector>

#include "rng/rng.hpp"
#include "rules5e/spells.hpp"
#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/grid.hpp"
#include "sim/light.hpp"
#include "sim/light_driver.hpp"
#include "sim/visibility.hpp"
#include "world/chunk.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
constexpr int N = world::kChunkSize;

void check_same_frame(const LightFrame& a, const LightFrame& b) {
    REQUIRE(a.chunks.size() == b.chunks.size());
    for (std::size_t i = 0; i < a.chunks.size(); ++i) {
        CHECK(a.chunks[i].coord == b.chunks[i].coord);
        CHECK(a.chunks[i].samples == b.chunks[i].samples);
    }
    CHECK(a.dirty_chunks == b.dirty_chunks);
    CHECK(a.dirty_rects == b.dirty_rects);
    CHECK(a.dirty_sources == b.dirty_sources);
    CHECK(a.added_chunks == b.added_chunks);
    CHECK(a.removed_chunks == b.removed_chunks);
    CHECK(a.visibility == b.visibility);
    CHECK(a.dirty_vis_chunks == b.dirty_vis_chunks);
}

uint16_t radius(rng::Pcg32& r) { return uint16_t(5 * r.range(0, 8)); }
} // namespace

TEST_CASE("light incremental: random lights / walls / zones / viewers over many steps equal the full rebuild") {
    rng::Pcg32 r(0x4C494E43u, 7);
    // A 3 x 3 window with sparse random walls; the chunk set itself changes now and then.
    std::vector<world::Chunk> cs;
    for (int cy = -1; cy <= 1; ++cy)
        for (int cx = -1; cx <= 1; ++cx) {
            world::Chunk c{};
            c.coord = {cx, cy, 0};
            c.palette = {"", "floor", "wall"};
            for (auto& cell : c.layers[0]) cell.tile = 1;
            for (auto& cell : c.layers[2])
                if (r.chance(1, 12)) cell.flags = world::tile_flags::kBlocksSight;
            cs.push_back(c);
        }
    std::vector<world::ChunkCoord> all;
    for (const auto& c : cs) all.push_back(c.coord);

    std::vector<LightSource> sources;
    uint32_t next_id = 1;
    auto add_source = [&] {
        AttachedLight a;
        a.id = next_id++;
        a.carrier = Carrier::Tile;
        a.tile = Vec2i{r.range(-N, 2 * N - 1), r.range(-N, 2 * N - 1)};
        a.bright_ft = radius(r); a.dim_ft = radius(r);
        a.flags = uint8_t(light_src::kLit | (r.chance(1, 4) ? light_src::kMagical : 0) | (r.chance(1, 6) ? light_src::kSunlight : 0));
        a.spell_level = uint8_t(r.range(0, 4));
        sources.push_back(attached_source(a, a.tile));
    };
    for (int i = 0; i < 10; ++i) add_source();
    // Static sources too (kind-tagged ids), one per chunk.
    for (std::size_t i = 0; i < cs.size(); ++i) {
        StaticLight sl;
        sl.tile = Vec2i{r.range(0, N - 1), r.range(0, N - 1)};
        sl.bright_ft = 20; sl.dim_ft = 20;
        sources.push_back(static_source(cs[i].coord, 0, sl, "torch"));
    }
    std::vector<Viewer> viewers;
    for (int i = 0; i < 6; ++i) {
        Viewer v;
        v.id = uint32_t(i + 1);
        v.team = TeamId(i % 3);
        v.tile = Vec2i{r.range(-N, 2 * N - 1), r.range(-N, 2 * N - 1)};
        v.anchor = v.tile;
        v.footprint = 1;
        if (i % 2) v.senses.darkvision_ft = 60;
        viewers.push_back(v);
    }
    std::vector<LightZone> zones;
    Ambient ambient;

    LightCache full, inc;
    VisibilityState vfull, vinc;
    std::shared_ptr<const LightFrame> pfull, pinc;
    std::vector<world::ChunkCoord> chunks = all;
    int incremental_steps = 0;
    constexpr int kSteps = 400;
    for (int step = 0; step < kSteps; ++step) {
        CAPTURE(step);
        // Mutations: most steps move one or two things, as in play; a few steps change much more.
        const uint32_t n = r.chance(1, 10) ? 8u : 1u + r.below(2);
        for (uint32_t m = 0; m < n; ++m) {
            switch (r.below(12)) {
            case 0: case 1: case 2: {   // move a source by up to 2 tiles
                if (sources.empty()) break;
                LightSource& s = sources[r.below(uint32_t(sources.size()))];
                s.tile.x += r.range(-2, 2); s.tile.y += r.range(-2, 2);
                break;
            }
            case 3: if (!sources.empty()) { LightSource& s = sources[r.below(uint32_t(sources.size()))]; s.lit = !s.lit; } break;
            case 4: if (!sources.empty()) { LightSource& s = sources[r.below(uint32_t(sources.size()))]; s.bright_ft = radius(r); s.dim_ft = radius(r); } break;
            case 5:
                if (r.chance(1, 2) && sources.size() > 2) sources.erase(sources.begin() + std::ptrdiff_t(r.below(uint32_t(sources.size()))));
                else add_source();
                break;
            case 6: case 7: {   // toggle walls
                world::Chunk& c = cs[r.below(uint32_t(cs.size()))];
                const uint32_t k = r.chance(1, 3) ? 6u : 1u;
                for (uint32_t q = 0; q < k; ++q) {
                    auto& cell = c.layers[2][r.below(uint32_t(N * N))];
                    cell.flags = uint8_t(cell.flags ^ world::tile_flags::kBlocksSight);
                }
                break;
            }
            case 8: case 9: {   // move / blind / re-sense a viewer
                Viewer& v = viewers[r.below(uint32_t(viewers.size()))];
                if (r.chance(1, 8)) v.blinded = !v.blinded;
                else if (r.chance(1, 8)) v.senses.blindsight_ft = uint16_t(r.chance(1, 2) ? 10 : 0);
                else { v.tile.x += r.range(-1, 1); v.tile.y += r.range(-1, 1); v.anchor = v.tile; }
                break;
            }
            case 10: {   // zones: add / change / remove (magical darkness, fog, opaque)
                if (!zones.empty() && r.chance(1, 3)) { zones.erase(zones.begin() + std::ptrdiff_t(r.below(uint32_t(zones.size())))); break; }
                LightZone z;
                z.zone_id = r.below(6) + 1;
                const uint32_t kinds[] = {rules5e::kZoneMagicalDarkness, rules5e::kZoneHeavyObscured, rules5e::kZoneLightObscured, rules5e::kZoneOpaque};
                z.flags = uint8_t(kinds[r.below(4)]);
                z.spell_level = uint8_t(r.range(0, 3));
                const Vec2i c{r.range(-N, 2 * N - 1), r.range(-N, 2 * N - 1)};
                const int32_t rad = r.range(0, 3);
                for (int32_t y = -rad; y <= rad; ++y)
                    for (int32_t x = -rad; x <= rad; ++x) z.tiles.push_back(Vec2i{c.x + x, c.y + y});
                bool replaced = false;
                for (auto& oz : zones)
                    if (oz.zone_id == z.zone_id) { oz = z; replaced = true; }
                if (!replaced) zones.push_back(z);
                break;
            }
            default:   // rare: ambient or chunk set changes (full rebuild paths)
                if (r.chance(1, 4)) ambient.level = Light(uint8_t(r.below(3)));
                else if (r.chance(1, 4)) {
                    chunks = all;
                    if (r.chance(1, 2)) chunks.erase(chunks.begin() + std::ptrdiff_t(r.below(uint32_t(chunks.size()))));
                }
                break;
            }
        }
        std::vector<const world::Chunk*> ptrs;
        for (const auto& c : cs) ptrs.push_back(&c);
        const Grid g(ptrs);   // a fresh grid each step: the cache diffs wall values, not the grid object
        LightWorld w;
        w.grid = &g;
        w.ambient = ambient;
        w.sources = sources;
        w.zones = zones;
        if (step % 2) std::reverse(w.sources.begin(), w.sources.end());   // input order is irrelevant (normalised)

        full.begin_step(w);
        const LightCache::StepStats st = inc.begin_step_incremental(w, chunks);
        if (!st.full) ++incremental_steps;
        vfull.begin_step(full, viewers, chunks);
        vinc.begin_step(inc, viewers, chunks);
        LightFrameInputs fi;
        fi.chunks = chunks;
        fi.visibility = &vfull;
        auto ff = publish_light_frame(full, fi, uint64_t(step + 1), pfull.get());
        fi.visibility = &vinc;
        auto fn = publish_light_frame(inc, fi, uint64_t(step + 1), pinc.get());
        check_same_frame(*ff, *fn);
        // And against the pure reference on a sparse sample of tiles.
        for (int q = 0; q < 16; ++q) {
            const world::ChunkCoord c = chunks[r.below(uint32_t(chunks.size()))];
            const Vec2i t = Grid::to_world(c, Vec2i{r.range(0, N - 1), r.range(0, N - 1)});
            LightWorld wn = w;
            wn.normalise();
            CHECK(inc.at(t) == light_at(wn, t));
        }
        pfull = ff; pinc = fn;
    }
    CHECK(incremental_steps > kSteps / 2);   // the diff path is really exercised, not always falling back
}

TEST_CASE("light incremental: an unchanged step invalidates nothing; one moved torch only its two halos") {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const world::Chunk* one[] = {&c};
    const Grid g{std::span<const world::Chunk* const>(one)};
    LightWorld w;
    w.grid = &g;
    AttachedLight a;
    a.id = 1; a.carrier = Carrier::Tile; a.tile = {10, 10}; a.bright_ft = 20; a.dim_ft = 20; a.flags = light_src::kLit;
    w.sources.push_back(attached_source(a, a.tile));
    const world::ChunkCoord cc[] = {{0, 0, 0}};
    LightCache cache;
    CHECK(cache.begin_step_incremental(w, cc).full);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) (void)cache.at({x, y});
    auto st = cache.begin_step_incremental(w, cc);
    CHECK_FALSE(st.full);
    CHECK(st.dirty_rects == 0);
    CHECK(cache.memoised() == std::size_t(N * N));
    w.sources[0].tile = {11, 10};
    st = cache.begin_step_incremental(w, cc);
    CHECK_FALSE(st.full);
    CHECK(st.dirty_rects == 2);
    CHECK(cache.memoised() < std::size_t(N * N));
    cache.begin_step(w);   // a plain begin_step drops the incremental baseline
    CHECK(cache.begin_step_incremental(w, cc).full);
}

namespace {
world::Chunk walled_room() {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (int y = 2; y < 30; ++y)
        if (y % 7 != 3) c.layers[2][std::size_t(y * N + 16)].flags = world::tile_flags::kBlocksSight;
    const std::pair<const char*, Vec2i> spawns[] = {{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {24, 20}},
                                                    {"srd51.monster.goblin", {26, 8}}};
    for (std::size_t i = 0; i < 3; ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    world::Chunk::Light l;
    l.tile = {8, 24}; l.bright_ft = 20; l.dim_ft = 20; l.preset = "srd51.core.light.torch";
    l.stable_id = world::chunk_light_stable_id(c.coord, 0);
    c.lights.push_back(l);
    c.content_hash = world::compute_content_hash(c);
    return c;
}
} // namespace

TEST_CASE("light incremental: LightDriver frames equal the full-rebuild driver over a whole combat, hashes unchanged") {
    static ContentLibrary lib = [] { ContentLibrary x; REQUIRE(x.load(sample_set()).empty()); return x; }();
    const world::Chunk ch = walled_room();
    const world::Chunk* one[] = {&ch};
    // Game torches that wander pseudo-randomly with the step (engine PRNG seeded from the log length: deterministic).
    auto hook = [](const Combat& c, std::vector<LightSource>& out) {
        rng::Pcg32 r(0x70524348u + c.log().size(), 3);
        for (uint32_t i = 0; i < 3; ++i) {
            AttachedLight t;
            t.id = i + 1;
            if (i == 0) {
                t.carrier = Carrier::Creature; t.carrier_id = c.state().combatants[0].id;
            } else {
                t.carrier = Carrier::Tile; t.tile = Vec2i{r.range(0, N - 1), r.range(0, N - 1)};
            }
            t.bright_ft = uint16_t(5 * r.range(0, 6)); t.dim_ft = 20;
            t.flags = uint8_t(r.chance(3, 4) ? light_src::kLit : 0);
            const Vec2i at = t.carrier == Carrier::Creature ? carrier_centre(c.pos(t.carrier_id), c.footprint_size(t.carrier_id)) : t.tile;
            out.push_back(attached_source(t, at, "torch"));
        }
    };
    LightProviderInputs a, b;
    a.chunks = {&ch}; b.chunks = {&ch};
    a.attached_sources = hook; b.attached_sources = hook;
    b.incremental = false;
    LightDriver da(std::move(a)), db(std::move(b));
    ecs::Registry ra, rb;
    auto ca = Combat::create(ra, std::span<const world::Chunk* const>(one), lib, 11);
    auto cb = Combat::create(rb, std::span<const world::Chunk* const>(one), lib, 11);
    REQUIRE(ca);
    REQUIRE(cb);
    ca->attach_light_driver(&da);
    cb->attach_light_driver(&db);
    int steps = 0, inc_steps = 0;
    while (!ca->ended() && steps < 300) {
        auto ra1 = ca->submit(next_command(*ca, std::nullopt));
        auto rb1 = cb->submit(next_command(*cb, std::nullopt));
        REQUIRE(ra1.ok == rb1.ok);
        if (!ra1.ok) break;
        ++steps;
        if (!da.last_stats().full) ++inc_steps;
        check_same_frame(*da.latest(), *db.latest());
    }
    CHECK(steps > 5);
    CHECK(inc_steps > 0);
    CHECK(ca->hash() == cb->hash());
    CHECK(ca->log_hash() == cb->log_hash());
}
