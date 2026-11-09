// Regain content (CONTENT_CONVENTIONS §55): monster@5 trait `regain` fills Combatant::regain.
// At the start of its turn, while it has at least 1 hit point, it regains that many, capped at maximum.
// At 0 it is dead. Not troll regen. The static amount is not hashed.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <utility>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kRegain = {{"kind", "regain"}, {"amount", 10}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects, int hp) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "regain_" + std::to_string(i++)}, {"name", "Regeneration"}, {"description", "test"},
                               {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("regain loader: amount 10, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json same = {{"kind", "regain"}, {"amount", 10}};
    REQUIRE_FALSE(add(lib, creature("t.guard", {kRegain, same}, 80)));
    const auto* t = lib.find("t.guard");
    REQUIRE(t);
    CHECK(t->base.regain == 10);
    CHECK(t->base.regeneration.amount == 0);

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}, 80), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_amt", {{"kind", "regain"}, {"amount", 0}}).find("amount must be an integer 1..32767") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "regain"}, {"amount", 10}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kRegain, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kRegain, {{"kind", "regain"}, {"amount", 8}}}, 80));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one regain") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.regain_pc";
    ch["features"].push_back({{"id", "regeneration"}, {"name", "Regeneration"}, {"description", "test"}, {"effect", kRegain}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("only valid on monsters") != std::string::npos);
}

TEST_CASE("regain: a loaded guardian heals on its turn only, and the amount is not hashed") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kRegain}, 40)));
    REQUIRE_FALSE(add(lib, creature("t.plain", {}, 40)));
    auto ch = arena({{"t.guard", Vec2i{5, 5}}, {"t.plain", Vec2i{6, 5}}});
    ch.spawns[1].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const CombatantId guard = ch.spawns[0].stable_id;
    const CombatantId other = ch.spawns[1].stable_id;
    auto* g = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), guard));
    REQUIRE(g->regain == 10);
    g->hp = 40;
    const uint64_t full = c->hash();
    g->regain = 0;
    CHECK(c->hash() == full);
    g->regain = 10;
    CHECK(c->hash() == full);

    to_actor(*c, other);
    g->hp = 12;
    const int before = rules5e::find(c->state(), guard)->hp;
    end_turn(*c);
    if (rules5e::current_actor(c->state()) != guard) CHECK(rules5e::find(c->state(), guard)->hp == before);
    to_actor(*c, guard);
    CHECK(rules5e::find(c->state(), guard)->hp == 22);
    CHECK_FALSE(rules5e::find(c->state(), guard)->dead);
    CHECK_FALSE(rules5e::find(c->state(), guard)->regen_down);
}
