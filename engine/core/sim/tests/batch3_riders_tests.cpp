// Batch 3 small monster riders (Lich rulings appended to batch3_gaps_proposal.md; SRD 5.1 wording only). Sim loader + combat.
#include <doctest/doctest.h>
#include "rules5e/dice.hpp"

#include <fstream>
#include <string>
#include <tuple>

#include "sample_set.hpp"
#include "rules5e/healing.hpp"
#include "rules5e/rest.hpp"
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
// until the disease is cured." The 24-hour hit point maximum reduction is the B6 `disease` key; this fixture is the
// poisoned half only, which is what death_dog.json still authors.
Json until_cured_rider(const std::string& cond, int dc) {
    return Json::array({{{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", dc}}},
                         {"apply", Json::array({{{"condition", cond}, {"duration", {{"kind", "until_cured"}}}}})}}});
}
}  // namespace

Json disease_rider(int dc, const std::string& hp = "1d10") {
    Json r = until_cured_rider("poisoned", dc);
    r[0]["disease"] = {{"every_hours", 24}, {"hp_max", hp}};
    return r;
}

TEST_CASE("bite disease loader: 24h schedule on an on_hit save_condition; refusals") {
    Json m = creature("t.otyugh");
    m["actions"][0]["on_hit"] = disease_rider(15);
    auto lib = lib_with({m});
    const auto* def = lib.spells()->find(rules5e::spell_id_of("t.otyugh#save_condition.0"));
    REQUIRE(def);
    CHECK(def->disease);
    CHECK(def->disease_hours == 24);
    CHECK(def->disease_dice == rules5e::DiceExpr{1, 10, 0});
    CHECK(def->condition == rules5e::Condition::Poisoned);
    CHECK(def->condition_untracked);
    CHECK(def->fixed_dc == 15);

    auto bad = [&](auto edit, const char* msg) {
        Json b = creature("t.bad");
        b["actions"][0]["on_hit"] = disease_rider(15);
        edit(b);
        CHECK(load_error(b).find(msg) != std::string::npos);
    };
    bad([](Json& b) { b["$schema"] = "monster@4"; }, "disease needs a monster@5 file");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["save"]["ability"] = "str"; }, "disease needs a Constitution save");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["disease"]["every_hours"] = 12; }, "disease.every_hours must be 24");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["disease"]["hp_max"] = "nope"; }, "disease.hp_max must be dice");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["disease"]["dust"] = true; }, "disease: unknown key 'dust'");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["apply"][0]["condition"] = "blinded"; },
        "disease needs exactly one until_cured poisoned apply");
    bad([](Json& b) { b["actions"][0]["on_hit"][0]["damage"] = Json::array({{{"dice", "1d10"}, {"type", "poison"}}});
                      b["actions"][0]["on_hit"][0]["on_success"] = "none"; },
        "disease takes no damage");
    Json trait = creature("t.trait");
    trait["traits"] = Json::array({{{"id", "bite"}, {"name", "Bite"}, {"description", "x"},
        {"effect", {{"kind", "on_event"}, {"on", "hit"}, {"effects", disease_rider(15)}}}}});
    CHECK(load_error(trait).find("disease is an on_hit save_condition") != std::string::npos);
}

TEST_CASE("bite disease: a hit that fails the save contracts it; a hit that saves does not") {
    auto run = [](int dc) {
        Json m = creature("t.otyugh");
        m["actions"][0]["on_hit"] = disease_rider(dc);
        auto lib = lib_with({m});
        auto ch = arena({{"t.otyugh", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 11);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        to_actor(*c, a);
        Command cmd;
        cmd.request = {a, ActionKind::Attack, f, 0};
        REQUIRE(c->submit(cmd).ok);
        return std::tuple<bool, int, uint32_t, uint32_t>{
            who(*c, f).has(rules5e::Condition::Poisoned),
            int(c->state().diseases.size()),
            c->state().diseases.empty() ? 0u : c->state().diseases[0].next_round,
            uint32_t(c->state().round)};
    };
    const auto [poisoned, n, due, round] = run(30);
    CHECK(poisoned);
    CHECK(n == 1);
    CHECK(due == round + 24u * 600u);
    const auto [clean, none, unused, round2] = run(1);
    CHECK_FALSE(clean);
    CHECK(none == 0);
    CHECK(unused == 0u);
    CHECK(round2 >= 1u);
}

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

namespace {
// Violet fungus Multiattack (SRD 5.1): "The violet fungus makes 1d4 Rotting Touch attacks."
Json violet_fungus(const std::string& id) {
    Json m = creature(id);
    m["traits"].push_back({{"id", "multiattack"}, {"name", "Multiattack"}, {"description", "1d4 attacks"},
                           {"effect", {{"kind", "multiattack"}, {"options", Json::array({{{"id", "touches"}, {"attacks", Json::array({"scimitar"})}, {"count_dice", "1d4"}}})}}}});
    return m;
}
}  // namespace

TEST_CASE("multiattack count_dice (violet fungus 1d4): rolled at start, one attack repeated; refusals") {
    auto lib = lib_with({violet_fungus("t.violet")});
    const auto& decl = lib.find("t.violet")->multiattack_decls.at(0);
    CHECK(decl.count_dice.count == 1);
    CHECK(decl.count_dice.sides == 4);
    bool seen[5] = {};
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        auto ch = arena({{"t.violet", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        to_actor(*c, a);
        rules5e::EncounterState s = c->state();
        rules5e::ActionRequest q{a, ActionKind::Attack, f, 0};
        q.target_within_5ft = true;
        q.multiattack = 1;
        int made = 0;
        for (; made < 8; ++made) {
            auto r = rules5e::step(s, q);
            if (!r.ok) break;
            q.multiattack = 0;
            if (s.budget.attacks_remaining == 0) { ++made; break; }
        }
        REQUIRE(made >= 1);
        REQUIRE(made <= 4);
        CHECK(s.budget.sequence == 0);
        seen[made] = true;
        rules5e::ActionRequest more{a, ActionKind::Attack, f, 0};
        more.target_within_5ft = true;
        CHECK_FALSE(rules5e::step(s, more).ok);   // the action is spent
    }
    CHECK((seen[1] && seen[2] && seen[3] && seen[4]));   // every count 1..4 occurs over 40 seeds

    Json two = violet_fungus("t.bad");
    two["traits"].back()["effect"]["options"][0]["attacks"] = Json::array({"scimitar", "scimitar"});
    CHECK(load_error(two).find("count_dice needs exactly one attack") != std::string::npos);
    Json old = violet_fungus("t.old");
    old["$schema"] = "monster@4";
    CHECK(load_error(old).find("count_dice needs a monster@5 file") != std::string::npos);
    Json bad = violet_fungus("t.bad2");
    bad["traits"].back()["effect"]["options"][0]["count_dice"] = "1d4+1";
    CHECK(load_error(bad).find("count_dice must be dice") != std::string::npos);
}

namespace {
// Blink dog Teleport (SRD 5.1): "Teleport (Recharge 4–6). The dog magically teleports, along with any equipment it is wearing or
// carrying, up to 40 ft. to an unoccupied space it can see. Before or after teleporting, the dog can make one bite attack."
Json blink_dog(const std::string& id) {
    Json m = creature(id);
    m["actions"].push_back({{"id", "teleport"}, {"name", "Teleport"}, {"kind", "teleport"}, {"description", "Teleport (Recharge 4-6)."},
                            {"feet", 40}, {"usage", {{"kind", "recharge"}, {"min", 4}}}, {"with_attack", "scimitar"}});
    return m;
}
}  // namespace

TEST_CASE("teleport action (blink dog): Recharge 4-6 self-teleport, then one bite without another action; refusals") {
    auto lib = lib_with({blink_dog("t.blink")});
    const Archetype* arch = lib.find("t.blink");
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].teleport);
    CHECK(arch->save_actions[0].usage_kind == 1);
    const uint32_t sid = rules5e::spell_id_of("t.blink#teleport");
    REQUIRE(arch->base.multiattacks.size() == 1);
    CHECK(arch->base.multiattacks[0].after_action == sid);
    auto ch = arena({{"t.blink", Vec2i{9, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    CHECK_FALSE(c->multiattack_option(a, "teleport.with_attack"));   // not a Multiattack option
    to_actor(*c, a);
    const auto* def = c->spell_def(sid);
    REQUIRE(def);
    CHECK(def->kind == rules5e::SpellEffectKind::Teleport);
    CHECK(def->teleport.max_feet == 40);
    CHECK_FALSE(def->is_spell);
    CHECK(def->magical);
    {   // the follow-up is armed only by the teleport: starting it as a multiattack is refused
        rules5e::EncounterState s = c->state();
        rules5e::ActionRequest q{a, ActionKind::Attack, f, 0};
        q.target_within_5ft = true;
        q.multiattack = 1;
        CHECK_FALSE(rules5e::step(s, q).ok);
    }
    Command cast;
    cast.request = {a, ActionKind::CastSpell, a};
    cast.request.spell_id = sid;
    cast.request.spell_level = 0;
    CHECK_FALSE(c->submit(cast).ok);   // needs a destination
    cast.request.has_teleport_dest = true; cast.request.teleport_x = 4; cast.request.teleport_y = 5;   // 25 ft, beside the fighter
    auto r = c->submit(cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->pos(a) == Vec2i{4, 5});
    CHECK(rules5e::find_action_use(c->state(), a, sid)->expended);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.sequence == 1);
    {   // only the named attack; a second one is refused
        rules5e::EncounterState s = c->state();
        rules5e::ActionRequest bow{a, ActionKind::Attack, f, 1};
        CHECK_FALSE(rules5e::step(s, bow).ok);
        rules5e::ActionRequest q{a, ActionKind::Attack, f, 0};
        q.target_within_5ft = true;
        auto rr = rules5e::step(s, q);
        REQUIRE_MESSAGE(rr.ok, std::string(rr.error));
        CHECK(s.budget.sequence == 0);
        CHECK(s.budget.attacks_remaining == 0);
        CHECK_FALSE(rules5e::step(s, q).ok);
    }
    // loader / validator refusals
    Json far = blink_dog("t.far");
    far["actions"].back()["feet"] = 42;
    CHECK(load_error(far).find("feet must be 5..500") != std::string::npos);
    Json bad = blink_dog("t.badw");
    bad["actions"].back()["with_attack"] = "nope";
    CHECK(load_error(bad).find("with_attack names unknown attack action") != std::string::npos);
}

TEST_CASE("multiattack count_dice: the 1d4 count is the FIRST PRNG draw of the multiattack start (pinned golden)") {
    // Lich: no event, but the count roll draws from the PRNG at a fixed point in resolution order. Fixed point: after the
    // action is spent and before the first attack roll, i.e. the first draw of the starting step. Pinned two ways: (a) the
    // count equals 1d4 rolled from a copy of the pre-step RNG, and (b) a golden over seeds 1..12 (changes if the draw moves).
    auto lib = lib_with({violet_fungus("t.violet")});
    std::vector<int> got;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        for (int replay = 0; replay < 2; ++replay) {
            auto ch = arena({{"t.violet", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
            to_actor(*c, a);
            rules5e::EncounterState s = c->state();
            rng::Rng probe = s.rng;
            const int expect = rules5e::roll_dice(probe, rules5e::DiceExpr{1, 4, 0});
            rules5e::ActionRequest q{a, ActionKind::Attack, f, 0};
            q.target_within_5ft = true;
            q.multiattack = 1;
            REQUIRE(rules5e::step(s, q).ok);
            const int count = int(s.budget.attacks_remaining) + 1;
            CHECK(count == expect);
            if (replay == 0) got.push_back(count); else CHECK(got.back() == count);   // replay identical
        }
    }
    const std::vector<int> golden = {3, 2, 3, 1, 4, 2, 4, 4, 3, 2, 2, 2};
    CHECK(got == golden);
}

// ---- ghast Stench (SRD 5.1): "Any creature that starts its turn within 5 feet of the ghast must succeed on a DC 10
// Constitution saving throw or be poisoned until the start of its next turn. On a successful saving throw, the creature is
// immune to the ghast's Stench for 24 hours." Lich: creature-sourced aura from the start of combat; alive = works. ----
namespace {
Json stench_effect(int dc) {
    return Json{{"kind", "aura"}, {"radius_ft", 5}, {"on", "start_turn"}, {"save", {{"ability", "con"}, {"dc", dc}}},
                {"condition", "poisoned"}, {"duration", "until_start_of_next_turn"}, {"success_immunity_hours", 24}};
}
Json ghast(int dc) {
    Json g = creature("t.ghast");
    g["traits"].push_back({{"id", "stench"}, {"name", "Stench"}, {"description", "SRD 5.1 Stench"}, {"effect", stench_effect(dc)}});
    return g;
}
int saves_by(const Combat& c, CombatantId who) {
    int n = 0;
    for (const auto& e : c.events())
        n += e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::SavingThrowRolled && e.rules.actor == who;
    return n;
}
void end_turns(Combat& c, int n) {
    for (int i = 0; i < n; ++i) {
        Command e; e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn}; REQUIRE(c.submit(e).ok);
    }
}
}  // namespace

TEST_CASE("ghast Stench: aura from combat start; start-of-turn save inside the radius only; replay hashes match") {
    auto lib = lib_with({ghast(30), creature("t.plain")});
    const auto* a = lib.find("t.ghast");
    REQUIRE(a->aura_spell_ids.size() == 1);
    const auto* def = lib.spells()->find(a->aura_spell_ids[0]);
    REQUIRE(def);
    CHECK(def->kind == rules5e::SpellEffectKind::Zone);
    CHECK(def->fixed_dc == 30);
    CHECK(def->zone.size_ft == 5);
    CHECK_FALSE(def->zone.needs_conscious_owner);
    auto ch = arena({{"t.ghast", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}, {"t.plain", Vec2i{12, 12}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id, p = ch.spawns[2].stable_id;
    REQUIRE(c->state().zones.size() == 1);   // exists from the start of combat
    CHECK(c->state().zones[0].owner == g);
    CHECK(c->state().zones[0].feature);
    to_actor(*c, f);
    CHECK(saves_by(*c, f) == 1);              // adjacent: within 5 feet
    CHECK(who(*c, f).has(rules5e::Condition::Poisoned));
    end_turns(*c, 6);
    CHECK(saves_by(*c, p) == 0);              // far away: no save
    CHECK(saves_by(*c, g) == 0);              // not its own aura
    CHECK(saves_by(*c, f) >= 2);              // fails every time (DC 30): saves at each of its turns
    CHECK(c->state().aura_immunities.empty());
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
}

TEST_CASE("ghast Stench: a success gives 24 h immunity (0xDE folds); replay hashes match") {
    auto lib = lib_with({ghast(1)});
    auto ch = arena({{"t.ghast", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const uint64_t h0 = c->hash();
    to_actor(*c, f);
    CHECK(saves_by(*c, f) == 1);
    CHECK_FALSE(who(*c, f).has(rules5e::Condition::Poisoned));
    REQUIRE(c->state().aura_immunities.size() == 1);
    const auto im = c->state().aura_immunities[0];
    CHECK(im.creature == f);
    CHECK(im.source == g);
    CHECK(im.until_round == uint32_t(c->state().round) + 14400u);   // 24 h = 14400 rounds
    end_turns(*c, 8);
    CHECK(saves_by(*c, f) == 1);              // immune: no more saves
    CHECK(h0 != c->hash());
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->state().aura_immunities == c->state().aura_immunities);
}

TEST_CASE("ghast Stench: loader refusals; a combat without an aura trait has no zone") {
    Json g = ghast(10);
    g["traits"].back()["effect"]["radius_ft"] = 7;
    CHECK(load_error(g).find("radius_ft must be a multiple of 5") != std::string::npos);
    g = ghast(10);
    g["traits"].back()["effect"]["duration"] = "1_minute";
    CHECK(load_error(g).find("duration must be until_start_of_next_turn") != std::string::npos);
    g = ghast(10);
    g["$schema"] = "monster@4";
    CHECK(load_error(g).find("aura") != std::string::npos);
    g = ghast(10);
    g["traits"].back()["effect"]["save"]["dc"] = 0;
    CHECK(load_error(g).find("save.dc must be 1..30") != std::string::npos);
    auto lib = lib_with({creature("t.plain")});
    auto ch = arena({{"t.plain", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    CHECK(c->state().zones.empty());
}

// ---- Batch 3: shadow Strength Drain (SRD 5.1 Shadow: "Hit: 9 (2d6 + 2) necrotic damage, and the target's Strength score is
// reduced by 1d4. The target dies if this reduces its Strength to 0. Otherwise, the reduction lasts until the target finishes a
// short or long rest.") ----
namespace {
Json shadow() {
    Json m = creature("t.shadow");
    m["actions"][0]["ability_drain"] = {{"ability", "str"}, {"dice", "1d4"}};
    return m;
}
ContentLibrary lib_str(int fighter_str) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    tank["abilities"]["str"] = fighter_str;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(lib.add(shadow(), "monster"));
    return lib;
}
// The shadow attacks the fighter until one hit lands; returns false if none did.
bool shadow_hits(Combat& c, CombatantId a, CombatantId f) {
    for (int i = 0; i < 8; ++i) {
        to_actor(c, a);
        const int before = who(c, f).hp;
        Command cmd;
        cmd.request = {a, ActionKind::Attack, f, 0};
        REQUIRE(c.submit(cmd).ok);
        c.settle(nullptr);
        if (who(c, f).hp < before || who(c, f).dead) return true;   // stop on the hit (a kill ends the combat)
        Command e; e.request = {a, ActionKind::EndTurn}; REQUIRE(c.submit(e).ok);
    }
    return false;
}
}  // namespace

TEST_CASE("ability_drain loader key (shadow Strength Drain): {ability: str, dice}; refusals") {
    auto lib = lib_str(16);
    const auto& d = lib.find("t.shadow")->base.attacks.at(0).strength_drain;
    CHECK(d.count == 1);
    CHECK(d.sides == 4);
    Json bad = creature("t.bad");
    bad["actions"][0]["ability_drain"] = {{"ability", "dex"}, {"dice", "1d4"}};
    CHECK(load_error(bad).find("ability_drain must be") != std::string::npos);
    bad["actions"][0]["ability_drain"] = {{"ability", "str"}};
    CHECK(load_error(bad).find("ability_drain must be") != std::string::npos);
    bad["actions"][0]["ability_drain"] = {{"ability", "str"}, {"dice", "x"}};
    CHECK(load_error(bad).find("ability_drain: bad dice") != std::string::npos);
    bad["$schema"] = "monster@4";
    bad["actions"][0]["ability_drain"] = {{"ability", "str"}, {"dice", "1d4"}};
    CHECK(load_error(bad).find("ability_drain needs a monster@5 file") != std::string::npos);
}

TEST_CASE("shadow Strength Drain: a hit reduces the effective score by 1d4; 0xDF folds; replay matches; a rest restores it") {
    auto lib = lib_str(16);
    auto ch = arena({{"t.shadow", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const uint64_t h0 = c->hash();
    CHECK(who(*c, f).ability_reduction[0] == 0);
    REQUIRE(shadow_hits(*c, a, f));
    const auto& t = who(*c, f);
    const int red = t.ability_reduction[0];
    CHECK(red >= 1);
    CHECK(red <= 4);
    CHECK(int(t.abilities.score[0]) + red == 16);   // abilities holds the effective score; true score = effective + reduction
    CHECK_FALSE(t.dead);
    CHECK(c->hash() != h0);
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
    CHECK(who(*rp, f).ability_reduction == t.ability_reduction);   // the reduction persists across a replay
    for (int kind = 0; kind < 2; ++kind) {   // short rest, long rest
        rules5e::Combatant copy = t;
        rng::Rng r(1, 1);
        const auto res = kind == 0 ? rules5e::short_rest(copy, r, 0) : rules5e::long_rest(copy);
        CHECK(res.ok);
        CHECK(copy.abilities.score[0] == 16);
        CHECK(copy.ability_reduction[0] == 0);
    }
}

TEST_CASE("shadow Strength Drain: reducing Strength to 0 kills through the death path") {
    auto lib = lib_str(1);   // any 1d4 >= 1 reduces Strength 1 to 0
    auto ch = arena({{"t.shadow", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    REQUIRE(shadow_hits(*c, a, f));
    const auto& t = who(*c, f);
    CHECK(t.dead);
    CHECK(t.hp == 0);
    CHECK(t.abilities.score[0] == 0);
    CHECK(t.ability_reduction[0] == 1);   // only the drop actually applied
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
}

TEST_CASE("Strength Drain: a rest with nothing reduced changes no score") {
    auto lib = lib_with({creature("t.plain")});
    auto ch = arena({{"t.plain", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry r1;
    auto c1 = Combat::create(r1, ch, lib, 7);
    REQUIRE(c1);
    // a restore with nothing reduced is a no-op
    rules5e::Combatant k = who(*c1, ch.spawns[1].stable_id);
    const auto before = k.abilities;
    rules5e::restore_ability_reduction(k);
    CHECK(k.abilities.score == before.score);
}

// ---- Lich rulings on Strength Drain (attack recompute, forms, revival) ----
namespace {
const rules5e::Attack* find_attack(const rules5e::Combatant& k, int8_t code, std::size_t* idx) {
    for (std::size_t i = 0; i < k.attacks.size(); ++i)
        if (k.attacks[i].ability_used == code && k.attacks[i].damage_ability_used < 0) { *idx = i; return &k.attacks[i]; }
    return nullptr;
}
}  // namespace

TEST_CASE("Strength Drain ruling 1: a drained Str attack's to-hit and damage both drop by the modifier change") {
    auto lib = lib_str(16);
    auto ch = arena({{"t.shadow", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId f = ch.spawns[1].stable_id;
    rules5e::EncounterState s = c->state();
    rules5e::Combatant* k = const_cast<rules5e::Combatant*>(rules5e::find(s, f));
    std::size_t i = 0;
    const rules5e::Attack* lsw = find_attack(*k, int8_t(rules5e::Ability::Str), &i);
    REQUIRE(lsw);
    const auto undrained = rules5e::effective_attack(s, f, i);
    k->abilities.score[0] = 12;   // 16 -> 12: modifier +3 -> +1
    k->ability_reduction[0] = 4;
    const auto drained = rules5e::effective_attack(s, f, i);
    CHECK(int(drained.to_hit) == int(undrained.to_hit) - 2);
    CHECK(int(drained.damage.bonus) == int(undrained.damage.bonus) - 2);
    // a Str save uses the effective score
    CHECK(rules5e::modifier(k->abilities.score[0]) == 1);
}

TEST_CASE("Strength Drain ruling 1: a finesse attack switches to Dex once drained Str falls below it") {
    Json m = creature("t.fencer");
    m["abilities"]["str"] = 18;   // +4
    m["abilities"]["dex"] = 14;   // +2
    m["actions"][0]["properties"] = Json::array({"finesse"});
    auto lib = lib_with({m});
    auto ch = arena({{"t.fencer", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id;
    rules5e::EncounterState s = c->state();
    rules5e::Combatant* k = const_cast<rules5e::Combatant*>(rules5e::find(s, a));
    REQUIRE(k->attacks.at(0).ability_used == rules5e::kAttackAbilityFinesse);
    const auto base = rules5e::effective_attack(s, a, 0);
    k->abilities.score[0] = 16; k->ability_reduction[0] = 2;   // +3: still above Dex
    CHECK(int(rules5e::effective_attack(s, a, 0).to_hit) == int(base.to_hit) - 1);
    k->abilities.score[0] = 10; k->ability_reduction[0] = 8;   // +0: Dex (+2) is now better
    const auto fin = rules5e::effective_attack(s, a, 0);
    CHECK(int(fin.to_hit) == int(base.to_hit) - 2);
    CHECK(int(fin.damage.bonus) == int(base.damage.bonus) - 2);
}

TEST_CASE("Strength Drain ruling 2: a drain on a polymorph / wild-shape form is lost on revert; the true form keeps its own") {
    for (auto kind : {rules5e::FormKind::Polymorph, rules5e::FormKind::WildShape}) {
        rules5e::Combatant k{};
        k.abilities.score = {14, 12, 12, 10, 10, 10};
        k.ability_reduction[0] = 2;   // the true form was drained first: true Str 16, effective 14
        k.hp = k.hp_max = 20;
        rules5e::FormStats form{};
        form.abilities.score = {18, 10, 12, 2, 10, 4};
        form.hp_max = 30;
        REQUIRE(rules5e::apply_form(k, kind, form));
        CHECK(k.ability_reduction[0] == 0);   // the form starts unreduced
        k.abilities.score[0] = 15; k.ability_reduction[0] = 3;   // the form is drained
        REQUIRE(rules5e::revert_form(k));
        CHECK(k.abilities.score[0] == 14);    // the form's drain is gone with the form
        CHECK(k.ability_reduction[0] == 2);   // the true form's own reduction is back
    }
}

TEST_CASE("Strength Drain ruling 4 (pinned current behaviour): revival does not clear the reduction (DEFERRED: revived at Str 0)") {
    auto lib = lib_str(1);
    auto ch = arena({{"t.shadow", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    REQUIRE(shadow_hits(*c, a, f));
    rules5e::Combatant k = who(*c, f);
    REQUIRE(k.dead);
    k.dead = false;   // a revival (no rules-level raise exists yet): clear death and restore hit points
    k.hp = 1;
    CHECK(k.abilities.score[0] == 0);       // still Strength 0: the reduction is not cleared by coming back
    CHECK(k.ability_reduction[0] == 1);
}

TEST_CASE("monster@5 action ability (CONTENT_CONVENTIONS \"Attack ability\"): explicit wins; none; Str/Dex tie warns; refusals") {
    auto attack0 = [](const ContentLibrary& lib, const char* id) { return lib.find(id)->base.attacks.at(0); };
    Json dexy = creature("t.dexy");   // goblin Str 8 / Dex 14, PB 2: to_hit 4 is Dex
    dexy["actions"][0]["to_hit"] = 4;
    Json expl = dexy; expl["id"] = "t.expl"; expl["actions"][0]["ability"] = "str";
    Json none = dexy; none["id"] = "t.none"; none["actions"][0]["ability"] = "none";
    Json tie = dexy; tie["id"] = "t.tie"; tie["abilities"]["str"] = 14;
    {
        auto lib = lib_with({dexy, expl, none});
        CHECK(attack0(lib, "t.dexy").ability_used == int8_t(rules5e::Ability::Dex));
        CHECK(attack0(lib, "t.expl").ability_used == int8_t(rules5e::Ability::Str));   // explicit wins over inference
        CHECK(attack0(lib, "t.none").ability_used == rules5e::kAttackAbilityNone);
        for (const auto& w : lib.warnings()) CHECK(w.find("t.") != 0);   // no tie among these
        // none: a drain changes nothing
        auto ch = arena({{"t.none", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id;
        rules5e::EncounterState s = c->state();
        auto* k = const_cast<rules5e::Combatant*>(rules5e::find(s, a));
        const auto base = rules5e::effective_attack(s, a, 0);
        k->abilities.score[0] = 2; k->ability_reduction[0] = 6;
        k->abilities.score[1] = 4; k->ability_reduction[1] = 10;
        CHECK(rules5e::effective_attack(s, a, 0).to_hit == base.to_hit);
    }
    {
        auto lib = lib_with({tie});
        CHECK(attack0(lib, "t.tie").ability_used == int8_t(rules5e::Ability::Str));
        const std::string a0 = "t.tie: action " + tie["actions"][0].value("id", "?") + ":";
        int warned = 0;
        for (const auto& w : lib.warnings()) warned += w.rfind(a0, 0) == 0 && w.find("matches both Str and Dex") != std::string::npos;
        CHECK(warned == 1);   // one warning per tied action (the goblin's shortbow ties too)
        Json tie2 = tie; tie2["id"] = "t.tie2"; tie2["actions"][0]["ability"] = "dex";   // explicit silences it
        auto lib2 = lib_with({tie2});
        const std::string b0 = "t.tie2: action " + tie2["actions"][0].value("id", "?") + ":";
        for (const auto& w : lib2.warnings()) CHECK(w.rfind(b0, 0) != 0);
    }
    Json bad = dexy; bad["actions"][0]["ability"] = "con";
    CHECK(load_error(bad).find("ability must be str, dex or none") != std::string::npos);
    bad["actions"][0]["ability"] = "str"; bad["$schema"] = "monster@4";
    CHECK(load_error(bad).find("ability needs a monster@5 file") != std::string::npos);
}

TEST_CASE("Attack ability fallback: the hobgoblin longbow (ranged, not thrown) resolves to Dex with no tie warning") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const auto* h = lib.find("srd51.monster.hobgoblin");
    REQUIRE(h);
    bool found = false;
    for (const auto& at : h->base.attacks)
        if (at.id == "longbow") { found = true; CHECK(at.ability_used == int8_t(rules5e::Ability::Dex)); }
    CHECK(found);
    for (const auto& w : lib.warnings()) CHECK(w.find("srd51.monster.hobgoblin: action longbow:") == std::string::npos);
    // a thrown ranged attack is not fallback 1: the bugbear's thrown javelin still infers from to_hit (a Str/Dex tie)
    const auto* b = lib.find("srd51.monster.bugbear");
    REQUIRE(b);
    for (const auto& at : b->base.attacks)
        if (at.id == "javelin_thrown") CHECK(at.ability_used == int8_t(rules5e::Ability::Str));
}
