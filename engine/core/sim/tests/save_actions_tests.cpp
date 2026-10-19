// monster@4 / character@4 save_effect actions (VERSION_BUMP_BATCH row 6): synthetic SpellDef compile, spells_known,
// usage rows, ActionUsed / SpellTarget, and the single-target AI.
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

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

// The srd51 goblin as a monster@4 "drake" with the given save_effect action.
Json drake(const std::string& id, Json action) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 200;
    m["actions"].push_back(std::move(action));
    return m;
}
Json breath(Json extra = Json::object()) {
    Json a = {{"kind", "save_effect"}, {"id", "fire_breath"}, {"name", "Fire Breath"},
              {"save", {{"ability", "dex"}, {"dc", 14}}}, {"range_ft", 30},
              {"damage", Json::array({{{"dice", "4d6"}, {"type", "fire"}}})}, {"on_success", "half"}};
    for (auto& [k, v] : extra.items()) a[k] = v;
    return a;
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
ContentLibrary sample_lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 6 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
std::size_t count(const Combat& c, EventKind k) {
    std::size_t n = 0;
    for (const auto& e : c.events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k;
    return n;
}
} // namespace

TEST_CASE("save_effect compiles to a synthetic SpellDef known to the creature") {
    auto lib = sample_lib();
    Json extra = {{"usage", {{"kind", "recharge"}, {"min", 5}}},
                  {"apply", Json::array({{{"condition", "frightened"}, {"repeat_save", "end_of_turn"}},
                                         {{"condition", "poisoned"}, {"duration", {{"kind", "until_save"}}}}})}};
    REQUIRE_FALSE(lib.add(drake("t.drake", breath(extra)), "monster"));
    const Archetype* a = lib.find("t.drake");
    REQUIRE(a);
    REQUIRE(a->save_actions.size() == 1);
    const uint32_t id = rules5e::spell_id_of("t.drake#fire_breath");
    CHECK(a->save_actions[0].spell_id == id);
    CHECK(a->save_actions[0].usage_kind == 1);
    CHECK_FALSE(a->save_actions[0].area);
    CHECK(std::count(a->base.spells_known.begin(), a->base.spells_known.end(), id) == 1);   // no spellcasting block needed
    const rules5e::SpellDef* d = lib.spells()->find(id);
    REQUIRE(d);
    CHECK(d->content_id == "t.drake#fire_breath");
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
    CHECK(d->kind == rules5e::SpellEffectKind::Save);
    CHECK(d->fixed_dc == 14);
    CHECK(d->save_ability == rules5e::Ability::Dex);
    CHECK(d->dice == rules5e::DiceExpr{4, 6, 0});
    CHECK(d->damage_type == rules5e::DamageType::Fire);
    CHECK(d->on_save == rules5e::OnSave::Half);
    CHECK(d->range_ft == 30);
    CHECK(d->usage_kind == 1);
    CHECK(d->recharge_min == 5);
    CHECK(d->applies_condition);
    CHECK(d->condition == rules5e::Condition::Frightened);
    CHECK(d->repeat_save == rules5e::RepeatSave::EndOfTurn);
    REQUIRE(d->extra_effects.size() == 1);   // the second condition: ApplyCondition on those who failed
    CHECK(d->extra_effects[0].kind == rules5e::SpellEffectKind::ApplyCondition);
    CHECK(d->extra_effects[0].affected_only);
}

TEST_CASE("save_effect: area is kept, errors name the action, pool usage and pre-@4 files are not compiled") {
    auto lib = sample_lib();
    Json cone = breath({{"area", {{"shape", "cone"}, {"size_ft", 60}}}, {"origin", "self"}, {"range_ft", nullptr}});
    cone.erase("range_ft");
    REQUIRE_FALSE(lib.add(drake("t.cone", cone), "monster"));
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.cone#fire_breath"));
    REQUIRE(d);
    CHECK(d->area == rules5e::AreaShape::Cone);
    CHECK(d->area_size_ft == 60);
    CHECK(d->range_ft == 0);   // self origin
    CHECK(lib.find("t.cone")->save_actions[0].area);

    auto bad = breath();
    bad["damage"][0]["type"] = "not_a_type";
    auto e = lib.add(drake("t.bad", bad), "monster");
    REQUIRE(e);
    CHECK(e->find("fire_breath") != std::string::npos);
    CHECK_FALSE(lib.find("t.bad"));

    // a condition with an unknown duration kind, or neither a duration nor a repeat save, is listed, not compiled
    REQUIRE_FALSE(lib.add(drake("t.dur", breath({{"apply", Json::array({{{"condition", "stunned"}, {"duration", {{"kind", "concentration"}}}}})}})), "monster"));
    CHECK(lib.find("t.dur")->save_actions.empty());
    CHECK(lib.find("t.dur")->unsupported_features.size() == 1);
    CHECK_FALSE(lib.spells()->find(rules5e::spell_id_of("t.dur#fire_breath")));
    REQUIRE_FALSE(lib.add(drake("t.nodur", breath({{"apply", Json::array({{{"condition", "stunned"}}})}})), "monster"));
    CHECK(lib.find("t.nodur")->save_actions.empty());
    CHECK(lib.find("t.nodur")->unsupported_features.size() == 1);
    // a malformed duration is a load error
    CHECK(lib.add(drake("t.bn", breath({{"apply", Json::array({{{"condition", "stunned"}, {"duration", {{"kind", "rounds"}, {"n", 0}}}}})}})), "monster"));
    // pool usage needs an integer-max resource of the same document
    REQUIRE_FALSE(lib.add(drake("t.nopool", breath({{"usage", {{"kind", "pool"}, {"pool", "p"}}}})), "monster"));
    CHECK(lib.find("t.nopool")->save_actions.empty());
    CHECK(lib.find("t.nopool")->unsupported_features.size() == 1);
    Json pooled = drake("t.pool", breath({{"usage", {{"kind", "pool"}, {"pool", "breaths"}, {"cost", 2}}}}));
    pooled["resources"] = Json::array({{{"id", "breaths"}, {"max", 4}, {"recharge", "long_rest"}}});
    REQUIRE_FALSE(lib.add(pooled, "monster"));
    const auto* pd = lib.spells()->find(rules5e::spell_id_of("t.pool#fire_breath"));
    REQUIRE(pd);
    CHECK(pd->usage_kind == 3);
    CHECK(pd->pool_id == rules5e::spell_id_of("breaths"));
    CHECK(pd->pool_cost == 2);
    CHECK(lib.find("t.pool")->save_actions[0].pool_max == 4);
    // further damage components become further Save effects
    auto two = breath();
    two["damage"].push_back({{"dice", "2d6"}, {"type", "cold"}});
    REQUIRE_FALSE(lib.add(drake("t.two", two), "monster"));
    const auto* td = lib.spells()->find(rules5e::spell_id_of("t.two#fire_breath"));
    REQUIRE(td);
    REQUIRE(td->extra_effects.size() == 1);
    CHECK(td->extra_effects[0].damage_type == rules5e::DamageType::Cold);
    CHECK(td->extra_effects[0].on_save == rules5e::OnSave::Half);

    auto old = drake("t.old", breath());
    REQUIRE_FALSE(lib.add(old, "monster", "", 3));   // declared @3: never compiled
    CHECK(lib.find("t.old")->save_actions.empty());
}

TEST_CASE("save_effect: overriding an archetype replaces its synthetic spells; collisions are load errors") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.over", breath()), "monster"));
    const uint32_t id = rules5e::spell_id_of("t.over#fire_breath");
    REQUIRE(lib.spells()->find(id));
    auto stronger = breath();
    stronger["damage"][0]["dice"] = "8d6";
    REQUIRE_FALSE(lib.add(drake("t.over", stronger), "monster"));   // same id again: no duplicate-spell failure
    CHECK(lib.spells()->find(id)->dice == rules5e::DiceExpr{8, 6, 0});
    auto renamed = breath();
    renamed["id"] = "frost_breath";
    REQUIRE_FALSE(lib.add(drake("t.over", renamed), "monster"));    // the replaced archetype's entry is gone
    CHECK_FALSE(lib.spells()->find(id));
    CHECK(lib.spells()->find(rules5e::spell_id_of("t.over#frost_breath")));
    // two actions of one archetype with the same id: collision, nothing half-registered
    Json two = drake("t.dup", breath());
    two["actions"].push_back(breath());
    auto e = lib.add(two, "monster");
    REQUIRE(e);
    CHECK(e->find("collide") != std::string::npos);
    CHECK_FALSE(lib.spells()->find(rules5e::spell_id_of("t.dup#fire_breath")));
}

TEST_CASE("save action in a fight: AI uses it, ActionUsed + SpellTarget, recharge roll, hash and replay") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.drake", breath({{"usage", {{"kind", "recharge"}, {"min", 6}}}, {"damage", Json::array({{{"dice", "1d2"}, {"type", "fire"}}})}})), "monster"));
    auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.drake", Vec2i{9, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.drake#fire_breath");
    REQUIRE(rules5e::find_action_use(c->state(), g, sid));
    CHECK_FALSE(rules5e::find_action_use(c->state(), g, sid)->expended);
    CHECK(rules5e::find_action_use(c->state(), f, sid) == nullptr);   // only creatures that have the action get a row
    advance_to(*c, g);
    const std::size_t from = c->events().size();
    Command cmd = next_command(*c, f);
    REQUIRE(cmd.request.kind == ActionKind::CastSpell);
    CHECK(cmd.request.spell_id == sid);
    const uint32_t before = event_stream_version(c->events());
    auto r = c->submit(cmd);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find_action_use(c->state(), g, sid)->expended);
    bool used = false, target = false;
    for (std::size_t i = from; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        used = used || (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::ActionUsed && e.rules.reaction_depth == 0 && e.rules.actor == g);
        target = target || (e.kind == SimEvent::Kind::SpellTarget && e.actor == g && e.rules.target == f && e.rules.a == int32_t(sid));
        CHECK_FALSE((e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SpellCast));   // not a spell
    }
    CHECK(used);
    CHECK(target);
    CHECK(before == 4);
    CHECK(event_stream_version(c->events()) == 7);   // ActionUsed is a v7 rules kind
    // Expended: the AI falls back to its other actions until the recharge roll (turn start draws a d6).
    Command next = next_command(*c, f);
    CHECK(next.request.kind != ActionKind::CastSpell);
    const std::size_t rolls0 = count(*c, EventKind::RechargeRolled);
    advance_to(*c, f);
    advance_to(*c, g);
    CHECK(count(*c, EventKind::RechargeRolled) == rolls0 + 1);   // one d6 per expended action at the owner's turn start
    // replay reproduces the state hash (rows are rebuilt by Combat::create)
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 11, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
}

TEST_CASE("save action AI: out of range, a lone area action's aim and per-day exhaustion are not chosen") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.far", breath()), "monster"));
    REQUIRE_FALSE(lib.add(drake("t.area", breath({{"area", {{"shape", "cone"}, {"size_ft", 30}}}, {"origin", "self"}})), "monster"));
    REQUIRE_FALSE(lib.add(drake("t.day", breath({{"usage", {{"kind", "per_day"}, {"uses", 1}}}, {"damage", Json::array({{{"dice", "1d2"}, {"type", "fire"}}})}})), "monster"));   // weak: the fighter must survive
    {
        auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.far", Vec2i{20, 5}}});   // 15 tiles = 75 ft > 30
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        advance_to(*c, ch.spawns[1].stable_id);
        CHECK(next_command(*c, ch.spawns[0].stable_id).request.kind != ActionKind::CastSpell);
    }
    {
        auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.area", Vec2i{8, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        advance_to(*c, ch.spawns[1].stable_id);
        CHECK(next_command(*c, ch.spawns[0].stable_id).request.kind != ActionKind::CastSpell);   // area AI (row 7): one hostile and a shortbow shot is legal
    }
    {
        auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.day", Vec2i{8, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
        const uint32_t sid = rules5e::spell_id_of("t.day#fire_breath");
        advance_to(*c, g);
        REQUIRE(rules5e::feature_uses_left(c->state(), g, sid) == 1);
        Command cmd = next_command(*c, f);
        REQUIRE(cmd.request.kind == ActionKind::CastSpell);
        REQUIRE(c->submit(cmd).ok);
        CHECK(rules5e::feature_uses_left(c->state(), g, sid) == 0);
        advance_to(*c, f);
        advance_to(*c, g);
        CHECK(next_command(*c, f).request.kind != ActionKind::CastSpell);
    }
}

TEST_CASE("save_effect timed durations compile to expiry / duration_rounds and expire in a fight") {
    auto lib = sample_lib();
    Json apply = Json::array({{{"condition", "poisoned"}, {"duration", {{"kind", "rounds"}, {"n", 3}}}},
                              {{"condition", "frightened"}, {"duration", {{"kind", "until_end_of_target_next_turn"}}}},
                              {{"condition", "blinded"}, {"duration", {{"kind", "rounds"}, {"n", 2}}}, {"repeat_save", "end_of_turn"}},
                              {{"condition", "restrained"}, {"duration", {{"kind", "until_save"}}}}});
    Json act = breath({{"apply", apply}, {"save", {{"ability", "dex"}, {"dc", 30}}}});
    { const auto er = lib.add(drake("t.timed", act), "monster"); REQUIRE_MESSAGE(!er, er.value_or("")); }
    const Archetype* a = lib.find("t.timed");
    REQUIRE(a);
    REQUIRE(a->save_actions.size() == 1);
    const auto* d = lib.spells()->find(a->save_actions[0].spell_id);
    REQUIRE(d);
    using rules5e::Expiry;
    CHECK(d->condition == rules5e::Condition::Poisoned);
    CHECK(d->expiry == uint8_t(Expiry::Rounds));
    CHECK(d->duration_rounds == 3);
    CHECK(d->repeat_save == rules5e::RepeatSave::None);
    REQUIRE(d->extra_effects.size() == 3);
    CHECK(d->extra_effects[0].expiry == uint8_t(Expiry::EndOfTargetNextTurn));
    CHECK(d->extra_effects[0].duration_rounds == 0);
    CHECK(d->extra_effects[1].expiry == uint8_t(Expiry::Rounds));
    CHECK(d->extra_effects[1].duration_rounds == 2);
    CHECK(d->extra_effects[1].repeat_save == rules5e::RepeatSave::EndOfTurn);   // both a timer and a repeat save
    CHECK(d->extra_effects[2].expiry == 0);
    CHECK(d->extra_effects[2].repeat_save == rules5e::RepeatSave::EndOfTurn);   // until_save

    // In a fight: the condition lands, then ends at the start of round R + 3.
    Json one = breath({{"apply", Json::array({{{"condition", "poisoned"}, {"duration", {{"kind", "rounds"}, {"n", 1}}}}})},
                       {"save", {{"ability", "dex"}, {"dc", 30}}}});
    REQUIRE_FALSE(lib.add(drake("t.one", one), "monster"));
    auto ch = arena({{kFighter, Vec2i{5, 5}}, {"t.one", Vec2i{8, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 21);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    advance_to(*c, g);
    Command cmd = next_command(*c, f);
    REQUIRE(cmd.request.kind == ActionKind::CastSpell);
    REQUIRE(c->submit(cmd).ok);
    CHECK(rules5e::find(c->state(), f)->has(rules5e::Condition::Poisoned));
    CHECK(rules5e::has_timed_effects(c->state()));
    const uint16_t round0 = c->state().round;
    for (int i = 0; i < 6 && c->state().round == round0; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    REQUIRE(c->state().round == round0 + 1);
    CHECK_FALSE(rules5e::find(c->state(), f)->has(rules5e::Condition::Poisoned));   // gone when the next round starts
    CHECK(count(*c, EventKind::ConditionRemoved) >= 1);
    // replay agrees, including the timed-effect digest
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 21, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
}
