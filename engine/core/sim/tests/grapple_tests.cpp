// R-G8, sim side: grapple / shove / escape requests, the 5 ft push, dragging a held creature, monster grapple riders, the AI's
// special requests, and the 0xA7 fold (Grappled entries only).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_turn(Combat& c, CombatantId id) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != id; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == id);
}
bool has(const Combat& c, CombatantId id, Condition k) { return rules5e::find(c.state(), id)->conditions & rules5e::bit(k); }
ContentLibrary make_lib(bool grapple_rider) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json f = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    f["hit_points"]["average"] = 2000;
    f["id"] = "t.fighter";
    REQUIRE_FALSE(lib.add(f, "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 2000;
    if (grapple_rider) {
        gob["$schema"] = "monster@4";
        gob["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 12}}});
    }
    REQUIRE_FALSE(lib.add(gob, "monster"));
    return lib;
}
}  // namespace

TEST_CASE("grapple / shove: requests, the 5 ft push, drag, 0xA7 fold, log fold, replay") {
    auto lib = make_lib(false);
    (void)kFighter;
    auto ch = arena({{"t.fighter", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    // out of reach is refused by sim
    {
        auto ch2 = arena({{"t.fighter", Vec2i{2, 2}}, {"srd51.monster.goblin", Vec2i{9, 9}}});
        ecs::Registry r2;
        auto far = Combat::create(r2, ch2, lib, 7);
        to_turn(*far, ch2.spawns[0].stable_id);
        Command x;
        x.request = {ch2.spawns[0].stable_id, ActionKind::Attack, ch2.spawns[1].stable_id};
        x.request.special = 1;
        CHECK_FALSE(far->submit(x).ok);
    }
    // shove 5 ft away until it lands: the goblin moves one tile straight away
    bool pushed = false;
    for (int round = 0; round < 30 && !pushed; ++round) {
        to_turn(*c, f);
        Command x;
        x.request = {f, ActionKind::Attack, g};
        x.request.special = 3;
        REQUIRE(c->submit(x).ok);
        c->settle(nullptr);
        pushed = c->pos(g) == Vec2i{7, 5};
        end_turn(*c);
    }
    REQUIRE(pushed);
    CHECK_FALSE(rules5e::has_sourced_conditions(c->state()));
    // step back next to it, then grapple until it holds
    to_turn(*c, f);
    Command st;
    st.kind = CommandKind::Step;
    st.to = Vec2i{6, 5};
    REQUIRE(c->submit(st).ok);
    bool held = false;
    for (int round = 0; round < 30 && !held; ++round) {
        to_turn(*c, f);
        Command x;
        x.request = {f, ActionKind::Attack, g};
        x.request.special = 1;
        REQUIRE(c->submit(x).ok);
        c->settle(nullptr);
        held = has(*c, g, Condition::Grappled);
        if (!held) end_turn(*c);
    }
    REQUIRE(held);
    CHECK(rules5e::condition_source(c->state(), g, Condition::Grappled) == f);
    end_turn(*c);
    to_turn(*c, f);
    // AI: holding a target that is not prone -> shove it prone
    const Command ai = next_command(*c, g);
    CHECK(ai.request.special == 2);
    // drag: the fighter steps away and the goblin comes along
    Command back;
    back.kind = CommandKind::Step;
    back.to = Vec2i{5, 5};
    REQUIRE(c->submit(back).ok);
    c->settle(nullptr);
    CHECK(c->pos(f) == Vec2i{5, 5});
    CHECK(c->distance_tiles(f, g) <= 1);
    CHECK(c->pos(g) == Vec2i{6, 5});
    CHECK(has(*c, g, Condition::Grappled));
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
}

TEST_CASE("grapple riders: a monster's hit grapples with its escape DC; Escape request") {
    auto lib = make_lib(true);
    REQUIRE(lib.find("srd51.monster.goblin")->grapple_on_hit.size() == 1);
    auto ch = arena({{"t.fighter", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    bool held = false;
    for (int round = 0; round < 30 && !held; ++round) {
        to_turn(*c, g);
        Command a;
        a.request = {g, ActionKind::Attack, f};
        a.request.attack_index = 0;
        REQUIRE(c->submit(a).ok);
        c->settle(nullptr);
        held = has(*c, f, Condition::Grappled);
        end_turn(*c);
    }
    REQUIRE(held);
    REQUIRE(c->state().sourced.size() == 1);
    CHECK(c->state().sourced[0].escape_dc == 12);
    CHECK(c->state().sourced[0].source == g);
    to_turn(*c, f);
    Command esc;
    esc.request = {f, ActionKind::Attack, g};
    esc.request.special = 4;
    CHECK(c->submit(esc).ok);
    // bad rider DC is a load error
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["$schema"] = "monster@4";
    gob["id"] = "t.badgob";
    gob["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 99}}});
    CHECK(lib.add(gob, "monster"));
}
