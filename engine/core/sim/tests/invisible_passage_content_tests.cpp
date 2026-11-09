// Sim loads monster@5 invisible_passage and replays it. Does not load green_hag.json.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
world::Chunk arena() {
    world::Chunk c{};
    c.coord = {6, 6, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    c.spawns.push_back({"t.hag", {2, 2}, world::spawn_stable_id(c.coord, 0), R"({"team":"party"})"});
    c.spawns.push_back({"t.foe", {4, 2}, world::spawn_stable_id(c.coord, 1), R"({"team":"foes"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void advance_to(Combat& c, rules5e::CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("invisible passage: loader, concentration end, and replay") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json doc = load_json("content/packs/srd51.core/monsters/goblin.json");
    doc["$schema"] = "monster@5";
    doc["id"] = "t.hag";
    doc["actions"].push_back(Json{{"id", "invisible_passage"}, {"name", "Invisible Passage"}, {"kind", "invisible_passage"}});
    REQUIRE_FALSE(lib.add(doc, "monster", "", 5));
    doc["id"] = "t.foe";
    doc["actions"] = Json::array();
    doc["actions"].push_back(load_json("content/packs/srd51.core/monsters/goblin.json")["actions"][0]);
    REQUIRE_FALSE(lib.add(doc, "monster", "", 5));

    auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const rules5e::CombatantId hag = ch.spawns[0].stable_id;
    advance_to(*c, hag);
    const uint32_t sid = rules5e::spell_id_of("t.hag#invisible_passage");
    Command go;
    go.request = {hag, ActionKind::CastSpell, hag};
    go.request.spell_id = sid;
    auto r = c->submit(go);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find(c->state(), hag)->has(rules5e::Condition::Invisible));
    CHECK(rules5e::find(c->state(), hag)->concentration_spell == sid);
    CHECK_FALSE(rules5e::find(c->state(), hag)->innate_invisible);
    CHECK(rules5e::track_evidence(c->state(), hag, rules5e::TrackEvidence::Physical) == rules5e::TrackAnswer::None);
    CHECK(rules5e::track_evidence(c->state(), hag, rules5e::TrackEvidence::Magical) == rules5e::TrackAnswer::Unanswered);
    const uint64_t up = c->hash();

    Command end;
    end.request = {hag, ActionKind::EndConcentration};
    r = c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(rules5e::find(c->state(), hag)->has(rules5e::Condition::Invisible));
    CHECK(rules5e::find(c->state(), hag)->concentration_spell == 0);
    CHECK(rules5e::track_evidence(c->state(), hag, rules5e::TrackEvidence::Physical) == rules5e::TrackAnswer::Unanswered);
    CHECK(c->hash() != up);

    std::string err;
    ecs::Registry reg2;
    auto replayed = replay(reg2, ch, lib, c->seed(), c->log(), &err);
    REQUIRE_MESSAGE(replayed, err);
    CHECK(replayed->hash() == c->hash());
    CHECK_FALSE(rules5e::find(replayed->state(), hag)->has(rules5e::Condition::Invisible));
}
