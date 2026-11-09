// Lightning Absorption content (CONTENT_CONVENTIONS §43): monster@5 trait `absorb` fills Combatant::absorb.
// The rules path is engine/rules/tests/absorb_tests.cpp. A loaded lightning hit heals that amount.
// No SIM_EVENTS tag. `by` is static.
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
using rules5e::DamageType;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kAbsorb = {{"kind", "absorb"}, {"by", {"lightning"}}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 136;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "absorb_" + std::to_string(i++)}, {"name", "Lightning Absorption"},
                               {"description", "test"}, {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }

Json swing_action(const char* id, const char* type, int flat) {
    Json dmg = Json::object();
    dmg["flat"] = flat;
    dmg["type"] = type;
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
Json striker_monster() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.striker";
    m["actions"] = Json::array({swing_action("hack", "slashing", 6), swing_action("bolt", "lightning", 13)});
    return m;
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
bool last_attack_hit(const Combat& c) {
    int flags = -1;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::AttackResolved) flags = e.rules.c;
    return flags >= 0 && (flags & 1) != 0;
}
void swing(Combat& c, CombatantId who, CombatantId target, int index) {
    to_actor(c, who);
    Command cmd;
    cmd.request = {who, ActionKind::Attack, target, uint8_t(index)};
    const auto res = c.submit(cmd);
    REQUIRE_MESSAGE(res.ok, res.error);
    REQUIRE(last_attack_hit(c));
}
}  // namespace

TEST_CASE("absorb loader: lightning, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json swapped = {{"kind", "absorb"}, {"by", {"lightning"}}};
    REQUIRE_FALSE(add(lib, creature("t.absorb", {kAbsorb, swapped})));
    const auto* t = lib.find("t.absorb");
    REQUIRE(t);
    CHECK(t->base.absorb == rules5e::bit(DamageType::Lightning));

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_empty", {{"kind", "absorb"}, {"by", Json::array()}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_duptype", {{"kind", "absorb"}, {"by", {"lightning", "lightning"}}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_badtype", {{"kind", "absorb"}, {"by", {"holy"}}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "absorb"}, {"by", {"lightning"}}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kAbsorb, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kAbsorb, {{"kind", "absorb"}, {"by", {"lightning", "fire"}}}}));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one absorb") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.absorb_pc";
    ch["features"].push_back({{"id", "absorb"}, {"name", "Lightning Absorption"}, {"description", "test"}, {"effect", kAbsorb}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);

    const auto* gob = lib.find("srd51.monster.goblin");
    REQUIRE(gob);
    CHECK(gob->base.absorb == 0u);
}

TEST_CASE("absorb: static config is not in Combat::hash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.absorb", {kAbsorb})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.absorb", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId elem = ch.spawns[1].stable_id;
    const uint64_t h0 = c->hash();
    const uint64_t l0 = c->log_hash();
    rules5e::Combatant* t = poke(*c, elem);
    REQUIRE(t->absorb == rules5e::bit(DamageType::Lightning));
    t->absorb = 0;
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);
}

TEST_CASE("absorb: a loaded lightning hit heals that amount, and a replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.absorb", {kAbsorb})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const auto* striker_arch = lib.find("t.striker");
    REQUIRE(striker_arch);
    const int hack = attack_index(striker_arch->base, "hack");
    const int bolt = attack_index(striker_arch->base, "bolt");
    REQUIRE(hack >= 0);
    REQUIRE(bolt >= 0);
    constexpr uint64_t kSeed = 11;
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.absorb", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId striker = ch.spawns[0].stable_id;
    const CombatantId elem = ch.spawns[1].stable_id;

    if (rules5e::current_actor(c->state()) == elem) end_turn(*c);
    swing(*c, striker, elem, hack);
    CHECK(poke(*c, elem)->hp == 130);   // 136 - 6 slashing
    end_turn(*c);
    swing(*c, striker, elem, bolt);
    CHECK(poke(*c, elem)->hp == 136);   // +13 lightning, capped at the maximum (healed 6)

    ecs::Registry again;
    auto rp = replay(again, ch, lib, kSeed, c->log());
    REQUIRE(rp);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
    CHECK(rules5e::find(rp->state(), elem)->hp == 136);
}

TEST_CASE("absorb: lightning immunity on the loaded creature does not zero the regain") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.absorb", {kAbsorb})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const int bolt = attack_index(lib.find("t.striker")->base, "bolt");
    REQUIRE(bolt >= 0);
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.absorb", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId striker = ch.spawns[0].stable_id;
    const CombatantId elem = ch.spawns[1].stable_id;
    poke(*c, elem)->immunities |= rules5e::bit(DamageType::Lightning);
    poke(*c, elem)->hp = 100;
    if (rules5e::current_actor(c->state()) == elem) end_turn(*c);
    swing(*c, striker, elem, bolt);
    CHECK(poke(*c, elem)->hp == 113);
}
