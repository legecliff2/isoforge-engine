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
    {   // v8: the push's Moved carries the forced bit (and only that one); the stream now hashes as v8
        int forced = 0;
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::Moved && e.rules.c != 0) {
                CHECK(e.rules.c == kMovedForced);
                CHECK(e.rules.actor == g);
                CHECK(e.rules.target == f);
                ++forced;
            }
        CHECK(forced >= 1);
        CHECK(event_stream_version(c->events()) == 8);
    }
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
    {   // v8: the dragged goblin gets a Moved{actor = goblin, target = fighter, a = 5, c = drag}, right before its TileMoved
        const auto& ev = c->events();
        std::size_t k = ev.size();
        for (std::size_t i = 0; i < ev.size(); ++i)
            if (ev[i].kind == SimEvent::Kind::Rules && ev[i].rules.kind == rules5e::EventKind::Moved && (ev[i].rules.c & kMovedDrag)) k = i;
        REQUIRE(k + 1 < ev.size());
        CHECK(ev[k].rules.c == kMovedDrag);
        CHECK(ev[k].rules.actor == g);
        CHECK(ev[k].rules.target == f);
        CHECK(ev[k].rules.a == 5);
        CHECK(ev[k + 1].kind == SimEvent::Kind::TileMoved);
        CHECK(ev[k + 1].actor == g);
        CHECK(ev[k + 1].tile == Vec2i{6, 5});
    }
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

// Druid gap G: monster@5 grapple rider fields (restrains / exclusive / max_grapples), loader to sim.
namespace {
struct Rig {
    ContentLibrary lib;
    world::Chunk ch;
};
// The shipped giant toad (bite: grapple DC 13, restrains, exclusive), or a crab-like copy whose bite grapples with
// max_grapples 2 (SRD giant crab: two claws, each grapples one target), against three tough fighters.
void rig(Rig& r, bool crab) {
    REQUIRE(r.lib.load(sample_set()).empty());
    Json f = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    f["hit_points"]["average"] = 2000;
    f["id"] = "t.fighter";
    REQUIRE_FALSE(r.lib.add(f, "character"));
    Json toad = load_json("content/packs/srd51.core/monsters/giant_toad.json");
    toad["hit_points"]["average"] = 2000;
    toad["id"] = "t.toad";
    if (crab) toad["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 11}, {"max_grapples", 2}}});
    REQUIRE_FALSE(r.lib.add(toad, "monster"));
    r.ch = arena({{"t.toad", Vec2i{6, 5}}, {"t.fighter", Vec2i{5, 5}}, {"t.fighter", Vec2i{5, 6}}, {"t.fighter", Vec2i{8, 5}}});
}
// The toad bites `target` until a hit lands (returns false if none in 40 rounds); fighters only end their turns.
bool bite_until_hit(Combat& c, CombatantId toad, CombatantId target) {
    for (int round = 0; round < 40; ++round) {
        to_turn(c, toad);
        const int hp = rules5e::find(c.state(), target)->hp;
        Command a;
        a.request = {toad, ActionKind::Attack, target};
        a.request.attack_index = 0;
        REQUIRE(c.submit(a).ok);
        c.settle(nullptr);
        const bool hit = rules5e::find(c.state(), target)->hp < hp;
        end_turn(c);
        if (hit) return true;
    }
    return false;
}
}  // namespace

TEST_CASE("grapple rider (monster@5): giant toad bite restrains and can't bite another target; escape clears both") {
    Rig r;
    rig(r, false);
    const auto& g = r.lib.find("t.toad")->grapple_on_hit;
    REQUIRE(g.size() == 1);
    CHECK(g.at(0).escape_dc == 13);
    CHECK(g.at(0).restrains);
    CHECK(g.at(0).exclusive);
    CHECK(g.at(0).max_grapples == 0);
    ecs::Registry reg;
    auto c = Combat::create(reg, r.ch, r.lib, 5);
    REQUIRE(c);
    const CombatantId toad = r.ch.spawns[0].stable_id, a = r.ch.spawns[1].stable_id, b = r.ch.spawns[2].stable_id;
    REQUIRE(bite_until_hit(*c, toad, a));
    CHECK(has(*c, a, Condition::Grappled));
    CHECK(has(*c, a, Condition::Restrained));
    to_turn(*c, toad);
    bool other = false, held = false;
    for (const auto& q : c->legal_actions())
        if (q.kind == ActionKind::Attack && q.special == 0 && q.attack_index == 0) { other = other || q.target == b; held = held || q.target == a; }
    CHECK(held);
    CHECK_FALSE(other);
    Command bite_b;
    bite_b.request = {toad, ActionKind::Attack, b};
    bite_b.request.attack_index = 0;
    CHECK_FALSE(c->submit(bite_b).ok);
    end_turn(*c);
    bool freed = false;
    for (int round = 0; round < 40 && !freed; ++round) {
        to_turn(*c, a);
        Command esc;
        esc.request = {a, ActionKind::Attack, toad};
        esc.request.special = 4;
        REQUIRE(c->submit(esc).ok);
        c->settle(nullptr);
        freed = !has(*c, a, Condition::Grappled);
        if (!freed) CHECK(has(*c, a, Condition::Restrained));
        end_turn(*c);
    }
    REQUIRE(freed);
    CHECK_FALSE(has(*c, a, Condition::Restrained));
    CHECK(c->state().sourced.empty());
}

TEST_CASE("grapple rider (monster@5): max_grapples 2: a third hit deals damage but doesn't grapple") {
    Rig r;
    rig(r, true);
    CHECK(r.lib.find("t.toad")->grapple_on_hit.at(0).max_grapples == 2);
    ecs::Registry reg;
    auto c = Combat::create(reg, r.ch, r.lib, 7);
    REQUIRE(c);
    const CombatantId toad = r.ch.spawns[0].stable_id;
    for (int i = 1; i <= 2; ++i) {
        REQUIRE(bite_until_hit(*c, toad, r.ch.spawns[std::size_t(i)].stable_id));
        CHECK(has(*c, r.ch.spawns[std::size_t(i)].stable_id, Condition::Grappled));
    }
    const CombatantId third = r.ch.spawns[3].stable_id;
    REQUIRE(bite_until_hit(*c, toad, third));   // the hit dealt damage
    CHECK_FALSE(has(*c, third, Condition::Grappled));
    CHECK(c->state().sourced.size() == 2);
    // a bad max_grapples is a load error
    Json bad = load_json("content/packs/srd51.core/monsters/giant_toad.json");
    bad["id"] = "t.badtoad";
    bad["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"max_grapples", 0}}});
    CHECK(r.lib.add(bad, "monster"));
}

TEST_CASE("grapple rider: a plain rider leaves the appended fields unset (no 0xDB fold)") {
    auto lib = make_lib(true);
    const auto& g = lib.find("srd51.monster.goblin")->grapple_on_hit.at(0);
    CHECK_FALSE(g.extended());
}

TEST_CASE("grapple max_size: a larger target takes the damage and is not grappled") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    Json small = tank;
    small["id"] = "t.small";
    small["size"] = "small";
    REQUIRE_FALSE(lib.add(small, "character"));
    Json grab = load_json("content/packs/srd51.core/monsters/goblin.json");
    grab["$schema"] = "monster@5";
    grab["id"] = "t.grab";
    grab["hit_points"]["average"] = 2000;
    grab["actions"][0]["to_hit"] = 40;
    grab["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 11}, {"max_size", "small"}}});
    REQUIRE_FALSE(lib.add(grab, "monster"));
    const auto& g = lib.find("t.grab")->grapple_on_hit.at(0);
    CHECK(g.max_size == int8_t(rules5e::Size::Small));
    CHECK_FALSE(g.extended());   // size-only: a successful grapple leaves via_attack 0xFF
    auto ch = arena({{"t.grab", Vec2i{6, 5}}, {"t.small", Vec2i{5, 5}}, {kFighter, Vec2i{7, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const CombatantId grabber = ch.spawns[0].stable_id, tiny = ch.spawns[1].stable_id, med = ch.spawns[2].stable_id;
    to_turn(*c, grabber);
    const int med_hp = rules5e::find(c->state(), med)->hp;
    Command bite;
    bite.request = {grabber, ActionKind::Attack, tiny};
    bite.request.attack_index = 0;
    bite.request.target_within_5ft = true;
    REQUIRE(c->submit(bite).ok);
    c->settle(nullptr);
    CHECK(has(*c, tiny, Condition::Grappled));
    CHECK(c->state().sourced.at(0).via_attack == 0xFF);
    end_turn(*c);
    to_turn(*c, grabber);
    Command bite_med;
    bite_med.request = {grabber, ActionKind::Attack, med};
    bite_med.request.attack_index = 0;
    bite_med.request.target_within_5ft = true;
    REQUIRE(c->submit(bite_med).ok);
    c->settle(nullptr);
    CHECK(rules5e::find(c->state(), med)->hp < med_hp);   // the hit landed
    CHECK_FALSE(has(*c, med, Condition::Grappled));
    Json bad = grab;
    bad["id"] = "t.badsize";
    bad["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"max_size", "colossal"}}});
    CHECK(lib.add(bad, "monster"));
    bad["$schema"] = "monster@4";
    bad["id"] = "t.oldsize";
    bad["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"max_size", "large"}}});
    const auto err = lib.add(bad, "monster");
    REQUIRE(err);
    CHECK(err->find("grapple max_size needs a monster@5 file") != std::string::npos);
}
