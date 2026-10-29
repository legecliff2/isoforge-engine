// B4: Petrifying Breath is the gaze escalation path on an area save_effect. A failed Constitution
// save restrains; a failed repeat at the end of the target's turn petrifies. escalate_margin > 0
// petrifies on the first save, as medusa. No new hash tag: ActiveSpellEffect::escalates is already
// folded (has_leftover_state / leftover_digest). SRD 5.1 gorgon text matches that path; "until freed
// by greater restoration" stays description only, as basilisk.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/encounter.hpp"
#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json breath(int dc, int margin) {
    return {{"kind", "save_effect"}, {"id", "petrifying_breath"}, {"name", "Petrifying Breath"},
            {"save", {{"ability", "con"}, {"dc", dc}}}, {"origin", "self"},
            {"area", {{"shape", "cone"}, {"size_ft", 30}}},
            {"apply", Json::array({{{"condition", "restrained"}, {"repeat_save", "end_of_turn"}}})},
            {"escalates", true}, {"escalate_to", "petrified"}, {"escalate_margin", margin},
            {"usage", {{"kind", "recharge"}, {"min", 5}}}};
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
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}

void cast_breath(Combat& c, CombatantId who, uint32_t sid, Vec2i aim) {
    Command cmd;
    cmd.aim = true;
    cmd.to = aim;
    cmd.request = {who, ActionKind::CastSpell};
    cmd.request.spell_id = sid;
    auto r = c.submit(cmd);
    while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
}

const rules5e::Combatant* who(const Combat& c, CombatantId id) {
    return rules5e::find(c.state(), id);
}
} // namespace

TEST_CASE("gorgon Petrifying Breath compiles as an area save that escalates") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    const uint32_t sid = rules5e::spell_id_of("srd51.monster.gorgon#petrifying_breath");
    const rules5e::SpellDef* d = lib.spells()->find(sid);
    REQUIRE(d);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
    CHECK(d->area == rules5e::AreaShape::Cone);
    CHECK(d->area_size_ft == 30);
    CHECK(d->save_ability == rules5e::Ability::Con);
    CHECK(d->fixed_dc == 13);
    CHECK(d->applies_condition);
    CHECK(d->condition == Condition::Restrained);
    CHECK(d->repeat_save == rules5e::RepeatSave::EndOfTurn);
    CHECK(d->escalates);
    CHECK(d->escalate_to == Condition::Petrified);
    CHECK(d->escalate_margin == 0);
    CHECK(d->usage_kind == 1);
    CHECK(d->recharge_min == 5);
    const Archetype* a = lib.find("srd51.monster.gorgon");
    REQUIRE(a);
    REQUIRE(a->save_actions.size() == 1);
    CHECK(a->save_actions[0].area);
    CHECK(a->save_actions[0].spell_id == sid);
}

TEST_CASE("area save escalates: restrained, then petrified on the failed repeat save") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    Json g = load_json("content/packs/srd51.core/monsters/gorgon.json");
    g["id"] = "t.gorgon";
    g["actions"][2]["save"]["dc"] = 30;   // the shipped DC is 13; 30 fails every save
    REQUIRE_FALSE(lib.add(g, "monster"));
    // Large 2x2 anchor (10,10). East cone aim (12,10) covers the fighter (area_footprint golden).
    auto ch = arena({{kFighter, {12, 10}}, {"t.gorgon", {10, 10}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, gorgon = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.gorgon#petrifying_breath");
    advance_to(*c, gorgon);
    const uint64_t before = c->hash();
    cast_breath(*c, gorgon, sid, {12, 10});
    CHECK(who(*c, f)->has(Condition::Restrained));
    CHECK_FALSE(who(*c, f)->has(Condition::Petrified));
    REQUIRE(c->state().effects.size() == 1);
    CHECK(c->state().effects[0].escalates);
    CHECK(c->state().effects[0].escalate_to == Condition::Petrified);
    CHECK(rules5e::has_leftover_state(c->state()));
    CHECK(c->hash() != before);   // existing 0xAA fold; no new tag
    advance_to(*c, f);
    Command end;
    end.request = {f, ActionKind::EndTurn};
    auto r = c->submit(end);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE(r.ok);
    CHECK(who(*c, f)->has(Condition::Petrified));
    CHECK_FALSE(who(*c, f)->has(Condition::Restrained));
    REQUIRE(c->state().effects.size() == 1);
    CHECK_FALSE(c->state().effects[0].escalates);
    CHECK(c->state().effects[0].condition == Condition::Petrified);
    CHECK_FALSE(rules5e::has_leftover_state(c->state()));
}

TEST_CASE("area save escalates: a successful repeat save ends the restraint") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    Json g = load_json("content/packs/srd51.core/monsters/gorgon.json");
    g["id"] = "t.gorgon";
    g["actions"][2]["save"]["dc"] = 30;
    REQUIRE_FALSE(lib.add(g, "monster"));
    auto ch = arena({{kFighter, {12, 10}}, {"t.gorgon", {10, 10}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, gorgon = ch.spawns[1].stable_id;
    advance_to(*c, gorgon);
    cast_breath(*c, gorgon, rules5e::spell_id_of("t.gorgon#petrifying_breath"), {12, 10});
    REQUIRE(who(*c, f)->has(Condition::Restrained));
    CombatTestAccess::state(*c).effects[0].dc = 1;   // the repeat save succeeds; the first already failed
    advance_to(*c, f);
    Command end;
    end.request = {f, ActionKind::EndTurn};
    auto r = c->submit(end);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE(r.ok);
    CHECK_FALSE(who(*c, f)->has(Condition::Restrained));
    CHECK_FALSE(who(*c, f)->has(Condition::Petrified));
    CHECK(c->state().effects.empty());
}

TEST_CASE("area save escalate_margin petrifies on the first failed save") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.margin";
    m["hit_points"]["average"] = 40;
    m["actions"].push_back(breath(30, 5));
    REQUIRE_FALSE(lib.add(m, "monster"));
    auto ch = arena({{kFighter, {7, 5}}, {"t.margin", {5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, caster = ch.spawns[1].stable_id;
    advance_to(*c, caster);
    cast_breath(*c, caster, rules5e::spell_id_of("t.margin#petrifying_breath"), {9, 5});
    CHECK(who(*c, f)->has(Condition::Petrified));
    CHECK_FALSE(who(*c, f)->has(Condition::Restrained));
    REQUIRE(c->state().effects.size() == 1);
    CHECK_FALSE(c->state().effects[0].escalates);
    CHECK(c->state().effects[0].repeat_save == rules5e::RepeatSave::None);
}

TEST_CASE("area save escalates: a successful first save applies nothing") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    Json g = load_json("content/packs/srd51.core/monsters/gorgon.json");
    g["id"] = "t.gorgon";
    g["actions"][2]["save"]["dc"] = 1;
    REQUIRE_FALSE(lib.add(g, "monster"));
    auto ch = arena({{kFighter, {12, 10}}, {"t.gorgon", {10, 10}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, gorgon = ch.spawns[1].stable_id;
    advance_to(*c, gorgon);
    cast_breath(*c, gorgon, rules5e::spell_id_of("t.gorgon#petrifying_breath"), {12, 10});
    CHECK_FALSE(who(*c, f)->has(Condition::Restrained));
    CHECK_FALSE(who(*c, f)->has(Condition::Petrified));
    CHECK(c->state().effects.empty());
    CHECK_FALSE(rules5e::has_leftover_state(c->state()));
}

TEST_CASE("escalates on a save_effect is refused when it is not the gaze path") {
    auto lib = ContentLibrary{};
    REQUIRE(lib.load(sample_set()).empty());
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.bad";
    Json rider = breath(13, 0);
    rider.erase("usage");
    rider["kind"] = "save_condition";
    rider.erase("id");
    rider.erase("name");
    rider.erase("origin");
    rider.erase("area");
    m["actions"][0]["on_hit"] = Json::array({rider});
    auto err = lib.add(m, "monster");
    REQUIRE(err);
    CHECK(err->find("escalates is a save_effect action, not a rider") != std::string::npos);

    Json old = load_json("content/packs/srd51.core/monsters/goblin.json");
    old["$schema"] = "monster@4";
    old["id"] = "t.old";
    old["actions"].push_back(breath(13, 0));
    err = lib.add(old, "monster");
    REQUIRE(err);
    CHECK(err->find("need a monster@5 file") != std::string::npos);

    Json bare = load_json("content/packs/srd51.core/monsters/goblin.json");
    bare["$schema"] = "monster@5";
    bare["id"] = "t.bare";
    Json no_rep = breath(13, 0);
    // A duration so the apply compiles; no repeat save, so escalation is refused rather than listed unsupported.
    no_rep["apply"] = Json::array({{{"condition", "restrained"}, {"duration", {{"kind", "rounds"}, {"n", 1}}}}});
    bare["actions"].push_back(no_rep);
    err = lib.add(bare, "monster");
    REQUIRE(err);
    CHECK(err->find("escalates needs a repeat save") != std::string::npos);
}
