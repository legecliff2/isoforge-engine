// Data-driven feature hooks: Dueling (damage_bonus), Nimble Escape (bonus_action_grant),
// Second Wind (self_heal via rules5e UseFeature).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "assets/content_migrations.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

assets::Json load_doc(const std::string& rel) {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    auto r = reg.load_file(kPack + "/" + rel);
    REQUIRE_MESSAGE(r.ok, r.error);
    return r.data;
}
world::Chunk arena(std::vector<std::tuple<std::string, int, int>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        auto& [a, x, y] = spawns[i];
        c.spawns.push_back({a, Vec2i{x, y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 4 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
uint32_t active_id(const Archetype& a, const std::string& kind) {
    for (const auto& f : a.active) if (f.kind == kind) return f.feature_id;
    return 0;
}
} // namespace

TEST_CASE("pack features resolve through effect kinds") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Archetype& f = *lib.find(kFighter);
    const Archetype& g = *lib.find(kGoblin);
    CHECK(f.unsupported_features.empty());
    CHECK(g.unsupported_features.empty());
    // Dueling: +2 on the one-handed melee weapon only (content holds base damage 1d8+3).
    REQUIRE(f.base.attacks.size() == 2);
    CHECK(f.base.attacks[0].damage == rules5e::DiceExpr{1, 8, 5});
    CHECK(f.base.attacks[1].damage == rules5e::DiceExpr{1, 8, 2});   // crossbow: unchanged
    // Second Wind: active, bonus action, 1d10 + fighter level (1), 1 use per short rest.
    REQUIRE(f.active.size() == 1);
    const ActiveFeature& sw = f.active[0];
    CHECK(sw.id == "second_wind");
    CHECK(sw.feature_id == 2);
    CHECK(sw.cost == FeatureCost::BonusAction);
    CHECK(sw.heal == rules5e::DiceExpr{1, 10, 1});
    CHECK(sw.uses == 1);
    CHECK(sw.recharge == Recharge::ShortRest);
    // Nimble Escape: bonus Disengage + Hide.
    CHECK((g.base.bonus_action_grants & rules5e::bit(ActionKind::Disengage)) != 0);
    CHECK((g.base.bonus_action_grants & rules5e::bit(ActionKind::Hide)) != 0);
    CHECK((g.base.bonus_action_grants & rules5e::bit(ActionKind::Dash)) == 0);
    CHECK(g.active.empty());
}

TEST_CASE("hooks key on effect.kind, not names; unknown kinds are reported; bad params rejected") {
    auto doc = load_doc("characters/fighter_1.json");
    // Rename every feature: behaviour must not change.
    for (auto& ft : doc["features"]) ft["id"] = "renamed_" + ft["id"].get<std::string>();
    doc["id"] = "test.renamed";
    doc["features"].push_back({{"id", "mystery"}, {"name", "Mystery"}, {"effect", {{"kind", "not_a_hook"}}}});
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(doc, "character"));
    const Archetype& a = *lib.find("test.renamed");
    CHECK(a.base.attacks[0].damage.bonus == 5);
    REQUIRE(a.active.size() == 1);
    CHECK(a.active[0].id == "renamed_second_wind");
    CHECK(a.unsupported_features == std::vector<std::string>{"mystery (not_a_hook)"});

    // A custom registry: new kinds plug in without touching sim.
    FeatureRegistry reg = FeatureRegistry::builtin();
    reg.add_hook("not_a_hook", {[](Archetype& ar, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
        ar.base.ac = int8_t(ar.base.ac + 1);
        return std::optional<std::string>{};
    }});
    ContentLibrary lib2(reg);
    doc["id"] = "test.custom";
    REQUIRE_FALSE(lib2.add(doc, "character"));
    CHECK(lib2.find("test.custom")->base.ac == 19);
    CHECK(lib2.find("test.custom")->unsupported_features.empty());

    auto bad = load_doc("characters/fighter_1.json");
    bad["features"][0]["effect"]["condition"] = "no_such_condition";
    CHECK(lib.add(bad, "character"));
    bad = load_doc("characters/fighter_1.json");
    bad["features"][1]["effect"]["cost"] = "free";
    CHECK(lib.add(bad, "character"));
    // cost "reaction" is schema-valid (CONTENT_EFFECTS.md) but reserved: rejected with a clear error.
    bad = load_doc("characters/fighter_1.json");
    bad["features"][1]["effect"]["cost"] = "reaction";
    auto e = lib.add(bad, "character");
    REQUIRE(e);
    CHECK(e->find("reserved, not supported") != std::string::npos);
    // Unknown recharge is an error, not "never". Omitted recharge = never.
    for (const assets::Json& v : {assets::Json("dawn"), assets::Json(3)}) {
        bad = load_doc("characters/fighter_1.json");
        bad["features"][1]["effect"]["recharge"] = v;
        auto er = lib.add(bad, "character");
        REQUIRE(er);
        CHECK(er->find("unknown recharge") != std::string::npos);
    }
    auto ok = load_doc("characters/fighter_1.json");
    ok["id"] = "test.no_recharge";
    ok["features"][1]["effect"].erase("recharge");
    REQUIRE_FALSE(lib.add(ok, "character"));
    CHECK(lib.find("test.no_recharge")->active.at(0).recharge == Recharge::None);
}

TEST_CASE("Second Wind via UseFeature: bonus action, rolled heal, one use, replays") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    auto ch = arena({{kFighter, 2, 2}, {kGoblin, 20, 20}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id;
    advance_to(*c, f);
    const uint32_t sw = active_id(*lib.find(kFighter), "self_heal");
    REQUIRE(sw != 0);
    // Wound the fighter (a logged Heal-style request isn't damage; poke state through a test-only path).
    const_cast<rules5e::EncounterState&>(c->state()).combatants[0].hp = 3;
    CHECK(c->can_use_feature(f, sw));
    Command u;
    u.request = {f, ActionKind::UseFeature};
    u.request.feature_id = sw;
    u.request.heal_dice = {9, 9, 99};   // ignored: parameters come from content
    auto r = c->submit(u);
    REQUIRE_MESSAGE(r.ok, r.error);
    const auto* k = rules5e::find(c->state(), f);
    CHECK(k->hp >= 3 + 2);
    CHECK(k->hp <= 3 + 11);
    CHECK_FALSE(c->state().budget.bonus_action);
    CHECK(c->state().budget.action);   // the action is still available
    CHECK(c->feature_uses_left(f, sw) == 0);
    CHECK_FALSE(c->can_use_feature(f, sw));
    CHECK_FALSE(c->submit(u).ok);
    bool used = false, healed = false;
    for (const auto& e : c->events()) {
        used |= e.rules.kind == EventKind::FeatureUsed && e.rules.actor == f && e.rules.a == int(sw) && e.rules.b == 1;
        healed |= e.rules.kind == EventKind::Healed && e.rules.actor == f && e.rules.target == f;
    }
    CHECK(used);
    CHECK(healed);
    CHECK(c->log().back().step.request.heal_dice == rules5e::DiceExpr{1, 10, 1});
    // Unknown feature ids / non-active features are refused.
    u.request.feature_id = 1;   // Dueling: static, not usable
    CHECK_FALSE(c->submit(u).ok);
    // Replay reproduces it (uses are re-derived from the log).
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 11, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->feature_uses_left(f, sw) == 0);
    CHECK(rep->log_hash() == c->log_hash());   // (the test-only hp poke isn't in the log)
}

TEST_CASE("policy: goblin uses its granted bonus Disengage before leaving reach; no opportunity attack") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    // Goblin adjacent to fighter A, chasing fighter B on the far side: its path leaves A's reach.
    auto ch = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}, {kFighter, 12, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id, b = ch.spawns[2].stable_id;
    advance_to(*c, g);
    // Move goblin toward B; first step east (7,5) leaves A's reach.
    Command cmd = next_command(*c, b);
    CHECK(cmd.kind == CommandKind::Rules);
    CHECK(cmd.request.kind == ActionKind::Disengage);
    CHECK(cmd.request.as_bonus_action);
    REQUIRE(c->submit(cmd).ok);
    CHECK(c->state().budget.action);   // bonus action, not the action
    cmd = next_command(*c, b);
    REQUIRE(cmd.kind == CommandKind::Step);
    auto r = c->submit(cmd);
    REQUIRE(r.ok);
    CHECK(r.status == StepStatus::Done);   // no window: disengaged
    (void)a;
}

TEST_CASE("policy: fighter uses Second Wind when at or below half hp") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    auto ch = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 2);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    advance_to(*c, f);
    CHECK(next_command(*c, g).request.kind == ActionKind::Attack);   // healthy: attack
    const_cast<rules5e::EncounterState&>(c->state()).combatants[0].hp = 6;   // 6/12 = half
    Command cmd = next_command(*c, g);
    CHECK(cmd.request.kind == ActionKind::UseFeature);
    REQUIRE(c->submit(cmd).ok);
    CHECK(next_command(*c, g).request.kind == ActionKind::Attack);   // then still attacks
}
