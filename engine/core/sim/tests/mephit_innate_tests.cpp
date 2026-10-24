// Mephit innate casting end to end (Bestiary v0.1): SRD 5.1 "Innate Spellcasting (1/Day)", Charisma, no material
// components. Content loads -> legal_actions lists the innate cast -> the built-in AI picks it -> the use is spent.
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
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// listed: rules legal_actions lists the cast; ai: the built-in AI picks it. With fix/rules-legal-actions-spells, Blur (range
// self, modifier) is listed with target = the caster and Heat Metal once per eligible metal item (equip_item set); the AI
// picks both. Fog Cloud (zone) is listed but stays the caller's decision.
struct Mephit { const char* id; const char* spell; int dc; bool listed; bool ai; };
const Mephit kMephits[] = {{"t.dust_mephit", "sleep", 10, true, true}, {"t.ice_mephit", "fog_cloud", 0, true, false},
                           {"t.magma_mephit", "heat_metal", 10, true, true}, {"t.steam_mephit", "blur", 11, true, true}};
constexpr const char* kKitted = "srd51.character.fighter_level_1";   // chain mail, shield, longsword: metal
constexpr const char* kBare = "t.club_wizard";                       // holds a club (wood), no armor: no metal
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
// The sample set (srd51.core items and spells) plus the four mephits and a wizard holding a club (material wood) and wearing no armor.
ContentLibrary& library() {
    static ContentLibrary l;
    static bool ready = false;
    if (ready) return l;
    ready = true;
    REQUIRE(l.load(sample_set()).empty());
    for (const Mephit& m : kMephits) {
        auto err = l.add(mephit(m), "monster");
        REQUIRE_MESSAGE(!err, *err);
    }
    Json w = load_json("content/packs/srd51.core/characters/wizard_level_1.json");
    w["id"] = kBare;
    w["derive"] = false;
    w["equipment"] = Json::array({Json{{"item", "srd51.item.club"}, {"equipped", true}, {"hand", "main"}}, "component_pouch", "spellbook"});
    Json acts = Json::array();
    for (const auto& x : w["actions"]) if (!x.contains("weapon")) acts.push_back(x);
    w["actions"] = acts;
    auto err = l.add(w, "character");
    REQUIRE_MESSAGE(!err, *err);
    return l;
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
bool listed_cast(const Combat& c, const std::string& spell, CombatantId target) {
    for (const auto& q : c.legal_actions())
        if (q.kind == ActionKind::CastSpell && q.innate && q.spell_id == spell_id_of("srd51.spell." + spell) && q.target == target) return true;
    return false;
}
void to_turn(Combat& c, CombatantId me) {
    for (int i = 0; i < 4 && rules5e::current_actor(c.state()) != me; ++i) {
        Command e; e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == me);
}
}  // namespace

TEST_CASE("mephit innate 1/day: loads, is legal, the AI casts it, the use is spent, replay matches") {
    for (const Mephit& m : kMephits) {
        CAPTURE(m.id);
        const std::string spell = m.spell;
        auto ch = arena({{m.id, Vec2i{5, 5}}, {kKitted, Vec2i{7, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, library(), 4, CombatOptions{true});
        REQUIRE(c);
        const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
        to_turn(*c, me);
        const auto* a = rules5e::find(c->state(), me);
        REQUIRE(a->innate.size() == 1);
        CHECK(a->innate[0].uses_left == 1);
        CHECK(a->spell_dc_override == m.dc);
        bool legal = false;
        for (const auto& q : c->legal_actions())
            legal = legal || (q.kind == ActionKind::CastSpell && q.innate && q.spell_id == spell_id_of("srd51.spell." + spell));
        CHECK(legal == m.listed);
        if (spell == "blur") CHECK(listed_cast(*c, spell, me));       // self range: target = the caster
        if (spell == "heat_metal") CHECK(listed_cast(*c, spell, foe));
        const Command cmd = next_command(*c, foe);
        const bool ai_casts = cmd.request.kind == ActionKind::CastSpell && cmd.request.innate;
        CHECK(ai_casts == m.ai);
        if (!m.ai) continue;
        CHECK(cmd.request.spell_id == spell_id_of("srd51.spell." + spell));
        if (spell == "blur") { CHECK(cmd.request.target == me); CHECK(cmd.request.targets == std::vector<CombatantId>{me}); }
        if (spell == "heat_metal") { CHECK(cmd.request.target == foe); CHECK(cmd.request.equip_item != 0); }
        auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        c->settle(nullptr);
        CHECK(rules5e::find(c->state(), me)->innate[0].uses_left == 0);
        CHECK(c->log().back().step.request.innate);
        if (spell == "blur") {
            bool on_self = false;
            for (const auto& mod : c->state().modifiers)
                on_self = on_self || (mod.target == me && mod.spec.source == spell_id_of("srd51.spell.blur"));
            CHECK(on_self);
        }
        // spent: no longer listed, and the AI falls back to its attack plan
        bool again = false;
        for (const auto& q : c->legal_actions()) again = again || (q.kind == ActionKind::CastSpell && q.innate);
        CHECK_FALSE(again);
        const Command after = next_command(*c, foe);
        CHECK_FALSE((after.request.kind == ActionKind::CastSpell && after.request.innate));
        ecs::Registry reg2;
        std::string rerr;
        auto rep = replay(reg2, ch, library(), 4, CombatOptions{true}, c->log(), &rerr);
        REQUIRE_MESSAGE(rep, rerr);
        CHECK(rep->hash() == c->hash());
        CHECK(rep->log_hash() == c->log_hash());
    }
}

TEST_CASE("magma mephit: target holds only a wooden club -> Heat Metal is not listed and the AI does not cast it") {
    const Mephit& m = kMephits[2];
    auto ch = arena({{m.id, Vec2i{5, 5}}, {kBare, Vec2i{7, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, library(), 4, CombatOptions{true});
    REQUIRE(c);
    const CombatantId me = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
    to_turn(*c, me);
    REQUIRE(rules5e::find(c->state(), me)->innate[0].uses_left == 1);
    for (const auto& q : c->legal_actions())
        CHECK_FALSE((q.kind == ActionKind::CastSpell && q.spell_id == spell_id_of("srd51.spell.heat_metal")));
    const Command cmd = next_command(*c, foe);
    CHECK_FALSE((cmd.request.kind == ActionKind::CastSpell && cmd.request.innate));
}
