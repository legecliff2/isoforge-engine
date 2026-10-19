// Row 9b, sim side: the save predicate, bonus_attack (Rampage) and area_save (Death Burst) riders compile, the
// EncounterHooks::area_targets hook feeds the rules their targets, and the AI spends a Rampage grant.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::PredicateKind;
using rules5e::RiderKind;
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json goblin(const std::string& id, std::vector<Json> traits, int hp = 400) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    for (auto& a : m["actions"]) if (a.contains("to_hit")) a["to_hit"] = 30;   // always hits
    for (std::size_t i = 0; i < traits.size(); ++i)
        m["traits"].push_back({{"id", i == 0 ? "burst" : "t" + std::to_string(i)}, {"name", "T"}, {"description", "x"}, {"effect", traits[i]}});
    return m;
}
Json burst(const char* on = "death") {
    return {{"kind", "on_event"}, {"on", on}, {"effects", Json::array({{{"kind", "area_save"},
            {"area", {{"shape", "sphere"}, {"size_ft", 10}}}, {"save", {{"ability", "dex"}, {"dc", 30}}},
            {"damage", Json::array({{{"dice", "2d6"}, {"type", "fire"}}})}, {"on_success", "half"}}})}};
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
ContentLibrary lib_with(const std::vector<Json>& docs) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : docs) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
}  // namespace

TEST_CASE("9b content: save predicate, bonus_attack and area_save riders compile") {
    Json undead = {{"kind", "on_event"}, {"on", "drop_to_zero"},
                   {"when", Json::array({{{"kind", "not_crit"}}, {{"kind", "save"}, {"ability", "con"}, {"dc_from_damage", true}, {"base", 5}}})},
                   {"effects", Json::array({{{"kind", "set_hp"}, {"value", 1}}})}};
    Json rampage = {{"kind", "on_event"}, {"on", "reduce_to_zero"},
                    {"effects", Json::array({{{"kind", "bonus_attack"}, {"attack_id", "shortbow"}}})}};
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(goblin("t.9b", {burst(), undead, rampage}), "monster"));
    const auto& tr = lib.find("t.9b")->base.triggers;
    REQUIRE(tr.size() == 3);
    CHECK(tr[0].on == rules5e::TriggerOn::Death);
    REQUIRE(tr[0].effects.size() == 1);
    CHECK(tr[0].effects[0].kind == RiderKind::AreaSave);
    const auto* d = lib.spells()->find(tr[0].effects[0].spell_id);
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::Save);
    CHECK_FALSE(d->is_spell);
    CHECK(d->fixed_dc == 30);
    CHECK(d->area == rules5e::AreaShape::Sphere);
    CHECK(d->range_ft == 0);
    CHECK(lib.find("t.9b")->trigger_spell_ids.size() == 1);
    CHECK(std::find(lib.find("t.9b")->base.spells_known.begin(), lib.find("t.9b")->base.spells_known.end(), d->id) ==
          lib.find("t.9b")->base.spells_known.end());   // reachable only through the trigger
    REQUIRE(tr[1].when.size() == 2);
    CHECK(tr[1].when[1].kind == PredicateKind::Save);
    CHECK(tr[1].when[1].a == int(rules5e::Ability::Con));
    CHECK(tr[1].when[1].b == 5);
    CHECK((tr[1].when[1].mask & 1u) == 1u);
    CHECK(tr[2].effects[0].kind == RiderKind::BonusAttack);
    CHECK(tr[2].effects[0].value == 1);   // shortbow is attack index 1
    // overriding the archetype replaces its trigger spells (no stale entries, no collision)
    const std::size_t n = lib.spells()->all().size();
    REQUIRE_FALSE(lib.add(goblin("t.9b", {burst()}), "monster"));
    CHECK(lib.spells()->all().size() == n);
    // load errors
    Json bad = rampage;
    bad["effects"][0]["attack_id"] = "nope";
    const auto e1 = lib.add(goblin("t.bad1", {bad}), "monster");
    REQUIRE(e1);
    CHECK(e1->find("nope") != std::string::npos);
    Json noarea = burst();
    noarea["effects"][0].erase("area");
    REQUIRE(lib.add(goblin("t.bad2", {noarea}), "monster"));
    Json badsave = undead;
    badsave["when"][1]["dc"] = 10;   // dc and dc_from_damage together
    REQUIRE(lib.add(goblin("t.bad3", {badsave}), "monster"));
    CHECK_FALSE(lib.find("t.bad3"));
}

TEST_CASE("9b in a fight: Death Burst hits the neighbour of a dead creature; Rampage grant is spent by the AI") {
    auto lib = lib_with({goblin("t.burst", {burst()}, 1), goblin("t.ramp", {{{"kind", "on_event"}, {"on", "damage_dealt"},
                         {"effects", Json::array({{{"kind", "bonus_attack"}, {"attack_id", "scimitar"}}})}}})});
    {
        auto ch = arena({{"t.burst", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        for (int i = 0; i < 40 && !rules5e::find(c->state(), g)->dead; ++i) {
            if (rules5e::current_actor(c->state()) == f) {
                Command a;
                a.request = {f, ActionKind::Attack, g};
                a.request.attack_index = 0;
                REQUIRE(c->submit(a).ok);
                c->settle(nullptr);
                if (rules5e::find(c->state(), g)->dead) break;
            }
            end_turn(*c);
        }
        REQUIRE(rules5e::find(c->state(), g)->dead);
        const auto* fr = rules5e::find(c->state(), f);
        CHECK(fr->hp < fr->hp_max);   // the burst hurt the neighbour (the goblin never attacked)
        bool fired = false;
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered &&
                uint32_t(e.rules.a) == spell_id_of("t.burst#burst#0")) fired = true;
        CHECK(fired);
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, ch, lib, 4, c->log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->hash() == c->hash());
    }
    {   // Rampage: the goblin's hit grants a bonus attack with the scimitar (index 0); the AI sends it as a bonus action
        auto ch = arena({{"t.ramp", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != g; ++i) end_turn(*c);
        REQUIRE(rules5e::current_actor(c->state()) == g);
        Command a;
        a.request = {g, ActionKind::Attack, f};
        a.request.attack_index = 0;
        REQUIRE(c->submit(a).ok);
        c->settle(nullptr);
        REQUIRE(c->state().budget.bonus_attack_grant == 1);
        const Command nx = next_command(*c, f);
        CHECK(nx.request.kind == ActionKind::Attack);
        CHECK(nx.request.as_bonus_action);
        CHECK(nx.request.attack_index == 0);
        REQUIRE(c->submit(nx).ok);
        c->settle(nullptr);
        CHECK_FALSE(c->state().budget.bonus_action);   // the bonus action was spent by the granted attack
    }
}

// ---- Row 9c: choice ask, spend, per_slot_level, save_condition / apply_condition ----
namespace {
Json smite(const char* choice, Json spend) {
    return {{"kind", "on_event"}, {"on", "hit"}, {"choice", choice},
            {"effects", Json::array({spend, {{"kind", "extra_damage"}, {"damage", Json::array({{{"dice", "2d8"}, {"type", "radiant"}}})},
                                             {"scale", {{"per_slot_level", {{"base_level", 1}, {"add_dice", "1d8"}}}}}}})}};
}
}  // namespace

TEST_CASE("9c content: spend, per_slot_level, save_condition and apply_condition compile") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json save = {{"kind", "on_event"}, {"on", "hit"},
                 {"effects", Json::array({{{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", 12}}},
                                           {"damage", Json::array({{{"dice", "1d6"}, {"type", "poison"}}})}, {"on_success", "half"},
                                           {"apply", Json::array({{{"condition", "poisoned"}, {"duration", {{"kind", "rounds"}, {"n", 3}}}}})}}})}};
    Json apply = {{"kind", "on_event"}, {"on", "hit"},
                  {"effects", Json::array({{{"kind", "apply_condition"},
                                            {"apply", Json::array({{{"condition", "prone"}, {"duration", {{"kind", "until_end_of_target_next_turn"}}}}})}}})}};
    REQUIRE_FALSE(lib.add(goblin("t.9c", {smite("ask", {{"kind", "spend"}, {"slot", "any"}}), smite("auto", {{"kind", "spend"}, {"pool", "ki"}, {"cost", 2}}),
                                          smite("never", {{"kind", "spend"}, {"slot", 3}}), save, apply}), "monster"));
    const auto& tr = lib.find("t.9c")->base.triggers;
    REQUIRE(tr.size() == 5);
    CHECK(tr[0].choice == 1);
    CHECK(tr[1].choice == 0);
    CHECK(tr[2].choice == 2);
    CHECK(tr[0].effects[0].kind == RiderKind::Spend);
    CHECK(tr[0].effects[0].slot == 10);
    CHECK(tr[1].effects[0].slot == 0);
    CHECK(tr[1].effects[0].pool == spell_id_of("ki"));
    CHECK(tr[1].effects[0].value == 2);
    CHECK(tr[2].effects[0].slot == 3);
    CHECK(tr[0].effects[1].slot_base == 1);
    CHECK(tr[0].effects[1].slot_dice.count == 1);
    CHECK(tr[0].effects[1].slot_dice.sides == 8);
    const auto* sd = lib.spells()->find(tr[3].effects[0].spell_id);
    REQUIRE(sd);
    CHECK(sd->kind == rules5e::SpellEffectKind::Save);
    CHECK_FALSE(sd->is_spell);
    CHECK(sd->fixed_dc == 12);
    CHECK(sd->applies_condition);
    CHECK(sd->expiry == uint8_t(rules5e::Expiry::Rounds));
    CHECK(sd->duration_rounds == 3);
    const auto* ad = lib.spells()->find(tr[4].effects[0].spell_id);
    REQUIRE(ad);
    CHECK(ad->kind == rules5e::SpellEffectKind::ApplyCondition);
    CHECK(ad->expiry == uint8_t(rules5e::Expiry::EndOfTargetNextTurn));
    // load errors: same_as_weapon in save_condition damage, bad slot, apply_condition without a duration
    Json sw = save;
    sw["effects"][0]["damage"][0]["type"] = "same_as_weapon";
    REQUIRE(lib.add(goblin("t.e1", {sw}), "monster"));
    REQUIRE(lib.add(goblin("t.e2", {smite("ask", {{"kind", "spend"}, {"slot", 12}})}), "monster"));
    Json nd = apply;
    nd["effects"][0]["apply"][0].erase("duration");
    REQUIRE(lib.add(goblin("t.e3", {nd}), "monster"));
}

TEST_CASE("9c in a fight: an ask rider opens a HitRider window; the default policy declines, a decision applies it") {
    auto lib = lib_with({goblin("t.ask", {smite("ask", {{"kind", "spend"}, {"pool", "ki"}, {"cost", 1}})})});
    const uint32_t tid = spell_id_of("t.ask#burst#0");
    for (bool take : {false, true}) {
        auto ch = arena({{"t.ask", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != g; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        Command a;
        a.request = {g, ActionKind::Attack, f};
        a.request.attack_index = 0;
        auto r = c->submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        REQUIRE(r.needs);
        CHECK(r.needs->trigger == rules5e::ReactionTrigger::HitRider);
        CHECK(r.needs->reactor == g);
        class P final : public IActorPolicy {
        public:
            std::optional<CombatantId> choose_target(const Combat&, CombatantId) override { return std::nullopt; }
        } policy;
        auto d = policy.decide_reaction(*c, *r.needs);
        CHECK_FALSE(d.react);   // the default declines HitRider
        d.react = take;
        REQUIRE(c->decide(d).ok);
        int outcome = 0;
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered && uint32_t(e.rules.a) == tid) outcome = int(e.rules.c);
        CHECK(outcome == (take ? 1 : 2));
    }
}
