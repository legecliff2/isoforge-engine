// Row 12, sim side: spell and feature apply_modifier content compiles (SpellDef::modifier / feature_modifiers rows),
// used features put timed modifiers on the board (AlliesWithin30 through the area_targets hook), Combat::hash folds
// them under 0xA2 only then, and the AI uses a self modifier feature.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::ModStat;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json spell(Json effect) {
    Json s = load_json("content/packs/srd51.core/spells/cure_wounds.json");
    s["effects"] = Json::array({effect});
    s.erase("scaling");
    return s;
}
Json leader(const std::string& id, std::vector<Json> modifiers, const char* cost = "bonus_action") {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    c["hit_points"]["average"] = 2000;
    c["features"].push_back({{"id", "inspire"}, {"name", "Inspire"}, {"description", "x"},
                             {"effect", {{"kind", "apply_modifier"}, {"cost", cost}, {"modifiers", Json(modifiers)}}}});
    return c;
}
Json dur(int n) { return {{"kind", "rounds"}, {"n", n}}; }
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
}  // namespace

TEST_CASE("modifier content: spell apply_modifier compiles into SpellDef::modifier") {
    Json bless = {{"kind", "apply_modifier"}, {"modifier", {{"stat", "attack_roll"}, {"die", "1d4"}, {"duration", {{"kind", "concentration"}}}}}};
    auto r = rules5e_content::spell_from_json(spell(bless));
    REQUIRE_MESSAGE(r.def, r.error);
    CHECK(r.def->kind == rules5e::SpellEffectKind::ApplyModifier);
    CHECK(r.def->modifier.stat == ModStat::AttackRoll);
    CHECK(r.def->modifier.die.sides == 4);
    CHECK(r.def->modifier.concentration);
    CHECK(r.def->modifier.id == rules5e::spell_id_of(r.def->content_id + "#modifier"));
    Json bane = {{"kind", "apply_modifier"}, {"modifier", {{"stat", "save"}, {"die", "-1d4"}, {"duration", dur(3)}}}};
    r = rules5e_content::spell_from_json(spell(bane));
    REQUIRE_MESSAGE(r.def, r.error);
    CHECK(r.def->modifier.die_mult == -1);
    CHECK(r.def->modifier.expiry == 1);
    CHECK(r.def->modifier.rounds == 3);
    for (Json bad : std::vector<Json>{
             {{"kind", "apply_modifier"}, {"modifier", {{"stat", "damage"}, {"bonus_table", Json::object()}, {"duration", dur(1)}}}},
             {{"kind", "apply_modifier"}, {"modifier", {{"stat", "speed"}, {"duration", dur(1)}}}},
             {{"kind", "apply_modifier"}, {"modifier", {{"stat", "resistance"}, {"duration", dur(1)}}}},
             {{"kind", "apply_modifier"}, {"modifier", {{"stat", "ac"}, {"bonus", 2}}}}}) {
        CHECK_FALSE(rules5e_content::spell_from_json(spell(bad)).def);
    }
}

TEST_CASE("modifier content: features compile (bonus_table resolves by level) and load errors") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json rage = {{"stat", "damage"}, {"applies_to", "self"}, {"duration", dur(10)},
                 {"bonus_table", {{"class", "fighter"}, {"by_level", Json::array({{{"level", 1}, {"value", 2}}, {{"level", 9}, {"value", 3}}})}}}};
    Json res = {{"stat", "resistance"}, {"damage_type", "slashing"}, {"applies_to", "self"}, {"duration", dur(10)}};
    REQUIRE_FALSE(lib.add(leader("t.rager", {rage, res}), "character"));
    const auto* a = lib.find("t.rager");
    REQUIRE(a);
    REQUIRE(a->feature_modifiers.size() == 1);
    const auto& rows = a->feature_modifiers.begin()->second;
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].stat == ModStat::Damage);
    CHECK(rows[0].bonus == 2);   // fighter level 1
    CHECK(rows[1].stat == ModStat::Resistance);
    CHECK(rows[0].source == rows[1].source);
    CHECK(rows[0].id != rows[1].id);
    CHECK(a->active.size() >= 1);
    // usage: listed, not offered
    Json usage = leader("t.pooled", {res});
    usage["features"].back()["effect"]["usage"] = {{"kind", "pool"}, {"pool", "rage"}, {"cost", 1}};
    REQUIRE_FALSE(lib.add(usage, "character"));
    CHECK(lib.find("t.pooled")->feature_modifiers.empty());
    // load errors
    Json bad = res;
    bad.erase("damage_type");
    REQUIRE(lib.add(leader("t.bad1", {bad}), "character"));
    Json tbl = res;
    tbl["bonus_table"] = rage["bonus_table"];
    REQUIRE(lib.add(leader("t.bad2", {tbl}), "character"));
    REQUIRE(lib.add(leader("t.bad3", {res}, "reaction"), "character"));
    // a file that declared < 4 lists it inert
    REQUIRE_FALSE(lib.add(leader("t.old", {res}), "character", "", 3));
    CHECK(lib.find("t.old")->feature_modifiers.empty());
}

TEST_CASE("modifiers in a fight: AlliesWithin30 uses the area hook; 0xA2 folds only with modifiers; AI uses a self feature") {
    Json inspire = {{"stat", "attack_roll"}, {"bonus", 2}, {"applies_to", "allies_within_30"}, {"duration", dur(5)}};
    Json self = {{"stat", "ac"}, {"bonus", 1}, {"applies_to", "self"}, {"duration", dur(5)}};
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json ally = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    ally["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(ally, "character"));
    REQUIRE_FALSE(lib.add(leader("t.bard", {inspire}), "character"));
    REQUIRE_FALSE(lib.add(leader("t.self", {self}), "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(gob, "monster"));
    auto ch = arena({{"t.bard", Vec2i{5, 5}}, {kFighter, Vec2i{8, 5}}, {"srd51.monster.goblin", Vec2i{9, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId bard = ch.spawns[0].stable_id, fi = ch.spawns[1].stable_id;
    CHECK_FALSE(rules5e::has_modifier_state(c->state()));
    const uint64_t before = c->hash();
    for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != bard; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c->state()) == bard);
    const Archetype* arch = c->archetype(bard);
    REQUIRE(arch);
    REQUIRE(arch->feature_modifiers.size() == 1);
    Command u;
    u.request = {bard, ActionKind::UseFeature};
    u.request.feature_id = arch->feature_modifiers.begin()->first;
    auto r = c->submit(u);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(c->state().modifiers.size() == 1);   // the ally, not the caster, not the goblin
    CHECK(c->state().modifiers[0].target == fi);
    CHECK(c->state().modifiers[0].caster == bard);
    CHECK(rules5e::has_modifier_state(c->state()));
    CHECK(c->hash() != before);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 4, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    // AI: a self-only modifier feature is used when a hostile is close, then not again while it lasts
    auto ch2 = arena({{"t.self", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg3;
    auto c2 = Combat::create(reg3, ch2, lib, 4);
    REQUIRE(c2);
    const CombatantId me = ch2.spawns[0].stable_id, foe = ch2.spawns[1].stable_id;
    for (int i = 0; i < 8 && rules5e::current_actor(c2->state()) != me; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c2->state()), ActionKind::EndTurn};
        REQUIRE(c2->submit(e).ok);
    }
    const Command first = next_command(*c2, foe);
    CHECK(first.request.kind == ActionKind::UseFeature);
    REQUIRE(c2->submit(first).ok);
    const Command second = next_command(*c2, foe);
    CHECK(second.request.kind != ActionKind::UseFeature);
}

// ---- Row 12 follow-ups: consume, speed_sub, ac_floor, per_turn, when, rider modifiers ----
TEST_CASE("modifier follow-ups: new properties compile for spells and features; rider effects") {
    auto def_of = [](Json m) {
        auto r = rules5e_content::spell_from_json(spell({{"kind", "apply_modifier"}, {"modifier", m}}));
        REQUIRE_MESSAGE(r.def, r.error);
        return *r.def;
    };
    auto d = def_of({{"stat", "attack_roll"}, {"mode", "advantage"}, {"consume", "next_attack"}, {"duration", dur(1)}});   // True Strike
    CHECK(d.modifier.consume == 1);
    d = def_of({{"stat", "attacks_against"}, {"mode", "advantage"}, {"consume", "next_attack_against"}, {"duration", {{"kind", "until_start_of_caster_turn"}}}});
    CHECK(d.modifier.consume == 2);
    CHECK(d.modifier.until_caster_turn);
    d = def_of({{"stat", "attack_roll"}, {"mode", "disadvantage"}, {"consume", "until_damaged"}, {"duration", dur(1)}});
    CHECK(d.modifier.consume == 3);
    d = def_of({{"stat", "speed"}, {"speed_scale", {{"num", 1}, {"den", 1}}}, {"speed_sub", 10}, {"duration", dur(1)}});   // Ray of Frost
    CHECK(d.modifier.speed_sub == 10);
    d = def_of({{"stat", "ac"}, {"ac_floor", 13}, {"ac_floor_dex", true}, {"duration", dur(480)}});   // Mage Armor
    CHECK(d.modifier.ac_floor == 13);
    CHECK(d.modifier.ac_floor_dex);
    d = def_of({{"stat", "temp_hp"}, {"bonus", 10}, {"per_turn", true}, {"duration", dur(10)}});   // Heroism
    CHECK(d.modifier.per_turn);
    d = def_of({{"stat", "damage"}, {"bonus", 2}, {"duration", dur(10)},
                {"when", Json::array({{{"kind", "weapon"}, {"is", "melee"}}, {{"kind", "not_crit"}}})}});
    REQUIRE(d.modifier.when.size() == 2);
    CHECK(d.modifier.when[0].kind == rules5e::PredicateKind::Weapon);
    CHECK(d.modifier.when[1].kind == rules5e::PredicateKind::NotCrit);
    for (Json bad : std::vector<Json>{
             {{"stat", "attack_roll"}, {"mode", "advantage"}, {"consume", "sometimes"}, {"duration", dur(1)}},
             {{"stat", "save"}, {"bonus", 1}, {"speed_sub", 5}, {"duration", dur(1)}},
             {{"stat", "attack_roll"}, {"bonus", 1}, {"ac_floor", 13}, {"duration", dur(1)}},
             {{"stat", "attack_roll"}, {"bonus", 1}, {"per_turn", true}, {"duration", dur(1)}},
             {{"stat", "save"}, {"bonus", 1}, {"when", Json::array({{{"kind", "not_crit"}}})}, {"duration", dur(1)}},
             {{"stat", "attack_roll"}, {"bonus", 1}, {"when", Json::array({{{"kind", "vibes"}}})}, {"duration", dur(1)}}}) {
        CHECK_FALSE(rules5e_content::spell_from_json(spell({{"kind", "apply_modifier"}, {"modifier", bad}})).def);
    }
    // rider (Guiding Bolt): a spell attack, then apply_modifier on the affected only, with its own modifier id
    Json gb = spell({{"kind", "spell_attack"}, {"melee", false}, {"damage", {{"dice", "4d6"}, {"type", "radiant"}}}});
    gb["effects"].push_back({{"kind", "apply_modifier"}, {"applies_to", "affected"},
                             {"modifier", {{"stat", "attacks_against"}, {"mode", "advantage"}, {"consume", "next_attack_against"},
                                           {"duration", {{"kind", "until_start_of_caster_turn"}}}}}});
    auto r = rules5e_content::spell_from_json(gb);
    REQUIRE_MESSAGE(r.def, r.error);
    REQUIRE(r.def->extra_effects.size() == 1);
    CHECK(r.def->extra_effects[0].kind == rules5e::SpellEffectKind::ApplyModifier);
    CHECK(r.def->extra_effects[0].affected_only);
    CHECK(r.def->extra_effects[0].modifier.consume == 2);
    CHECK(r.def->extra_effects[0].modifier.id == rules5e::spell_id_of(r.def->content_id + "#modifier1"));
    // features take the same properties; a consume=1 self modifier is spent by the next attack (ModifierExpired reason 3)
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json strike = {{"stat", "attack_roll"}, {"mode", "advantage"}, {"consume", "next_attack"}, {"applies_to", "self"}, {"duration", dur(5)}};
    REQUIRE_FALSE(lib.add(leader("t.strike", {strike}), "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(gob, "monster"));
    auto ch = arena({{"t.strike", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
    for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != me; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    Command u;
    u.request = {me, ActionKind::UseFeature};
    u.request.feature_id = c->archetype(me)->feature_modifiers.begin()->first;
    REQUIRE(c->submit(u).ok);
    REQUIRE(c->state().modifiers.size() == 1);
    CHECK(c->state().modifiers[0].spec.consume == 1);
    Command a;
    a.request = {me, ActionKind::Attack, foe};
    a.request.attack_index = 0;
    REQUIRE(c->submit(a).ok);
    c->settle(nullptr);
    CHECK(c->state().modifiers.empty());
    bool spent = false;
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::ModifierExpired && e.rules.b == 3) spent = true;
    CHECK(spent);
}
