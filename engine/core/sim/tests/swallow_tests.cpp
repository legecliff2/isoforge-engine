// C-T TP1 loader: variant_of, target_max_size, the swallow rider, instead, and the T11 legendary shape.
#include <doctest/doctest.h>

#include <algorithm>
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
Json ai_monster() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@7";
    m["id"] = "t.swallow.ai";
    m["name"] = "Swallow AI";
    m["size"] = "medium";
    m["type"] = "monstrosity";
    m["hit_points"]["average"] = 80;
    m["armor_class"]["value"] = 15;
    m["actions"] = Json::array({
        melee("bite", 8, "1d4+4", 5),
        melee("claw", 8, "1d4+4", 5),
        swallow_row(),
    });
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
                                {"effect", {{"kind", "multiattack"},
                                            {"options", Json::array({{{"id", "full"},
                                                                      {"attacks", Json::array({"bite", "claw"})},
                                                                      {"instead", {{"bite", "swallow"}}}}})}}}}});
    m.erase("behavior");
    return m;
}
Json sim_beast() {
    Json m = beast();
    m.erase("legendary_actions");
    m.erase("legendary_actions_per_round");
    return m;
}
world::Chunk flat_chunk(std::vector<std::pair<std::string, Vec2i>> spawns, std::vector<Vec2i> only = {},
                         int party_from = 1) {
    world::Chunk c{};
    c.coord = {0, 0, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = only.empty() ? 1 : 0;
    for (Vec2i k : only) c.layers[0][std::size_t(k.y * 32 + k.x)].tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        const std::string over = int(i) >= party_from ? std::string("{\"team\":\"party\"}") : std::string();
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), over});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}
SubmitResult settled(Combat& c, SubmitResult r) {
    if (r.ok) r = c.settle(nullptr);
    return r;
}
void end_turn(Combat& c) {
    if (c.state().legendary_window.x != 0) {
        Command pass;
        pass.request = {c.state().legendary_window.candidate, ActionKind::LegendaryAction};
        REQUIRE(settled(c, c.submit(pass)).ok);
        return;
    }
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(settled(c, c.submit(e)).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 16 && rules5e::current_actor(c.state()) != who && !c.ended(); ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
void wake(Combat& c) {
    for (auto& k : CombatTestAccess::state(c).combatants) k.surprised = false;
}
void hold(Combat& c, CombatantId src, CombatantId who) {
    auto& st = CombatTestAccess::state(c);
    rules5e::SourcedCondition g;
    g.condition = Condition::Grappled; g.source = src; g.target = who; g.via_attack = 0;
    st.sourced.push_back(g);
    for (auto& k : st.combatants) if (k.id == who) k.conditions |= rules5e::bit(Condition::Grappled);
}
bool swallowed_by(const Combat& c, CombatantId prey, CombatantId box) {
    return rules5e::container_of(c.state(), prey) == box;
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

TEST_CASE("TP2 invariants: schema 14, 0xEC free, wyrmling digest, pool 42") {
    CHECK(kSimEventSchemaVersion == 14);
    std::ifstream combat(kRoot + "/engine/core/sim/src/combat.cpp");
    std::string src((std::istreambuf_iterator<char>(combat)), std::istreambuf_iterator<char>());
    CHECK(src.find("byte(0xEC)") == std::string::npos);
    std::ifstream golden(kRoot + "/scenarios/wyrmling_breath/golden.txt");
    REQUIRE(golden.good());
    std::string g((std::istreambuf_iterator<char>(golden)), std::istreambuf_iterator<char>());
    CHECK(g.find("digest e3a911c69efcd4bc") != std::string::npos);
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

TEST_CASE("T9 and T11: toad and frog carry a swallowed creature; distance 0 and the old tile is free") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const struct { const char* box; const char* spell; Vec2i at; Vec2i prey_at; Vec2i witness_at; } rows[] = {
        {"srd51.monster.giant_toad", "srd51.monster.giant_toad#swallow", {8, 8}, {10, 8}, {12, 9}},
        {"srd51.monster.giant_frog", "srd51.monster.giant_frog#swallow", {8, 8}, {9, 8}, {12, 8}},
    };
    for (const auto& row : rows) {
        bool carried = false;
        for (uint64_t seed = 1; seed < 24 && !carried; ++seed) {
            auto ch = flat_chunk({{row.box, row.at}, {"srd51.monster.goblin", row.prey_at},
                                  {"srd51.monster.goblin", row.witness_at}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            const CombatantId box = ch.spawns[0].stable_id, prey = ch.spawns[1].stable_id, witness = ch.spawns[2].stable_id;
            REQUIRE(c->pos(box) == row.at);
            const Vec2i spawn = c->pos(prey);
            to_actor(*c, box);
            if (rules5e::find(c->state(), box)->surprised) {
                end_turn(*c);
                to_actor(*c, box);
            }
            Command cast;
            cast.request = {box, ActionKind::CastSpell, prey};
            cast.request.spell_id = rules5e::spell_id_of(row.spell);
            auto hit = settled(*c, c->submit(cast));
            if (!hit.ok || !swallowed_by(*c, prey, box)) continue;
            carried = true;
            CHECK(c->distance_tiles(box, prey) == 0);
            CHECK(c->pos(prey) == c->pos(box));
            CHECK(c->occupant_at(spawn, 0) == 0);
            CHECK(c->occupant_at(c->pos(prey), box) == 0);
            if (std::string(row.box).find("toad") != std::string::npos) {
                rules5e::SpellDef cube;
                cube.area = rules5e::AreaShape::Cube;
                cube.area_size_ft = 5;
                cube.range_ft = 30;
                const Vec2i corner{c->pos(box).x + 1, c->pos(box).y + 1};
                const auto area = c->area_targets(witness, cube, corner);
                CHECK(std::find(area.begin(), area.end(), prey) != area.end());
                const auto near = c->within_targets(witness, 15);
                CHECK(std::find(near.begin(), near.end(), prey) != near.end());
            }
            const Vec2i before = c->pos(box);
            Command step;
            step.kind = CommandKind::Step;
            step.to = {before.x, before.y - 1};
            REQUIRE(settled(*c, c->submit(step)).ok);
            CHECK(c->pos(box) == Vec2i{before.x, before.y - 1});
            CHECK(c->pos(prey) == c->pos(box));
            CHECK(c->distance_tiles(box, prey) == 0);
            CHECK(c->occupant_at(spawn, 0) == 0);
            CHECK(c->occupant_at(before, box) == 0);
            to_actor(*c, prey);
            Command atk;
            atk.request = {prey, ActionKind::Attack, box, 0};
            auto swung = settled(*c, c->submit(atk));
            CHECK(swung.ok);
            ecs::Registry reg2;
            std::string err;
            auto again = replay(reg2, ch, lib, seed, c->log(), &err, c->surprised_at_start());
            REQUIRE_MESSAGE(again, err);
            CHECK(again->hash() == c->hash());
            CHECK(again->pos(prey) == again->pos(box));
        }
        REQUIRE(carried);
    }
}

TEST_CASE("T11: a toad's death frees the swallowed creature prone at the carried tile, kind 1") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    bool freed = false;
    for (uint64_t seed = 1; seed < 24 && !freed; ++seed) {
        auto ch = flat_chunk({{"srd51.monster.giant_toad", Vec2i{8, 8}}, {"srd51.monster.goblin", Vec2i{4, 8}},
                              {"srd51.monster.goblin", Vec2i{10, 8}}}, {}, 2);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        wake(*c);
        const CombatantId box = ch.spawns[0].stable_id, prey = ch.spawns[2].stable_id;
        to_actor(*c, box);
        if (rules5e::find(c->state(), box)->surprised) {
            end_turn(*c);
            to_actor(*c, box);
        }
        Command cast;
        cast.request = {box, ActionKind::CastSpell, prey};
        cast.request.spell_id = rules5e::spell_id_of("srd51.monster.giant_toad#swallow");
        if (!settled(*c, c->submit(cast)).ok || !swallowed_by(*c, prey, box)) continue;
        const Vec2i inside = c->pos(prey);
        for (auto& k : CombatTestAccess::state(*c).combatants)
            if (k.id == box) { k.hp = 0; k.dead = true; }
        end_turn(*c);
        REQUIRE_FALSE(swallowed_by(*c, prey, box));
        CHECK(rules5e::find(c->state(), prey)->has(Condition::Prone));
        REQUIRE(c->state().contain_exits.size() == 1);
        CHECK(c->state().contain_exits[0].kind == 1);
        CHECK(c->pos(prey) == inside);
        to_actor(*c, prey);
        Command step;
        step.kind = CommandKind::Step;
        step.to = {inside.x + 1, inside.y};
        auto walked = settled(*c, c->submit(step));
        REQUIRE(walked.ok);
        CHECK(c->log().back().step.request.move_ft != 30);
        freed = true;
    }
    REQUIRE(freed);
}

TEST_CASE("T7: kind 2 lands on the nearest tile within 10 ft, row scan on ties, else the nearest") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(sim_beast(), "monster"));
    REQUIRE_FALSE(lib.add(victim(), "monster"));
    bool placed = false;
    for (uint64_t seed = 1; seed < 40 && !placed; ++seed) {
        auto ch = flat_chunk({{"t.swallow", Vec2i{10, 10}}, {"t.swallow.victim", Vec2i{11, 10}},
                              {"t.swallow.victim", Vec2i{12, 10}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        wake(*c);
        const CombatantId box = ch.spawns[0].stable_id, a = ch.spawns[1].stable_id, b = ch.spawns[2].stable_id;
        REQUIRE(c->pos(box) == Vec2i{10, 10});
        for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == box) k.attacks_per_action = 2;
        to_actor(*c, box);
        const int swallow = index_of(*rules5e::find(c->state(), box), "swallow");
        REQUIRE(swallow >= 0);
        hold(*c, box, a);
        hold(*c, box, b);
        Command first;
        first.request = {box, ActionKind::Attack, a, uint8_t(swallow)};
        if (!settled(*c, c->submit(first)).ok || !swallowed_by(*c, a, box)) continue;
        Command second;
        second.request = {box, ActionKind::Attack, b, uint8_t(swallow)};
        if (!settled(*c, c->submit(second)).ok || !swallowed_by(*c, b, box)) continue;
        for (auto& e : CombatTestAccess::state(*c).effects)
            if (e.contain == 2 && e.target == a) { e.container_damage = 60; e.regurgitate_dc = 30; }
        end_turn(*c);
        REQUIRE_FALSE(swallowed_by(*c, a, box));
        REQUIRE_FALSE(swallowed_by(*c, b, box));
        CHECK(rules5e::find(c->state(), a)->has(Condition::Prone));
        CHECK(rules5e::find(c->state(), b)->has(Condition::Prone));
        CHECK(c->pos(a) == Vec2i{9, 9});
        CHECK(c->pos(b) == Vec2i{10, 9});
        CHECK(c->distance_tiles(box, a) <= 2);
        CHECK(c->distance_tiles(box, b) <= 2);
        REQUIRE(c->state().contain_exits.size() == 2);
        CHECK(c->state().contain_exits[0].kind == 2);
        CHECK(c->state().contain_exits[1].kind == 2);
        CHECK(c->state().contain_exits[0].creature == a);
        CHECK(c->state().contain_exits[1].creature == b);
        placed = true;
    }
    REQUIRE(placed);

    bool fallback = false;
    for (uint64_t seed = 1; seed < 40 && !fallback; ++seed) {
        auto ch = flat_chunk({{"t.swallow", Vec2i{10, 10}}, {"t.swallow.victim", Vec2i{10, 10}}},
                             {Vec2i{10, 10}, Vec2i{10, 20}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        wake(*c);
        const CombatantId box = ch.spawns[0].stable_id, prey = ch.spawns[1].stable_id;
        to_actor(*c, box);
        const int swallow = index_of(*rules5e::find(c->state(), box), "swallow");
        REQUIRE(swallow >= 0);
        hold(*c, box, prey);
        Command atk;
        atk.request = {box, ActionKind::Attack, prey, uint8_t(swallow)};
        if (!settled(*c, c->submit(atk)).ok || !swallowed_by(*c, prey, box)) continue;
        for (auto& e : CombatTestAccess::state(*c).effects)
            if (e.contain == 2) { e.container_damage = 60; e.regurgitate_dc = 30; }
        end_turn(*c);
        REQUIRE_FALSE(swallowed_by(*c, prey, box));
        CHECK(c->pos(prey) == Vec2i{10, 20});
        CHECK(c->distance_tiles(box, prey) > 2);
        fallback = true;
    }
    REQUIRE(fallback);
}

TEST_CASE("T8: a corpse exit spends exit_ft, lands prone on the chosen tile, and a short budget needs a Dash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(sim_beast(), "monster"));
    REQUIRE_FALSE(lib.add(victim(), "monster"));
    bool exited = false;
    for (uint64_t seed = 1; seed < 40 && !exited; ++seed) {
        auto ch = flat_chunk({{"t.swallow", Vec2i{10, 10}}, {"t.swallow.victim", Vec2i{6, 10}},
                              {"t.swallow.victim", Vec2i{11, 10}}}, {}, 2);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        wake(*c);
        const CombatantId box = ch.spawns[0].stable_id, prey = ch.spawns[2].stable_id;
        to_actor(*c, box);
        const int swallow = index_of(*rules5e::find(c->state(), box), "swallow");
        REQUIRE(swallow >= 0);
        hold(*c, box, prey);
        Command atk;
        atk.request = {box, ActionKind::Attack, prey, uint8_t(swallow)};
        if (!settled(*c, c->submit(atk)).ok || !swallowed_by(*c, prey, box)) continue;
        CHECK(c->pos(prey) == c->pos(box));
        CHECK(c->distance_tiles(box, prey) == 0);
        for (auto& k : CombatTestAccess::state(*c).combatants)
            if (k.id == box) { k.hp = 0; k.dead = true; }
        end_turn(*c);
        REQUIRE(swallowed_by(*c, prey, box));
        CHECK(rules5e::find(c->state(), prey)->has(Condition::Blinded));
        CHECK_FALSE(rules5e::find(c->state(), prey)->has(Condition::Restrained));
        CHECK_FALSE(rules5e::find(c->state(), prey)->has(Condition::Prone));
        CHECK(c->state().contain_exits.empty());
        to_actor(*c, prey);
        for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) k.speed_ft = 25;
        CombatTestAccess::state(*c).budget.movement_ft = 25;
        const Vec2i at = c->pos(box);
        Command far;
        far.kind = CommandKind::Step;
        far.to = {at.x + 2, at.y};
        auto refused = c->submit(far);
        CHECK_FALSE(refused.ok);
        CHECK(refused.error == "inside a corpse: exit with exit_ft of movement");
        CHECK(swallowed_by(*c, prey, box));
        Command near;
        near.kind = CommandKind::Step;
        near.to = {at.x + 1, at.y};
        auto poor = c->submit(near);
        CHECK_FALSE(poor.ok);
        CHECK(poor.error == "not enough movement");
        CHECK(swallowed_by(*c, prey, box));
        CHECK_FALSE(rules5e::find(c->state(), prey)->has(Condition::Prone));
        Command dash;
        dash.request = {prey, ActionKind::Dash};
        REQUIRE(settled(*c, c->submit(dash)).ok);
        auto out = settled(*c, c->submit(near));
        REQUIRE(out.ok);
        CHECK(c->log().back().step.request.move_ft == 30);
        REQUIRE(c->log().back().to);
        CHECK(*c->log().back().to == Vec2i{at.x + 1, at.y});
        CHECK(c->pos(prey) == Vec2i{at.x + 1, at.y});
        CHECK_FALSE(swallowed_by(*c, prey, box));
        CHECK_FALSE(rules5e::find(c->state(), prey)->has(Condition::Blinded));
        CHECK(rules5e::find(c->state(), prey)->has(Condition::Prone));
        REQUIRE(c->state().contain_exits.size() == 1);
        CHECK(c->state().contain_exits[0] == rules5e::ContainExit{box, prey, 4});
        exited = true;
    }
    REQUIRE(exited);
}

TEST_CASE("T12: multiattack uses Swallow when the bite target is grappled, Large or smaller, and alive") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(ai_monster(), "monster"));
    REQUIRE_FALSE(lib.add(victim(), "monster"));
    auto ch = flat_chunk({{"t.swallow.ai", Vec2i{4, 4}}, {"t.swallow.victim", Vec2i{5, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    wake(*c);
    const CombatantId self = ch.spawns[0].stable_id, prey = ch.spawns[1].stable_id;
    to_actor(*c, self);
    const auto& me = *rules5e::find(c->state(), self);
    const int bite = index_of(me, "bite"), swallow = index_of(me, "swallow");
    REQUIRE(bite >= 0);
    REQUIRE(swallow >= 0);
    Command plain = next_command(*c, prey);
    CHECK(plain.request.kind == ActionKind::Attack);
    CHECK(plain.request.attack_index == uint8_t(bite));
    CHECK(plain.request.multiattack == 1);
    hold(*c, self, prey);
    // Prone does not block Swallow. The held target is still the bite slot's alternate.
    for (auto& k : CombatTestAccess::state(*c).combatants)
        if (k.id == prey) k.conditions |= rules5e::bit(Condition::Prone);
    Command sw = next_command(*c, prey);
    CHECK(sw.request.kind == ActionKind::Attack);
    CHECK(sw.request.special == 0);
    CHECK(sw.request.attack_index == uint8_t(swallow));
    CHECK(sw.request.multiattack == 1);
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) k.size = rules5e::Size::Huge;
    Command big = next_command(*c, prey);
    CHECK(big.request.attack_index == uint8_t(bite));
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) k.size = rules5e::Size::Medium;
    CombatTestAccess::state(*c).legendary_window.x = self;
    CombatTestAccess::state(*c).legendary_window.candidate = self;
    Command pass = next_command(*c, prey);
    CHECK(pass.request.kind == ActionKind::LegendaryAction);
    CHECK(pass.request.feature_id == 0);
    CHECK(pass.request.actor == self);
    CombatTestAccess::state(*c).legendary_window = {};
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) { k.hp = 0; k.dead = true; }
    Command done = next_command(*c, prey);
    CHECK(done.request.kind == ActionKind::EndTurn);
}

TEST_CASE("T12: a grappled not-prone legal target is Swallowed, and a shove remains when Swallow is not legal") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(ai_monster(), "monster"));
    REQUIRE_FALSE(lib.add(victim(), "monster"));
    auto ch = flat_chunk({{"t.swallow.ai", Vec2i{4, 4}}, {"t.swallow.victim", Vec2i{5, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    wake(*c);
    const CombatantId self = ch.spawns[0].stable_id, prey = ch.spawns[1].stable_id;
    to_actor(*c, self);
    const auto& me = *rules5e::find(c->state(), self);
    const int bite = index_of(me, "bite"), swallow = index_of(me, "swallow");
    REQUIRE(bite >= 0);
    REQUIRE(swallow >= 0);
    hold(*c, self, prey);
    REQUIRE_FALSE(rules5e::find(c->state(), prey)->has(Condition::Prone));
    Command sw = next_command(*c, prey);
    CHECK(sw.request.kind == ActionKind::Attack);
    CHECK(sw.request.special == 0);
    CHECK(sw.request.attack_index == uint8_t(swallow));
    CHECK(sw.request.multiattack == 1);
    CHECK(sw.request.target == prey);
    // Too large for swallow_slot: the held, not-prone creature is still shoved prone.
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) k.size = rules5e::Size::Huge;
    REQUIRE_FALSE(rules5e::find(c->state(), prey)->has(Condition::Prone));
    Command huge = next_command(*c, prey);
    CHECK(huge.request.kind == ActionKind::Attack);
    CHECK(huge.request.special == 2);
    CHECK(huge.request.target == prey);
    // No alternate, so Swallow is not a choice. The same shove stands.
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == prey) k.size = rules5e::Size::Medium;
    for (auto& k : CombatTestAccess::state(*c).combatants)
        if (k.id == self)
            for (auto& seq : k.multiattacks) seq.alternates.clear();
    REQUIRE_FALSE(rules5e::find(c->state(), prey)->has(Condition::Prone));
    Command shoved = next_command(*c, prey);
    CHECK(shoved.request.kind == ActionKind::Attack);
    CHECK(shoved.request.special == 2);
    CHECK(shoved.request.target == prey);
    CHECK(shoved.request.attack_index != uint8_t(swallow));
}
