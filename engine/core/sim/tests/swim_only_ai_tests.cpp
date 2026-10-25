// A swim-only creature (speed {"walk":0,"swim":N}, SRD 5.1 reef shark: swim 40 ft.) in water: the AI's reach / approach
// checks read the swimming allowance, so it closes to melee instead of treating walk 0 as unable to move.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
namespace tf = world::tile_flags;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json goblin(const std::string& id, const char* speed) {
    std::ifstream f(kRoot + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE(f.good());
    Json m = Json::parse(f);
    m["$schema"] = "monster@3";
    m["id"] = "test.monster." + id;
    m["speed"] = Json::parse(speed);
    return m;
}
}  // namespace

TEST_CASE("swim-only AI: in water, a walk 0 / swim 40 creature closes to melee rather than shooting") {
    ContentLibrary l;
    REQUIRE_FALSE(l.add(goblin("swim_only", R"({"walk":0,"swim":40})"), "monster"));
    REQUIRE_FALSE(l.add(goblin("foe", R"({"walk":30,"swim":30})"), "monster"));
    world::Chunk ch{};
    ch.coord = {5, 5, 0};
    ch.origin = world::Chunk::Origin::Handcrafted;
    ch.palette = {"", "floor"};
    for (auto& cell : ch.layers[0]) { cell.tile = 1; cell.flags = tf::kWater; }
    ch.spawns.push_back({"test.monster.swim_only", Vec2i{4, 4}, world::spawn_stable_id(ch.coord, 0), R"({"team":"a"})"});
    ch.spawns.push_back({"test.monster.foe", Vec2i{10, 4}, world::spawn_stable_id(ch.coord, 1), R"({"team":"b"})"});
    ch.content_hash = world::compute_content_hash(ch);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 1);
    REQUIRE(c);
    const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
    for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != me; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c->state()) == me);
    CHECK(c->state().budget.movement_ft == 0);
    const Command cmd = next_command(*c, foe);
    CHECK(cmd.kind == CommandKind::Step);   // 25 ft of water to reach: swims, does not fall back to the shortbow
}
