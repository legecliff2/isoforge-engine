// B10: Air Elemental Whirlwind on the grid. Creatures in the elemental's space (footprint overlap,
// including a B9 Air Form stop) make the Strength save. A failure flings them up to 20 feet. The
// elemental's own tiles do not stop the fling. A wall at the third step is an object strike.
// air_elemental.json stays deferred. No new hash tag.
#include <doctest/doctest.h>

#include <cstdlib>
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

Json whirl_action(int dc, int impact_dc = 13) {
    return {{"id", "whirlwind"}, {"name", "Whirlwind"}, {"kind", "whirlwind"},
            {"save", {{"ability", "str"}, {"dc", dc}}},
            {"damage", Json::array({{{"dice", "3d8+2"}, {"type", "bludgeoning"}}})},
            {"fling_ft", 20},
            {"impact", {{"dice", "1d6"}, {"per_ft", 10}, {"type", "bludgeoning"}}},
            {"impact_save", {{"ability", "dex"}, {"dc", impact_dc}}},
            {"usage", {{"kind", "recharge"}, {"min", 4}}}};
}

Json creature(const std::string& id, const std::string& size, bool form, int str, int dc) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 200;
    m["speed"]["walk"] = 40;
    m["abilities"]["str"] = str;
    m["abilities"]["dex"] = 10;
    m["traits"] = Json::array();
    if (form) m["traits"].push_back(trait("air_form", "Air Form"));
    m["actions"] = form ? Json::array({whirl_action(dc)}) : Json::array();
    m.erase("behavior");
    return m;
}

struct Spawn {
    std::string id;
    Vec2i at;
    const char* team = nullptr;
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

void step_ok(Combat& c, Vec2i to) {
    Command x;
    x.kind = CommandKind::Step;
    x.to = to;
    auto r = c.submit(x);
    REQUIRE_MESSAGE(r.ok, r.error);
    c.settle(nullptr);
}

uint32_t whirl_id(const std::string& arch) { return rules5e::spell_id_of(arch + "#whirlwind"); }

std::vector<int> damages_since(const Combat& c, rules5e::CombatantId id, std::size_t from) {
    std::vector<int> out;
    for (std::size_t i = from; i < c.events().size(); ++i) {
        const auto& ev = c.events()[i];
        if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == rules5e::EventKind::DamageDealt && ev.rules.target == id)
            out.push_back(ev.rules.a);
    }
    return out;
}

int moved_feet(const Combat& c, rules5e::CombatantId id, std::size_t from) {
    for (std::size_t i = from; i < c.events().size(); ++i) {
        const auto& ev = c.events()[i];
        if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == rules5e::EventKind::Moved && ev.rules.actor == id &&
            (ev.rules.c & rules5e::kMovedForced))
            return ev.rules.a;
    }
    return -1;
}

std::vector<Vec2i> ring(Vec2i at) {
    const Vec2i dirs[8] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    std::vector<Vec2i> out;
    for (Vec2i d : dirs) out.push_back({at.x + 3 * d.x, at.y + 3 * d.y});
    return out;
}

void replay_matches(const Arena& a, uint64_t seed = 1) {
    ecs::Registry reg;
    std::string err;
    auto again = replay(reg, a.ch, a.lib, seed, a.c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == a.c->hash());
    CHECK(again->log_hash() == a.c->log_hash());
    for (const auto& k : a.c->state().combatants) CHECK(again->pos(k.id) == a.c->pos(k.id));
}

}  // namespace

TEST_CASE("whirlwind compiles to one failure-only save with a recharge, and is not a multiattack") {
    auto lib = lib_of({creature("t.air", "large", true, 14, 13)});
    const auto* arch = lib.find("t.air");
    REQUIRE(arch);
    CHECK(arch->base.multiattacks.empty());
    CHECK(arch->base.enter_hostile_space);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].whirl);
    CHECK(arch->save_actions[0].usage_kind == 1);
    const auto* d = lib.spells()->find(whirl_id("t.air"));
    REQUIRE(d);
    CHECK(d->on_save == rules5e::OnSave::None);
    CHECK(d->save_ability == rules5e::Ability::Str);
    CHECK(d->fixed_dc == 13);
    CHECK(d->whirl_ft == 20);
    CHECK(d->impact_per_ft == 10);
    CHECK(d->impact_die.count == 1);
    CHECK(d->impact_die.sides == 6);
    CHECK(d->impact_save == rules5e::Ability::Dex);
    CHECK(d->impact_dc == 13);
    CHECK(d->recharge_min == 4);
    CHECK(d->condition == Condition::Prone);
    CHECK(d->condition_untracked);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
}

TEST_CASE("whirlwind: the loader refuses a dishonest action") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    auto bad = [&](Json m) {
        ContentLibrary one;
        REQUIRE(one.load(sample_set()).empty());
        return one.add(m, "monster");
    };
    Json early = creature("t.early", "large", true, 10, 13);
    early["$schema"] = "monster@4";
    auto e = bad(early);
    REQUIRE(e);
    CHECK(e->find("whirlwind needs a monster@5 file") != std::string::npos);

    Json keys = creature("t.keys", "large", true, 10, 13);
    keys["actions"][0]["push_ft"] = 5;
    e = bad(keys);
    REQUIRE(e);
    CHECK(e->find("unknown key") != std::string::npos);

    Json will = creature("t.will", "large", true, 10, 13);
    will["actions"][0]["usage"] = {{"kind", "at_will"}};
    e = bad(will);
    REQUIRE(e);
    CHECK(e->find("recharge is the sentence") != std::string::npos);

    Json ft = creature("t.ft", "large", true, 10, 13);
    ft["actions"][0]["fling_ft"] = 7;
    e = bad(ft);
    REQUIRE(e);
    CHECK(e->find("fling_ft must be 5..60") != std::string::npos);

    Json two = creature("t.two", "large", true, 10, 13);
    two["actions"][0]["damage"].push_back({{"dice", "1d6"}, {"type", "piercing"}});
    e = bad(two);
    REQUIRE(e);
    CHECK(e->find("damage needs one component") != std::string::npos);
}

TEST_CASE("Whirlwind: a creature the elemental stopped on is flung into a wall, and replay matches") {
    const Vec2i foe_at{10, 8};
    Arena a;
    fill(a, {creature("t.air", "large", true, 14, 30), creature("t.foe", "medium", false, 10, 13)},
         {{"t.air", {8, 8}}, {"t.foe", foe_at, "party"}}, 3, ring(foe_at));
    const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
    to_actor(*a.c, elem);
    step_ok(*a.c, {9, 8});
    CHECK(a.c->pos(elem) == Vec2i{9, 8});
    CHECK(a.c->pos(foe) == foe_at);
    const auto from = a.c->events().size();
    Command cast;
    cast.request = {elem, ActionKind::CastSpell, foe};
    cast.request.spell_id = whirl_id("t.air");
    cast.request.targets = {foe};
    auto res = a.c->submit(cast);
    REQUIRE_MESSAGE(res.ok, res.error);
    a.c->settle(nullptr);
    CHECK(rules5e::find(a.c->state(), foe)->has(Condition::Prone));
    CHECK(moved_feet(*a.c, foe, from) == 10);   // two free steps, then the wall. The elemental's tiles are not the wall.
    const Vec2i delta{a.c->pos(foe).x - foe_at.x, a.c->pos(foe).y - foe_at.y};
    const bool on_a_ray = (delta.x == 0 || delta.y == 0 || std::abs(delta.x) == std::abs(delta.y)) &&
                          (std::abs(delta.x) == 2 || std::abs(delta.y) == 2);
    CHECK(on_a_ray);
    const auto packets = damages_since(*a.c, foe, from);
    REQUIRE(packets.size() == 2);
    CHECK(packets[0] >= 5);
    CHECK(packets[0] <= 26);
    CHECK(packets[1] >= 1);
    CHECK(packets[1] <= 6);
    const auto* use = rules5e::find_action_use(a.c->state(), elem, whirl_id("t.air"));
    REQUIRE(use);
    CHECK(use->expended);
    replay_matches(a, 3);
}

TEST_CASE("Whirlwind: empty space spends the recharge, a creature outside is refused, and a success is not half") {
    {
        Arena a;
        // A distant hostile keeps the encounter open. It is not in the space, so the listed cast has no target.
        fill(a, {creature("t.air", "large", true, 14, 13), creature("t.far", "medium", false, 10, 13)},
             {{"t.air", {8, 8}}, {"t.far", {20, 20}, "party"}});
        const CombatantId elem = id_at(a, 0);
        to_actor(*a.c, elem);
        int offered = 0;
        for (const auto& q : a.c->legal_actions())
            if (q.kind == ActionKind::CastSpell && q.spell_id == whirl_id("t.air")) { CHECK(q.target == 0); ++offered; }
        CHECK(offered == 1);
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {elem, ActionKind::CastSpell};
        cast.request.spell_id = whirl_id("t.air");
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK(damages_since(*a.c, elem, from).empty());
        CHECK(rules5e::find_action_use(a.c->state(), elem, whirl_id("t.air"))->expended);
        CHECK_FALSE(a.c->state().budget.action);
    }
    {
        Arena a;
        fill(a, {creature("t.air", "large", true, 14, 13), creature("t.foe", "medium", false, 10, 13)},
             {{"t.air", {8, 8}}, {"t.foe", {12, 8}, "party"}});
        const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
        to_actor(*a.c, elem);
        const uint64_t h = a.c->hash();
        Command cast;
        cast.request = {elem, ActionKind::CastSpell, foe};
        cast.request.spell_id = whirl_id("t.air");
        cast.request.targets = {foe};
        auto res = a.c->submit(cast);
        CHECK_FALSE(res.ok);
        CHECK(res.error == "target is not in the elemental's space");
        CHECK(a.c->hash() == h);
        CHECK(a.c->state().budget.action);
        CHECK_FALSE(rules5e::find_action_use(a.c->state(), elem, whirl_id("t.air"))->expended);
    }
    {
        Arena a;
        fill(a, {creature("t.air", "large", true, 14, 1), creature("t.foe", "medium", false, 20, 13)},
             {{"t.air", {8, 8}}, {"t.foe", {10, 8}, "party"}});
        const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
        to_actor(*a.c, elem);
        step_ok(*a.c, {9, 8});
        const Vec2i stood = a.c->pos(foe);
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {elem, ActionKind::CastSpell, foe};
        cast.request.spell_id = whirl_id("t.air");
        cast.request.targets = {foe};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK(damages_since(*a.c, foe, from).empty());
        CHECK_FALSE(rules5e::find(a.c->state(), foe)->has(Condition::Prone));
        CHECK(a.c->pos(foe) == stood);
        CHECK(moved_feet(*a.c, foe, from) == -1);
        CHECK(rules5e::find_action_use(a.c->state(), elem, whirl_id("t.air"))->expended);
    }
}

TEST_CASE("Whirlwind: two occupants, immunity, and an ally in the same space") {
    {
        Arena a;
        fill(a, {creature("t.air", "large", true, 14, 30), creature("t.a", "medium", false, 10, 13),
                 creature("t.b", "medium", false, 10, 13)},
             {{"t.air", {8, 8}}, {"t.a", {10, 8}, "party"}, {"t.b", {10, 9}, "party"}});
        const CombatantId elem = id_at(a, 0), a_id = id_at(a, 1), b_id = id_at(a, 2);
        to_actor(*a.c, elem);
        step_ok(*a.c, {9, 8});
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {elem, ActionKind::CastSpell, a_id};
        cast.request.spell_id = whirl_id("t.air");
        cast.request.targets = {a_id, b_id};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK(rules5e::find(a.c->state(), a_id)->has(Condition::Prone));
        CHECK(rules5e::find(a.c->state(), b_id)->has(Condition::Prone));
        CHECK_FALSE(damages_since(*a.c, a_id, from).empty());
        CHECK_FALSE(damages_since(*a.c, b_id, from).empty());
        replay_matches(a);
    }
    {
        Json immune = creature("t.foe", "medium", false, 10, 13);
        immune["damage_immunities"] = Json::array({"bludgeoning"});
        immune["condition_immunities"] = Json::array({"prone"});
        Arena a;
        fill(a, {creature("t.air", "large", true, 14, 30), immune},
             {{"t.air", {8, 8}}, {"t.foe", {10, 8}, "party"}});
        const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
        to_actor(*a.c, elem);
        step_ok(*a.c, {9, 8});
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {elem, ActionKind::CastSpell, foe};
        cast.request.spell_id = whirl_id("t.air");
        cast.request.targets = {foe};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK_FALSE(rules5e::find(a.c->state(), foe)->has(Condition::Prone));
        int sum = 0;
        for (int n : damages_since(*a.c, foe, from)) sum += n;
        CHECK(sum == 0);
        CHECK(moved_feet(*a.c, foe, from) == 20);   // open ground: the full fling, no strike
    }
    {
        Arena a;
        fill(a, {creature("t.air", "medium", true, 14, 30), creature("t.ally", "medium", false, 10, 13),
                 creature("t.far", "medium", false, 10, 13)},
             {{"t.air", {8, 8}}, {"t.ally", {8, 8}}, {"t.far", {20, 20}, "party"}});
        const CombatantId elem = id_at(a, 0), ally = id_at(a, 1);
        to_actor(*a.c, elem);
        bool listed = false;
        for (const auto& q : a.c->legal_actions())
            if (q.kind == ActionKind::CastSpell && q.spell_id == whirl_id("t.air") && q.target == ally) listed = true;
        CHECK_FALSE(listed);
        Command cast;
        cast.request = {elem, ActionKind::CastSpell, ally};
        cast.request.spell_id = whirl_id("t.air");
        cast.request.targets = {ally};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK(rules5e::find(a.c->state(), ally)->has(Condition::Prone));
        CHECK(a.c->pos(ally) != Vec2i{8, 8});
    }
}
