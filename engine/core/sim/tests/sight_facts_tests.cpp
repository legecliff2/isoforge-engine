// sim/sight-facts: Combat::sight_facts_of + EncounterHooks::sight_facts from light samples, LOS, zones, invisibility.
// Rules derive Sight (derive_sight); answers are not hashed. Stacks on light-zones (Fog / Darkness filled samples).
#include <doctest/doctest.h>

#include <fstream>
#include <memory>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/light_driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::Obscured;
using rules5e::Sense;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kMage = "test.sf.mage";
const char* kGob = "test.sf.gob";

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
    for (const Json& d : {fixture("fog_cloud", "test.sf.spell.fog"), fixture("darkness", "test.sf.spell.darkness")}) {
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
                         {"spells", {"test.sf.spell.fog", "test.sf.spell.darkness"}}};
    REQUIRE_FALSE(lib.add(m, "character"));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = kGob;
    g["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(g, "monster"));
    return lib;
}

world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns, std::vector<Vec2i> walls = {}) {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls) {
        c.layers[0][std::size_t(w.y * 32 + w.x)].tile = 2;
        c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
    }
    world::Chunk::Light lamp;
    lamp.tile = {16, 16};
    lamp.bright_ft = 200;   // covers the whole 32x32 arena (Bright everywhere outside magical darkness)
    lamp.dim_ft = 0;
    lamp.preset = "srd51.core.light.lantern";
    lamp.stable_id = world::chunk_light_stable_id(c.coord, 0);
    c.lights.push_back(lamp);
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::unique_ptr<LightDriver> driver;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns,
          std::vector<Vec2i> walls = {}, uint64_t seed = 1, bool with_light = true)
        : lib(&l) {
        chunk = arena(spawns, std::move(walls));
        c = Combat::create(reg, chunk, l, seed);
        REQUIRE(c);
        if (with_light) {
            LightProviderInputs in;
            in.chunks = {&chunk};
            in.incremental = true;
            driver = std::make_unique<LightDriver>(std::move(in));
            c->attach_light_driver(driver.get());
            REQUIRE(driver->latest());
        }
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
};

rules5e::Combatant& mut(Combat& c, CombatantId id) {
    for (auto& k : CombatTestAccess::state(c).combatants)
        if (k.id == id) return k;
    FAIL("missing combatant");
    return CombatTestAccess::state(c).combatants.front();
}
} // namespace

TEST_CASE("sight facts: bright LOS — subject Bright, can_see via derive_sight") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {kGob, {10, 2}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    const rules5e::SightFacts facts = f.c->sight_facts_of(me, gob);
    CHECK(facts.line_of_sight);
    CHECK(facts.subject.light == rules5e::Light::Bright);
    CHECK(facts.viewer.light == rules5e::Light::Bright);
    CHECK(facts.subject.obscured == Obscured::None);
    CHECK(facts.between == Obscured::None);
    CHECK_FALSE(facts.subject_invisible);
    CHECK(facts.distance_ft == uint16_t(8 * 5));   // Chebyshev 8 tiles
    const auto* v = rules5e::find(f.c->state(), me);
    REQUIRE(v);
    const rules5e::Sight s = rules5e::derive_sight(*v, facts);
    CHECK(s.can_see);
    CHECK(s.sense == Sense::Sight);
}

TEST_CASE("sight facts: no LightDriver degrades to Bright/clear but still applies LOS and Invisible") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {kGob, {10, 2}}}, {}, 1, /*with_light=*/false);
    const CombatantId me = f.id(0), gob = f.id(1);
    const rules5e::SightFacts a = f.c->sight_facts_of(me, gob);
    CHECK(a.subject.light == rules5e::Light::Bright);
    CHECK(a.line_of_sight);
    mut(*f.c, gob).conditions |= rules5e::bit(Condition::Invisible);
    const rules5e::SightFacts b = f.c->sight_facts_of(me, gob);
    CHECK(b.subject_invisible);
    CHECK_FALSE(rules5e::derive_sight(*rules5e::find(f.c->state(), me), b).can_see);
}

TEST_CASE("sight facts: Fog Cloud — heavy obscurement on subject / between, can_see false") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {20, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    // Fog centred on the goblin (20 ft sphere, Chebyshev r=4).
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.sf.spell.fog", 1, {20, 5}))).ok);
    const rules5e::SightFacts facts = f.c->sight_facts_of(me, gob);
    CHECK(facts.line_of_sight);
    CHECK(facts.subject.obscured == Obscured::Heavy);
    CHECK(facts.subject.light == rules5e::Light::Bright);   // Fog does not change light level
    const rules5e::Sight s = rules5e::derive_sight(*rules5e::find(f.c->state(), me), facts);
    CHECK_FALSE(s.can_see);
    CHECK(s.obscured == Obscured::Heavy);
}

TEST_CASE("sight facts: Darkness vs darkvision — magical darkness blocks darkvision") {
    const auto lib = make_lib();
    // Goblin has darkvision 60 ft; mage has none.
    // Darkness 15 ft (Chebyshev r=3) centred at {10,5}; goblin on the rim at {13,5}, mage adjacent outside at {14,5}
    // so the centre-to-centre walk has no mid-tiles (between stays Clear).
    Fight f(lib, {{kMage, {14, 5}}, {kGob, {13, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.sf.spell.darkness", 2, {10, 5}))).ok);
    const rules5e::SightFacts mage_sees = f.c->sight_facts_of(me, gob);
    CHECK(mage_sees.subject.magical_darkness);
    CHECK(mage_sees.subject.light == rules5e::Light::Dark);
    CHECK(mage_sees.between == Obscured::None);   // adjacent: no mid-tiles
    CHECK_FALSE(rules5e::derive_sight(*rules5e::find(f.c->state(), me), mage_sees).can_see);
    // Goblin looking out: subject (mage) is Bright / clear; darkvision not needed.
    const rules5e::SightFacts gob_sees_mage = f.c->sight_facts_of(gob, me);
    CHECK(gob_sees_mage.subject.light == rules5e::Light::Bright);
    CHECK_FALSE(gob_sees_mage.subject.magical_darkness);
    CHECK(rules5e::derive_sight(*rules5e::find(f.c->state(), gob), gob_sees_mage).can_see);
    // Darkvision does not pierce magical darkness on the subject tile (goblin viewing facts about itself-as-subject).
    CHECK_FALSE(rules5e::derive_sight(*rules5e::find(f.c->state(), gob), mage_sees).can_see);
}

TEST_CASE("sight facts: Invisible subject — subject_invisible, can_see false") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {kGob, {8, 2}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    mut(*f.c, gob).conditions |= rules5e::bit(Condition::Invisible);
    const rules5e::SightFacts facts = f.c->sight_facts_of(me, gob);
    CHECK(facts.subject_invisible);
    CHECK(facts.subject_grounded);   // flat map, alt_ft 0: still in contact
    CHECK(facts.line_of_sight);
    CHECK_FALSE(rules5e::derive_sight(*rules5e::find(f.c->state(), me), facts).can_see);
}

TEST_CASE("sight facts: blocked LOS — line_of_sight false, can_see false") {
    const auto lib = make_lib();
    std::vector<Vec2i> wall;
    for (int y = 0; y < 12; ++y) wall.push_back({5, y});
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {8, 5}}}, wall);
    const CombatantId me = f.id(0), gob = f.id(1);
    const rules5e::SightFacts facts = f.c->sight_facts_of(me, gob);
    CHECK_FALSE(facts.line_of_sight);
    CHECK_FALSE(rules5e::derive_sight(*rules5e::find(f.c->state(), me), facts).can_see);
}

TEST_CASE("sight facts: determinism — identical facts across repeated queries") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {kGob, {14, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.zone_at("test.sf.spell.fog", 1, {10, 5}))).ok);
    const rules5e::SightFacts a = f.c->sight_facts_of(me, gob);
    const rules5e::SightFacts b = f.c->sight_facts_of(me, gob);
    CHECK(a.subject.light == b.subject.light);
    CHECK(a.subject.obscured == b.subject.obscured);
    CHECK(a.viewer.light == b.viewer.light);
    CHECK(a.between == b.between);
    CHECK(a.distance_ft == b.distance_ft);
    CHECK(a.line_of_sight == b.line_of_sight);
    CHECK(a.subject_invisible == b.subject_invisible);
    CHECK(a.subject_grounded == b.subject_grounded);
    CHECK(a.audible == b.audible);
    CHECK(a.subject.magical_darkness == b.subject.magical_darkness);
    CHECK(a.subject.sunlight == b.subject.sunlight);
}

TEST_CASE("sight facts: hook installed — rules5e::sight answers after submit rebinds hooks") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {kGob, {10, 2}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    // create() does not install_hooks; the first submit does. EndTurn once so hooks.sight_facts is bound,
    // then advance back to the mage (each EndTurn rebinds).
    f.end_turn();
    f.advance_to(me);
    const rules5e::Sight s = rules5e::sight(f.c->state(), me, gob);
    CHECK(s.can_see);
    CHECK(s.sense == Sense::Sight);
    mut(*f.c, gob).conditions |= rules5e::bit(Condition::Invisible);
    // Hook prefers the combatant rules passed; Invisible is live on state_.
    const rules5e::Sight hidden = rules5e::sight(f.c->state(), me, gob);
    CHECK_FALSE(hidden.can_see);
}
