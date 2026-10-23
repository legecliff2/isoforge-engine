// sim/light-zones: LightDriver::step maps R-G11 ActiveZones into LightWorld::zones so Fog Cloud / Darkness / Web set
// LightSample flags (kObscured*, kMagicalDark). Composition already honours LightZone; this wires the driver.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/light_driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kMage = "test.lz.mage";
const char* kGob = "test.lz.gob";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json fixture(const char* name, const char* id) {
    Json j = load_json(std::string("tests/validate/fixtures/zones/spells/") + name + ".json");
    j["id"] = id;
    return j;
}

ContentLibrary make_lib() {
    ContentLibrary lib;
    for (const Json& d : {fixture("fog_cloud", "test.lz.spell.fog"), fixture("darkness", "test.lz.spell.darkness"),
                          fixture("moonbeam", "test.lz.spell.moonbeam"), fixture("web", "test.lz.spell.web"),
                          fixture("spike_growth", "test.lz.spell.spike")}) {
        auto e = lib.add_spell(d);
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    Json m = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    m["$schema"] = "character@4";
    m["id"] = kMage;
    m["hit_points"]["average"] = 300;
    m["spellcasting"] = {{"ability", "int"},
                         {"caster_level", 9},
                         {"dc", 30},
                         {"slots", {{"1", 4}, {"2", 3}, {"3", 3}}},
                         {"spells",
                          {"test.lz.spell.fog", "test.lz.spell.darkness", "test.lz.spell.moonbeam", "test.lz.spell.web",
                           "test.lz.spell.spike"}}};
    REQUIRE_FALSE(lib.add(m, "character"));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = kGob;
    g["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(g, "monster"));
    return lib;
}

world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns) {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    // A static lantern so light levels are Bright outside darkness (composition is still exercised).
    world::Chunk::Light lamp;
    lamp.tile = {20, 20};
    lamp.bright_ft = 60;
    lamp.dim_ft = 60;
    lamp.preset = "srd51.core.light.lantern";
    lamp.stable_id = world::chunk_light_stable_id(c.coord, 0);
    c.lights.push_back(lamp);
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

LightSample sample_at(const LightFrame& f, Vec2i tile) {
    REQUIRE_FALSE(f.chunks.empty());
    const auto& ch = f.chunks[0];
    REQUIRE(ch.coord == world::ChunkCoord{0, 0, 0});
    REQUIRE(tile.x >= 0);
    REQUIRE(tile.x < 32);
    REQUIRE(tile.y >= 0);
    REQUIRE(tile.y < 32);
    return ch.samples[std::size_t(tile.y * 32 + tile.x)];
}

struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::unique_ptr<LightDriver> driver;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns, uint64_t seed = 1) : lib(&l) {
        chunk = arena(spawns);
        LightProviderInputs in;
        in.chunks = {&chunk};
        in.incremental = true;
        driver = std::make_unique<LightDriver>(std::move(in));
        c = Combat::create(reg, chunk, l, seed);
        REQUIRE(c);
        c->attach_light_driver(driver.get());
        REQUIRE(driver->latest());
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(settle(c->submit(e)).ok);
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != who; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    Command zone_at(const char* sp, uint8_t level, Vec2i t) const {
        Command cmd;
        cmd.request = {rules5e::current_actor(c->state()), ActionKind::CastSpell, rules5e::current_actor(c->state())};
        cmd.request.spell_id = spell_id_of(sp);
        cmd.request.spell_level = level;
        cmd.request.has_zone_dest = true;
        cmd.request.zone_x = t.x;
        cmd.request.zone_y = t.y;
        return cmd;
    }
    std::shared_ptr<const LightFrame> frame() const { return driver->latest(); }
};
} // namespace

TEST_CASE("light zones: no ActiveZones leaves LightSample flags clear (driver gap closed only when zones exist)") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {kGob, {14, 2}}});
    auto fr = f.frame();
    REQUIRE(fr);
    const LightSample s = sample_at(*fr, {10, 10});
    CHECK(s.level == Light::Bright);   // the chunk lantern
    CHECK((s.flags & (light_flags::kObscuredLight | light_flags::kObscuredHeavy | light_flags::kMagicalDark)) == 0);
}

TEST_CASE("light zones: Fog Cloud tiles get kObscuredHeavy; outside stays clear") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {20, 5}}});
    const CombatantId me = f.id(0);
    f.advance_to(me);
    // 20 ft sphere = Chebyshev radius 4 tiles around (10, 10).
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.lz.spell.fog", 1, {10, 10}))).ok);
    REQUIRE_FALSE(f.c->state().zones.empty());
    auto fr = f.frame();
    REQUIRE(fr);
    const LightSample centre = sample_at(*fr, {10, 10});
    CHECK((centre.flags & light_flags::kObscuredHeavy) != 0);
    CHECK((centre.flags & light_flags::kMagicalDark) == 0);
    const LightSample edge = sample_at(*fr, {14, 10});   // radius 4 inclusive
    CHECK((edge.flags & light_flags::kObscuredHeavy) != 0);
    const LightSample out = sample_at(*fr, {15, 10});
    CHECK((out.flags & light_flags::kObscuredHeavy) == 0);
    CHECK(out.level == Light::Bright);
}

TEST_CASE("light zones: Darkness sets kMagicalDark (and heavy obscurement from the fixture flags)") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {20, 5}}});
    const CombatantId me = f.id(0);
    f.advance_to(me);
    // 15 ft sphere = Chebyshev radius 3 around (10, 10). Fixture: heavy_obscured + magical_darkness.
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.lz.spell.darkness", 2, {10, 10}))).ok);
    auto fr = f.frame();
    REQUIRE(fr);
    const LightSample in = sample_at(*fr, {10, 10});
    CHECK((in.flags & light_flags::kMagicalDark) != 0);
    CHECK((in.flags & light_flags::kObscuredHeavy) != 0);
    CHECK(in.level == Light::Dark);   // nonmagical lantern cannot illuminate magical darkness
    const LightSample out = sample_at(*fr, {14, 10});
    CHECK((out.flags & light_flags::kMagicalDark) == 0);
    CHECK(out.level == Light::Bright);
}

TEST_CASE("light zones: Web sets kObscuredLight; Spike Growth (difficult only) does not") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {20, 5}}});
    const CombatantId me = f.id(0);
    f.advance_to(me);
    // Web: 20 ft cube centred on anchor → half-side 2 tiles (sz=4, lo=-1..hi=2? sz/2 with cube formula).
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.lz.spell.web", 2, {10, 10}))).ok);
    auto fr = f.frame();
    REQUIRE(fr);
    CHECK((sample_at(*fr, {10, 10}).flags & light_flags::kObscuredLight) != 0);
    CHECK((sample_at(*fr, {10, 10}).flags & light_flags::kObscuredHeavy) == 0);

    // Expire Web by casting Spike Growth (also concentration): difficult-only → no light flags.
    f.end_turn();
    f.advance_to(me);
    {
        auto r = f.settle(f.c->submit(f.zone_at("test.lz.spell.spike", 2, {10, 10})));
        REQUIRE_MESSAGE(r.ok, r.error);
    }
    REQUIRE_FALSE(f.c->state().zones.empty());
    // Only Spike remains (Web ended with concentration).
    CHECK(f.c->state().zones.size() == 1);
    fr = f.frame();
    REQUIRE(fr);
    const LightSample s = sample_at(*fr, {10, 10});
    CHECK((s.flags & (light_flags::kObscuredLight | light_flags::kObscuredHeavy | light_flags::kMagicalDark)) == 0);
}

TEST_CASE("light zones: zone move updates sample flags; expire clears them") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {18, 5}}});
    const CombatantId me = f.id(0);
    f.advance_to(me);
    // Moonbeam fixture: light_obscured + movable 60 ft. Cast at (4, 9), then move onto (16, 5).
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.lz.spell.moonbeam", 2, {4, 9}))).ok);
    const uint32_t zid = f.c->state().zones.at(0).zone_id;
    auto fr = f.frame();
    REQUIRE(fr);
    CHECK((sample_at(*fr, {4, 9}).flags & light_flags::kObscuredLight) != 0);
    CHECK((sample_at(*fr, {16, 5}).flags & light_flags::kObscuredLight) == 0);

    f.end_turn();
    f.advance_to(me);
    Command mv;
    mv.request = {me, ActionKind::CastSpell, me};
    mv.request.spell_id = spell_id_of("test.lz.spell.moonbeam");
    mv.request.spell_level = 2;
    mv.request.zone_move = zid;
    mv.request.has_zone_dest = true;
    mv.request.zone_x = 16;
    mv.request.zone_y = 5;
    REQUIRE(f.settle(f.c->submit(mv)).ok);
    fr = f.frame();
    REQUIRE(fr);
    CHECK((sample_at(*fr, {4, 9}).flags & light_flags::kObscuredLight) == 0);
    CHECK((sample_at(*fr, {16, 5}).flags & light_flags::kObscuredLight) != 0);

    // Expire Moonbeam by casting Fog Cloud elsewhere (new concentration ends the beam).
    f.end_turn();
    f.advance_to(me);
    {
        auto r = f.settle(f.c->submit(f.zone_at("test.lz.spell.fog", 1, {14, 20})));
        REQUIRE_MESSAGE(r.ok, r.error);
    }
    // Moonbeam gone; Fog is far from (16,5).
    CHECK(f.c->state().zones.size() == 1);
    fr = f.frame();
    REQUIRE(fr);
    CHECK((sample_at(*fr, {16, 5}).flags & light_flags::kObscuredLight) == 0);
    CHECK((sample_at(*fr, {16, 5}).flags & light_flags::kObscuredHeavy) == 0);
}

TEST_CASE("light zones: hashes unchanged when no zones (equipment-off path still bit-identical on light tags)") {
    // LightFrames are never hashed; ActiveZones already fold under 0xAB. Mapping zones into LightWorld must not invent a
    // new Combat::hash / log_hash tag. A combat with a light driver and no zones hashes the same as without the mapping
    // side effects — checked by running two identical seed combats (driver attached vs not) until one EndTurn.
    const auto lib = make_lib();
    auto chunk = arena({{kMage, {2, 2}}, {kGob, {12, 2}}});
    const world::Chunk* one[] = {&chunk};

    ecs::Registry r0;
    auto plain = Combat::create(r0, std::span<const world::Chunk* const>(one), lib, 9);
    REQUIRE(plain);
    {
        Command e;
        e.request = {rules5e::current_actor(plain->state()), ActionKind::EndTurn};
        REQUIRE(plain->submit(e).ok);
    }
    const uint64_t h0 = plain->hash(), lh0 = plain->log_hash();

    LightProviderInputs in;
    in.chunks = {&chunk};
    LightDriver d(std::move(in));
    ecs::Registry r1;
    auto lit = Combat::create(r1, std::span<const world::Chunk* const>(one), lib, 9);
    REQUIRE(lit);
    lit->attach_light_driver(&d);
    {
        Command e;
        e.request = {rules5e::current_actor(lit->state()), ActionKind::EndTurn};
        REQUIRE(lit->submit(e).ok);
    }
    CHECK(lit->hash() == h0);
    CHECK(lit->log_hash() == lh0);
    REQUIRE(d.latest());
    CHECK((sample_at(*d.latest(), {8, 8}).flags &
           (light_flags::kObscuredLight | light_flags::kObscuredHeavy | light_flags::kMagicalDark)) == 0);
}
