// B15 content: horrific_appearance trait and Death Glare save_effect. No sea_hag.json.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
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
Json trait(int dc = 30) {
    return {{"id", "horrific_appearance"}, {"name", "Horrific Appearance"}, {"description", "x"},
            {"effect", {{"kind", "horrific_appearance"}, {"radius_ft", 30},
                        {"save", {{"ability", "wis"}, {"dc", dc}}}, {"immunity_hours", 24}}}};
}
Json glare(int dc = 30) {
    return {{"id", "death_glare"}, {"name", "Death Glare"}, {"kind", "save_effect"}, {"range_ft", 30},
            {"save", {{"ability", "wis"}, {"dc", dc}}}, {"magical", true},
            {"target_must_see_source", true}, {"caster_sees_target", true},
            {"target_has_condition", "frightened"}, {"drop_to_zero", true}};
}
Json hag(const std::string& id, std::vector<Json> traits, std::vector<Json> actions = {}, int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["hit_points"]["average"] = 80;
    for (auto& t : traits) m["traits"].push_back(std::move(t));
    for (auto& a : actions) m["actions"].push_back(std::move(a));
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) {
    return lib.add(m, "monster", "", version);
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
Command act(CombatantId a, ActionKind k, bool avert = false) {
    Command x;
    x.request = {a, k};
    x.request.avert_gaze = avert;
    return x;
}
void to_actor(Combat& c, CombatantId who) {
    auto step_off = [&] {
        const CombatantId a = rules5e::current_actor(c.state());
        if (c.state().budget.gaze_pending) REQUIRE(c.submit(act(a, ActionKind::GazeResponse)).ok);
        if (c.state().budget.appearance_pending) REQUIRE(c.submit(act(a, ActionKind::AppearanceResponse)).ok);
        REQUIRE(c.submit(act(a, ActionKind::EndTurn)).ok);
    };
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) step_off();
    REQUIRE(rules5e::current_actor(c.state()) == who);
    // Combat::create starts the opening turn before auras and sight hooks exist. If that actor is `who`,
    // the appearance was not asked. Leave and come back once the hooks are installed.
    if (!c.state().budget.appearance_pending) {
        step_off();
        for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) step_off();
        REQUIRE(rules5e::current_actor(c.state()) == who);
    }
}
}  // namespace

TEST_CASE("horrific appearance loader: the aura, refusals, one per creature") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, hag("t.hag", {trait(), trait()})));
    const auto* t = lib.find("t.hag");
    REQUIRE(t);
    REQUIRE(t->aura_spell_ids.size() == 1);
    const auto* d = lib.spells()->find(t->aura_spell_ids[0]);
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::Zone);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
    CHECK(d->fixed_dc == 30);
    CHECK(d->zone.size_ft == 30);
    REQUIRE(d->zone.triggers.size() == 1);
    const auto& row = d->zone.triggers[0];
    CHECK(row.avert_eyes);
    CHECK(row.must_see_source);
    CHECK(row.condition_rounds == 10);
    CHECK(row.success_immunity_hours == 24);
    CHECK(row.immunity_on_end);
    CHECK(row.repeat_los_disadvantage);
    CHECK(row.creature_types == rules5e::bit(rules5e::CreatureType::Humanoid));
    CHECK(row.effect.condition == Condition::Frightened);
    CHECK(row.effect.repeat_save == rules5e::RepeatSave::EndOfTurn);

    auto err = [&](const char* id, Json e, int v = 5) {
        Json tr = trait();
        tr["effect"] = std::move(e);
        const auto r = add(lib, hag(id, {std::move(tr)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_key", {{"kind", "horrific_appearance"}, {"radius_ft", 30},
                          {"save", {{"ability", "wis"}, {"dc", 11}}}, {"immunity_hours", 24}, {"note", 1}})
              .find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_rad", {{"kind", "horrific_appearance"}, {"radius_ft", 31},
                          {"save", {{"ability", "wis"}, {"dc", 11}}}, {"immunity_hours", 24}})
              .find("radius_ft") != std::string::npos);
    CHECK(err("t.e_ver", trait()["effect"], 4).find("needs a monster@5 file") != std::string::npos);

    Json other = trait(11);
    other["id"] = "horrific_appearance_2";
    CHECK(add(lib, hag("t.two", {trait(), other})).value_or("").find("one horrific appearance") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.hag_pc";
    ch["features"].push_back(trait());
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);
}

TEST_CASE("death glare loader: the save drops to 0 and is magic") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, hag("t.glare", {}, {glare()})));
    const auto* t = lib.find("t.glare");
    REQUIRE(t);
    REQUIRE(t->save_actions.size() == 1);
    const auto* d = lib.spells()->find(t->save_actions[0].spell_id);
    REQUIRE(d);
    CHECK(d->drop_to_zero);
    CHECK(d->magical);
    CHECK(d->has_target_condition);
    CHECK(d->target_condition == Condition::Frightened);
    CHECK(d->caster_sees_target);
    CHECK(d->target_sees_source);
    CHECK(d->fixed_dc == 30);
    CHECK(d->range_ft == 30);
    CHECK_FALSE(d->applies_condition);

    Json bad = glare();
    bad["damage"] = Json::array({{{"dice", "1d6"}, {"type", "psychic"}}});
    bad["on_success"] = "none";
    CHECK(add(lib, hag("t.bad_glare", {}, {bad})).value_or("").find("drop_to_zero takes no damage") != std::string::npos);
    Json cond = glare();
    cond["target_has_condition"] = "poisoned";
    CHECK(add(lib, hag("t.bad_cond", {}, {cond})).value_or("").find("must be frightened") != std::string::npos);
}

TEST_CASE("horrific appearance: a humanoid who looks is frightened; averting is disadvantage against the hag only") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 40;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(add(lib, hag("t.hag", {trait()})));
    const auto ch = arena({{kFighter, {5, 5}}, {"t.hag", {6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    const CombatantId hag_id = ch.spawns[1].stable_id;
    to_actor(*c, pc);
    REQUIRE(c->state().budget.appearance_pending);
    CHECK(c->submit(act(pc, ActionKind::Dodge)).error == "appearance pending: answer with AppearanceResponse first");
    REQUIRE(c->submit(act(pc, ActionKind::AppearanceResponse)).ok);
    CHECK(rules5e::find(c->state(), pc)->has(Condition::Frightened));
    CHECK_FALSE(rules5e::find(c->state(), pc)->gaze_averted);

    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, ch, lib, 1);
    REQUIRE(c2);
    to_actor(*c2, pc);
    REQUIRE(c2->state().budget.appearance_pending);
    REQUIRE(c2->submit(act(pc, ActionKind::AppearanceResponse, true)).ok);
    CHECK_FALSE(rules5e::find(c2->state(), pc)->has(Condition::Frightened));
    CHECK_FALSE(rules5e::find(c2->state(), pc)->gaze_averted);
    bool marked = false;
    for (const auto& m : c2->state().modifiers)
        if (m.target == pc && m.mark == hag_id && m.spec.mode == rules5e::RollMode::Disadvantage && m.spec.mark_scope == 1)
            marked = true;
    CHECK(marked);
}

TEST_CASE("death glare: the frightened fighter drops to 0 with no damage event") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 40;
    REQUIRE_FALSE(lib.add(tank, "character"));
    REQUIRE_FALSE(add(lib, hag("t.hag", {trait()}, {glare()})));
    const auto ch = arena({{kFighter, {5, 5}}, {"t.hag", {6, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 2);
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    const CombatantId hag_id = ch.spawns[1].stable_id;
    to_actor(*c, pc);
    REQUIRE(c->state().budget.appearance_pending);
    REQUIRE(c->submit(act(pc, ActionKind::AppearanceResponse)).ok);
    REQUIRE(rules5e::find(c->state(), pc)->has(Condition::Frightened));
    REQUIRE(c->submit(act(pc, ActionKind::EndTurn)).ok);
    to_actor(*c, hag_id);
    const uint32_t spell = lib.find("t.hag")->save_actions[0].spell_id;
    Command cast;
    cast.request = {hag_id, ActionKind::CastSpell, pc};
    cast.request.spell_id = spell;
    const auto before = c->events().size();
    auto r = c->submit(cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    bool damage = false;
    for (std::size_t i = before; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::DamageDealt) damage = true;
    }
    CHECK_FALSE(damage);
    CHECK(rules5e::find(c->state(), pc)->hp == 0);
    CHECK_FALSE(rules5e::find(c->state(), pc)->dead);
    CHECK(rules5e::find(c->state(), pc)->has(Condition::Unconscious));
}
