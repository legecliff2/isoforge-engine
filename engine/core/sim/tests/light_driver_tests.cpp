// R-G13 phase 2 PR 2: LightDriver (sim-driven LightFrames), the halo gate blocking the step, combat pins, chunk@2 static
// lights reaching the frame, senses from content, and that none of it moves a hash.
#include <doctest/doctest.h>

#include <atomic>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/light_driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/chunk_streamer.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
world::Chunk room_at(world::ChunkCoord cc, std::vector<std::pair<std::string, Vec2i>> spawns = {}) {
    world::Chunk c{};
    c.coord = cc;
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
world::Chunk::Light lamp(Vec2i t, uint16_t b, uint16_t d, uint32_t idx, world::ChunkCoord cc) {
    world::Chunk::Light l;
    l.tile = t; l.bright_ft = b; l.dim_ft = d; l.preset = "srd51.core.light.lantern";
    l.stable_id = world::chunk_light_stable_id(cc, idx);
    return l;
}
const ContentLibrary& lib() {
    static ContentLibrary l = [] { ContentLibrary x; REQUIRE(x.load(sample_set()).empty()); return x; }();
    return l;
}
// Chunks (0,0) and (1,0) on level 0; (1,0) can fail until `fail` is cleared.
struct TwoChunks final : world::IChunkProvider {
    world::Chunk a, b;
    std::atomic<bool> fail{false};
    std::string_view name() const override { return "two"; }
    bool covers(world::ChunkCoord c) const override { return c.level == 0 && c.y == 0 && (c.x == 0 || c.x == 1); }
    world::ChunkResult load(const world::ChunkRequest& r) override {
        if (r.coord.x == 1 && fail) return {world::ChunkStatus::Error, std::nullopt, "disk on fire"};
        return {world::ChunkStatus::Ready, r.coord.x == 0 ? a : b, {}};
    }
};
NearestEnemyPolicy g_nearest;
uint64_t run_hash(Combat& c) {
    auto rr = run_to_end(c, [](CombatantId) -> IActorPolicy* { return &g_nearest; });
    REQUIRE(rr.error.empty());
    return c.hash() ^ (c.log_hash() * 31u);
}
} // namespace

TEST_CASE("light driver: senses come from monster content (darkvision etc.), zero by default") {
    const Archetype* gob = lib().find("srd51.monster.goblin");
    REQUIRE(gob);
    CHECK(gob->base.darkvision_ft == 60);   // SRD goblin: darkvision 60 ft
    CHECK(gob->base.blindsight_ft == 0);
    const Archetype* fighter = lib().find("srd51.character.fighter_1");
    REQUIRE(fighter);
    CHECK(fighter->base.darkvision_ft == 0);   // characters: no senses block (race darkvision is a gap)
}

TEST_CASE("light driver: publishes at attach and after each committed step; viewers; hashes unchanged") {
    auto ch = room_at({0, 0, 0}, {{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {12, 4}}});
    ch.lights.push_back(lamp({20, 20}, 30, 30, 0, ch.coord));
    ch.content_hash = world::compute_content_hash(ch);
    const world::Chunk* one[] = {&ch};

    ecs::Registry r0;
    auto plain = Combat::create(r0, std::span<const world::Chunk* const>(one), lib(), 3);
    REQUIRE(plain);
    const uint64_t h0 = run_hash(*plain);

    LightProviderInputs in;
    in.chunks = {&ch};
    int hook_calls = 0;
    in.attached_sources = [&](const Combat& c, std::vector<LightSource>& out) {
        ++hook_calls;
        AttachedLight t;
        t.id = 1; t.carrier = Carrier::Creature; t.carrier_id = c.state().combatants[0].id;
        t.bright_ft = 20; t.dim_ft = 20; t.flags = light_src::kLit;
        out.push_back(attached_source(t, carrier_centre(c.pos(t.carrier_id), c.footprint_size(t.carrier_id)), "torch"));
    };
    LightDriver d(std::move(in));
    ecs::Registry r1;
    auto lit = Combat::create(r1, std::span<const world::Chunk* const>(one), lib(), 3);
    REQUIRE(lit);
    CHECK(d.latest() == nullptr);
    lit->attach_light_driver(&d);
    auto f1 = d.latest();
    REQUIRE(f1);
    CHECK(f1->step == 1);
    CHECK(hook_calls == 1);
    REQUIRE(f1->chunks.size() == 1);
    CHECK(f1->chunks[0].samples[20 * 32 + 20].level == Light::Bright);   // the chunk@2 lantern
    CHECK(f1->sources.size() == 2);                                      // static lantern + game torch
    CHECK(f1->sources[0].id == LightId{LightKind::Static, 0, 0, 0, 0});
    CHECK(f1->sources[0].preset == "srd51.core.light.lantern");
    CHECK(f1->visibility.size() == 2);
    CHECK(f1->added_chunks.size() == 1);
    CHECK_FALSE(d.blocked());
    const auto vs = combat_viewers(*lit);
    REQUIRE(vs.size() == 2);
    for (const Viewer& v : vs)   // the goblin has darkvision 60 ft, the fighter none
        CHECK(v.senses.darkvision_ft == (lit->archetype(v.id)->id == "srd51.monster.goblin" ? 60 : 0));
    const uint64_t h1 = run_hash(*lit);
    CHECK(h1 == h0);   // light, visibility and frames never reach the hashes
    CHECK(d.latest()->step == 1 + lit->log().size());
    // A copy of the combat does not drive the frames.
    Combat copy = *lit;
    CHECK(copy.light_driver() == nullptr);
}

TEST_CASE("light driver: halo gate - Pending blocks the step, Failed is an error until retried; pins and foci") {
    TwoChunks prov;
    prov.a = room_at({0, 0, 0}, {{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {9, 4}}});
    prov.b = room_at({1, 0, 0});
    prov.b.lights.push_back(lamp({0, 5}, 20, 20, 0, prov.b.coord));   // world (32, 5): lights the east edge of (0,0)
    prov.b.content_hash = world::compute_content_hash(prov.b);
    for (bool failing : {false, true}) {
        CAPTURE(failing);
        prov.fail = failing;
        world::ChunkStreamer st(prov, 1);
        LightProviderInputs in;
        in.streamer = &st;
        in.camera_tile = Vec2i{16, 16};
        {
            LightDriver d(std::move(in));
            const world::Chunk* one[] = {&prov.a};
            ecs::Registry reg;
            auto c = Combat::create(reg, std::span<const world::Chunk* const>(one), lib(), 5);
            REQUIRE(c);
            c->attach_light_driver(&d);
            CHECK(st.pinned() == std::vector<world::ChunkCoord>{{0, 0, 0}});
            CHECK(d.blocked());   // nothing loaded yet
            REQUIRE(d.latest());
            CHECK(d.latest()->chunks.empty());
            CHECK(d.latest()->blocked_chunks == std::vector<world::ChunkCoord>{{0, 0, 0}});
            auto r = c->submit(next_command(*c, std::nullopt));
            CHECK_FALSE(r.ok);
            CHECK(r.error == "light halo pending");
            CHECK(c->log().empty());
            st.wait_idle();
            if (failing) {
                r = c->submit(next_command(*c, std::nullopt));
                CHECK_FALSE(r.ok);
                CHECK(r.error == "light halo failed");
                CHECK(d.failed().size() == 1);
                prov.fail = false;
                st.retry({1, 0, 0});
                st.update();
                st.wait_idle();
            }
            r = c->submit(next_command(*c, std::nullopt));
            CHECK(r.ok);
            CHECK_FALSE(d.halted());
            auto f = d.latest();
            REQUIRE(f->chunks.size() == 1);
            CHECK(f->chunks[0].samples[5 * 32 + 30].level == Light::Bright);   // static light of the neighbour chunk
            CHECK(f->blocked_chunks.empty());
            CHECK(f->added_chunks.empty());   // tracked since the blocked frame
            c->attach_light_driver(nullptr);
        }
        CHECK(st.pinned().empty());   // the driver unpins
    }
}

TEST_CASE("light driver: chunk ambient authored by every chunk wins over the default") {
    auto ch = room_at({0, 0, 0}, {{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {12, 4}}});
    ch.ambient = world::Chunk::Ambient{1, 2};   // sun, bright
    const world::Chunk* one[] = {&ch};
    LightProviderInputs in;
    in.chunks = {&ch};
    LightDriver d(std::move(in));
    ecs::Registry reg;
    auto c = Combat::create(reg, std::span<const world::Chunk* const>(one), lib(), 1);
    REQUIRE(c);
    c->attach_light_driver(&d);
    CHECK(d.latest()->chunks[0].samples[31 * 32 + 31].level == Light::Bright);
    c->attach_light_driver(nullptr);
}
