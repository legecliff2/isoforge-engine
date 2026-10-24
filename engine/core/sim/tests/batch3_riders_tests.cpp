// Batch 3 small monster riders (Lich rulings appended to batch3_gaps_proposal.md; SRD 5.1 wording only). Sim loader + combat.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "rules5e/healing.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// The sample goblin (scimitar first) re-stamped monster@5, with a sure-hit scimitar.
Json creature(const std::string& id) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["actions"][0]["to_hit"] = 40;
    return m;
}
ContentLibrary lib_with(const std::vector<Json>& monsters, int fighter_hp = 2000) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = fighter_hp;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
std::string load_error(const Json& m) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const auto e = lib.add(m, "monster");
    REQUIRE(e);
    return *e;
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
const rules5e::Combatant& who(Combat& c, CombatantId id) { return *rules5e::find(c.state(), id); }
}  // namespace

TEST_CASE("drain loader key (wight Life Drain): max_hp + optional Con save; refusals") {
    Json w = creature("t.wight");
    w["actions"][0]["drain"] = {{"max_hp", true}, {"save", {{"ability", "con"}, {"dc", 13}}}};
    Json sp = creature("t.specterish");
    sp["actions"][0]["drain"] = {{"max_hp", true}};
    auto lib = lib_with({w, sp, creature("t.plain")});
    CHECK(lib.find("t.wight")->base.attacks.at(0).drain_max_hp);
    CHECK(lib.find("t.wight")->base.attacks.at(0).drain_save_dc == 13);
    CHECK(lib.find("t.specterish")->base.attacks.at(0).drain_max_hp);
    CHECK(lib.find("t.specterish")->base.attacks.at(0).drain_save_dc == 0);
    CHECK_FALSE(lib.find("t.plain")->base.attacks.at(0).drain_max_hp);

    Json bad = creature("t.bad");
    bad["actions"][0]["drain"] = {{"max_hp", false}};
    CHECK(load_error(bad).find("drain needs max_hp: true") != std::string::npos);
    bad["actions"][0]["drain"] = {{"max_hp", true}, {"save", {{"ability", "wis"}, {"dc", 13}}}};
    CHECK(load_error(bad).find("drain.save must be") != std::string::npos);
    bad["actions"][0]["drain"] = {{"max_hp", true}, {"heal", true}};
    CHECK(load_error(bad).find("unknown key 'heal'") != std::string::npos);
    bad["$schema"] = "monster@4";
    bad["actions"][0]["drain"] = {{"max_hp", true}};
    CHECK(load_error(bad).find("drain needs a monster@5 file") != std::string::npos);
}

TEST_CASE("drain combat: failed save reduces the maximum by the damage taken; a success does not") {
    auto run = [](int dc) {
        Json w = creature("t.wight");
        w["actions"][0]["drain"] = {{"max_hp", true}, {"save", {{"ability", "con"}, {"dc", dc}}}};
        auto lib = lib_with({w});
        auto ch = arena({{"t.wight", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        int taken = 0;
        for (int i = 0; i < 6 && taken == 0; ++i) {
            to_actor(*c, a);
            const int before = who(*c, f).hp;
            Command cmd;
            cmd.request = {a, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(cmd).ok);
            taken = before - who(*c, f).hp;
            if (taken == 0) { Command e; e.request = {a, ActionKind::EndTurn}; REQUIRE(c->submit(e).ok); }
        }
        REQUIRE(taken > 0);
        return std::pair<int, int>{taken, int(who(*c, f).hp_max_reduction)};
    };
    const auto [t1, r1] = run(30);   // no fighter_1 Con save reaches 30
    CHECK(r1 == t1);
    const auto [t2, r2] = run(1);    // every Con save total reaches DC 1 (a natural 1 is not an automatic failure on saves)
    CHECK(t2 > 0);
    CHECK(r2 == 0);
}

namespace {
// Death dog Bite rider (SRD 5.1): "must succeed on a DC 12 Constitution saving throw against disease or become poisoned
// until the disease is cured." The 24-hour hit point maximum reduction is deferred (no day clock in combat).
Json until_cured_rider(const std::string& cond, int dc) {
    return Json::array({{{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", dc}}},
                         {"apply", Json::array({{{"condition", cond}, {"duration", {{"kind", "until_cured"}}}}})}}});
}
}  // namespace

TEST_CASE("until_cured duration (death dog): poisoned, untracked, no timer; refusals") {
    Json d = creature("t.death_dog");
    d["actions"][0]["on_hit"] = until_cured_rider("poisoned", 30);
    auto lib = lib_with({d});
    auto ch = arena({{"t.death_dog", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    for (int i = 0; i < 6 && !who(*c, f).has(rules5e::Condition::Poisoned); ++i) {
        to_actor(*c, a);
        Command cmd;
        cmd.request = {a, ActionKind::Attack, f, 0};
        REQUIRE(c->submit(cmd).ok);
        c->settle(nullptr);
        Command e; e.request = {a, ActionKind::EndTurn}; REQUIRE(c->submit(e).ok);
    }
    REQUIRE(who(*c, f).has(rules5e::Condition::Poisoned));
    CHECK(c->state().effects.empty());   // no lasting-effect entry: nothing expires it
    for (int i = 0; i < 24; ++i) {       // 12 rounds later it is still there (no repeat save, no timer)
        Command e; e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn}; REQUIRE(c->submit(e).ok);
    }
    CHECK(who(*c, f).has(rules5e::Condition::Poisoned));
    // the cure path: Lesser Restoration's EndCondition removes a bare condition bit with remove_condition
    rules5e::Combatant copy = who(*c, f);
    rules5e::remove_condition(copy, rules5e::Condition::Poisoned);
    CHECK_FALSE(copy.has(rules5e::Condition::Poisoned));

    for (const char* bad : {"prone", "frightened", "grappled", "charmed"}) {
        Json b = creature("t.bad");
        b["actions"][0]["on_hit"] = until_cured_rider(bad, 12);
        CHECK(load_error(b).find("until_cured needs blinded, deafened, paralyzed or poisoned") != std::string::npos);
    }
    Json b = creature("t.bad");
    b["actions"][0]["on_hit"] = until_cured_rider("poisoned", 12);
    b["actions"][0]["on_hit"][0]["apply"][0]["repeat_save"] = "end_of_turn";
    CHECK(load_error(b).find("until_cured takes no repeat_save") != std::string::npos);
}

TEST_CASE("curse loader key (mummy Rotting Fist): failed Con save sets no_regain_hp; 0xDC folds; refusals") {
    auto run = [](int dc) {
        Json m = creature("t.mummy");
        m["actions"][0]["curse"] = {{"no_regain_hp", true}, {"save", {{"ability", "con"}, {"dc", dc}}}};
        auto lib = lib_with({m});
        CHECK(lib.find("t.mummy")->base.attacks.at(0).curse_no_regain_dc == dc);
        auto ch = arena({{"t.mummy", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        const uint64_t h0 = c->hash();
        bool hit = false;
        for (int i = 0; i < 6 && !hit; ++i) {
            to_actor(*c, a);
            const int before = who(*c, f).hp;
            Command cmd;
            cmd.request = {a, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(cmd).ok);
            c->settle(nullptr);
            hit = who(*c, f).hp < before;
            Command e; e.request = {a, ActionKind::EndTurn}; REQUIRE(c->submit(e).ok);
        }
        REQUIRE(hit);
        rules5e::Combatant copy = who(*c, f);
        if (copy.no_regain_hp) CHECK(rules5e::apply_healing(copy, 5).healed == 0);   // "can't regain hit points"
        ecs::Registry reg2;
        std::string err;
        auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
        REQUIRE_MESSAGE(rp, err);
        CHECK(rp->hash() == c->hash());
        CHECK(h0 != 0);
        return who(*c, f).no_regain_hp;
    };
    CHECK(run(30));         // fighter_1 cannot reach DC 30
    CHECK_FALSE(run(1));    // every save total reaches DC 1

    Json bad = creature("t.bad");
    bad["actions"][0]["curse"] = {{"no_regain_hp", true}};
    CHECK(load_error(bad).find("curse.save must be") != std::string::npos);
    bad["actions"][0]["curse"] = {{"no_regain_hp", false}, {"save", {{"ability", "con"}, {"dc", 12}}}};
    CHECK(load_error(bad).find("curse needs no_regain_hp: true") != std::string::npos);
    bad["$schema"] = "monster@4";
    bad["actions"][0]["curse"] = {{"no_regain_hp", true}, {"save", {{"ability", "con"}, {"dc", 12}}}};
    CHECK(load_error(bad).find("curse needs a monster@5 file") != std::string::npos);
}

TEST_CASE("0xDC: no_regain_hp folds only while a curse attack exists") {
    auto lib = lib_with({creature("t.plain")});
    auto ch = arena({{"t.plain", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry r1, r2;
    auto c1 = Combat::create(r1, ch, lib, 7);
    REQUIRE(c1);
    // no curse attack: flipping no_regain_hp does not reach Combat::hash through 0xDC (existing hashes unchanged)
    Json m = creature("t.plain");
    m["actions"][0]["curse"] = {{"no_regain_hp", true}, {"save", {{"ability", "con"}, {"dc", 12}}}};
    auto lib2 = lib_with({m});
    auto c2 = Combat::create(r2, ch, lib2, 7);
    REQUIRE(c2);
    CHECK(c1->hash() != c2->hash());   // the fold is present only with the curse attack
}

namespace {
// Vampire spawn Bite (SRD 5.1): "Hit: 6 (1d6 + 3) piercing damage plus 7 (2d6) necrotic damage. The target's hit point maximum
// is reduced by an amount equal to the necrotic damage taken, and the vampire regains hit points equal to that amount."
Json vampire_spawn(const std::string& id) {
    Json m = creature(id);
    auto& act = m["actions"][0];
    act["damage"].push_back({{"dice", "2d6"}, {"type", "necrotic"}});
    act["drain"] = {{"max_hp", true}, {"damage_type", "necrotic"}, {"heal_self", true}};
    act["target_requires_any"] = Json::array({"willing", "grappled_by_self", "incapacitated", "restrained"});
    m["hit_points"]["average"] = 400;
    return m;
}
}  // namespace

TEST_CASE("vampire spawn drain: necrotic-only reduction, self-heal, target gate") {
    auto lib = lib_with({vampire_spawn("t.vspawn")});
    const auto& at = lib.find("t.vspawn")->base.attacks.at(0);
    CHECK(at.drain_damage_type == int8_t(rules5e::DamageType::Necrotic));
    CHECK(at.drain_heal_self);
    CHECK(at.target_requires_any == 15);
    auto ch = arena({{"t.vspawn", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    to_actor(*c, a);
    rules5e::ActionRequest q{a, ActionKind::Attack, f, 0};
    q.target_within_5ft = true;
    {   // an enemy that is not grappled, incapacitated or restrained: refused, and "willing" never counts for an enemy
        rules5e::EncounterState s = c->state();
        auto r = rules5e::step(s, q);
        CHECK_FALSE(r.ok);
        CHECK(std::string(r.error).find("willing, grappled by the attacker, incapacitated, or restrained") != std::string::npos);
        q.target_willing = true;
        r = rules5e::step(s, q);
        CHECK_FALSE(r.ok);
        q.target_willing = false;
    }
    // restrained: allowed; the maximum drops by the necrotic damage only and the spawn heals that much
    rules5e::EncounterState s = c->state();
    rules5e::add_condition(*const_cast<rules5e::Combatant*>(rules5e::find(s, f)), rules5e::Condition::Restrained);
    const_cast<rules5e::Combatant*>(rules5e::find(s, a))->hp = 100;
    const int before = rules5e::find(s, f)->hp;
    auto r = rules5e::step(s, q);
    REQUIRE_MESSAGE(r.ok, std::string(r.error));
    const auto* ft = rules5e::find(s, f);
    const int lost = before - ft->hp;
    int necrotic = -1;
    for (const auto& e : r.events)
        if (e.kind == rules5e::EventKind::ModifierApplied && e.target == f && e.b == int32_t(rules5e::ModStat::MaxHp)) necrotic = -e.c;
    REQUIRE(lost > 0);   // seed 7: the sure-hit bite lands
    REQUIRE(necrotic > 0);
    CHECK(necrotic < lost);                      // the scimitar's slashing part (1d6 + 2) is not drained
    CHECK(ft->hp_max_reduction == necrotic);
    CHECK(rules5e::find(s, a)->hp == 100 + necrotic);
}
