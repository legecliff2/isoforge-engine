// Tier-A §27 (vampire spawn Claws: "Instead of dealing damage, the vampire can grapple the target (escape DC 13)"), sim side:
// loader key, the grapple hook keyed on AttackResolved c & 8, log_hash 0xE2, and the AI rule (driver owner's ack pending).
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
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
// A goblin re-stamped monster@5 as a vampire spawn: claws (2d4+3, grapple instead DC 13), bite (gated), multiattack options
// 1 "claw_bite" = claws, bite; 2 "bite_claw" = bite, claws.
Json vspawn(const std::string& id) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    Json claws = m["actions"][0];
    claws["id"] = "claws"; claws["name"] = "Claws"; claws["to_hit"] = 40;
    claws["damage"] = Json::array({{{"dice", "2d4+3"}, {"type", "slashing"}}});
    claws["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 13}, {"instead_of_damage", true}}});
    Json bite = m["actions"][0];
    bite["id"] = "bite"; bite["name"] = "Bite"; bite["to_hit"] = 40;
    bite["target_requires_any"] = Json::array({"willing", "grappled_by_self", "incapacitated", "restrained"});
    m["actions"] = Json::array({claws, bite});
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"}, {"description", "two attacks"},
        {"effect", {{"kind", "multiattack"}, {"options", Json::array({
            {{"id", "claw_bite"}, {"attacks", Json::array({"claws", "bite"})}},
            {{"id", "bite_claw"}, {"attacks", Json::array({"bite", "claws"})}}})}}}}});
    return m;
}
ContentLibrary lib_with(const std::vector<Json>& monsters) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
world::Chunk arena() {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    c.spawns.push_back({"t.vspawn", Vec2i{6, 5}, world::spawn_stable_id(c.coord, 0), ""});
    c.spawns.push_back({kFighter, Vec2i{5, 5}, world::spawn_stable_id(c.coord, 1), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fx {
    ContentLibrary lib = lib_with({vspawn("t.vspawn")});
    world::Chunk ch = arena();
    ecs::Registry reg;
    std::optional<Combat> c;
    CombatantId a = 0, f = 0;
    explicit Fx(uint64_t seed = 7) {
        c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        a = ch.spawns[0].stable_id; f = ch.spawns[1].stable_id;
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != a; ++i) {
            Command e; e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == a);
    }
    rules5e::Combatant& mut(CombatantId id) {   // test-only: arrange a state
        return *const_cast<rules5e::Combatant*>(rules5e::find(c->state(), id));
    }
    rules5e::ActionRequest req(uint8_t index, uint8_t multi, bool instead = false) const {
        rules5e::ActionRequest q{a, ActionKind::Attack, f, index};
        q.target_within_5ft = true; q.multiattack = multi; q.grapple_instead = instead;
        return q;
    }
    bool grappled_by_me() const {
        for (auto id : rules5e::grappled_by(c->state(), a)) if (id == f) return true;
        return false;
    }
};
}  // namespace

TEST_CASE("Tier-A §27: loader sets grapple_instead_ok and the rider; refuses it with restrains") {
    auto lib = lib_with({vspawn("t.vspawn")});
    const auto* arch = lib.find("t.vspawn");
    REQUIRE(arch);
    CHECK(arch->base.attacks.at(0).grapple_instead_ok);
    CHECK_FALSE(arch->base.attacks.at(1).grapple_instead_ok);
    REQUIRE(arch->grapple_on_hit.count(0));
    CHECK(arch->grapple_on_hit.at(0).instead_of_damage);
    CHECK(arch->grapple_on_hit.at(0).escape_dc == 13);
    Json bad = vspawn("t.bad");
    bad["actions"][0]["on_hit"][0]["restrains"] = true;
    ContentLibrary l2;
    REQUIRE(l2.load(sample_set()).empty());
    const auto e = l2.add(bad, "monster");
    REQUIRE(e);
    CHECK(e->find("can't be combined with restrains") != std::string::npos);
    Json bad2 = vspawn("t.bad2");
    bad2["actions"][0]["on_hit"][0]["instead_of_damage"] = 1;
    const auto e2 = l2.add(bad2, "monster");
    REQUIRE(e2);
    CHECK(e2->find("bad grapple instead_of_damage") != std::string::npos);
}

TEST_CASE("Tier-A §27: claws with grapple_instead grapple and deal no damage; the bite is then legal; log_hash folds 0xE2") {
    Fx x;
    const int hp0 = rules5e::find(x.c->state(), x.f)->hp;
    const uint64_t lh0 = x.c->log_hash();
    Command cmd; cmd.request = x.req(0, 1, true);
    auto r = x.c->submit(cmd);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(x.grappled_by_me());
    CHECK(rules5e::find(x.c->state(), x.f)->hp == hp0);
    CHECK(x.c->log_hash() != lh0);
    Command bite; bite.request = x.req(1, 1);
    auto r2 = x.c->submit(bite);
    CHECK_MESSAGE(r2.ok, r2.error);   // the grappled_by_self gate passes
    CHECK(rules5e::find(x.c->state(), x.f)->hp < hp0);
}

TEST_CASE("Tier-A §27: claws without the choice deal damage and do not grapple") {
    Fx x;
    const int hp0 = rules5e::find(x.c->state(), x.f)->hp;
    Command cmd; cmd.request = x.req(0, 1, false);
    REQUIRE(x.c->submit(cmd).ok);
    CHECK_FALSE(x.grappled_by_me());
    CHECK(rules5e::find(x.c->state(), x.f)->hp < hp0);
}

TEST_CASE("Tier-A §27 AI fixture 1: a free target with the Bite unused is grappled") {
    Fx x;
    CHECK(choose_grapple_instead(*x.c, x.req(0, 1)));
    const Command cmd = next_command(*x.c, x.f);
    if (cmd.kind == CommandKind::Rules && cmd.request.kind == ActionKind::Attack && cmd.request.attack_index == 0)
        CHECK(cmd.request.grapple_instead);
}

TEST_CASE("Tier-A §27 AI fixture 2: a restrained target takes damage") {
    Fx x;
    rules5e::add_condition(x.mut(x.f), Condition::Restrained);
    CHECK_FALSE(choose_grapple_instead(*x.c, x.req(0, 1)));
}

TEST_CASE("Tier-A §27 AI fixture 3: Bite already used this turn: damage") {
    Fx x;
    rules5e::add_condition(x.mut(x.f), Condition::Restrained);   // so the opening bite is legal
    Command bite; bite.request = x.req(1, 2);
    auto r = x.c->submit(bite);
    REQUIRE_MESSAGE(r.ok, r.error);
    rules5e::remove_condition(x.mut(x.f), Condition::Restrained);
    REQUIRE(x.c->state().budget.sequence == 2);
    CHECK_FALSE(choose_grapple_instead(*x.c, x.req(0, 2)));
}

TEST_CASE("Tier-A §27 AI fixture 4: the claw would drop the target: damage") {
    Fx x;
    x.mut(x.f).hp = 3;
    x.mut(x.f).temp_hp = 0;
    CHECK_FALSE(choose_grapple_instead(*x.c, x.req(0, 1)));
    x.mut(x.f).hp = 500;
    CHECK(choose_grapple_instead(*x.c, x.req(0, 1)));   // same state with HP to spare: grapple
}

TEST_CASE("Tier-A §27: grapple_instead is refused on every non-Attack request and never hashed") {
    Fx x;
    const uint64_t lh0 = x.c->log_hash();
    for (ActionKind k : {ActionKind::EndTurn, ActionKind::Dodge, ActionKind::Dash}) {
        Command cmd; cmd.request = {x.a, k}; cmd.request.grapple_instead = true;
        const auto r = x.c->submit(cmd);
        CHECK_FALSE(r.ok);
        CHECK(std::string(r.error).find("only valid on an Attack") != std::string::npos);
        rules5e::EncounterState s = x.c->state();
        const auto rr = rules5e::step(s, cmd.request);
        CHECK_FALSE(rr.ok);
    }
    Command st; st.kind = CommandKind::Step; st.to = Vec2i{6, 6}; st.request.actor = x.a; st.request.grapple_instead = true;
    CHECK_FALSE(x.c->submit(st).ok);
    CHECK(x.c->log_hash() == lh0);
    for (const auto& q : rules5e::legal_actions(x.c->state()))
        if (q.grapple_instead) CHECK(q.kind == ActionKind::Attack);
}
