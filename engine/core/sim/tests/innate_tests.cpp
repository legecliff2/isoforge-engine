// Innate spellcasting (VERSION_BUMP_BATCH row 8, sim side): monster@4 / character@4 `spellcasting.innate`, `dc` and
// `attack_bonus` fill Combatant::innate and the overrides; innate spells are not in spells_known; Combat::hash folds
// the innate state under 0xA4 only when it exists, log_hash folds `ActionRequest::innate` only when set.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
const char* kSpellFiles[] = {"fire_bolt", "magic_missile", "hold_person"};

Json mage(const std::string& id, Json spellcasting, const char* schema = "monster@4") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["hit_points"]["average"] = 60;
    m["spellcasting"] = std::move(spellcasting);
    return m;
}
Json block(Json extra = Json::object()) {
    Json sc = {{"ability", "int"}, {"caster_level", 5}, {"dc", 15}, {"attack_bonus", 7},
               {"innate", {{"ability", "cha"}, {"at_will", Json::array({"srd51.spell.fire_bolt"})},
                           {"per_day", {{"2", Json::array({"srd51.spell.magic_missile"})}}},
                           {"recharge", Json::array({{{"spell", "srd51.spell.hold_person"}, {"min", 5}}})}}}};
    for (auto& [k, v] : extra.items()) sc[k] = v;
    return sc;
}
ContentLibrary lib_with(const std::vector<Json>& docs) {
    ContentLibrary lib;
    for (const char* n : kSpellFiles) REQUIRE_FALSE(lib.add_spell(load_json(std::string("content/packs/srd51.core/spells/") + n + ".json")));
    REQUIRE_FALSE(lib.add(load_json("content/packs/sample.vertical_slice/characters/fighter_1.json"), "character"));
    for (const Json& d : docs) REQUIRE_FALSE(lib.add(d, "monster"));
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
    for (int i = 0; i < 6 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
Command innate_cast(CombatantId who, CombatantId target, const char* spell, uint8_t level) {
    Command c;
    c.request = {who, ActionKind::CastSpell, target};
    c.request.spell_id = spell_id_of(spell);
    c.request.spell_level = level;
    c.request.innate = true;
    return c;
}
} // namespace

TEST_CASE("innate content: rows, overrides, ability and components fill the base combatant; spells_known is untouched") {
    auto lib = lib_with({mage("t.mage", block())});
    const auto& b = lib.find("t.mage")->base;
    CHECK(b.spell_dc_override == 15);
    CHECK(b.spell_attack_override == 7);
    CHECK(b.spells_known.empty());   // an innate-only caster has no `spells`, and innate spells are never added
    REQUIRE(b.innate.size() == 3);
    const auto& at_will = b.innate[0];
    CHECK(at_will.spell_id == spell_id_of("srd51.spell.fire_bolt"));
    CHECK(at_will.uses_max == 0);
    CHECK(at_will.at_will());
    CHECK(at_will.has_ability);
    CHECK(at_will.ability == rules5e::Ability::Cha);
    CHECK_FALSE(at_will.as_listed);
    const auto& per_day = b.innate[1];
    CHECK(per_day.spell_id == spell_id_of("srd51.spell.magic_missile"));
    CHECK(per_day.uses_max == 2);
    CHECK(per_day.uses_left == 2);
    const auto& rech = b.innate[2];
    CHECK(rech.recharge_min == 5);
    CHECK(rech.uses_left == 1);
    CHECK(rech.uses_max == 1);
    CHECK(lib.find("t.mage")->innate_spell_ids.size() == 3);

    // `spells` and innate together: spells_known holds only the slot spells; components as_listed is carried.
    Json sc = block({{"spells", Json::array({"srd51.spell.hold_person"})}});
    sc["innate"]["components"] = "as_listed";
    sc["innate"].erase("per_day");
    sc["innate"].erase("recharge");
    auto lib2 = lib_with({mage("t.both", sc)});
    const auto& b2 = lib2.find("t.both")->base;
    REQUIRE(b2.spells_known.size() == 1);
    CHECK(b2.spells_known[0] == spell_id_of("srd51.spell.hold_person"));
    REQUIRE(b2.innate.size() == 1);
    CHECK(b2.innate[0].as_listed);
}

TEST_CASE("innate content: a file that declared < 4 cannot carry the block; bad blocks are load errors") {
    ContentLibrary lib;
    Json sc3 = block({{"spells", Json::array({"srd51.spell.fire_bolt"})}});
    REQUIRE_FALSE(lib.add(mage("t.old", sc3, "monster@3"), "monster", "", 3));
    const auto& b = lib.find("t.old")->base;
    CHECK(b.innate.empty());
    CHECK(b.spell_dc_override == 0);
    CHECK(b.spell_attack_override == 0);
    CHECK(b.spells_known.size() == 1);

    auto bad = [&](Json innate_patch, const char* what) {
        Json sc = block();
        for (auto& [k, v] : innate_patch.items()) sc["innate"][k] = v;
        const auto e = lib.add(mage("t.bad", sc), "monster");
        CAPTURE(what);
        REQUIRE(e);
        CHECK(e->find("t.bad") != std::string::npos);
    };
    bad({{"ability", "luck"}}, "ability");
    bad({{"at_will", "srd51.spell.fire_bolt"}}, "at_will not an array");
    bad({{"at_will", Json::array({7})}}, "non-string id");
    bad({{"per_day", {{"0", Json::array({"srd51.spell.fire_bolt"})}}}}, "per_day key");
    bad({{"per_day", {{"10", Json::array({"srd51.spell.fire_bolt"})}}}}, "per_day key");
    bad({{"recharge", Json::array({{{"spell", "srd51.spell.hold_person"}, {"min", 1}}})}}, "recharge min");
    bad({{"at_will", Json::array({"srd51.spell.fire_bolt", "srd51.spell.fire_bolt"})}}, "duplicate");
    Json s = block();
    s["dc"] = "high";
    CHECK(lib.add(mage("t.bad2", s), "monster"));
}

TEST_CASE("innate casting in a fight: uses, overrides, hash and log_hash folds, replay") {
    auto lib = lib_with({mage("t.mage", block())});
    auto ch = arena({{"t.mage", Vec2i{5, 5}}, {kFighter, Vec2i{8, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId m = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
    const auto* me = rules5e::find(c->state(), m);
    REQUIRE(me);
    CHECK(me->innate.size() == 3);
    CHECK(me->spell_dc_override == 15);
    CHECK(rules5e::has_innate_state(c->state()));
    CHECK(rules5e::find(c->state(), f)->innate.empty());
    advance_to(*c, m);
    const uint64_t log0 = c->log_hash();
    auto r = c->submit(innate_cast(m, f, "srd51.spell.magic_missile", 1));
    REQUIRE_MESSAGE(r.ok, r.error);
    r = c->settle(nullptr);
    CHECK(rules5e::find(c->state(), m)->innate[1].uses_left == 1);
    CHECK(c->log_hash() != log0);
    CHECK(c->log().back().step.request.innate);
    // a creature without the row, and an upcast, are refused by rules and surface through submit
    advance_to(*c, f);
    advance_to(*c, m);
    CHECK_FALSE(c->submit(innate_cast(m, f, "srd51.spell.magic_missile", 2)).ok);   // innate spells cannot be upcast
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 4, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
}

TEST_CASE("innate state is hashed only when it exists: an archetype without it hashes as before") {
    // The same goblin with and without an (empty-innate) spellcasting block: no innate rows, no overrides, no fold.
    Json plain = mage("t.plain", Json{{"ability", "int"}, {"caster_level", 5}, {"spells", Json::array({"srd51.spell.fire_bolt"})}});
    Json with = mage("t.with", block());
    auto lib = lib_with({plain, with});
    auto mk = [&](const char* who) {
        auto ch = arena({{who, Vec2i{5, 5}}, {kFighter, Vec2i{8, 5}}});
        auto reg = std::make_unique<ecs::Registry>();
        auto c = Combat::create(*reg, ch, lib, 9);
        REQUIRE(c);
        return std::make_pair(std::move(reg), std::move(*c));
    };
    auto a = mk("t.plain");
    CHECK_FALSE(rules5e::has_innate_state(a.second.state()));
    auto b = mk("t.with");
    CHECK(rules5e::has_innate_state(b.second.state()));
    CHECK(a.second.hash() != b.second.hash());   // 0xA4 section present only for the innate caster
}
