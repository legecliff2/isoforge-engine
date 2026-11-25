// C-T TP1 loader: variant_of, target_max_size, the swallow rider, instead, and the T11 legendary shape.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
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

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json melee(const char* id, int to_hit, const char* dice, int reach = 10) {
    return {{"id", id}, {"name", id}, {"kind", "melee_weapon_attack"}, {"to_hit", to_hit}, {"reach", reach},
            {"targets", 1}, {"damage", Json::array({{{"dice", dice}, {"type", "piercing"}}})},
            {"natural", {{"dice", "4d12"}, {"ability", "str"}}}};
}
Json swallow_row() {
    return {{"id", "swallow"}, {"name", "Swallow"}, {"kind", "melee_weapon_attack"},
            {"description", "One bite attack against a Large or smaller creature it is grappling."},
            {"variant_of", "bite"},
            {"target_requires_any", Json::array({"grappled_by_self"})},
            {"target_max_size", "large"},
            {"on_hit", Json::array({{{"kind", "swallow"},
                                     {"damage", {{"dice", "16d6"}, {"type", "acid"}}},
                                     {"capacity", "unlimited"},
                                     {"regurgitate", {{"dc", 20}, {"damage", 60}, {"all", true}}},
                                     {"exit_ft", 30}}})}};
}
Json frightful() {
    return {{"id", "frightful_presence"}, {"name", "Frightful Presence"}, {"kind", "save_effect"},
            {"origin", "self"}, {"within_ft", 120}, {"total_cover", "no_skip"}, {"chooses_targets", true},
            {"aware_only", true}, {"save", {{"ability", "wis"}, {"dc", 17}}},
            {"apply", Json::array({{{"condition", "frightened"},
                                    {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                    {"repeat_save", "end_of_turn"}}})},
            {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}},
            {"immune_on_end", true}};
}
Json beast() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@7";
    m["id"] = "t.swallow";
    m["name"] = "Swallow Fixture";
    m["size"] = "medium";
    m["type"] = "monstrosity";
    m["hit_points"]["average"] = 300;
    m["armor_class"]["value"] = 25;
    m["actions"] = Json::array({
        melee("bite", 19, "4d12+10"),
        melee("claw", 19, "4d8+10"),
        melee("tail", 19, "4d6+10"),
        swallow_row(),
        frightful(),
    });
    m["actions"][0]["on_hit"] = Json::array({{{"kind", "grapple"}, {"escape_dc", 20}, {"restrains", true}, {"exclusive", true}}});
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
                                {"effect", {{"kind", "multiattack"},
                                            {"options", Json::array({{{"id", "full"},
                                                                      {"attacks", Json::array({"bite", "claw", "claw", "tail"})},
                                                                      {"instead", {{"bite", "swallow"}}},
                                                                      {"lead_action", "frightful_presence"}}})}}}}});
    m["legendary_actions_per_round"] = 3;
    m["legendary_actions"] = Json::array({
        {{"id", "attack_claw"}, {"name", "Attack: claw"}, {"description", "One claw."}, {"cost", 1}, {"use", {{"attack", "claw"}}}},
        {{"id", "attack_tail"}, {"name", "Attack: tail"}, {"description", "One tail."}, {"cost", 1}, {"use", {{"attack", "tail"}}}},
        {{"id", "move"}, {"name", "Move"}, {"description", "Half speed."}, {"cost", 1}, {"use", {{"move", "half_speed"}}}},
        {{"id", "chomp_bite"}, {"name", "Chomp (Costs 2 Actions): bite"}, {"description", "One bite."}, {"cost", 2}, {"use", {{"attack", "bite"}}}},
        {{"id", "chomp_swallow"}, {"name", "Chomp (Costs 2 Actions): Swallow"}, {"description", "Swallow."}, {"cost", 2}, {"use", {{"attack", "swallow"}}}},
    });
    m.erase("behavior");
    return m;
}
Json victim() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.swallow.victim";
    m["hit_points"]["average"] = 80;
    m["armor_class"]["value"] = 5;
    m.erase("behavior");
    return m;
}
std::optional<std::string> load_one(Json m) {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l.add(std::move(m), "monster");
}
int index_of(const rules5e::Combatant& c, const char* id) {
    for (int i = 0; i < int(c.attacks.size()); ++i) if (c.attacks[i].id == id) return i;
    return -1;
}

}  // namespace

TEST_CASE("T1 T2 T3 T5 T6 T8 T10 T11: the swallow row, the rider, instead, and the legendary shape load") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(beast(), "monster"));
    const Archetype* a = l.find("t.swallow");
    REQUIRE(a);
    const auto& c = a->base;
    const int bite = index_of(c, "bite"), swallow = index_of(c, "swallow");
    REQUIRE(bite >= 0);
    REQUIRE(swallow >= 0);
    CHECK(c.attacks[swallow].to_hit == c.attacks[bite].to_hit);
    CHECK(c.attacks[swallow].to_hit == 19);
    CHECK(c.attacks[swallow].damage.count == 4);
    CHECK(c.attacks[swallow].damage.sides == 12);
    CHECK(c.attacks[swallow].damage.bonus == 10);
    CHECK(c.attacks[swallow].type == rules5e::DamageType::Piercing);
    CHECK(c.attacks[swallow].reach_or_range_ft == 10);
    CHECK_FALSE(c.attacks[swallow].ranged);
    CHECK(c.attacks[swallow].target_requires_any == rules5e::kTargetGateGrappledBySelf);
    CHECK(c.attacks[swallow].target_max_size == int8_t(rules5e::Size::Large));
    CHECK(c.attacks[bite].target_max_size == -1);
    CHECK(a->grapple_on_hit.count(uint8_t(bite)) == 1);
    CHECK(a->grapple_on_hit.count(uint8_t(swallow)) == 0);
    const rules5e::SpellDef* rider = nullptr;
    for (uint32_t id : a->trigger_spell_ids) {
        const auto* d = l.spells()->find(id);
        if (d && d->contain == 2) rider = d;
    }
    REQUIRE(rider);
    CHECK(rider->kind == rules5e::SpellEffectKind::ApplyCondition);
    CHECK(rider->condition == Condition::Restrained);
    CHECK(rider->contain_dice.count == 16);
    CHECK(rider->contain_dice.sides == 6);
    CHECK(rider->contain_damage_type == rules5e::DamageType::Acid);
    CHECK(rider->contain_unlimited);
    CHECK(rider->regurgitate_dc == 20);
    CHECK(rider->regurgitate_damage == 60);
    CHECK(rider->regurgitate_all);
    CHECK(rider->contain_exit_ft == 30);
    CHECK(rider->contain_ends_grapple);
    REQUIRE(c.multiattacks.size() >= 2);
    CHECK(c.multiattacks[0].alternates.size() == 1);
    CHECK(c.multiattacks[0].alternates[0].slot == uint8_t(bite));
    CHECK(c.multiattacks[0].alternates[0].instead == uint8_t(swallow));
    CHECK(c.multiattacks[1].after_action != 0);
    CHECK(c.multiattacks[1].alternates.size() == 1);
    CHECK(c.multiattacks[1].alternates[0].instead == uint8_t(swallow));
    REQUIRE(c.legendary.size() == 5);
    CHECK(c.la_max == 3);
    CHECK(c.legendary[0].cost == 1);
    CHECK(c.legendary[0].attack_index == uint8_t(index_of(c, "claw")));
    CHECK(c.legendary[1].attack_index == uint8_t(index_of(c, "tail")));
    CHECK(c.legendary[2].use == rules5e::LegendaryUse::Move);
    CHECK(c.legendary[2].half_speed);
    CHECK(c.legendary[3].cost == 2);
    CHECK(c.legendary[3].attack_index == uint8_t(bite));
    CHECK(c.legendary[4].cost == 2);
    CHECK(c.legendary[4].attack_index == uint8_t(swallow));
}

TEST_CASE("T1 T3 T10: loader errors name the contract phrases") {
    Json missing = beast();
    missing["actions"][3]["variant_of"] = "nope";
    auto err = load_one(missing);
    REQUIRE(err);
    CHECK(err->find("variant_of names no attack of this creature") != std::string::npos);
    Json chain = beast();
    chain["actions"][3]["variant_of"] = "swallow";
    err = load_one(chain);
    REQUIRE(err);
    CHECK(err->find("variant_of can't name a variant") != std::string::npos);
    Json copied = beast();
    copied["actions"][3]["to_hit"] = 19;
    err = load_one(copied);
    REQUIRE(err);
    CHECK(err->find("variant_of copies to_hit from bite") != std::string::npos);
    Json old = beast();
    old["$schema"] = "monster@4";
    old.erase("legendary_actions");
    old.erase("legendary_actions_per_round");
    err = load_one(old);
    REQUIRE(err);
    CHECK(err->find("variant_of needs a monster@5 file") != std::string::npos);
    Json cap = beast();
    cap["actions"][3]["on_hit"][0].erase("capacity");
    err = load_one(cap);
    REQUIRE(err);
    CHECK(err->find("swallow rider: capacity is required") != std::string::npos);
    Json ranged = beast();
    ranged["actions"][3]["kind"] = "ranged_weapon_attack";
    err = load_one(ranged);
    REQUIRE(err);
    CHECK(err->find("variant_of copies kind from bite") != std::string::npos);
    Json instead = beast();
    instead["traits"][0]["effect"]["options"][0]["instead"] = {{"claw", "swallow"}};
    err = load_one(instead);
    REQUIRE(err);
    CHECK(err->find("instead: the alternate must be a variant_of that attack") != std::string::npos);
    Json absent = beast();
    absent["traits"][0]["effect"]["options"][0]["instead"] = {{"horn", "swallow"}};
    err = load_one(absent);
    REQUIRE(err);
    CHECK(err->find("instead names no attack of this option") != std::string::npos);
}

TEST_CASE("T3: a loaded swallow hit blinds, restrains, and ends the bite's grapple") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(beast(), "monster"));
    REQUIRE_FALSE(l.add(victim(), "monster"));
    bool hit = false;
    for (uint64_t seed = 1; seed < 30 && !hit; ++seed) {
        world::Chunk ch{};
        ch.coord = {5, 5, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor", "wall"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        ch.spawns.push_back({"t.swallow", Vec2i{4, 4}, world::spawn_stable_id(ch.coord, 0), ""});
        ch.spawns.push_back({"t.swallow.victim", Vec2i{5, 4}, world::spawn_stable_id(ch.coord, 1),
                              std::string("{\"team\":\"party\"}")});
        ch.content_hash = world::compute_content_hash(ch);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, seed);
        REQUIRE(c);
        const CombatantId boss = ch.spawns[0].stable_id, prey = ch.spawns[1].stable_id;
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != boss && !c->state().ended; ++i) {
            Command end; end.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            auto r = c->submit(end);
            while (r.needs) r = c->decide(rules5e::ReactionDecision{r.needs->window_seq, false, 0});
            REQUIRE(r.ok);
        }
        if (rules5e::current_actor(c->state()) != boss) continue;
        auto& st = CombatTestAccess::state(*c);
        rules5e::SourcedCondition g;
        g.condition = Condition::Grappled; g.source = boss; g.target = prey;
        g.via_attack = 0; g.exclusive = true; g.restrains = true; g.owns_restrained = true;
        st.sourced.push_back(g);
        for (auto& k : st.combatants) if (k.id == prey) {
            k.conditions |= rules5e::bit(Condition::Grappled) | rules5e::bit(Condition::Restrained);
        }
        int swallow = index_of(*rules5e::find(st, boss), "swallow");
        REQUIRE(swallow >= 0);
        Command atk;
        atk.request = {boss, ActionKind::Attack, prey, uint8_t(swallow)};
        atk.request.target_within_5ft = true;
        auto r = c->submit(atk);
        while (r.needs) r = c->decide(rules5e::ReactionDecision{r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
        const auto* prey_c = rules5e::find(st, prey);
        REQUIRE(prey_c);
        if (!prey_c->has(Condition::Blinded)) continue;
        hit = true;
        CHECK(prey_c->has(Condition::Restrained));
        CHECK_FALSE(prey_c->has(Condition::Grappled));
        CHECK(rules5e::container_of(st, prey) == boss);
    }
    CHECK(hit);
}

TEST_CASE("TP1 invariants: schema 14, wyrmling digest, 0xEC free, couatl pool 42") {
    CHECK(kSimEventSchemaVersion == 14);
    {
        std::ifstream g(kRoot + "/scenarios/wyrmling_breath/golden.txt");
        REQUIRE(g.good());
        std::string all((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(all.find("digest e3a911c69efcd4bc") != std::string::npos);
    }
    std::ifstream combat(kRoot + "/engine/core/sim/src/combat.cpp");
    std::string src((std::istreambuf_iterator<char>(combat)), std::istreambuf_iterator<char>());
    CHECK(src.find("byte(0xEC)") == std::string::npos);
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const Archetype* couatl = l.find("srd51.monster.couatl");
    REQUIRE(couatl);
    const Archetype::FormFeature* pool = nullptr;
    for (const auto& [id, feat] : couatl->form_features) {
        (void)id;
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) pool = &feat;
    }
    REQUIRE(pool);
    CHECK(pool->forms.size() == 42);
}
