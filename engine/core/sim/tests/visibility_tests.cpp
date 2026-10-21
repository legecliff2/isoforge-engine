// R-G13 phase 2 (docs/proposals/LIGHTING.md §10): per-team seen_now / explored masks, the TileMask bit layout, the
// per-rect dirty set, added / removed / blocked chunks, the halo split, and that none of it reaches any hash.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>

#include "rules5e/encounter.hpp"
#include "rules5e/spells.hpp"
#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/light.hpp"
#include "sim/visibility.hpp"
#include "world/chunk_codec.hpp"
#include "world/chunk_streamer.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
world::Chunk room_at(world::ChunkCoord cc, const std::vector<Vec2i>& walls,
                     std::vector<std::pair<std::string, Vec2i>> spawns = {}) {
    world::Chunk c{};
    c.coord = cc;
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
LightZone zone(uint32_t id, uint8_t flags, uint8_t level, Vec2i c, int r) {
    LightZone z;
    z.zone_id = id; z.flags = flags; z.spell_level = level;
    for (int y = c.y - r; y <= c.y + r; ++y)
        for (int x = c.x - r; x <= c.x + r; ++x) z.tiles.push_back({x, y});
    return z;
}
Viewer viewer(uint32_t id, TeamId team, Vec2i at, SenseRanges s = {}, bool blind = false) {
    Viewer v;
    v.id = id; v.team = team; v.tile = at; v.senses = s; v.blinded = blind;
    return v;
}
rules5e::Light to_rules(Light l) {
    return l == Light::Bright ? rules5e::Light::Bright : l == Light::Dim ? rules5e::Light::Dim : rules5e::Light::Dark;
}
// The rules' own answer for a visible, grounded subject standing on t (SRD derive_sight).
bool rules_can_see(const LightWorld& w, const Viewer& v, Vec2i t) {
    rules5e::Combatant c{};
    c.darkvision_ft = v.senses.darkvision_ft; c.blindsight_ft = v.senses.blindsight_ft;
    c.tremorsense_ft = v.senses.tremorsense_ft; c.truesight_ft = v.senses.truesight_ft;
    if (v.blinded) c.conditions |= rules5e::bit(rules5e::Condition::Blinded);
    auto tl = [&](Vec2i p) {
        const LightSample s = light_at(w, p);
        rules5e::TileLight o;
        o.light = to_rules(s.level);
        o.magical_darkness = (s.flags & light_flags::kMagicalDark) != 0;
        o.sunlight = (s.flags & light_flags::kSunlight) != 0;
        o.obscured = (s.flags & light_flags::kObscuredHeavy) ? rules5e::Obscured::Heavy
                     : (s.flags & light_flags::kObscuredLight) ? rules5e::Obscured::Light : rules5e::Obscured::None;
        return o;
    };
    rules5e::SightFacts f;
    f.subject = tl(t);
    f.viewer = tl(v.tile);
    const int dx = std::abs(v.tile.x - t.x), dy = std::abs(v.tile.y - t.y);
    f.distance_ft = uint16_t(std::max(dx, dy) * 5);
    f.line_of_sight = light_reaches(w, v.tile, t);
    return rules5e::derive_sight(c, f).can_see;
}
const TileMask* seen_mask(const std::vector<TeamVisibility>& teams, TeamId team, world::ChunkCoord c, bool explored) {
    for (const auto& tv : teams)
        if (tv.team == team)
            for (const auto& cv : tv.chunks)
                if (cv.coord == c) return explored ? &cv.explored : &cv.seen_now;
    return nullptr;
}
bool bit(const std::vector<TeamVisibility>& teams, TeamId team, world::ChunkCoord c, int x, int y, bool explored = false) {
    const TileMask* m = seen_mask(teams, team, c, explored);
    return m && m->test(x, y);
}
} // namespace

TEST_CASE("visibility: TileMask layout - bit = y*32+x, word = bit/64, LSB-first (bit & 63)") {
    TileMask m;
    m.set(0, 0);
    CHECK(m.words[0] == 1u);
    m = {}; m.set(1, 0);
    CHECK(m.words[0] == 2u);
    m = {}; m.set(31, 0);
    CHECK(m.words[0] == uint64_t(1) << 31);
    m = {}; m.set(0, 1);
    CHECK(m.words[0] == uint64_t(1) << 32);
    m = {}; m.set(31, 1);
    CHECK(m.words[0] == uint64_t(1) << 63);
    m = {}; m.set(0, 2);
    CHECK(m.words[1] == 1u);
    m = {}; m.set(5, 7);   // bit 229: word 3, bit 37
    CHECK(m.words[3] == uint64_t(1) << 37);
    m = {}; m.set(31, 31);
    CHECK(m.words[15] == uint64_t(1) << 63);
    for (std::size_t i = 0; i < 15; ++i) CHECK(m.words[i] == 0u);
    CHECK(m.test(31, 31));
    CHECK_FALSE(m.test(30, 31));
    CHECK(TileMask::bit_of(5, 7) == 229);
}

TEST_CASE("visibility: senses come from the combatant; darkvision raises one level outside magical darkness") {
    rules5e::Combatant c{};
    CHECK(senses_of(c) == SenseRanges{});   // default: normal sight
    c.darkvision_ft = 60; c.truesight_ft = 10;
    CHECK(senses_of(c) == SenseRanges{60, 0, 0, 10});
    const SenseRanges dv{60, 0, 0, 0};
    CHECK(perceived_light({Light::Dark, 0}, dv, 60) == Light::Dim);
    CHECK(perceived_light({Light::Dim, 0}, dv, 30) == Light::Bright);
    CHECK(perceived_light({Light::Dark, 0}, dv, 65) == Light::Dark);
    CHECK(perceived_light({Light::Dark, light_flags::kMagicalDark}, dv, 5) == Light::Dark);
    CHECK(perceived_light({Light::Dark, 0}, SenseRanges{}, 5) == Light::Dark);
}

TEST_CASE("visibility: masks equal the brute-force reference and rules5e::derive_sight on every tile (two chunks)") {
    const auto a = room_at({0, 0, 0}, {{6, 4}, {6, 5}, {6, 6}, {9, 9}, {10, 10}, {20, 3}, {20, 4}, {20, 5}, {31, 12}, {31, 13}});
    const auto b = room_at({1, 0, 0}, {{2, 10}, {3, 10}, {4, 10}, {12, 20}});
    const world::Chunk* cs[] = {&a, &b};
    const Grid g{std::span<const world::Chunk* const>(cs)};
    REQUIRE(g.ok());
    LightWorld w;
    w.grid = &g;
    w.ambient = Ambient{AmbientKind::Moon, Light::Dark};
    w.sources = {src(1, {4, 5}, 20, 20), src(2, {36, 14}, 30, 30), src(3, {24, 24}, 60, 60, light_src::kMagical, 3)};
    w.zones = {zone(1, rules5e::kZoneMagicalDarkness, 2, {14, 20}, 3), zone(2, rules5e::kZoneHeavyObscured, 1, {40, 4}, 3),
               zone(3, rules5e::kZoneOpaque, 4, {28, 8}, 1), zone(4, rules5e::kZoneLightObscured, 1, {50, 26}, 2)};
    w.normalise();
    LightCache cache(w);
    const std::vector<Viewer> vs = {
        viewer(10, 0, {4, 6}),                                        // normal sight, by a torch
        viewer(11, 0, {14, 19}, SenseRanges{60, 0, 0, 0}),            // darkvision, inside magical darkness
        viewer(20, 1, {40, 20}, SenseRanges{60, 0, 0, 0}),            // darkvision in the dark
        viewer(21, 1, {30, 30}, SenseRanges{0, 10, 30, 0}),           // blindsight + tremorsense
        viewer(30, 2, {44, 6}, SenseRanges{0, 0, 0, 30}),             // truesight in fog
        viewer(31, 2, {4, 4}, SenseRanges{120, 0, 0, 0}, true),       // blinded
    };
    const world::ChunkCoord chunks[] = {{1, 0, 0}, {0, 0, 0}};
    VisibilityState vis;
    vis.begin_step(cache, vs, chunks);
    REQUIRE(vis.teams().size() == 3);
    int seen = 0, unseen = 0;
    for (TeamId team = 0; team < 3; ++team)
        for (const auto& c : chunks)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    const Vec2i t = Grid::to_world(c, {x, y});
                    bool ref = false, rules = false;
                    for (const auto& v : vs)
                        if (v.team == team) {
                            ref = ref || tile_visible(w, cache, v, t);
                            rules = rules || rules_can_see(w, v, t);
                        }
                    REQUIRE_MESSAGE(ref == rules, "team " << int(team) << " t = (" << t.x << ", " << t.y << ")");
                    REQUIRE_MESSAGE(bit(vis.teams(), team, c, x, y) == ref, "team " << int(team) << " t = (" << t.x << ", " << t.y << ")");
                    REQUIRE(bit(vis.teams(), team, c, x, y, true) == ref);   // first step: explored == seen_now
                    (ref ? seen : unseen) += 1;
                }
    CHECK(seen > 300);
    CHECK(unseen > 300);
    // Spot checks: the blinded viewer alone sees nothing; blindsight sees its own dark corner through nothing.
    VisibilityState only_blind;
    const Viewer blind[] = {vs[5]};
    only_blind.begin_step(cache, blind, chunks);
    REQUIRE(only_blind.teams().size() == 1);
    CHECK(only_blind.teams()[0].chunks.empty());
    CHECK(bit(vis.teams(), 1, {0, 0, 0}, 30, 30));
}

TEST_CASE("visibility: explored accumulates, survives a chunk leaving the frame, and is path independent") {
    const auto a = room_at({0, 0, 0}, {{10, 0}, {10, 1}, {10, 2}, {10, 3}, {10, 4}, {10, 5}, {10, 6}, {10, 7}});
    const Grid g(a);
    auto world_with_torch = [&](Vec2i at) {
        LightWorld w;
        w.grid = &g;
        w.sources = {src(1, at, 20, 20)};
        w.normalise();
        return w;
    };
    const world::ChunkCoord c0[] = {{0, 0, 0}};
    auto walk = [&](const std::vector<Vec2i>& path) {
        VisibilityState vis;
        LightCache cache;
        std::shared_ptr<const LightFrame> f;
        uint64_t step = 0;
        for (Vec2i p : path) {
            cache.begin_step(world_with_torch(p));
            const Viewer v[] = {viewer(1, 0, p)};
            vis.begin_step(cache, v, c0);
            LightFrameInputs in;
            in.chunks = c0;
            in.visibility = &vis;
            f = publish_light_frame(cache, in, ++step, f.get());
        }
        return std::make_pair(vis.explored(0, {0, 0, 0}), f);
    };
    const auto [ex1, f1] = walk({{2, 2}, {20, 3}, {5, 25}, {25, 25}});
    const auto [ex2, f2] = walk({{20, 3}, {2, 2}, {5, 25}, {25, 25}});
    const auto [ex3, f3] = walk({{5, 25}, {20, 3}, {2, 2}, {25, 25}});
    CHECK(ex1 == ex2);
    CHECK(ex1 == ex3);
    CHECK(f1->visibility == f2->visibility);
    CHECK(f1->visibility == f3->visibility);
    CHECK(f1->chunks[0].samples == f3->chunks[0].samples);
    // explored ⊇ seen_now, and strictly more than the final step sees.
    const auto& cv = f1->visibility[0].chunks[0];
    int only_explored = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            if (cv.seen_now.test(x, y)) CHECK(cv.explored.test(x, y));
            if (cv.explored.test(x, y) && !cv.seen_now.test(x, y)) ++only_explored;
        }
    CHECK(only_explored > 0);
    // A step whose frame excludes the chunk keeps explored; a team without viewers keeps its masks, seen_now empty.
    VisibilityState vis;
    LightCache cache(world_with_torch({2, 2}));
    const Viewer v[] = {viewer(1, 0, {2, 2})};
    vis.begin_step(cache, v, c0);
    const TileMask before = vis.explored(0, {0, 0, 0});
    REQUIRE(before.any());
    vis.begin_step(cache, v, {});
    CHECK(vis.explored(0, {0, 0, 0}) == before);
    vis.begin_step(cache, {}, c0);
    REQUIRE(vis.teams().size() == 1);
    REQUIRE(vis.teams()[0].chunks.size() == 1);
    CHECK_FALSE(vis.teams()[0].chunks[0].seen_now.any());
    CHECK(vis.teams()[0].chunks[0].explored == before);
}

TEST_CASE("visibility: incremental cache invalidation gives the same masks as a rebuild") {
    const auto a = room_at({0, 0, 0}, {{6, 4}, {6, 5}, {6, 6}, {15, 15}});
    const Grid g(a);
    LightWorld w;
    w.grid = &g;
    w.sources = {src(1, {4, 5}, 20, 20), src(2, {25, 25}, 30, 30)};
    w.normalise();
    LightCache inc(w);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) (void)inc.at({x, y});
    w.sources[0].tile = {0, 0};
    const RectI dirty[] = {{4 - 8, 5 - 8, 17, 17}, {-8, -8, 17, 17}};
    inc.begin_step(w, dirty);
    LightCache full(w);
    const Viewer vs[] = {viewer(1, 0, {1, 1}), viewer(2, 1, {26, 26}, SenseRanges{30, 0, 0, 0})};
    const world::ChunkCoord c0[] = {{0, 0, 0}};
    VisibilityState v1, v2;
    v1.begin_step(inc, vs, c0);
    v2.begin_step(full, vs, c0);
    CHECK(v1.teams() == v2.teams());
}

TEST_CASE("visibility: LightFrame dirty rects, added / removed / blocked chunks and dirty visibility chunks") {
    LightWorld w;
    w.sources = {src(7, {5, 5}, 20, 20)};
    w.normalise();
    LightCache cache(w);
    const world::ChunkCoord c00{0, 0, 0}, c10{1, 0, 0}, c20{2, 0, 0};
    VisibilityState vis;
    const Viewer v[] = {viewer(1, 0, {5, 5})};
    const world::ChunkCoord both[] = {c10, c00};
    vis.begin_step(cache, v, both);
    LightFrameInputs in;
    in.chunks = both;
    in.visibility = &vis;
    auto f1 = publish_light_frame(cache, in, 1, nullptr);
    CHECK(f1->added_chunks == std::vector<world::ChunkCoord>{c00, c10});
    CHECK(f1->removed_chunks.empty());
    CHECK(f1->blocked_chunks.empty());
    REQUIRE(f1->dirty_rects.size() == 2);
    CHECK(f1->dirty_rects[0] == DirtyRect{c00, 0, 0, 31, 31});   // new: whole chunk
    CHECK(f1->dirty_chunks == std::vector<world::ChunkCoord>{c00, c10});
    CHECK(f1->dirty_vis_chunks == std::vector<world::ChunkCoord>{c00});   // c10 is beyond the torch: unseen
    REQUIRE(f1->visibility.size() == 1);

    // The torch moves 1 tile east: the dirty rect is the bounding box of the changed samples only.
    w.sources[0].tile = {6, 5};
    cache.begin_step(w);
    const Viewer v2[] = {viewer(1, 0, {6, 5})};
    vis.begin_step(cache, v2, both);
    auto f2 = publish_light_frame(cache, in, 2, f1.get());
    REQUIRE(f2->dirty_rects.size() == 1);
    const DirtyRect r = f2->dirty_rects[0];
    CHECK(r.coord == c00);
    // Old 4..4+... ring: bright r=4 tiles, dim to 8: x from 5-8=-3 (clamped 0) .. 6+8 = 14; the columns that changed.
    CHECK(r.x0 <= r.x1);
    CHECK(r.x1 == 14);
    CHECK(r.y0 == 0);
    CHECK(r.y1 == 13);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const bool inside = x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
            if (!inside) CHECK(f2->chunks[0].samples[std::size_t(y * 32 + x)] == f1->chunks[0].samples[std::size_t(y * 32 + x)]);
        }
    CHECK(f2->added_chunks.empty());
    CHECK(f2->removed_chunks.empty());
    CHECK(f2->dirty_vis_chunks == std::vector<world::ChunkCoord>{c00});

    // c10 becomes blocked (halo pending), c20 is wanted and blocked too: neither has samples, both tracked.
    const world::ChunkCoord ready[] = {c00};
    const world::ChunkCoord blocked[] = {c20, c10};
    in.chunks = ready;
    in.blocked = blocked;
    vis.begin_step(cache, v2, ready);
    auto f3 = publish_light_frame(cache, in, 3, f2.get());
    CHECK(f3->chunks.size() == 1);
    CHECK(f3->blocked_chunks == std::vector<world::ChunkCoord>{c10, c20});
    CHECK(f3->added_chunks == std::vector<world::ChunkCoord>{c20});
    CHECK(f3->removed_chunks.empty());   // c10 is held, not freed
    CHECK(f3->dirty_chunks.empty());
    CHECK(f3->dirty_vis_chunks.empty());

    // c10 completes again (its samples are dirty: the previous frame had none); c20 is no longer wanted.
    in.chunks = both;
    in.blocked = {};
    vis.begin_step(cache, v2, both);
    auto f4 = publish_light_frame(cache, in, 4, f3.get());
    CHECK(f4->added_chunks.empty());
    CHECK(f4->removed_chunks == std::vector<world::ChunkCoord>{c20});
    CHECK(f4->dirty_chunks == std::vector<world::ChunkCoord>{c10});
    CHECK(f4->dirty_rects == std::vector<DirtyRect>{DirtyRect{c10, 0, 0, 31, 31}});

    // A chunk listed both ready and blocked counts as ready.
    const world::ChunkCoord dup[] = {c00};
    in.chunks = dup;
    in.blocked = dup;
    auto f5 = publish_light_frame(cache, in, 5, f4.get());
    CHECK(f5->blocked_chunks.empty());
    // The 4-argument form publishes no visibility and no blocked chunks.
    auto f6 = publish_light_frame(cache, std::span<const world::ChunkCoord>(both), 6, nullptr);
    CHECK(f6->visibility.empty());
    CHECK(f6->dirty_vis_chunks.empty());
    CHECK(f6->added_chunks.size() == 2);
}

TEST_CASE("visibility: split_by_halo - Complete is ready, Pending and Failed are blocked, ascending and deduped") {
    const world::ChunkCoord w[] = {{2, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}, {3, 0, 0}};
    std::vector<world::ChunkCoord> ready, blocked;
    split_by_halo(w, [](world::ChunkCoord c) {
        return c.x == 1 ? world::HaloState::Pending : c.x == 3 ? world::HaloState::Failed : world::HaloState::Complete;
    }, ready, blocked);
    CHECK(ready == std::vector<world::ChunkCoord>{{0, 0, 0}, {2, 0, 0}});
    CHECK(blocked == std::vector<world::ChunkCoord>{{1, 0, 0}, {3, 0, 0}});
}

TEST_CASE("visibility: derived, never hashed - Combat::hash / log_hash unchanged by masks and frames") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    const auto ch = room_at({0, 0, 0}, {{6, 4}, {6, 5}}, {{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {12, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const uint64_t h = c->hash(), lh = c->log_hash();
    LightWorld w;
    w.grid = &c->grid();
    w.sources = {src(1, {4, 4}, 20, 20)};
    w.normalise();
    LightCache cache(w);
    std::vector<Viewer> vs;
    for (const auto& cb : c->state().combatants)
        vs.push_back(viewer(cb.id, cb.team, carrier_centre(c->pos(cb.id), c->footprint_size(cb.id)), senses_of(cb)));
    VisibilityState vis;
    const world::ChunkCoord c0[] = {{0, 0, 0}};
    vis.begin_step(cache, vs, c0);
    LightFrameInputs in;
    in.chunks = c0;
    in.visibility = &vis;
    auto f = publish_light_frame(cache, in, 1, nullptr);
    CHECK(f->visibility.size() == 2);
    CHECK(c->hash() == h);
    CHECK(c->log_hash() == lh);
}

TEST_CASE("visibility: memoised line of sight across steps equals a fresh state (viewer moves, walls / opaque zones change)") {
    const auto a = room_at({0, 0, 0}, {{6, 4}, {6, 5}, {6, 6}, {15, 15}, {16, 15}, {20, 2}});
    const Grid g(a);
    LightWorld w;
    w.grid = &g;
    w.ambient = Ambient{AmbientKind::Sun, Light::Bright};
    w.normalise();
    const world::ChunkCoord c0[] = {{0, 0, 0}};
    VisibilityState kept;
    auto step = [&](const LightWorld& lw, std::vector<Viewer> vs) {
        LightCache cache(lw);
        kept.begin_step(cache, vs, c0);
        VisibilityState fresh;
        LightCache cache2(lw);
        fresh.begin_step(cache2, vs, c0);
        REQUIRE(kept.teams().size() == fresh.teams().size());
        for (std::size_t i = 0; i < fresh.teams().size(); ++i) {
            REQUIRE(kept.teams()[i].chunks.size() == fresh.teams()[i].chunks.size());
            for (std::size_t j = 0; j < fresh.teams()[i].chunks.size(); ++j)
                CHECK(kept.teams()[i].chunks[j].seen_now == fresh.teams()[i].chunks[j].seen_now);
        }
    };
    step(w, {viewer(1, 0, {2, 5}), viewer(2, 0, {25, 25})});
    step(w, {viewer(1, 0, {2, 5}), viewer(2, 0, {25, 25})});   // warm: every answer from the memo
    step(w, {viewer(1, 0, {3, 5}), viewer(2, 0, {25, 25})});   // one viewer moved
    LightWorld walled = w;
    walled.zones = {zone(1, rules5e::kZoneOpaque, 4, {10, 5}, 1)};   // Wall of Fire-like: blocks sight
    walled.normalise();
    step(walled, {viewer(1, 0, {3, 5}), viewer(2, 0, {25, 25})});
    step(w, {viewer(1, 0, {3, 5}), viewer(2, 0, {25, 25})});        // the zone ends
}
