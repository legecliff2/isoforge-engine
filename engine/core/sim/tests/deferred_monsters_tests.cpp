// Deferred SRD monsters, sim side: ghoul claws (on_hit rider `when`: not_creature_type undead + not_target_tag elf; Combatant::tags
// from monster tags[] / character race), giant spider Web (`attack_effect` action: attack roll -> restrained with a Strength
// escape DC, usage recharge 5-6), and on_event `usage: recharge` (ActionUse row per trigger, rolled at the owner's turn start).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json goblin(const std::string& id, int hp = 400) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    for (auto& a : m["actions"]) if (a.contains("to_hit")) a["to_hit"] = 30;   // always hits (but a natural 1)
    m["traits"] = Json::array();
    return m;
}
Json fighter(const std::string& id, const std::string& race) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    c["race"] = race;
    c["hit_points"]["average"] = 2000;
    return c;
}
Json ghoul() {   // claws (the scimitar slot): Con DC 30 (always fails) or paralyzed 1 minute, repeat at the end of each turn
    Json m = goblin("t.ghoul");
    m["actions"][0]["on_hit"] = Json::array({{{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", 30}}},
        {"apply", Json::array({{{"condition", "paralyzed"}, {"duration", {{"kind", "rounds"}, {"n", 10}}}, {"repeat_save", "end_of_turn"}}})},
        {"when", Json::array({{{"kind", "not_creature_type"}, {"creature_types", Json::array({"undead"})}},
                              {{"kind", "not_target_tag"}, {"tag", "elf"}}})}}});
    return m;
}
Json spider(int escape_dc = 12, int range = 30) {
    Json m = goblin("t.spider");
    m["actions"].push_back({{"id", "web"}, {"name", "Web"}, {"kind", "attack_effect"}, {"attack", "ranged"}, {"to_hit", 30},
                            {"range_ft", range}, {"apply", Json::array({{{"condition", "restrained"}, {"escape", {{"ability", "str"}, {"dc", escape_dc}}}}})},
                            {"usage", {{"kind", "recharge"}, {"min", 5}}}});
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
ContentLibrary lib_with(const std::vector<Json>& monsters, const std::vector<Json>& chars = {}) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : chars) { const auto e = lib.add(d, "character"); REQUIRE_MESSAGE(!e, e.value_or("")); }
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
int count(const Combat& c, EventKind k, CombatantId actor = 0) {
    int n = 0;
    for (const auto& e : c.events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k && (actor == 0 || e.rules.actor == actor);
    return n;
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
}  // namespace

TEST_CASE("ghoul claws: paralysis on a failed Con save, except elves (race tag) and undead; half-elves are not immune; replay") {
    Json undead = goblin("t.zombieish");
    undead["type"] = "undead";
    Json elf_tagged = goblin("t.drowish");
    elf_tagged["type"] = "humanoid";
    elf_tagged["tags"] = Json::array({"Elf"});
    auto lib = lib_with({ghoul(), undead, elf_tagged}, {fighter("t.human", "human"), fighter("t.highelf", "high_elf"),
                                                     fighter("t.elf", "elf"), fighter("t.halfelf", "half_elf")});
    // loader: tags and the rider's predicates (after the AttackIs gate)
    const auto& tr = lib.find("t.ghoul")->base.triggers;
    REQUIRE(tr.size() == 1);
    REQUIRE(tr[0].when.size() == 3);
    CHECK(tr[0].when[0].kind == rules5e::PredicateKind::AttackIs);
    CHECK(tr[0].when[1].kind == rules5e::PredicateKind::NotCreatureType);
    CHECK(tr[0].when[2].kind == rules5e::PredicateKind::NotTargetTag);
    CHECK(tr[0].when[2].mask == rules5e::spell_id_of("elf"));
    const auto& he = lib.find("t.highelf")->base.tags;
    CHECK(std::find(he.begin(), he.end(), rules5e::spell_id_of("elf")) != he.end());
    const auto& hf = lib.find("t.halfelf")->base.tags;
    CHECK(std::find(hf.begin(), hf.end(), rules5e::spell_id_of("elf")) == hf.end());
    CHECK(lib.find("t.drowish")->base.tags == std::vector<uint32_t>{rules5e::spell_id_of("elf")});   // case-folded
    Json bad = ghoul();
    bad["id"] = "t.badghoul";
    bad["actions"][0]["on_hit"][0]["when"] = Json::array({{{"kind", "not_target_tag"}}});
    CHECK(lib.add(bad, "monster").value_or("").find("bad tag") != std::string::npos);

    struct Case { const char* target; bool paralyzed; };
    for (const Case k : {Case{"t.human", true}, Case{"t.halfelf", true}, Case{"t.highelf", false}, Case{"t.elf", false},
                         Case{"t.zombieish", false}, Case{"t.drowish", false}}) {
        const std::string tname = k.target;
        CAPTURE(tname);
        const auto ch = arena({{"t.ghoul", Vec2i{5, 5}}, {k.target, Vec2i{6, 5}}, {kFighter, Vec2i{14, 14}}});   // a far PC keeps monster-vs-monster going
        const CombatantId g = ch.spawns[0].stable_id, t = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        bool hit = false;
        for (int turn = 0; turn < 3 && !hit; ++turn) {   // until the claws hit (a natural 1 misses)
            to_actor(*c, g);
            { const auto ar = submit(*c, {g, ActionKind::Attack, t, 0}); REQUIRE_MESSAGE(ar.ok, ar.error); }
            for (const auto& e : c->events())
                hit = hit || (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == g && (e.rules.c & 1));
            if (!hit) end_turn(*c);
        }
        REQUIRE(hit);
        CHECK(has(*c, t, Condition::Paralyzed) == k.paralyzed);
        CHECK((count(*c, EventKind::SavingThrowRolled, t) > 0) == k.paralyzed);   // immune: no save at all
        same_on_replay(*c, ch, lib, 3);
    }
}

TEST_CASE("giant spider Web: attack roll -> restrained, Strength escape vs DC, recharge 5-6 gates it; out of range refused; replay") {
    const uint32_t web = rules5e::spell_id_of("t.spider#web");
    for (int dc : {1, 30}) {   // 1: the fighter always breaks free; 30: never
        CAPTURE(dc);
        auto lib = lib_with({spider(dc)});
        const auto* def = lib.spells()->find(web);
        REQUIRE(def);
        CHECK(def->kind == rules5e::SpellEffectKind::SpellAttack);
        CHECK_FALSE(def->is_spell);
        CHECK(def->usage_kind == 1);
        CHECK(def->recharge_min == 5);
        REQUIRE(def->extra_effects.size() == 1);
        CHECK(def->extra_effects[0].escape_dc == dc);
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{10, 5}}});   // 25 ft
        const CombatantId f = ch.spawns[0].stable_id, s = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 8);
        REQUIRE(c);
        to_actor(*c, s);
        rules5e::ActionRequest q{s, ActionKind::CastSpell, f};
        q.spell_id = web;
        auto r = submit(*c, q);
        REQUIRE_MESSAGE(r.ok, r.error);
        const bool hit = has(*c, f, Condition::Restrained);
        CHECK(rules5e::find_action_use(c->state(), s, web)->expended);
        end_turn(*c);
        if (hit) {
            to_actor(*c, f);
            rules5e::ActionRequest esc{f, ActionKind::Attack};
            esc.special = 4;
            r = submit(*c, esc);
            REQUIRE_MESSAGE(r.ok, r.error);
            CHECK(has(*c, f, Condition::Restrained) == (dc == 30));
            CHECK(count(*c, EventKind::CheckRolled, f) == 1);
            end_turn(*c);
        }
        to_actor(*c, s);   // the spider's turn start rolled a d6 for the expended Web
        int rolled = 0, back = 0;
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::RechargeRolled && uint32_t(e.rules.a) == web) {
                ++rolled;
                back = e.rules.c;
                CHECK((e.rules.b >= 5) == (e.rules.c == 1));
            }
        CHECK(rolled == 1);
        r = submit(*c, q);
        CHECK(r.ok == (back == 1));   // still expended: refused
        same_on_replay(*c, ch, lib, 8);
    }
    // out of range (35 ft > 30): refused, nothing spent
    auto lib = lib_with({spider()});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.spider", Vec2i{12, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 8);
    to_actor(*c, ch.spawns[1].stable_id);
    rules5e::ActionRequest q{ch.spawns[1].stable_id, ActionKind::CastSpell, ch.spawns[0].stable_id};
    q.spell_id = web;
    CHECK_FALSE(submit(*c, q).ok);
    CHECK_FALSE(rules5e::find_action_use(c->state(), ch.spawns[1].stable_id, web)->expended);
    // loader errors / unsupported
    Json bad = spider();
    bad["id"] = "t.bad";
    bad["actions"].back()["apply"][0]["escape"]["ability"] = "dex";
    CHECK(lib.add(bad, "monster").value_or("").find("escape ability") != std::string::npos);
    bad = spider();
    bad["id"] = "t.noescape";
    bad["actions"].back()["apply"][0].erase("escape");
    REQUIRE_FALSE(lib.add(bad, "monster"));
    CHECK_FALSE(lib.find("t.noescape")->unsupported_features.empty());
}

TEST_CASE("on_event usage recharge: fires, then only after a recharge roll of N+ at the owner's turn start; deterministic; replay") {
    Json g = goblin("t.recharger");
    g["traits"].push_back({{"id", "burst"}, {"name", "B"}, {"description", "x"},
                           {"effect", {{"kind", "on_event"}, {"on", "hit"}, {"usage", {{"kind", "recharge"}, {"min", 4}}},
                                       {"effects", Json::array({{{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "1d6"}, {"type", "fire"}}})}}})}}}});
    auto lib = lib_with({g});
    const uint32_t tid = rules5e::spell_id_of("t.recharger#burst#0");
    REQUIRE(lib.find("t.recharger")->base.triggers.size() == 1);
    CHECK(lib.find("t.recharger")->base.triggers[0].recharge_min == 4);
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.recharger", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, gb = ch.spawns[1].stable_id;
    uint64_t h[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        REQUIRE(rules5e::find_action_use(c->state(), gb, tid));
        CHECK_FALSE(rules5e::find_action_use(c->state(), gb, tid)->expended);
        bool available = true;
        int expect = 0;
        for (int turn = 0; turn < 8; ++turn) {
            to_actor(*c, gb);
            // the turn start may have rolled for it: the latest RechargeRolled of this trigger decides
            int last = -1;
            for (const auto& e : c->events())
                if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::RechargeRolled && uint32_t(e.rules.a) == tid) last = e.rules.c;
            if (!available && last == 1) available = true;
            const std::size_t n0 = c->events().size();
            REQUIRE(submit(*c, {gb, ActionKind::Attack, f, 0}).ok);
            bool hit = false;
            for (std::size_t i = n0; i < c->events().size(); ++i) {
                const auto& e = c->events()[i];
                hit = hit || (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && (e.rules.c & 1));
            }
            if (hit && available) { ++expect; available = false; }
            int fired = 0;
            for (const auto& e : c->events())
                fired += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered && uint32_t(e.rules.a) == tid;
            CHECK(fired == expect);
            CHECK(rules5e::find_action_use(c->state(), gb, tid)->expended == !available);
            end_turn(*c);
        }
        CHECK(expect >= 2);   // it came back at least once in 8 turns (min 4: 50% a roll)
        CHECK(count(*c, EventKind::RechargeRolled, gb) >= 1);
        h[run] = c->hash();
        same_on_replay(*c, ch, lib, 21);
    }
    CHECK(h[0] == h[1]);   // the recharge rolls come from the encounter PRNG: same seed, same rolls
}
