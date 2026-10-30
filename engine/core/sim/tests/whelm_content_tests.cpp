// B11: Water Elemental Whelm on the grid. Creatures in the elemental's space (footprint overlap,
// including a B9 Water Form stop) make the Strength save. A failure grapples. A success is pushed
// until the footprints no longer overlap. water_elemental.json stays deferred. No new hash tag.
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

Json whelm_action(int dc) {
    return {{"id", "whelm"}, {"name", "Whelm"}, {"kind", "whelm"},
            {"save", {{"ability", "str"}, {"dc", dc}}},
            {"damage", Json::array({{{"flat", 13}, {"type", "bludgeoning"}}})},
            {"grapple", {{"escape_dc", 14}, {"max_size", "large"}}},
            {"crush", {{"flat", 4}, {"type", "bludgeoning"}}},
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
    if (form) {
        m["traits"].push_back({{"id", "water_form"}, {"name", "Water Form"},
                               {"description", "The elemental can enter a hostile creature's space and stop there."}});
        m["actions"] = Json::array({whelm_action(dc)});
    } else {
        m["actions"] = Json::array();
    }
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

void fill(Arena& a, const std::vector<Json>& docs, const std::vector<Spawn>& spawns, uint64_t seed = 1) {
    a.lib = lib_of(docs);
    a.ch = world::Chunk{};
    a.ch.coord = {5, 5, 0};
    a.ch.origin = world::Chunk::Origin::Handcrafted;
    a.ch.palette = {"", "floor", "wall"};
    for (auto& cell : a.ch.layers[0]) cell.tile = 1;
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

uint32_t whelm_id(const std::string& arch) { return rules5e::spell_id_of(arch + "#whelm"); }

bool overlaps(Vec2i a, int an, Vec2i b, int bn) {
    return a.x < b.x + bn && b.x < a.x + an && a.y < b.y + bn && b.y < a.y + an;
}

bool held(const Combat& c, CombatantId id) {
    for (const auto& e : c.state().sourced)
        if (e.condition == Condition::Grappled && e.target == id && e.via_attack == rules5e::kWhelmGrapple) return true;
    return false;
}

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

void cast_whelm(Combat& c, CombatantId elem, const std::vector<CombatantId>& ids) {
    Command cast;
    cast.request = {elem, ActionKind::CastSpell, ids.empty() ? rules5e::CombatantId(0) : ids[0]};
    cast.request.spell_id = whelm_id("t.water");
    cast.request.targets = ids;
    auto res = c.submit(cast);
    REQUIRE_MESSAGE(res.ok, res.error);
    c.settle(nullptr);
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

TEST_CASE("whelm compiles to a none-on-success save with a recharge") {
    auto lib = lib_of({creature("t.water", "large", true, 18, 15)});
    const auto* arch = lib.find("t.water");
    REQUIRE(arch);
    CHECK(arch->base.enter_hostile_space);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].whelm);
    CHECK_FALSE(arch->save_actions[0].whirl);
    CHECK(arch->save_actions[0].usage_kind == 1);
    const auto* d = lib.spells()->find(whelm_id("t.water"));
    REQUIRE(d);
    CHECK(d->on_save == rules5e::OnSave::None);
    CHECK(d->whelm);
    CHECK(d->whelm_escape_dc == 14);
    CHECK(d->whelm_max_size == int8_t(rules5e::Size::Large));
    CHECK(d->dice.bonus == 13);
    CHECK(d->whelm_crush.bonus == 4);
    CHECK(d->recharge_min == 4);
    CHECK(d->fixed_dc == 15);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
}

TEST_CASE("whelm: the loader refuses a dishonest action") {
    auto bad = [&](Json m) {
        ContentLibrary one;
        REQUIRE(one.load(sample_set()).empty());
        return one.add(m, "monster");
    };
    Json early = creature("t.early", "large", true, 10, 15);
    early["$schema"] = "monster@4";
    auto e = bad(early);
    REQUIRE(e);
    CHECK(e->find("whelm needs a monster@5 file") != std::string::npos);

    Json keys = creature("t.keys", "large", true, 10, 15);
    keys["actions"][0]["fling_ft"] = 20;
    e = bad(keys);
    REQUIRE(e);
    CHECK(e->find("unknown key") != std::string::npos);

    Json will = creature("t.will", "large", true, 10, 15);
    will["actions"][0]["usage"] = {{"kind", "at_will"}};
    e = bad(will);
    REQUIRE(e);
    CHECK(e->find("recharge is the sentence") != std::string::npos);

    Json cap = creature("t.cap", "large", true, 10, 15);
    cap["actions"][0]["grapple"]["max_size"] = "colossal";
    e = bad(cap);
    REQUIRE(e);
    CHECK(e->find("bad grapple") != std::string::npos);
}

TEST_CASE("Whelm: a creature the elemental stopped on is grappled, and a success is pushed clear") {
    {
        Arena a;
        fill(a, {creature("t.water", "large", true, 18, 30), creature("t.foe", "medium", false, 10, 15)},
             {{"t.water", {8, 8}}, {"t.foe", {10, 8}, "party"}});
        const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
        to_actor(*a.c, elem);
        step_ok(*a.c, {9, 8});
        CHECK(overlaps(a.c->pos(elem), 2, a.c->pos(foe), 1));
        const auto from = a.c->events().size();
        cast_whelm(*a.c, elem, {foe});
        CHECK(held(*a.c, foe));
        CHECK(rules5e::find(a.c->state(), foe)->has(Condition::Restrained));
        CHECK(a.c->pos(foe) == Vec2i{10, 8});
        const auto packets = damages_since(*a.c, foe, from);
        REQUIRE(packets.size() == 1);
        CHECK(packets[0] == 13);
        CHECK(moved_feet(*a.c, foe, from) == -1);
        const int16_t hp = rules5e::find(a.c->state(), foe)->hp;
        end_turn(*a.c);
        to_actor(*a.c, elem);
        CHECK(rules5e::find(a.c->state(), foe)->hp == int16_t(hp - 4));
        CHECK(held(*a.c, foe));
        replay_matches(a);
    }
    {
        Json water = creature("t.water", "large", true, 18, 1);
        Arena a;
        fill(a, {water, creature("t.foe", "medium", false, 10, 15)},
             {{"t.water", {8, 8}}, {"t.foe", {10, 8}, "party"}});
        const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
        to_actor(*a.c, elem);
        step_ok(*a.c, {9, 8});
        const Vec2i stood = a.c->pos(foe);
        const auto from = a.c->events().size();
        cast_whelm(*a.c, elem, {foe});
        CHECK_FALSE(held(*a.c, foe));
        CHECK(damages_since(*a.c, foe, from).empty());
        CHECK(moved_feet(*a.c, foe, from) == 5);
        CHECK_FALSE(overlaps(a.c->pos(elem), 2, a.c->pos(foe), 1));
        CHECK(a.c->pos(foe) != stood);
        replay_matches(a);
    }
}

TEST_CASE("Whelm: a Large creature fully overlapping is pushed clear in two steps") {
    Json water = creature("t.water", "large", true, 18, 1);
    Arena a;
    fill(a, {water, creature("t.foe", "large", false, 10, 15)},
         {{"t.water", {8, 8}}, {"t.foe", {10, 8}, "party"}});
    const CombatantId elem = id_at(a, 0), foe = id_at(a, 1);
    to_actor(*a.c, elem);
    step_ok(*a.c, {9, 8});
    step_ok(*a.c, {10, 8});
    CHECK(a.c->pos(elem) == a.c->pos(foe));
    const auto from = a.c->events().size();
    cast_whelm(*a.c, elem, {foe});
    CHECK(moved_feet(*a.c, foe, from) == 10);
    CHECK_FALSE(overlaps(a.c->pos(elem), 2, a.c->pos(foe), 2));
    CHECK_FALSE(held(*a.c, foe));
}

TEST_CASE("Whelm: empty space spends the recharge, and a creature outside is refused") {
    Arena a;
    fill(a, {creature("t.water", "large", true, 18, 15), creature("t.far", "medium", false, 10, 15)},
         {{"t.water", {8, 8}}, {"t.far", {20, 20}, "party"}});
    const CombatantId elem = id_at(a, 0);
    to_actor(*a.c, elem);
    int offered = 0;
    for (const auto& q : a.c->legal_actions())
        if (q.kind == ActionKind::CastSpell && q.spell_id == whelm_id("t.water")) { CHECK(q.target == 0); ++offered; }
    CHECK(offered == 1);
    cast_whelm(*a.c, elem, {});
    CHECK(rules5e::find_action_use(a.c->state(), elem, whelm_id("t.water"))->expended);
    CHECK_FALSE(a.c->state().budget.action);

    Arena b;
    fill(b, {creature("t.water", "large", true, 18, 15), creature("t.foe", "medium", false, 10, 15)},
         {{"t.water", {8, 8}}, {"t.foe", {12, 8}, "party"}});
    const CombatantId elem_b = id_at(b, 0), foe = id_at(b, 1);
    to_actor(*b.c, elem_b);
    const uint64_t h = b.c->hash();
    Command cast;
    cast.request = {elem_b, ActionKind::CastSpell, foe};
    cast.request.spell_id = whelm_id("t.water");
    cast.request.targets = {foe};
    auto res = b.c->submit(cast);
    CHECK_FALSE(res.ok);
    CHECK(res.error == "target is not in the elemental's space");
    CHECK(b.c->hash() == h);
    CHECK(b.c->state().budget.action);
}

TEST_CASE("Whelm: two of three Medium creatures are grappled, and the crush lands next turn") {
    Arena a;
    fill(a, {creature("t.water", "large", true, 18, 30), creature("t.a", "medium", false, 10, 15),
             creature("t.b", "medium", false, 10, 15), creature("t.c", "medium", false, 10, 15)},
         {{"t.water", {8, 8}}, {"t.a", {11, 8}, "party"}, {"t.b", {11, 9}, "party"}, {"t.c", {10, 9}, "party"}});
    const CombatantId elem = id_at(a, 0), a_id = id_at(a, 1), b_id = id_at(a, 2), c_id = id_at(a, 3);
    to_actor(*a.c, elem);
    step_ok(*a.c, {9, 8});
    step_ok(*a.c, {10, 8});
    CHECK(overlaps(a.c->pos(elem), 2, a.c->pos(a_id), 1));
    CHECK(overlaps(a.c->pos(elem), 2, a.c->pos(b_id), 1));
    CHECK(overlaps(a.c->pos(elem), 2, a.c->pos(c_id), 1));
    const auto from = a.c->events().size();
    cast_whelm(*a.c, elem, {a_id, b_id, c_id});
    CHECK(held(*a.c, a_id));
    CHECK(held(*a.c, b_id));
    CHECK_FALSE(held(*a.c, c_id));
    CHECK(damages_since(*a.c, c_id, from).size() == 1);
    CHECK(damages_since(*a.c, c_id, from)[0] == 13);
    replay_matches(a);
}
