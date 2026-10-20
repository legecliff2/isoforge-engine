// Giant spider follow-up (Director): Web range 30/60 (long range at disadvantage, like ranged weapon attacks), the bite's 0-HP
// clause (stable, poisoned 600 rounds = 1 hour, paralyzed while that lasts), and the web as a destructible object (AC 10,
// 5 HP, fire vulnerable, immune to bludgeoning / poison / psychic; ActionRequest::special 6; Combat::hash 0xBE).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";
const uint32_t kWeb = rules5e::spell_id_of("t.spider#web");

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json web_object(Json vuln = Json::array({"fire"}), Json immune = Json::array({"bludgeoning", "poison", "psychic"}), int hp = 5) {
    return {{"ac", 10}, {"hp", hp}, {"vulnerable", vuln}, {"immune", immune}};
}
Json spider(int to_hit = 30, int escape_dc = 30, Json object = web_object(), Json bite = nullptr) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = "t.spider";
    m["hit_points"]["average"] = 400;
    m["traits"] = Json::array();
    Json apply = {{"condition", "restrained"}, {"escape", {{"ability", "str"}, {"dc", escape_dc}}}};
    if (!object.is_null()) apply["object"] = object;
    m["actions"].push_back({{"id", "web"}, {"name", "Web"}, {"kind", "attack_effect"}, {"attack", "ranged"}, {"to_hit", to_hit},
                            {"range_ft", 30}, {"long_range_ft", 60}, {"apply", Json::array({apply})},
                            {"usage", {{"kind", "recharge"}, {"min", 5}}}});
    if (!bite.is_null()) m["actions"][0] = bite;
    return m;
}
// bite: 1 piercing, then Con DC 30 (always fails) or `poison` poison damage, with the 0-HP clause unless `clause` is false
Json bite(const char* weapon = "1d1", const char* poison = "20d1", bool clause = true) {
    Json b = load_json("content/packs/srd51.core/monsters/goblin.json")["actions"][0];
    b["id"] = "bite";
    b["to_hit"] = 30;
    b["damage"] = Json::array({{{"dice", weapon}, {"type", "piercing"}}});
    Json r = {{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", 30}}}, {"on_success", "half"},
              {"damage", Json::array({{{"dice", poison}, {"type", "poison"}}})}};
    if (clause) r["zero_hp"] = {{"condition", "poisoned"}, {"while", "paralyzed"}, {"duration", {{"kind", "rounds"}, {"n", 600}}}};
    b["on_hit"] = Json::array({r});
    return b;
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
ContentLibrary lib_with(const std::vector<Json>& monsters, int fighter_hp = 2000) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = fighter_hp;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
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
SubmitResult submit(Combat& c, const rules5e::ActionRequest& q) {
    Command x;
    x.request = q;
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
}
rules5e::ActionRequest web_at(CombatantId s, CombatantId t) {
    rules5e::ActionRequest q{s, ActionKind::CastSpell, t};
    q.spell_id = kWeb;
    return q;
}
void same_on_replay(const Combat& c, const world::Chunk& ch, const ContentLibrary& lib, uint64_t seed) {
    ecs::Registry reg;
    std::string err;
    auto r = replay(reg, ch, lib, seed, c.log(), &err, c.surprised_at_start());
    REQUIRE_MESSAGE(r, err);
    CHECK(r->hash() == c.hash());
    CHECK(r->log_hash() == c.log_hash());
}
bool has(const Combat& c, CombatantId id, Condition k) { return rules5e::find(c.state(), id)->has(k); }
const rules5e::ActiveSpellEffect* web_on(const Combat& c, CombatantId t) {
    for (const auto& e : c.state().effects) if (e.target == t && e.object_hp > 0) return &e;
    return nullptr;
}
int hits(const Combat& c, CombatantId a, std::size_t from = 0) {
    int n = 0;
    for (std::size_t i = from; i < c.events().size(); ++i) {
        const auto& e = c.events()[i];
        n += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == a && (e.rules.c & 1);
    }
    return n;
}
}  // namespace

TEST_CASE("Web range 30/60: legal to 60 ft, disadvantage beyond 30 ft or with a hostile within 5 ft; refused past 60; replay") {
    auto lib = lib_with({spider(8)});   // +8 vs AC 18: hits on 10+ (55%), 30% at disadvantage
    const auto* def = lib.spells()->find(kWeb);
    REQUIRE(def);
    CHECK(def->range_ft == 30);
    CHECK(def->long_range_ft == 60);
    struct Setup { int fx, sx, extra_x; bool far; bool close_hostile; };
    int rate[3] = {0, 0, 0};
    const Setup setups[3] = {{5, 10, -1, false, false},    // 25 ft: normal
                             {5, 14, -1, true, false},     // 45 ft: long range
                             {5, 10, 11, false, true}};    // 25 ft, a second fighter next to the spider
    for (int k = 0; k < 3; ++k) {
        for (uint64_t seed = 1; seed <= 200; ++seed) {
            std::vector<std::pair<std::string, Vec2i>> sp = {{kFighter, Vec2i{setups[k].fx, 5}}, {"t.spider", Vec2i{setups[k].sx, 5}}};
            if (setups[k].extra_x >= 0) sp.push_back({kFighter, Vec2i{setups[k].extra_x, 5}});
            const auto ch = arena(sp);
            const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            to_actor(*c, s);
            const auto r = submit(*c, web_at(s, f));
            REQUIRE_MESSAGE(r.ok, r.error);
            const auto& logged = c->log().back().step.request;
            CHECK(logged.beyond_normal_range == setups[k].far);
            CHECK(logged.ranged_hostile_within_5ft == setups[k].close_hostile);
            rate[k] += hits(*c, s);
            if (seed == 1) same_on_replay(*c, ch, lib, seed);
        }
    }
    MESSAGE("web hits /200: normal " << rate[0] << ", long " << rate[1] << ", hostile adjacent " << rate[2]);
    CHECK(rate[1] < rate[0] - 25);
    CHECK(rate[2] < rate[0] - 25);
    // 65 ft: beyond long range
    const auto ch = arena({{kFighter, Vec2i{1, 5}}, {"t.spider", Vec2i{14, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    to_actor(*c, ch.spawns[1].stable_id);
    CHECK_FALSE(submit(*c, web_at(ch.spawns[1].stable_id, ch.spawns[0].stable_id)).ok);
    // loader: long_range_ft must exceed range_ft, ranged only
    Json bad = spider();
    bad["id"] = "t.bad";
    bad["actions"].back()["long_range_ft"] = 30;
    CHECK(lib.add(bad, "monster").value_or("").find("bad long_range_ft") != std::string::npos);
}

TEST_CASE("spider bite 0-HP clause: stable, poisoned 600 rounds, paralyzed while poisoned this way; survives healing; replay") {
    auto lib = lib_with({spider(30, 30, web_object(), bite())}, 20);
    const auto* sd = lib.spells()->find(rules5e::spell_id_of("t.spider#save_condition.0"));
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{6, 5}}, {kFighter, Vec2i{14, 14}}});   // a far PC keeps the fight on
    const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    for (int turn = 0; turn < 4 && rules5e::find(c->state(), f)->hp > 0; ++turn) {
        to_actor(*c, s);
        REQUIRE(submit(*c, {s, ActionKind::Attack, f, 0}).ok);
        end_turn(*c);
    }
    const auto* t = rules5e::find(c->state(), f);
    REQUIRE(t->hp == 0);
    CHECK_FALSE(t->dead);
    CHECK(t->death.stable);
    CHECK(has(*c, f, Condition::Poisoned));
    CHECK(has(*c, f, Condition::Paralyzed));
    CHECK(has(*c, f, Condition::Unconscious));
    const rules5e::ActiveSpellEffect* e = nullptr;
    for (const auto& x : c->state().effects) if (x.target == f && x.has_also) e = &x;
    REQUIRE(e);
    CHECK(e->condition == Condition::Poisoned);
    CHECK(e->also == Condition::Paralyzed);
    CHECK(e->timed.kind == rules5e::Expiry::Rounds);
    CHECK(e->timed.rounds_left >= 599);
    if (sd) CHECK(sd->zero_hp_rounds == 600);
    // stable: the fighter's turn rolls no death save
    int ds = 0;
    to_actor(*c, f);
    end_turn(*c);
    for (const auto& ev : c->events()) ds += ev.kind == SimEvent::Kind::Rules && ev.rules.kind == EventKind::DeathSaveRolled;
    CHECK(ds == 0);
    same_on_replay(*c, ch, lib, 5);
    // regaining HP: still poisoned and paralyzed (on a copy of the state)
    auto st = c->state();
    REQUIRE(rules5e::heal(st, f, 5).ok);
    CHECK(rules5e::find(st, f)->hp == 5);
    CHECK(rules5e::find(st, f)->has(Condition::Poisoned));
    CHECK(rules5e::find(st, f)->has(Condition::Paralyzed));
    // the hour runs out: poisoned and paralyzed end together
    for (auto& x : CombatTestAccess::state(*c).effects) if (x.target == f && x.has_also) x.timed.rounds_left = 1;
    for (int i = 0; i < 6 && has(*c, f, Condition::Poisoned); ++i) end_turn(*c);
    CHECK_FALSE(has(*c, f, Condition::Poisoned));
    CHECK_FALSE(has(*c, f, Condition::Paralyzed));
}

TEST_CASE("spider bite 0-HP clause: not when the poison leaves HP, not when the bite's piercing already dropped it, not without the key") {
    struct Case { const char* weapon; const char* poison; bool clause; int hp; const char* why; };
    for (const Case k : {Case{"1d1", "2d1", true, 50, "poison leaves HP"}, Case{"20d1", "2d1", true, 20, "piercing dropped it"},
                         Case{"1d1", "20d1", false, 20, "no zero_hp key"}}) {
        CAPTURE(std::string(k.why));
        auto lib = lib_with({spider(30, 30, web_object(), bite(k.weapon, k.poison, k.clause))}, k.hp);
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{6, 5}}, {kFighter, Vec2i{14, 14}}});   // a far PC keeps the fight on
        const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 5);
        int h = 0;
        for (int turn = 0; turn < 4 && h == 0; ++turn) {
            to_actor(*c, s);
            REQUIRE(submit(*c, {s, ActionKind::Attack, f, 0}).ok);
            h = hits(*c, s);
            end_turn(*c);
        }
        REQUIRE(h == 1);
        CHECK_FALSE(has(*c, f, Condition::Paralyzed));
        for (const auto& x : c->state().effects) CHECK_FALSE((x.target == f && x.has_also));
        if (std::string(k.why) == "poison leaves HP") CHECK_FALSE(rules5e::find(c->state(), f)->death.stable);
        same_on_replay(*c, ch, lib, 5);
    }
    // loader errors
    ContentLibrary lib = lib_with({});
    Json b = bite();
    b["on_hit"][0]["zero_hp"]["while"] = "poisoned";
    CHECK(lib.add(spider(30, 30, web_object(), b), "monster").value_or("").find("bad zero_hp") != std::string::npos);
    b = bite();
    b["on_hit"][0]["zero_hp"]["duration"] = {{"kind", "minutes"}, {"n", 60}};
    CHECK(lib.add(spider(30, 30, web_object(), b), "monster").value_or("").find("bad zero_hp duration") != std::string::npos);
}

TEST_CASE("the web as an object: AC 10, 5 HP, damage modifiers; destroying it ends restrained; special 6 refusals; 0xBE; replay") {
    // the restrained fighter hacks at its own web (longsword, slashing: no modifier); 5 HP falls to one or two hits
    auto lib = lib_with({spider()});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{10, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 12);
    REQUIRE(c);
    // no web yet: refused
    to_actor(*c, f);
    rules5e::ActionRequest cut{f, ActionKind::Attack, f, 0};
    cut.special = 6;
    CHECK_FALSE(submit(*c, cut).ok);
    end_turn(*c);
    for (int i = 0; i < 6 && !has(*c, f, Condition::Restrained); ++i) {
        to_actor(*c, s);
        if (rules5e::find_action_use(c->state(), s, kWeb)->expended) { end_turn(*c); continue; }
        REQUIRE(submit(*c, web_at(s, f)).ok);
        end_turn(*c);
    }
    REQUIRE(has(*c, f, Condition::Restrained));
    REQUIRE(web_on(*c, f));
    CHECK(web_on(*c, f)->object_hp == 5);
    CHECK(web_on(*c, f)->object_ac == 10);
    // 0xBE: the web's HP is in Combat::hash
    {
        const uint64_t h0 = c->hash();
        for (auto& x : CombatTestAccess::state(*c).effects) if (x.object_hp > 0) x.object_hp = 4;
        CHECK(c->hash() != h0);
        for (auto& x : CombatTestAccess::state(*c).effects) if (x.object_hp > 0) x.object_hp = 5;
        CHECK(c->hash() == h0);
    }
    int strikes = 0, last_hp = 5;
    for (int turn = 0; turn < 12 && has(*c, f, Condition::Restrained); ++turn) {
        to_actor(*c, f);
        const std::size_t n0 = c->events().size();
        const auto r = submit(*c, cut);
        REQUIRE_MESSAGE(r.ok, r.error);
        bool object_flag = false;
        for (std::size_t i = n0; i < c->events().size(); ++i)
            object_flag = object_flag || (c->events()[i].kind == SimEvent::Kind::Rules && c->events()[i].rules.kind == EventKind::AttackResolved &&
                                          (c->events()[i].rules.c & 4));
        CHECK(object_flag);
        if (web_on(*c, f)) { CHECK(web_on(*c, f)->object_hp <= last_hp); last_hp = web_on(*c, f)->object_hp; }
        ++strikes;
        end_turn(*c);
    }
    CHECK_FALSE(has(*c, f, Condition::Restrained));   // destroyed
    CHECK_FALSE(web_on(*c, f));
    CHECK(strikes >= 1);
    same_on_replay(*c, ch, lib, 12);
}

TEST_CASE("the web's damage modifiers: immune takes nothing, vulnerable takes double") {
    for (bool vulnerable : {false, true}) {
        CAPTURE(vulnerable);
        Json obj = vulnerable ? web_object(Json::array({"slashing"}), Json::array(), 200) : web_object(Json::array(), Json::array({"slashing"}), 200);
        auto lib = lib_with({spider(30, 30, obj)});
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{10, 5}}});
        const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        for (int i = 0; i < 6 && !has(*c, f, Condition::Restrained); ++i) {
            to_actor(*c, s);
            if (!rules5e::find_action_use(c->state(), s, kWeb)->expended) REQUIRE(submit(*c, web_at(s, f)).ok);
            end_turn(*c);
        }
        REQUIRE(web_on(*c, f));
        rules5e::ActionRequest cut{f, ActionKind::Attack, f, 0};
        cut.special = 6;
        int landed = 0;
        for (int turn = 0; turn < 6; ++turn) {
            to_actor(*c, f);
            const std::size_t n0 = c->events().size();
            const int before = web_on(*c, f)->object_hp;
            REQUIRE(submit(*c, cut).ok);
            const int lost = before - web_on(*c, f)->object_hp;
            if (hits(*c, f, n0) > 0) {
                ++landed;
                if (vulnerable) { CHECK(lost >= 8); CHECK(lost % 2 == 0); }   // (1d8+3) x 2 (a crit: 2d8+3, x 2)
                else CHECK(lost == 0);
            } else CHECK(lost == 0);
            end_turn(*c);
        }
        CHECK(landed >= 1);
        same_on_replay(*c, ch, lib, 3);
    }
}
