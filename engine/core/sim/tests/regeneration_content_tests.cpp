// Regeneration content (CONTENT_CONVENTIONS §31): monster@5 trait `regen` fills Combatant::regeneration.
// The rules path is engine/rules/tests/regeneration_tests.cpp. This file proves a loaded stat block
// reaches that path: start-of-turn heal, acid/fire suppression, and death only when a turn starts at
// 0 HP without regenerating. No SIM_EVENTS tag (0xE3 stays next free). amount / suppressed_by are
// static. regen_suppressed and regen_down are the existing rules flags and are not folded.
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
const Json kRegen = {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", {"acid", "fire"}}};
const uint32_t kAcidFire = rules5e::bit(DamageType::Acid) | rules5e::bit(DamageType::Fire);

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 84;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "rg_" + std::to_string(i++)}, {"name", "Regeneration"}, {"description", "test"}, {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }

Json swing_action(const char* id, const char* type) {
    // monster@5 flat damage: no rng draw, and a critical does not double it.
    Json dmg = Json::object();
    dmg["flat"] = 6;
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
    m["actions"] = Json::array({swing_action("hack", "slashing"), swing_action("brand", "fire")});
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
// AttackResolved.c bit 0 is a hit (encounter.hpp).
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
    REQUIRE(c.submit(cmd).ok);
    REQUIRE(last_attack_hit(c));
}
}  // namespace

TEST_CASE("regen loader: acid and fire, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json swapped = {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", {"fire", "acid"}}};
    REQUIRE_FALSE(add(lib, creature("t.regen", {kRegen, swapped})));
    const auto* t = lib.find("t.regen");
    REQUIRE(t);
    CHECK(t->base.regeneration.amount == 10);
    CHECK(t->base.regeneration.suppressed_by == kAcidFire);
    CHECK_FALSE(t->base.regen_suppressed);
    CHECK_FALSE(t->base.regen_down);

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_amt0", {{"kind", "regen"}, {"amount", 0}, {"suppressed_by", {"fire"}}}).find("amount must be an integer 1..32767") != std::string::npos);
    CHECK(err("t.e_amtbig", {{"kind", "regen"}, {"amount", 32768}, {"suppressed_by", {"fire"}}}).find("amount must be an integer 1..32767") != std::string::npos);
    CHECK(err("t.e_amtmiss", {{"kind", "regen"}, {"suppressed_by", {"fire"}}}).find("amount must be an integer 1..32767") != std::string::npos);
    CHECK(err("t.e_empty", {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", Json::array()}}).find("bad suppressed_by") != std::string::npos);
    CHECK(err("t.e_duptype", {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", {"fire", "fire"}}}).find("bad suppressed_by") != std::string::npos);
    CHECK(err("t.e_badtype", {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", {"holy"}}}).find("bad suppressed_by") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "regen"}, {"amount", 10}, {"suppressed_by", {"fire"}}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kRegen, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kRegen, {{"kind", "regen"}, {"amount", 5}, {"suppressed_by", {"acid", "fire"}}}}));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one regeneration") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.regen_pc";
    ch["features"].push_back({{"id", "regen"}, {"name", "Regeneration"}, {"description", "test"}, {"effect", kRegen}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);

    // Description-only regeneration stays off this effect. A spawned oni or vampire spawn still dies at 0 HP.
    for (const char* id : {"srd51.monster.goblin", "srd51.monster.oni", "srd51.monster.vampire_spawn"}) {
        const auto* a = lib.find(id);
        REQUIRE(a);
        CHECK(a->base.regeneration.amount == 0);
        CHECK(a->base.regeneration.suppressed_by == 0u);
    }
}

TEST_CASE("regen: static config and the rules flags are not in Combat::hash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.regen", {kRegen})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.regen", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";   // the striker is a monster; a second team lets it attack
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId regen = ch.spawns[1].stable_id;
    const uint64_t h0 = c->hash();
    const uint64_t l0 = c->log_hash();
    rules5e::Combatant* t = poke(*c, regen);
    REQUIRE(t->regeneration.amount == 10);
    REQUIRE(t->regeneration.suppressed_by == kAcidFire);
    t->regeneration.amount = 0;
    t->regeneration.suppressed_by = 0;
    t->regen_suppressed = true;
    t->regen_down = true;
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);
    t->regeneration.amount = 10;
    t->regeneration.suppressed_by = kAcidFire;
    t->regen_suppressed = false;
    t->regen_down = false;
}

TEST_CASE("regen: loaded troll contract heals, suppresses, and dies only at a suppressed turn start") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.regen", {kRegen})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const auto* striker_arch = lib.find("t.striker");
    REQUIRE(striker_arch);
    const int hack = attack_index(striker_arch->base, "hack");
    const int brand = attack_index(striker_arch->base, "brand");
    REQUIRE(hack >= 0);
    REQUIRE(brand >= 0);
    CHECK(striker_arch->base.attacks[std::size_t(hack)].damage.bonus == 6);
    CHECK(striker_arch->base.attacks[std::size_t(hack)].damage.count == 0);
    CHECK(striker_arch->base.attacks[std::size_t(brand)].type == DamageType::Fire);

    constexpr uint64_t kSeed = 11;
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.regen", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId striker_id = ch.spawns[0].stable_id;
    const CombatantId regen = ch.spawns[1].stable_id;
    REQUIRE(poke(*c, regen)->regeneration.suppressed_by == kAcidFire);

    // The opening turn may already be the regenerator's, at full hit points (no heal).
    if (rules5e::current_actor(c->state()) == regen) end_turn(*c);
    poke(*c, regen)->hp = 70;
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->hp == 80);
    end_turn(*c);
    poke(*c, regen)->hp = 80;
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->hp == 84);   // capped at the loaded maximum

    end_turn(*c);
    poke(*c, regen)->hp = 40;
    swing(*c, striker_id, regen, hack);
    CHECK(poke(*c, regen)->hp == 34);
    CHECK_FALSE(poke(*c, regen)->regen_suppressed);
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->hp == 44);

    end_turn(*c);
    poke(*c, regen)->hp = 40;
    swing(*c, striker_id, regen, brand);
    CHECK(poke(*c, regen)->hp == 34);
    CHECK(poke(*c, regen)->regen_suppressed);
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->hp == 34);   // suppressed this turn
    CHECK_FALSE(poke(*c, regen)->regen_suppressed);

    end_turn(*c);
    poke(*c, regen)->hp = 1;
    swing(*c, striker_id, regen, hack);
    CHECK(poke(*c, regen)->hp == 0);
    CHECK_FALSE(poke(*c, regen)->dead);
    CHECK(poke(*c, regen)->regen_down);
    CHECK_FALSE(c->ended());
    to_actor(*c, regen);
    CHECK_FALSE(poke(*c, regen)->dead);
    CHECK_FALSE(poke(*c, regen)->regen_down);
    CHECK(poke(*c, regen)->hp == 10);

    end_turn(*c);
    poke(*c, regen)->hp = 1;
    swing(*c, striker_id, regen, brand);
    CHECK(poke(*c, regen)->hp == 0);
    CHECK(poke(*c, regen)->regen_suppressed);
    CHECK_FALSE(poke(*c, regen)->dead);
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->dead);
    CHECK(c->ended());
}

TEST_CASE("regen: a content hit and the following turn replay") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.regen", {kRegen})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    constexpr uint64_t kSeed = 11;
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.regen", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId striker_id = ch.spawns[0].stable_id;
    const CombatantId regen = ch.spawns[1].stable_id;
    const int hack = attack_index(*rules5e::find(c->state(), striker_id), "hack");
    REQUIRE(hack >= 0);
    // Skip an opening turn that is already the regenerator's: it is at full hit points, so nothing heals.
    if (rules5e::current_actor(c->state()) == regen) end_turn(*c);
    swing(*c, striker_id, regen, hack);
    const int wounded = poke(*c, regen)->hp;
    CHECK(wounded == 78);   // 84 loaded, flat 6, slashing does not suppress
    CHECK_FALSE(poke(*c, regen)->regen_suppressed);
    to_actor(*c, regen);
    CHECK(poke(*c, regen)->hp == 84);   // +10 capped at the loaded maximum

    ecs::Registry again;
    auto rp = replay(again, ch, lib, kSeed, c->log());
    REQUIRE(rp);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
    CHECK(rules5e::find(rp->state(), regen)->hp == 84);
}
