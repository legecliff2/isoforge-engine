// monster@3 / character@3: resistances, immunities, vulnerabilities, condition immunities and
// multi-component attack damage reach the sim through ContentLibrary. Pre-@3 files stay inert
// (pinned by compat_defenses_tests.cpp).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::DamageType;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json goblin3() { return load_json("content/packs/srd51.core/monsters/goblin.json"); }
Json fighter3() { return load_json("content/packs/sample.vertical_slice/characters/fighter_1.json"); }

world::Chunk arena(const std::vector<std::string>& archetypes) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < archetypes.size(); ++i)
        c.spawns.push_back({archetypes[i], Vec2i{10 + int(i), 10}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

// First DamageDealt on the goblin when the fighter attacks it (both at full HP, adjacent).
struct FirstHit { bool hit = false, crit = false; int dealt = 0; int events = 0; };
FirstHit fighter_hits_goblin(const Json& fighter, const Json& goblin, uint64_t seed) {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(goblin, "monster"));
    REQUIRE_FALSE(lib.add(fighter, "character"));
    ecs::Registry reg;
    auto chunk = arena({fighter["id"], goblin["id"]});
    auto c = Combat::create(reg, chunk, lib, seed);
    REQUIRE(c);
    const CombatantId f = chunk.spawns[0].stable_id, g = chunk.spawns[1].stable_id;
    for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != f; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        auto r = c->submit(e);
        while (r.needs) r = c->decide({r.needs->window_seq, true, 0});
    }
    REQUIRE(rules5e::current_actor(c->state()) == f);
    Command a;
    a.request = {f, ActionKind::Attack, g};
    a.request.attack_index = 0;
    auto r = c->submit(a);
    REQUIRE_MESSAGE(r.ok, r.error);
    while (r.needs) r = c->decide({r.needs->window_seq, true, 0});
    FirstHit out;
    for (const auto& e : c->events()) {
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == rules5e::EventKind::AttackResolved && e.rules.actor == f) {
            out.hit = e.rules.c & 1;
            out.crit = e.rules.c & 2;
        }
        if (e.rules.kind == rules5e::EventKind::DamageDealt && e.rules.target == g) {
            out.dealt += e.rules.a;
            ++out.events;
        }
    }
    return out;
}
} // namespace

TEST_CASE("defenses: monster@3 resistances / immunities / vulnerabilities / condition immunities load") {
    Json g = goblin3();
    g["damage_resistances"] = Json::array({"fire", "cold"});
    g["damage_immunities"] = Json::array({"poison"});
    g["damage_vulnerabilities"] = Json::array({"bludgeoning"});
    g["condition_immunities"] = Json::array({"frightened", "exhaustion"});
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(g, "monster"));
    const Archetype* a = lib.find("srd51.monster.goblin");
    REQUIRE(a);
    using rules5e::bit;
    CHECK(a->base.resistances == (bit(DamageType::Fire) | bit(DamageType::Cold)));
    CHECK(a->base.immunities == bit(DamageType::Poison));
    CHECK(a->base.vulnerabilities == bit(DamageType::Bludgeoning));
    CHECK(a->base.condition_immunities == (bit(rules5e::Condition::Frightened) | bit(rules5e::Condition::Exhaustion)));
    CHECK(a->inert_mechanics.empty());
    // Absent fields: all zero.
    ContentLibrary plain;
    REQUIRE_FALSE(plain.add(goblin3(), "monster"));
    const auto& b = plain.find("srd51.monster.goblin")->base;
    CHECK(b.resistances == 0);
    CHECK(b.immunities == 0);
    CHECK(b.vulnerabilities == 0);
    CHECK(b.condition_immunities == 0);
    // character@3 reads them too.
    Json f = fighter3();
    f["damage_resistances"] = Json::array({"slashing"});
    f["condition_immunities"] = Json::array({"charmed"});
    REQUIRE_FALSE(plain.add(f, "character"));
    const auto& fb = plain.find("srd51.character.fighter_1")->base;
    CHECK(fb.resistances == bit(DamageType::Slashing));
    CHECK(fb.condition_immunities == bit(rules5e::Condition::Charmed));
}

TEST_CASE("defenses: unknown damage type / condition names are load errors") {
    Json g = goblin3();
    g["damage_resistances"] = Json::array({"sonic"});
    ContentLibrary lib;
    auto e = lib.add(g, "monster");
    REQUIRE(e);
    CHECK(e->find("damage_resistances") != std::string::npos);
    g = goblin3();
    g["condition_immunities"] = Json::array({"sleepy"});
    e = lib.add(g, "monster");
    REQUIRE(e);
    CHECK(e->find("condition_immunities") != std::string::npos);
    g = goblin3();
    g["actions"][0]["damage"].push_back({{"dice", "1d4"}, {"type", "sonic"}});
    e = lib.add(g, "monster");
    REQUIRE(e);
    CHECK(e->find("bad damage") != std::string::npos);
}

TEST_CASE("defenses: multiple damage components per attack load in order; first stays Attack::damage") {
    Json g = goblin3();
    g["actions"][0]["damage"].push_back({{"dice", "2d6"}, {"type", "fire"}});
    g["actions"][0]["damage"].push_back({{"dice", "1d4+1"}, {"type", "cold"}});
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(g, "monster"));
    const auto& at = lib.find("srd51.monster.goblin")->base.attacks[0];
    CHECK(at.damage == rules5e::DiceExpr{1, 6, 2});
    CHECK(at.type == DamageType::Slashing);
    REQUIRE(at.extra_damage.size() == 2);
    CHECK(at.extra_damage[0].dice == rules5e::DiceExpr{2, 6, 0});
    CHECK(at.extra_damage[0].type == DamageType::Fire);
    CHECK(at.extra_damage[1].dice == rules5e::DiceExpr{1, 4, 1});
    CHECK(at.extra_damage[1].type == DamageType::Cold);
    CHECK(lib.find("srd51.monster.goblin")->base.attacks[1].extra_damage.empty());
}

TEST_CASE("defenses: the 2 -> 3 migration marks a pre-@3 file's defenses and extra damage inert") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    Json g = goblin3();
    g["$schema"] = "monster@2";
    g["damage_resistances"] = Json::array({"fire"});
    g["condition_immunities"] = Json::array({"frightened"});
    g["actions"][0]["damage"].push_back({{"dice", "1d4"}, {"type", "fire"}});
    auto r = reg.load(g);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["inert_mechanics"] == Json::array({"damage_resistances", "condition_immunities", "damage[1..]"}));
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(r.data, "monster"));
    const Archetype* a = lib.find("srd51.monster.goblin");
    REQUIRE(a);
    CHECK(a->base.resistances == 0);
    CHECK(a->base.condition_immunities == 0);
    CHECK(a->base.attacks[0].extra_damage.empty());
    CHECK(a->inert_mechanics == std::vector<std::string>{"condition_immunities", "damage[1..]", "damage_resistances"});
    // A file with none of them is left untouched by the migration (no marker).
    Json clean = goblin3();
    clean["$schema"] = "monster@2";
    auto rc = reg.load(clean);
    REQUIRE(rc.ok);
    CHECK_FALSE(rc.data.contains("inert_mechanics"));
    // An already-@3 file never gets one, and its defenses are active.
    Json v3 = goblin3();
    v3["damage_immunities"] = Json::array({"fire"});
    auto r3 = reg.load(v3);
    REQUIRE(r3.ok);
    CHECK_FALSE(r3.data.contains("inert_mechanics"));
}

TEST_CASE("defenses: sim fight applies each damage component's own type defenses (monster@3 goblin)") {
    Json goblin = goblin3();
    goblin["hit_points"]["average"] = 200;
    Json fighter = fighter3();
    fighter["actions"][0]["damage"].push_back({{"dice", "1d1"}, {"type", "fire"}});   // deterministic +1 (+2 on crit)
    Json fighter_flat = fighter3();
    int seen_hit = 0, seen_crit = 0;
    for (uint64_t seed = 1; seed < 300 && (seen_hit < 3 || seen_crit < 1); ++seed) {
        const FirstHit base = fighter_hits_goblin(fighter_flat, goblin, seed);
        if (!base.hit) continue;
        const FirstHit multi = fighter_hits_goblin(fighter, goblin, seed);
        const int bonus = base.crit ? 2 : 1;
        CHECK(multi.events == 1);                      // one DamageDealt per hit
        CHECK(multi.dealt == base.dealt + bonus);
        Json immune = goblin;
        immune["damage_immunities"] = Json::array({"fire"});
        CHECK(fighter_hits_goblin(fighter, immune, seed).dealt == base.dealt);
        Json resist = goblin;
        resist["damage_resistances"] = Json::array({"fire"});
        CHECK(fighter_hits_goblin(fighter, resist, seed).dealt == base.dealt + (bonus / 2));
        Json vuln = goblin;
        vuln["damage_vulnerabilities"] = Json::array({"fire"});
        CHECK(fighter_hits_goblin(fighter, vuln, seed).dealt == base.dealt + bonus * 2);
        Json slash = goblin;
        slash["damage_immunities"] = Json::array({"slashing"});
        CHECK(fighter_hits_goblin(fighter, slash, seed).dealt == bonus);
        ++seen_hit;
        seen_crit += base.crit;
    }
    CHECK(seen_hit >= 3);
    CHECK(seen_crit >= 1);
}
