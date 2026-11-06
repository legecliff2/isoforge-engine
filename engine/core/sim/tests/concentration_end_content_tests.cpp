// Sim forwards EndConcentration off the current turn. Not Invisible Passage. No green_hag.json.
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
    c.spawns.push_back({"t.a", {2, 2}, world::spawn_stable_id(c.coord, 0), R"({"team":"party"})"});
    c.spawns.push_back({"t.b", {4, 2}, world::spawn_stable_id(c.coord, 1), R"({"team":"foes"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void advance_to(Combat& c, CombatantId who) {
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

TEST_CASE("concentration end: sim accepts it off the current turn and replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json doc = load_json("content/packs/srd51.core/monsters/goblin.json");
    doc["$schema"] = "monster@5";
    doc["id"] = "t.a";
    REQUIRE_FALSE(lib.add(doc, "monster", "", 5));
    doc["id"] = "t.b";
    REQUIRE_FALSE(lib.add(doc, "monster", "", 5));
    auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id;
    const CombatantId b = ch.spawns[1].stable_id;
    advance_to(*c, a);
    Command refused;
    refused.request = {b, ActionKind::EndConcentration};
    auto no = c->submit(refused);
    CHECK_FALSE(no.ok);
    CHECK(no.error == "not concentrating");
    CHECK(rules5e::current_actor(c->state()) == a);

    auto* holder = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), b));
    holder->concentration_spell = 1;
    Command end;
    end.request = {b, ActionKind::EndConcentration};
    auto r = c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find(c->state(), b)->concentration_spell == 0);
    CHECK(rules5e::current_actor(c->state()) == a);
    bool ended = false;
    for (const auto& ev : c->events())
        ended = ended || (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == rules5e::EventKind::ConcentrationEnded);
    CHECK(ended);
}
