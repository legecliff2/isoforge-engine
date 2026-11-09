// Innate Invisibility content (CONTENT_CONVENTIONS §44): monster@5 trait `innate_invisibility`
// sets Combatant::innate_invisible and Condition::Invisible. An attack does not clear the bit.
// Sight reads it as subject_invisible. No SIM_EVENTS tag. The flag is static.
#include <doctest/doctest.h>

#include <algorithm>
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
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kInnate = {{"kind", "innate_invisibility"}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 104;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "invis_" + std::to_string(i++)}, {"name", "Invisibility"},
                               {"description", "test"}, {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }

Json swing_action(const char* id) {
    Json dmg = Json::object();
    dmg["flat"] = 4;
    dmg["type"] = "bludgeoning";
    Json act = Json::object();
    act["id"] = id;
    act["name"] = id;
    act["kind"] = "melee_weapon_attack";
    act["to_hit"] = 30;
    act["reach"] = 5;
    act["targets"] = 1;
    act["damage"] = Json::array({dmg});
    return act;
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
rules5e::Combatant* poke(Combat& c, CombatantId id) {
    return const_cast<rules5e::Combatant*>(rules5e::find(c.state(), id));
}
int attack_index(const rules5e::Combatant& c, const char* id) {
    for (int i = 0; i < int(c.attacks.size()); ++i)
        if (c.attacks[std::size_t(i)].id == id) return i;
    return -1;
}
}  // namespace

TEST_CASE("innate invisibility loader: the condition, an identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.stalker", {kInnate, kInnate})));
    const auto* t = lib.find("t.stalker");
    REQUIRE(t);
    CHECK(t->base.innate_invisible);
    CHECK(t->base.has(Condition::Invisible));

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_key", {{"kind", "innate_invisibility"}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kInnate, 4).find("needs a monster@5 file") != std::string::npos);

    Json immune = creature("t.immune", {kInnate});
    immune["condition_immunities"] = Json::array({"invisible"});
    REQUIRE_FALSE(add(lib, immune));
    const auto* im = lib.find("t.immune");
    REQUIRE(im);
    CHECK(im->base.innate_invisible);
    CHECK_FALSE(im->base.has(Condition::Invisible));

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.invis_pc";
    ch["features"].push_back({{"id", "invisibility"}, {"name", "Invisibility"}, {"description", "test"}, {"effect", kInnate}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);

    const auto* gob = lib.find("srd51.monster.goblin");
    REQUIRE(gob);
    CHECK_FALSE(gob->base.innate_invisible);
    CHECK_FALSE(gob->base.has(Condition::Invisible));
}

TEST_CASE("innate invisibility: the flag is not in Combat::hash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json doc = creature("t.stalker", {kInnate});
    doc["actions"] = Json::array({swing_action("slam")});
    REQUIRE_FALSE(add(lib, doc));
    auto ch = arena({{"t.stalker", Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId id = ch.spawns[0].stable_id;
    const uint64_t h0 = c->hash();
    const uint64_t l0 = c->log_hash();
    rules5e::Combatant* t = poke(*c, id);
    REQUIRE(t->innate_invisible);
    REQUIRE(t->has(Condition::Invisible));
    t->innate_invisible = false;
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);
}

TEST_CASE("innate invisibility: a loaded attack does not end it, sight and gaze use the condition, replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json doc = creature("t.stalker", {kInnate});
    doc["actions"] = Json::array({swing_action("slam")});
    REQUIRE_FALSE(add(lib, doc));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    const auto* arch = lib.find("t.stalker");
    REQUIRE(arch);
    const int slam = attack_index(arch->base, "slam");
    REQUIRE(slam >= 0);
    constexpr uint64_t kSeed = 11;
    auto ch = arena({{"t.foe", Vec2i{5, 5}}, {"t.stalker", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId foe_id = ch.spawns[0].stable_id;
    const CombatantId stalker = ch.spawns[1].stable_id;
    REQUIRE(poke(*c, stalker)->has(Condition::Invisible));
    REQUIRE(poke(*c, stalker)->innate_invisible);
    CHECK(poke(*c, stalker)->concentration_spell == 0);
    CHECK(c->sight_facts_of(foe_id, stalker).subject_invisible);
    CHECK_FALSE(c->sight_facts_of(stalker, foe_id).subject_invisible);
    poke(*c, stalker)->gazes.push_back({1, 30});
    const auto seen = c->gaze_seen(foe_id);
    CHECK(std::find(seen.begin(), seen.end(), stalker) == seen.end());

    to_actor(*c, stalker);
    Command cmd;
    cmd.request = {stalker, ActionKind::Attack, foe_id, uint8_t(slam)};
    const auto res = c->submit(cmd);
    REQUIRE_MESSAGE(res.ok, res.error);
    CHECK(poke(*c, stalker)->has(Condition::Invisible));
    CHECK(poke(*c, stalker)->concentration_spell == 0);
    bool removed = false;
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::ConditionRemoved &&
            e.rules.actor == stalker && e.rules.a == int32_t(Condition::Invisible))
            removed = true;
    CHECK_FALSE(removed);

    ecs::Registry again;
    auto rp = replay(again, ch, lib, kSeed, c->log());
    REQUIRE(rp);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
    CHECK(rules5e::find(rp->state(), stalker)->has(Condition::Invisible));
    CHECK(rules5e::find(rp->state(), stalker)->innate_invisible);
}
