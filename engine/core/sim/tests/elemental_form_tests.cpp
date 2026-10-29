// B9: Air Form / Water Form. Trait ids air_form and water_form set Combatant::enter_hostile_space.
// The mover may enter a hostile creature's space and stop there. A creature without the trait cannot.
// air_elemental.json and water_elemental.json stay deferred. No new hash tag.
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <string>
#include <vector>

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

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json trait(const char* id, const char* name) {
    return {{"id", id}, {"name", name}, {"description", "The elemental can enter a hostile creature's space and stop there."}};
}

// A goblin re-authored as a test creature. `form` is air_form, water_form, fire_form, or empty.
Json creature(const std::string& id, const std::string& size, const char* form, int walk) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 200;
    m["speed"]["walk"] = walk;
    m["abilities"]["str"] = 10;
    m["traits"] = Json::array();
    if (form && form[0]) m["traits"].push_back(trait(form, "Form"));
    m.erase("behavior");
    return m;
}

Json leap_action() {
    return {{"id", "deadly_leap"}, {"name", "Deadly Leap"}, {"kind", "deadly_leap"}, {"jump_ft", 15},
            {"save", {{"ability", "str"}, {"dc", 1}}}, {"save_alt", "dex"}, {"push_ft", 5},
            {"damage", Json::array({{{"dice", "3d8+1"}, {"type", "bludgeoning"}},
                                    {{"dice", "3d8+1"}, {"type", "piercing"}}})}};
}

struct Spawn {
    std::string id;
    Vec2i at;
    const char* team = nullptr;   // nullptr: the schema default (monsters)
};

ContentLibrary lib_of(const std::vector<Json>& docs) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    for (const Json& d : docs) {
        const auto e = lib.add(d, "monster");
        REQUIRE_MESSAGE(!e, e.value_or(""));
    }
    return lib;
}

struct Arena {
    ContentLibrary lib;
    world::Chunk ch;
    ecs::Registry reg;
    std::optional<Combat> c;
};

void fill(Arena& a, const std::vector<Json>& docs, const std::vector<Spawn>& spawns, uint64_t seed = 1,
         const std::vector<Vec2i>& walls = {}) {
    a.lib = lib_of(docs);
    a.ch = world::Chunk{};
    a.ch.coord = {5, 5, 0};
    a.ch.origin = world::Chunk::Origin::Handcrafted;
    a.ch.palette = {"", "floor", "wall"};
    for (auto& cell : a.ch.layers[0]) cell.tile = 1;
    for (Vec2i w : walls) a.ch.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        a.ch.spawns.push_back({spawns[i].id, spawns[i].at, world::spawn_stable_id(a.ch.coord, uint32_t(i)), ""});
        if (spawns[i].team) a.ch.spawns.back().overrides_json = std::string("{\"team\":\"") + spawns[i].team + "\"}";
    }
    a.ch.content_hash = world::compute_content_hash(a.ch);
    std::string err;
    a.c = Combat::create(a.reg, a.ch, a.lib, seed, {}, &err);
    REQUIRE_MESSAGE(a.c, err);
}

CombatantId id_at(const Arena& a, std::size_t i) { return a.ch.spawns[i].stable_id; }

void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    auto r = c.submit(e);
    REQUIRE_MESSAGE(r.ok, r.error);
    c.settle(nullptr);
}

void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 16 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}

SubmitResult step(Combat& c, Vec2i to, uint16_t jump = 0) {
    Command x;
    x.kind = CommandKind::Step;
    x.to = to;
    x.request.jump_ft = jump;
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
}

void replay_matches(const Arena& a, uint64_t seed = 1) {
    ecs::Registry reg;
    std::string err;
    auto again = replay(reg, a.ch, a.lib, seed, a.c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == a.c->hash());
    CHECK(again->log_hash() == a.c->log_hash());
    CHECK(again->pos(id_at(a, 0)) == a.c->pos(id_at(a, 0)));
}

}  // namespace

TEST_CASE("air_form and water_form set enter_hostile_space; fire_form and a plain trait do not") {
    Json both = creature("t.both", "medium", nullptr, 30);
    both["traits"] = Json::array({trait("air_form", "Air Form"), trait("water_form", "Water Form")});
    Json fly = creature("t.fly", "medium", nullptr, 30);
    fly["traits"] = Json::array({trait("flyby", "Flyby"), trait("air_form", "Air Form")});
    auto lib = lib_of({creature("t.air", "medium", "air_form", 30), creature("t.water", "large", "water_form", 30),
                       creature("t.plain", "medium", nullptr, 30), creature("t.fire", "large", "fire_form", 30), both, fly});
    CHECK(lib.find("t.air")->base.enter_hostile_space);
    CHECK(lib.find("t.water")->base.enter_hostile_space);
    CHECK_FALSE(lib.find("t.plain")->base.enter_hostile_space);
    CHECK_FALSE(lib.find("t.fire")->base.enter_hostile_space);
    CHECK(lib.find("t.both")->base.enter_hostile_space);
    CHECK(lib.find("t.fly")->base.enter_hostile_space);
    CHECK(lib.find("t.fly")->base.flyby);
    CHECK_FALSE(lib.find("t.air")->base.flyby);
}

TEST_CASE("Air Form enters a hostile space, pays difficult terrain, and stops on its last movement") {
    Arena a;
    fill(a, {creature("t.air", "medium", "air_form", 10), creature("t.foe", "medium", nullptr, 30)},
         {{"t.air", {5, 5}}, {"t.foe", {6, 5}, "party"}});
    const CombatantId me = id_at(a, 0), foe = id_at(a, 1);
    to_actor(*a.c, me);
    CHECK(rules5e::find(a.c->state(), me)->enter_hostile_space);
    CHECK_FALSE(rules5e::find(a.c->state(), foe)->enter_hostile_space);
    auto r = step(*a.c, {6, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(a.c->pos(me) == a.c->pos(foe));
    CHECK(a.c->state().budget.movement_ft == 0);   // 10 ft of difficult terrain, nothing left
    r = step(*a.c, {7, 5});
    CHECK(r.error == "not enough movement");
    Command end;
    end.request = {me, ActionKind::EndTurn};
    r = a.c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    a.c->settle(nullptr);
    replay_matches(a);
}

TEST_CASE("Air Form can move through a hostile space and out the other side") {
    Arena a;
    fill(a, {creature("t.air", "medium", "air_form", 30), creature("t.foe", "medium", nullptr, 30)},
         {{"t.air", {5, 5}}, {"t.foe", {6, 5}, "party"}});
    const CombatantId me = id_at(a, 0);
    to_actor(*a.c, me);
    REQUIRE(step(*a.c, {6, 5}).ok);
    CHECK(a.c->state().budget.movement_ft == 20);
    auto r = step(*a.c, {7, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(a.c->pos(me) == Vec2i{7, 5});
    CHECK(a.c->state().budget.movement_ft == 15);
    CHECK(a.c->forbids_stop(me, a.c->pos(me), 1) == false);
}

TEST_CASE("a creature without the trait cannot enter a same-size hostile, and Air Form cannot stop in an ally") {
    {
        Arena a;
        fill(a, {creature("t.plain", "medium", nullptr, 30), creature("t.foe", "medium", nullptr, 30)},
             {{"t.plain", {5, 5}}, {"t.foe", {6, 5}, "party"}});
        to_actor(*a.c, id_at(a, 0));
        auto r = step(*a.c, {6, 5});
        CHECK(r.error == "tile occupied");
        CHECK(a.c->pos(id_at(a, 0)) == Vec2i{5, 5});
    }
    {
        Arena a;
        fill(a, {creature("t.air", "medium", "air_form", 30), creature("t.ally", "medium", nullptr, 30),
                 creature("t.far", "medium", nullptr, 30)},
             {{"t.air", {5, 5}}, {"t.ally", {5, 6}}, {"t.far", {20, 20}, "party"}});
        const CombatantId me = id_at(a, 0);
        to_actor(*a.c, me);
        auto r = step(*a.c, {5, 6});
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(a.c->state().budget.movement_ft == 20);   // an ally's space is still difficult terrain
        CHECK(a.c->pos(me) == Vec2i{5, 6});
        Command end;
        end.request = {me, ActionKind::EndTurn};
        r = a.c->submit(end);
        CHECK_FALSE(r.ok);
        CHECK(r.error == "cannot end movement in another creature's space");
        REQUIRE(step(*a.c, {5, 5}).ok);
        end.request = {me, ActionKind::EndTurn};
        REQUIRE(a.c->submit(end).ok);
    }
    {
        Arena a;
        fill(a, {creature("t.air", "medium", "air_form", 10), creature("t.ally", "medium", nullptr, 30),
                 creature("t.far", "medium", nullptr, 30)},
             {{"t.air", {5, 5}}, {"t.ally", {5, 6}}, {"t.far", {20, 20}, "party"}});
        to_actor(*a.c, id_at(a, 0));
        auto r = step(*a.c, {5, 6});
        CHECK(r.error == "cannot end movement in another creature's space");
        CHECK(a.c->pos(id_at(a, 0)) == Vec2i{5, 5});
        CHECK(a.c->state().budget.movement_ft == 10);
    }
}

TEST_CASE("size and several occupants: a Large form covers two hostiles; an ally cannot join them") {
    Json air = creature("t.air", "large", "air_form", 40);
    Json water = creature("t.water", "medium", "water_form", 40);
    Json ally = creature("t.ally", "medium", "air_form", 40);
    Json foe = creature("t.foe", "medium", nullptr, 30);
    Json foe2 = creature("t.foe2", "medium", nullptr, 30);
    // The pair at (7,4) and (7,5) has no step out once the Large stands on (6,4): every other neighbour is a wall, or a
    // creature of the same size. They can still end a turn while they are not yet overlapped.
    const std::vector<Vec2i> walls = {{6, 3}, {7, 3}, {8, 3}, {8, 4}, {6, 6}, {7, 6}, {8, 6}};
    Arena a;
    fill(a, {air, foe, foe2, water, ally},
         {{"t.air", {4, 4}},
          {"t.foe", {7, 4}, "party"},
          {"t.foe2", {7, 5}, "party"},
          {"t.water", {8, 5}, "rivals"},
          {"t.ally", {9, 5}}},
         1, walls);
    const CombatantId big = id_at(a, 0), h1 = id_at(a, 1), h2 = id_at(a, 2), wet = id_at(a, 3), pal = id_at(a, 4);
    to_actor(*a.c, big);
    REQUIRE(step(*a.c, {5, 4}).ok);
    CHECK(a.c->state().budget.movement_ft == 35);
    auto r = step(*a.c, {6, 4});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(a.c->state().budget.movement_ft == 25);   // the two hostiles are difficult terrain, once
    CHECK(a.c->pos(big) == Vec2i{6, 4});
    CHECK_FALSE(a.c->forbids_stop(big, a.c->pos(big), a.c->footprint_size(big)));
    Command end;
    end.request = {big, ActionKind::EndTurn};
    r = a.c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    a.c->settle(nullptr);
    const auto occ = a.c->occupied_except(big);
    CHECK(std::find(occ.begin(), occ.end(), Vec2i{7, 4}) != occ.end());
    CHECK(std::find(occ.begin(), occ.end(), Vec2i{7, 5}) != occ.end());
    const auto theirs = a.c->occupied_except(h1);
    CHECK(std::find(theirs.begin(), theirs.end(), Vec2i{6, 4}) != theirs.end());

    to_actor(*a.c, pal);
    REQUIRE(step(*a.c, {8, 5}).ok);   // through the hostile Water Form, which this ally may also enter
    r = step(*a.c, {7, 5});           // the Large is an ally, so the shared hostile space is not a place to stop
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(a.c->pos(pal) == Vec2i{7, 5});
    end.request = {pal, ActionKind::EndTurn};
    r = a.c->submit(end);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "cannot end movement in another creature's space");
    REQUIRE(step(*a.c, {8, 5}).ok);
    REQUIRE(step(*a.c, {9, 5}).ok);
    end.request = {pal, ActionKind::EndTurn};
    r = a.c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    a.c->settle(nullptr);

    to_actor(*a.c, wet);
    r = step(*a.c, {7, 5});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(a.c->pos(wet) == a.c->pos(h2));
    CHECK_FALSE(a.c->forbids_stop(wet, a.c->pos(wet), 1));
    end.request = {wet, ActionKind::EndTurn};
    r = a.c->submit(end);
    REQUIRE_MESSAGE(r.ok, r.error);
    a.c->settle(nullptr);
    replay_matches(a);
}

TEST_CASE("a Huge passes a Medium hostile and cannot stop; Water Form can") {
    {
        Arena a;
        fill(a, {creature("t.huge", "huge", nullptr, 30), creature("t.foe", "medium", nullptr, 30)},
             {{"t.huge", {4, 4}}, {"t.foe", {8, 5}, "party"}});
        const CombatantId h = id_at(a, 0);
        to_actor(*a.c, h);
        REQUIRE(step(*a.c, {5, 4}).ok);
        auto r = step(*a.c, {6, 4});
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(a.c->forbids_stop(h, a.c->pos(h), a.c->footprint_size(h)));
        Command end;
        end.request = {h, ActionKind::EndTurn};
        r = a.c->submit(end);
        CHECK_FALSE(r.ok);
        CHECK(r.error == "cannot end movement in another creature's space");
    }
    {
        Arena a;
        fill(a, {creature("t.huge", "huge", "water_form", 30), creature("t.foe", "medium", nullptr, 30)},
             {{"t.huge", {4, 4}}, {"t.foe", {8, 5}, "party"}});
        const CombatantId h = id_at(a, 0);
        to_actor(*a.c, h);
        REQUIRE(step(*a.c, {5, 4}).ok);
        REQUIRE(step(*a.c, {6, 4}).ok);
        CHECK_FALSE(a.c->forbids_stop(h, a.c->pos(h), a.c->footprint_size(h)));
        Command end;
        end.request = {h, ActionKind::EndTurn};
        auto r = a.c->submit(end);
        REQUIRE_MESSAGE(r.ok, r.error);
        a.c->settle(nullptr);
        replay_matches(a);
    }
    {
        Arena a;
        fill(a, {creature("t.large", "large", nullptr, 30), creature("t.foe", "medium", nullptr, 30)},
             {{"t.large", {4, 4}}, {"t.foe", {7, 4}, "party"}});
        to_actor(*a.c, id_at(a, 0));
        REQUIRE(step(*a.c, {5, 4}).ok);
        auto r = step(*a.c, {6, 4});
        CHECK(r.error == "tile occupied");
    }
}

TEST_CASE("Deadly Leap still refuses the leaper's overlap, and a form creature's tile is not a free push") {
    Json leap = creature("t.leap", "large", nullptr, 40);
    leap["actions"] = Json::array({leap_action()});
    Json foe = creature("t.foe", "medium", nullptr, 30);
    foe["abilities"]["str"] = 30;   // the success save cannot miss DC 1
    Json air = creature("t.air", "medium", "air_form", 30);
    // B8 geometry: three east jumps from (4,5) land on (7,5). The Medium at (8,5) is pushed to (9,4) when that tile is free.
    Arena a;
    fill(a, {leap, foe, air}, {{"t.leap", {4, 5}}, {"t.foe", {8, 5}, "party"}, {"t.air", {9, 4}}});
    const CombatantId leaper = id_at(a, 0), victim = id_at(a, 1), elem = id_at(a, 2);
    to_actor(*a.c, leaper);
    REQUIRE(step(*a.c, {5, 5}, 5).ok);
    REQUIRE(step(*a.c, {6, 5}, 5).ok);
    REQUIRE(step(*a.c, {7, 5}, 5).ok);
    CHECK(a.c->pos(leaper) == Vec2i{7, 5});
    CHECK(a.c->state().budget.jumped_ft == 15);
    Command end;
    end.request = {leaper, ActionKind::EndTurn};
    auto refused = a.c->submit(end);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "cannot end movement in another creature's space");

    const auto occ = a.c->occupied_except(victim);
    CHECK(std::find(occ.begin(), occ.end(), Vec2i{9, 4}) != occ.end());
    Command cast;
    cast.request = {leaper, ActionKind::CastSpell, victim};
    cast.request.spell_id = rules5e::spell_id_of("t.leap#deadly_leap");
    cast.request.targets = {victim};
    auto res = a.c->submit(cast);
    REQUIRE_MESSAGE(res.ok, res.error);
    a.c->settle(nullptr);
    CHECK(rules5e::find(a.c->state(), victim)->has(Condition::Prone));
    CHECK(a.c->pos(victim) == Vec2i{8, 5});
    CHECK(a.c->pos(elem) == Vec2i{9, 4});
    refused = a.c->submit(end);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "cannot end movement in another creature's space");
    REQUIRE(step(*a.c, {6, 5}).ok);
    auto left = a.c->submit(end);
    REQUIRE_MESSAGE(left.ok, left.error);
    a.c->settle(nullptr);
    replay_matches(a);
}
