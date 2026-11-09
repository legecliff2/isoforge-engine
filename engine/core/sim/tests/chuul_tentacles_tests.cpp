// B3b: Chuul Tentacles is a Multiattack free rider. It does not spend an action, it may follow a pincer
// that established the grapple this same Multiattack, it is not a standalone action, and it is once per
// Multiattack against one creature grappled by the chuul. Poisoned for 1 minute, paralyzed while that
// poison lasts, repeat save at the end of each of the target's turns (SRD 5.1).
#include <doctest/doctest.h>

#include <fstream>
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
using rules5e::EventKind;

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json chuul_doc(const std::string& id, int to_hit, bool grapple) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    Json pincer = m["actions"][0];
    pincer["id"] = "pincer";
    pincer["name"] = "Pincer";
    pincer["to_hit"] = to_hit;
    pincer["reach"] = 10;
    pincer["damage"] = Json::array({{{"dice", "1d4+1"}, {"type", "bludgeoning"}}});
    if (grapple)
        pincer["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 14}, {"max_grapples", 2}, {"max_size", "large"}}});
    else
        pincer.erase("on_hit");
    Json tent = {{"kind", "save_effect"}, {"id", "tentacles"}, {"name", "Tentacles"}, {"rider_only", true},
                 {"save", {{"ability", "con"}, {"dc", 30}}}, {"range_ft", 30},
                 {"target_requires_any", Json::array({"grappled_by_self"})},
                 {"apply", Json::array({{{"condition", "poisoned"}, {"while", "paralyzed"},
                                         {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                         {"repeat_save", "end_of_turn"}}})}};
    m["actions"] = Json::array({pincer, tent});
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
        {"description", "two pincers, then tentacles if grappling"},
        {"effect", {{"kind", "multiattack"}, {"options", Json::array({
            {{"id", "pincers"}, {"attacks", Json::array({"pincer", "pincer"})}, {"free_rider", "tentacles"}}})}}}}});
    return m;
}

ContentLibrary lib_with(const std::vector<Json>& monsters) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) {
        const auto e = lib.add(d, "monster");
        REQUIRE_MESSAGE(!e, e.value_or(""));
    }
    return lib;
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

void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}

bool listed(const Combat& c, uint32_t sid, CombatantId who) {
    for (const auto& q : c.legal_actions())
        if (q.kind == ActionKind::CastSpell && q.spell_id == sid && q.target == who) return true;
    return false;
}

rules5e::ActionRequest pincer(CombatantId a, CombatantId t, uint8_t multi) {
    rules5e::ActionRequest q{a, ActionKind::Attack, t, 0};
    q.multiattack = multi;
    q.target_within_5ft = true;
    return q;
}

rules5e::ActionRequest tentacles(CombatantId a, CombatantId t, uint32_t sid) {
    rules5e::ActionRequest q{a, ActionKind::CastSpell, t};
    q.spell_id = sid;
    q.targets = {t};
    return q;
}
}

TEST_CASE("chuul content: two pincers, max_size large, tentacles is the free rider") {
    auto lib = lib_with({});
    const auto e = lib.add(load_json("content/packs/srd51.core/monsters/chuul.json"), "monster");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    const auto* arch = lib.find("srd51.monster.chuul");
    REQUIRE(arch);
    REQUIRE(arch->multiattack_decls.size() == 1);
    CHECK(arch->multiattack_decls[0].attacks == std::vector<uint8_t>{0, 0});
    CHECK(arch->multiattack_decls[0].free_rider == "tentacles");
    REQUIRE(arch->base.multiattacks.size() == 1);
    const uint32_t sid = rules5e::spell_id_of("srd51.monster.chuul#tentacles");
    CHECK(arch->base.multiattacks[0].free_rider == sid);
    REQUIRE(arch->grapple_on_hit.count(0));
    CHECK(arch->grapple_on_hit.at(0).escape_dc == 14);
    CHECK(arch->grapple_on_hit.at(0).max_grapples == 2);
    CHECK(arch->grapple_on_hit.at(0).max_size == int(rules5e::Size::Large));
    const auto* d = lib.spells()->find(sid);
    REQUIRE(d);
    CHECK(d->rider_only);
    CHECK(d->target_requires_any == rules5e::kTargetGateGrappledBySelf);
    CHECK(d->targets == 1);
    CHECK(d->has_while);
    CHECK(d->while_condition == Condition::Paralyzed);
    CHECK(d->condition == Condition::Poisoned);
    CHECK(d->repeat_save == rules5e::RepeatSave::EndOfTurn);
    CHECK(d->duration_rounds == 10);
    CHECK(d->fixed_dc == 13);
    bool rider = false;
    for (const auto& sa : arch->save_actions) rider = rider || (sa.id == "tentacles" && sa.rider_only);
    CHECK(rider);
}

TEST_CASE("chuul tentacles: free same-turn rider after the pincers, not a standalone action") {
    auto lib = lib_with({chuul_doc("t.chuul", 40, true)});
    auto ch = arena({{"t.chuul", {6, 5}}, {kFighter, {5, 5}}, {kFighter, {7, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, held = ch.spawns[1].stable_id, other = ch.spawns[2].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.chuul#tentacles");
    advance_to(*c, a);
    CHECK(c->state().budget.action);
    CHECK_FALSE(listed(*c, sid, held));
    Command early;
    early.request = tentacles(a, held, sid);
    auto refused = c->submit(early);
    CHECK_FALSE(refused.ok);
    CHECK(std::string(refused.error).find("grappled by the user") != std::string::npos);
    CHECK(c->state().budget.action);

    Command first;
    first.request = pincer(a, held, 1);
    REQUIRE(c->submit(first).ok);
    c->settle(nullptr);
    CHECK(rules5e::find(c->state(), held)->has(Condition::Grappled));
    CHECK(c->state().budget.sequence != 0);
    CHECK_FALSE(listed(*c, sid, held));   // still inside the pincers; the rider is after them
    Command second;
    second.request = pincer(a, held, 1);
    REQUIRE(c->submit(second).ok);
    c->settle(nullptr);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.free_rider == sid);
    CHECK(listed(*c, sid, held));
    CHECK_FALSE(listed(*c, sid, other));
    Command wrong;
    wrong.request = tentacles(a, other, sid);
    auto bad_target = c->submit(wrong);
    CHECK_FALSE(bad_target.ok);
    CHECK(std::string(bad_target.error).find("grappled by the user") != std::string::npos);
    CHECK(c->state().budget.free_rider == sid);   // a refused target does not spend the one use

    const Command ai = next_command(*c, held);
    REQUIRE(ai.request.kind == ActionKind::CastSpell);
    CHECK(ai.request.spell_id == sid);
    CHECK(ai.request.target == held);
    REQUIRE(c->submit(ai).ok);
    c->settle(nullptr);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.free_rider == 0);
    const auto* t = rules5e::find(c->state(), held);
    CHECK(t->has(Condition::Poisoned));
    CHECK(t->has(Condition::Paralyzed));
    CHECK_FALSE(rules5e::find(c->state(), other)->has(Condition::Poisoned));
    bool both = false;
    for (const auto& e : c->state().effects)
        if (e.target == held && e.condition == Condition::Poisoned && e.has_also && e.also == Condition::Paralyzed &&
            e.repeat_save == rules5e::RepeatSave::EndOfTurn && e.timed.rounds_left == 10)
            both = true;
    CHECK(both);
    Command again;
    again.request = tentacles(a, held, sid);
    auto second_use = c->submit(again);
    CHECK_FALSE(second_use.ok);
    CHECK(std::string(second_use.error).find("only a multiattack rider") != std::string::npos);
    CHECK_FALSE(listed(*c, sid, held));
}

TEST_CASE("chuul tentacles: a grapple from before this Multiattack also qualifies, and a non-grappling multiattack does not") {
    auto lib = lib_with({chuul_doc("t.chuul", 40, false)});
    auto ch = arena({{"t.chuul", {6, 5}}, {kFighter, {5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.chuul#tentacles");
    advance_to(*c, a);
    std::vector<rules5e::Event> ev;
    REQUIRE(rules5e::grapple(CombatTestAccess::state(*c), a, f, 14, ev));
    Command open;
    open.request = tentacles(a, f, sid);
    auto alone = c->submit(open);
    CHECK_FALSE(alone.ok);
    CHECK(std::string(alone.error).find("only a multiattack rider") != std::string::npos);
    CHECK(c->state().budget.action);
    Command first;
    first.request = pincer(a, f, 1);
    REQUIRE(c->submit(first).ok);
    Command second;
    second.request = pincer(a, f, 1);
    REQUIRE(c->submit(second).ok);
    CHECK(c->state().budget.free_rider == sid);
    CHECK(listed(*c, sid, f));
    Command cast;
    cast.request = tentacles(a, f, sid);
    REQUIRE(c->submit(cast).ok);
    CHECK(rules5e::find(c->state(), f)->has(Condition::Poisoned));
    CHECK(rules5e::find(c->state(), f)->has(Condition::Paralyzed));
}

TEST_CASE("chuul tentacles: no grapple means the rider is armed and still refused") {
    auto lib = lib_with({chuul_doc("t.chuul", 40, false)});
    auto ch = arena({{"t.chuul", {6, 5}}, {kFighter, {5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.chuul#tentacles");
    advance_to(*c, a);
    Command first;
    first.request = pincer(a, f, 1);
    REQUIRE(c->submit(first).ok);
    Command second;
    second.request = pincer(a, f, 1);
    REQUIRE(c->submit(second).ok);
    CHECK(c->state().budget.free_rider == sid);
    CHECK_FALSE(listed(*c, sid, f));
    CHECK(next_command(*c, f).request.kind != ActionKind::CastSpell);
    Command cast;
    cast.request = tentacles(a, f, sid);
    auto r = c->submit(cast);
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.error).find("grappled by the user") != std::string::npos);
    CHECK(c->state().budget.free_rider == sid);
}

TEST_CASE("chuul tentacles: immunity to poisoned applies neither condition; a repeat save ends both") {
    auto lib = lib_with({chuul_doc("t.chuul", 40, true)});
    auto ch = arena({{"t.chuul", {6, 5}}, {kFighter, {5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.chuul#tentacles");
    advance_to(*c, a);
    auto& fighter = *const_cast<rules5e::Combatant*>(rules5e::find(c->state(), f));
    fighter.condition_immunities |= rules5e::bit(Condition::Poisoned);
    Command first;
    first.request = pincer(a, f, 1);
    REQUIRE(c->submit(first).ok);
    Command second;
    second.request = pincer(a, f, 1);
    REQUIRE(c->submit(second).ok);
    REQUIRE(rules5e::find(c->state(), f)->has(Condition::Grappled));
    Command cast;
    cast.request = tentacles(a, f, sid);
    REQUIRE(c->submit(cast).ok);
    CHECK_FALSE(rules5e::find(c->state(), f)->has(Condition::Poisoned));
    CHECK_FALSE(rules5e::find(c->state(), f)->has(Condition::Paralyzed));

    auto ch2 = arena({{"t.chuul", {6, 5}}, {kFighter, {5, 5}}});
    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, ch2, lib, 11);
    REQUIRE(c2);
    const CombatantId a2 = ch2.spawns[0].stable_id, f2 = ch2.spawns[1].stable_id;
    advance_to(*c2, a2);
    Command p1;
    p1.request = pincer(a2, f2, 1);
    REQUIRE(c2->submit(p1).ok);
    Command p2;
    p2.request = pincer(a2, f2, 1);
    REQUIRE(c2->submit(p2).ok);
    Command ok;
    ok.request = tentacles(a2, f2, sid);
    REQUIRE(c2->submit(ok).ok);
    REQUIRE(rules5e::find(c2->state(), f2)->has(Condition::Paralyzed));
    for (auto& e : CombatTestAccess::state(*c2).effects)
        if (e.target == f2 && e.has_also) e.dc = 1;
    Command end;
    end.request = {a2, ActionKind::EndTurn};
    REQUIRE(c2->submit(end).ok);
    REQUIRE(rules5e::current_actor(c2->state()) == f2);
    Command theirs;
    theirs.request = {f2, ActionKind::EndTurn};
    REQUIRE(c2->submit(theirs).ok);
    CHECK_FALSE(rules5e::find(c2->state(), f2)->has(Condition::Poisoned));
    CHECK_FALSE(rules5e::find(c2->state(), f2)->has(Condition::Paralyzed));
}

TEST_CASE("chuul tentacles loader: free_rider, rider_only, and while are refused when dishonest") {
    auto lib = lib_with({});
    auto bad_rider = [&](Json doc, const char* id, const char* needle) {
        doc["id"] = id;
        const auto e = lib.add(doc, "monster");
        REQUIRE(e);
        CHECK(e->find(needle) != std::string::npos);
    };
    Json m = chuul_doc("t.base", 6, true);
    Json named = m;
    named["traits"][0]["effect"]["options"][0]["free_rider"] = "pincer";
    bad_rider(named, "t.notsave", "must name a rider_only save_effect");
    Json loose = m;
    loose["actions"][1].erase("rider_only");
    bad_rider(loose, "t.loose", "must name a rider_only save_effect");
    Json orphan = m;
    orphan["traits"][0]["effect"]["options"][0].erase("free_rider");
    bad_rider(orphan, "t.orphan", "must be a multiattack free_rider");
    Json dice = m;
    dice["traits"][0]["effect"]["options"][0]["attacks"] = Json::array({"pincer"});
    dice["traits"][0]["effect"]["options"][0]["count_dice"] = "1d4";
    bad_rider(dice, "t.dice", "can't be combined with count_dice");
    Json same = m;
    same["actions"][1]["apply"][0]["while"] = "poisoned";
    bad_rider(same, "t.same", "while must be a different condition");
    Json old = m;
    old["$schema"] = "monster@4";
    bad_rider(old, "t.old", "needs a monster@5 file");
}
