// Monster predicates and on_event usage (bugbear Surprise Attack, boar Relentless, hobgoblin Martial Advantage): the loader
// keys `target_surprised`, `ally_near_target`, `damage_at_most {amount}` (rules5e PredicateKind TargetSurprised /
// AllyNearTarget / DamageAtMost, flagged rules5e edit) and on_event `usage` per_day / per_rest / pool (a pool Spend rider,
// trigger-owned pools registered like resources[] and recharged by rests, folded with the pools under 0xA3).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/rest.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::PredicateKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json monster(const std::string& id, std::vector<Json> traits, int hp = 400, int ac = 15) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    m["armor_class"]["value"] = ac;
    for (auto& a : m["actions"]) if (a.contains("to_hit")) a["to_hit"] = 30;   // always hits
    m["traits"] = Json::array();
    for (std::size_t i = 0; i < traits.size(); ++i)
        m["traits"].push_back({{"id", "t" + std::to_string(i)}, {"name", "T"}, {"description", "x"}, {"effect", traits[i]}});
    return m;
}
Json extra_2d6() { return Json::array({{{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "2d6"}, {"type", "same_as_weapon"}}})}}}); }
Json surprise_attack() {
    return {{"kind", "on_event"}, {"on", "hit"}, {"when", Json::array({{{"kind", "target_surprised"}}})}, {"effects", extra_2d6()}};
}
Json martial_advantage() {   // `when` is all-of: a weapon attack and an ally within 5 ft of the target
    return {{"kind", "on_event"}, {"on", "hit"}, {"limit", "once_per_turn"},
            {"when", Json::array({{{"kind", "weapon_attack"}}, {{"kind", "ally_near_target"}}})}, {"effects", extra_2d6()}};
}
Json relentless(int amount) {
    return {{"kind", "on_event"}, {"on", "drop_to_zero"}, {"when", Json::array({{{"kind", "damage_at_most"}, {"amount", amount}}})},
            {"usage", {{"kind", "per_rest"}, {"uses", 1}, {"rest", "short"}}},
            {"effects", Json::array({{{"kind", "set_hp"}, {"value", 1}}})}};
}
ContentLibrary lib_with(const std::vector<Json>& docs) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : docs) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
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
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
SubmitResult attack(Combat& c, CombatantId a, CombatantId t) {
    Command x;
    x.request = {a, ActionKind::Attack, t};
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
}
int fired(const Combat& c, uint32_t trigger_id) {
    int n = 0;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered && uint32_t(e.rules.a) == trigger_id &&
            e.rules.c == 1) ++n;
    return n;
}
uint32_t tid(const char* arch) { return rules5e::spell_id_of(std::string(arch) + "#t0#0"); }
void same_on_replay(const Combat& c, const world::Chunk& ch, const ContentLibrary& lib, uint64_t seed) {
    ecs::Registry reg;
    std::string err;
    auto r = replay(reg, ch, lib, seed, c.log(), &err, c.surprised_at_start());
    REQUIRE_MESSAGE(r, err);
    CHECK(r->hash() == c.hash());
    CHECK(r->log_hash() == c.log_hash());
}
}  // namespace

TEST_CASE("monster predicates loader: keys, usage pools, errors") {
    auto lib = lib_with({monster("t.bug", {surprise_attack()}), monster("t.hob", {martial_advantage()}), monster("t.boar", {relentless(7)})});
    const auto& bug = lib.find("t.bug")->base.triggers;
    REQUIRE(bug.size() == 1);
    REQUIRE(bug[0].when.size() == 1);
    CHECK(bug[0].when[0].kind == PredicateKind::TargetSurprised);
    const auto& hob = lib.find("t.hob")->base.triggers;
    REQUIRE(hob.size() == 1);
    REQUIRE(hob[0].when.size() == 2);
    CHECK(hob[0].when[0].kind == PredicateKind::WeaponAttack);
    CHECK(hob[0].when[1].kind == PredicateKind::AllyNearTarget);
    CHECK(hob[0].limit == rules5e::TriggerLimit::OncePerTurn);
    const Archetype* boar = lib.find("t.boar");
    REQUIRE(boar->base.triggers.size() == 1);
    const auto& rt = boar->base.triggers[0];
    CHECK(rt.on == rules5e::TriggerOn::DropToZero);
    CHECK(rt.when[0].kind == PredicateKind::DamageAtMost);
    CHECK(rt.when[0].a == 7);
    REQUIRE(rt.effects.size() == 2);
    CHECK(rt.effects[1].kind == rules5e::RiderKind::Spend);
    REQUIRE(boar->resources.size() == 1);
    CHECK(boar->resources[0].id == "t0#usage");
    CHECK(boar->resources[0].pool_id == rt.effects[1].pool);
    CHECK(boar->resources[0].max == 1);
    CHECK(boar->resources[0].recharge == uint8_t(rules5e::FeatureRecharge::ShortRest));
    CHECK(lib.find("t.bug")->resources.empty());   // no usage: no pool, so combats without one hash as before

    Json bad = relentless(0);
    CHECK(lib.add(monster("t.bad1", {bad}), "monster").value_or("").find("damage_at_most") != std::string::npos);
    bad = relentless(7);
    bad["usage"]["uses"] = 0;
    CHECK(lib.add(monster("t.bad2", {bad}), "monster").value_or("").find("bad uses") != std::string::npos);
    bad = relentless(7);
    bad["usage"] = {{"kind", "pool"}, {"pool", "nope"}};
    CHECK(lib.add(monster("t.bad3", {bad}), "monster").value_or("").find("not a declared resource") != std::string::npos);
    bad = relentless(7);
    bad["usage"] = {{"kind", "recharge"}, {"min", 5}};
    REQUIRE_FALSE(lib.add(monster("t.rech", {bad}), "monster"));   // recharge: supported since the deferred-monsters PR
    REQUIRE(lib.find("t.rech")->base.triggers.size() == 1);
    CHECK(lib.find("t.rech")->base.triggers[0].recharge_min == 5);
}

TEST_CASE("Surprise Attack: round 1 against a target in the surprised set (even after its turn); not round 2 or unsurprised; replay") {
    auto lib = lib_with({monster("t.bug", {surprise_attack()})});
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.bug", Vec2i{6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    for (bool bug_first : {true, false}) {
        uint64_t seed = 0;
        for (uint64_t s = 1; s < 200 && !seed; ++s) {   // a seed with the wanted initiative order
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, s);
            if ((rules5e::current_actor(c->state()) == g) == bug_first) seed = s;
        }
        REQUIRE(seed != 0);
        for (bool surprised : {true, false}) {
            CAPTURE(bug_first);
            CAPTURE(surprised);
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            if (surprised) REQUIRE(c->apply_surprise({f}));
            if (!bug_first) {
                end_turn(*c);   // the fighter's (surprised) turn ends first: the flag clears, the set membership stays
                CHECK_FALSE(rules5e::find(c->state(), f)->surprised);
            }
            REQUIRE(rules5e::current_actor(c->state()) == g);
            REQUIRE(c->state().round == 1);
            REQUIRE(attack(*c, g, f).ok);
            CHECK(fired(*c, tid("t.bug")) == (surprised ? 1 : 0));
            end_turn(*c);
            to_actor(*c, g);
            REQUIRE(c->state().round == 2);
            REQUIRE(attack(*c, g, f).ok);
            CHECK(fired(*c, tid("t.bug")) == (surprised ? 1 : 0));   // round 2: never
            same_on_replay(*c, ch, lib, seed);
        }
    }
}

TEST_CASE("Martial Advantage: extra damage with an ally within 5 ft of the target, once per turn; replay") {
    auto lib = lib_with({monster("t.hob", {martial_advantage()}), monster("t.pal", {})});
    for (bool near : {true, false}) {
        CAPTURE(near);
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.hob", Vec2i{6, 5}}, {"t.pal", near ? Vec2i{4, 5} : Vec2i{15, 5}}});
        const CombatantId f = ch.spawns[0].stable_id, h = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 5);
        REQUIRE(c);
        int prev = 0;
        for (int turn = 1; turn <= 4; ++turn) {
            to_actor(*c, h);
            REQUIRE(attack(*c, h, f).ok);
            const int now = fired(*c, tid("t.hob"));
            CHECK(now - prev <= 1);   // at most once in each of its turns (0 on a natural 1)
            prev = now;
            end_turn(*c);
        }
        if (near) CHECK(prev >= 3);
        else CHECK(prev == 0);
        same_on_replay(*c, ch, lib, 5);
    }
}

TEST_CASE("Relentless: 1 HP instead of 0 for damage <= N, once per rest; recharges on a short rest; replay") {
    // 20 HP: the dropping hit never overflows by 20 (no massive-damage instant death, which skips drop_to_zero triggers).
    for (int amount : {100, 1}) {   // 100: any longsword hit qualifies; 1: no longsword hit does (1d8 + 3 >= 4)
        CAPTURE(amount);
        auto lib = lib_with({monster("t.boar", {relentless(amount)}, 20, 1)});
        const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.boar", Vec2i{6, 5}}});
        const CombatantId f = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const uint32_t pool = lib.find("t.boar")->resources[0].pool_id;
        auto hits_until_change = [&]() {
            const int f0 = fired(*c, tid("t.boar"));
            for (int t = 0; t < 60 && !c->ended(); ++t) {   // until it drops (Relentless fires) or dies
                to_actor(*c, f);
                REQUIRE(attack(*c, f, b).ok);
                if (rules5e::find(c->state(), b)->dead || fired(*c, tid("t.boar")) > f0) return;
                end_turn(*c);
            }
        };
        hits_until_change();
        const auto* k = rules5e::find(c->state(), b);
        if (amount == 100) {
            CHECK(fired(*c, tid("t.boar")) == 1);
            CHECK_FALSE(k->dead);
            CHECK(k->hp == 1);
            CHECK(rules5e::find_pool(c->state(), b, pool)->cur == 0);
            rules5e::EncounterState roster = c->state();   // between encounters: a short rest refills the use
            rng::Rng rest = make_rest_rng(7);
            REQUIRE(short_rest(roster, b, rest, 0).ok);
            CHECK(rules5e::find_pool(roster, b, pool)->cur == 1);
            end_turn(*c);
            hits_until_change();   // the use is spent: the next hit kills
            CHECK(rules5e::find(c->state(), b)->dead);
            CHECK(fired(*c, tid("t.boar")) == 1);
        } else {
            CHECK(fired(*c, tid("t.boar")) == 0);
            CHECK(k->dead);
            CHECK(rules5e::find_pool(c->state(), b, pool)->cur == 1);   // not spent when the predicate fails
        }
        same_on_replay(*c, ch, lib, 7);
    }
}

TEST_CASE("weapon_attack: true for weapon attacks, false for spell attacks (Martial Advantage never fires on Fire Bolt)") {
    Json weapon_only = {{"kind", "on_event"}, {"on", "hit"}, {"when", Json::array({{{"kind", "weapon_attack"}}})}, {"effects", extra_2d6()}};
    Json mage = monster("t.hobmage", {martial_advantage(), weapon_only});
    mage["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"spells", Json::array({"srd51.spell.fire_bolt"})}};
    mage["abilities"]["int"] = 30;   // spell attack +12 or so: Fire Bolt hits the fighter most of the time
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(lib.add(mage, "monster"));
    REQUIRE_FALSE(lib.add(monster("t.pal", {}), "monster"));
    const auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.hobmage", Vec2i{6, 5}}, {"t.pal", Vec2i{4, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, h = ch.spawns[1].stable_id;
    const uint32_t ma = rules5e::spell_id_of("t.hobmage#t0#0"), wo = rules5e::spell_id_of("t.hobmage#t1#0");
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    int hits = 0;
    for (int turn = 0; turn < 4; ++turn) {   // spell attacks only
        to_actor(*c, h);
        Command cs;
        cs.request = {h, ActionKind::CastSpell, f};
        cs.request.spell_id = rules5e::spell_id_of("srd51.spell.fire_bolt");
        auto r = c->submit(cs);
        REQUIRE_MESSAGE(r.ok, r.error);
        REQUIRE(c->settle(nullptr).ok);
        end_turn(*c);
    }
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == h && (e.rules.c & 1)) ++hits;
    CHECK(hits >= 1);   // some Fire Bolt hit
    CHECK(fired(*c, ma) == 0);
    CHECK(fired(*c, wo) == 0);
    to_actor(*c, h);
    REQUIRE(attack(*c, h, f).ok);   // a weapon attack (scimitar, always hits): both fire
    CHECK(fired(*c, wo) == 1);
    CHECK(fired(*c, ma) == 1);
    same_on_replay(*c, ch, lib, 5);
}

TEST_CASE("7.2y predicate target_below_max_hp loads (Colossus Slayer)") {
    Json cs = {{"kind", "on_event"}, {"on", "hit"}, {"limit", "once_per_turn"},
               {"when", Json::array({{{"kind", "weapon_attack"}}, {{"kind", "target_below_max_hp"}}})}, {"effects", extra_2d6()}};
    auto lib = lib_with({monster("t.cs", {cs})});
    const auto& tr = lib.find("t.cs")->base.triggers;
    REQUIRE(tr.size() == 1);
    REQUIRE(tr[0].when.size() == 2);
    CHECK(tr[0].when[1].kind == PredicateKind::TargetBelowMaxHp);
    CHECK(tr[0].limit == rules5e::TriggerLimit::OncePerTurn);
}
