// Mephit innate casting end to end (Bestiary v0.1): SRD 5.1 "Innate Spellcasting (1/Day)", Charisma, no material
// components. Content loads -> legal_actions lists the innate cast -> the built-in AI picks it -> the use is spent.
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
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// listed: rules legal_actions lists the cast; ai: the built-in AI casts it at a hostile. Blur (range self, modifier)
// is not listed by rules for any caster; Fog Cloud (zone) and Heat Metal (object choice) are listed but left to the caller.
struct Mephit { const char* id; const char* spell; int dc; bool listed; bool ai; };
const Mephit kMephits[] = {{"t.dust_mephit", "sleep", 10, true, true}, {"t.ice_mephit", "fog_cloud", 0, true, false},
                           {"t.magma_mephit", "heat_metal", 10, true, false}, {"t.steam_mephit", "blur", 11, false, false}};
Json mephit(const Mephit& m) {
    Json d = load_json("content/packs/srd51.core/monsters/goblin.json");
    d["$schema"] = "monster@4";
    d["id"] = m.id;
    Json inn = {{"ability", "cha"}, {"per_day", {{"1", Json::array({std::string("srd51.spell.") + m.spell})}}}};
    Json sc = {{"ability", "cha"}, {"caster_level", 1}, {"innate", inn}};
    if (m.dc) sc["dc"] = m.dc;
    d["spellcasting"] = sc;
    return d;
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
}  // namespace

TEST_CASE("mephit innate 1/day: loads, is legal, the AI casts it, the use is spent") {
    for (const Mephit& m : kMephits) {
        CAPTURE(m.id);
        ContentLibrary lib;
        REQUIRE_FALSE(lib.add_spell(load_json(std::string("content/packs/srd51.core/spells/") + m.spell + ".json")));
        REQUIRE_FALSE(lib.add(load_json("content/packs/sample.vertical_slice/characters/fighter_1.json"), "character"));
        auto err = lib.add(mephit(m), "monster");
        REQUIRE_MESSAGE(!err, *err);
        auto ch = arena({{m.id, Vec2i{5, 5}}, {"srd51.character.fighter_1", Vec2i{7, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
        for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != me; ++i) {
            Command e; e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == me);
        const auto* a = rules5e::find(c->state(), me);
        REQUIRE(a->innate.size() == 1);
        CHECK(a->innate[0].uses_left == 1);
        CHECK(a->spell_dc_override == m.dc);
        bool legal = false;
        for (const auto& q : c->legal_actions())
            legal = legal || (q.kind == ActionKind::CastSpell && q.innate && q.spell_id == spell_id_of(std::string("srd51.spell.") + m.spell));
        CHECK(legal == m.listed);
        const Command cmd = next_command(*c, foe);
        const bool ai_casts = cmd.request.kind == ActionKind::CastSpell && cmd.request.innate;
        CHECK(ai_casts == m.ai);
        if (!m.ai) continue;
        CHECK(cmd.request.spell_id == spell_id_of(std::string("srd51.spell.") + m.spell));
        auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        c->settle(nullptr);
        CHECK(rules5e::find(c->state(), me)->innate[0].uses_left == 0);
        CHECK(c->log().back().step.request.innate);
        // spent: no longer listed, and the AI falls back to its attack plan
        bool again = false;
        for (const auto& q : c->legal_actions()) again = again || (q.kind == ActionKind::CastSpell && q.innate);
        CHECK_FALSE(again);
        ecs::Registry reg2;
        std::string rerr;
        auto rep = replay(reg2, ch, lib, 4, c->log(), &rerr);
        REQUIRE_MESSAGE(rep, rerr);
        CHECK(rep->hash() == c->hash());
    }
}
