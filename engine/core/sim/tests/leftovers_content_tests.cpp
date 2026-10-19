// Small leftovers, sim side (rules #111, VERSION_BUMP_BATCH 7.2j hand-off a-c): loader kinds `aggressive` (orc) and
// `reckless_attack` (berserker / minotaur Reckless, Barbarian Reckless Attack); the AI's bonus-action Aggressive Dash toward a
// visible target and the sim keeping that Dash's movement toward a hostile it can see; ActionRequest::reckless carried as
// submitted; replay reproduces Combat::hash (0xAA leftover_digest) and Combat::log_hash (0xB3 request flags).
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

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json trait(const char* id, const char* kind) {
    return {{"id", id}, {"name", id}, {"description", "x"}, {"effect", {{"kind", kind}}}};
}
// A goblin stat block (scimitar, shortbow; no Nimble Escape) as monster@4 with the given traits.
Json goblin_with(const std::string& id, std::vector<Json> traits, int hp = 400, int declared = 4) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(declared);
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    m["traits"] = Json::array();
    for (const Json& t : traits) m["traits"].push_back(t);
    return m;
}
Json fighter_with(const std::string& id, std::vector<Json> features) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    for (const Json& f : features) c["features"].push_back(f);
    return c;
}
ContentLibrary lib_with(const std::vector<Json>& monsters, int fighter_hp = 2000) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = fighter_hp;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
Command step(Vec2i to) {
    Command s;
    s.kind = CommandKind::Step;
    s.to = to;
    return s;
}
class NoTarget final : public IActorPolicy {
public:
    std::optional<CombatantId> choose_target(const Combat&, CombatantId) override { return std::nullopt; }
};
}  // namespace

TEST_CASE("leftovers loader: aggressive and reckless_attack set the rules flags; defaults unchanged") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    for (const auto& [id, a] : std::vector<std::pair<std::string, const Archetype*>>{{kFighter, lib.find(kFighter)},
                                                                                  {"srd51.monster.goblin", lib.find("srd51.monster.goblin")}}) {
        REQUIRE_MESSAGE(a, id);
        CHECK_FALSE(a->base.aggressive);
        CHECK_FALSE(a->base.reckless_attack);
        CHECK_FALSE(a->base.reckless_active);
    }
    REQUIRE_FALSE(lib.add(goblin_with("t.orc", {trait("aggressive", "aggressive")}), "monster"));
    const Archetype* orc = lib.find("t.orc");
    CHECK(orc->base.aggressive);
    CHECK_FALSE(orc->base.reckless_attack);
    CHECK(orc->unsupported_features.empty());
    REQUIRE_FALSE(lib.add(goblin_with("t.berserker", {trait("reckless", "reckless_attack")}), "monster"));
    const Archetype* bz = lib.find("t.berserker");
    CHECK(bz->base.reckless_attack);
    CHECK_FALSE(bz->base.reckless_active);   // declared per turn, never by the loader
    CHECK_FALSE(bz->base.aggressive);
    CHECK(bz->unsupported_features.empty());
    // character features (Barbarian Reckless Attack) take the same kind
    REQUIRE_FALSE(lib.add(fighter_with("t.barbarian", {trait("reckless_attack", "reckless_attack")}), "character"));
    CHECK(lib.find("t.barbarian")->base.reckless_attack);
    // open kinds: live at monster@3 too (like the R-G5 / R-G7 static kinds), extra fields are ignored
    Json old = goblin_with("t.orc3", {trait("aggressive", "aggressive")}, 7, 3);
    old["traits"][0]["effect"]["note"] = "ignored";
    REQUIRE_FALSE(lib.add(old, "monster", "", 3));
    CHECK(lib.find("t.orc3")->base.aggressive);
    CHECK(lib.find("t.orc3")->inert_features.empty());
}

TEST_CASE("Aggressive: the AI takes the bonus-action Dash toward a visible target; the Dash's movement goes toward a hostile") {
    const auto lib = lib_with({goblin_with("t.orc", {trait("aggressive", "aggressive")}), goblin_with("t.plain", {})});
    const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {15, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 21);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, o = ch.spawns[1].stable_id;
    to_actor(*c, o);
    // 45 ft to reach, 30 ft of movement: the Dash gets it there (it would otherwise shoot its shortbow).
    const Command d = next_command(*c, f);
    REQUIRE(d.kind == CommandKind::Rules);
    CHECK(d.request.kind == ActionKind::Dash);
    CHECK(d.request.as_bonus_action);
    CHECK(d.request.aggressive);
    auto r = c->submit(d);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->state().budget.aggressive_dash);
    CHECK_FALSE(c->state().budget.bonus_action);
    CHECK(c->state().budget.action);   // the action is still there
    CHECK(c->state().budget.movement_ft == 60);
    // Its own 30 ft go anywhere: one step away, then back.
    CHECK(c->aggressive_step_ok(o, {16, 5}));
    REQUIRE(c->submit(step({16, 5})).ok);
    REQUIRE(c->submit(step({15, 5})).ok);
    for (int x = 14; x >= 12; --x) REQUIRE(c->submit(step({x, 5})).ok);   // 25 ft of its own movement spent
    REQUIRE(c->submit(step({11, 4})).ok);                                   // the last 5 ft of its own: sideways is fine
    // Now only the Dash's movement is left: away or sideways is refused, toward the fighter is fine.
    CHECK_FALSE(c->aggressive_step_ok(o, {12, 4}));
    CHECK_FALSE(c->aggressive_step_ok(o, {11, 3}));
    auto away = c->submit(step({12, 4}));
    CHECK_FALSE(away.ok);
    CHECK(away.error.find("aggressive") != std::string::npos);
    CHECK(c->aggressive_step_ok(o, {10, 4}));
    // The AI finishes the approach and attacks; no refused command on the way.
    for (int i = 0; i < 12 && c->distance_tiles(o, f) > 1; ++i) {
        const Command s = next_command(*c, f);
        REQUIRE(s.kind == CommandKind::Step);
        auto sr = c->submit(s);
        REQUIRE_MESSAGE(sr.ok, sr.error);
    }
    CHECK(c->distance_tiles(o, f) <= 1);
    const Command a = next_command(*c, f);
    CHECK(a.request.kind == ActionKind::Attack);
    // Next turn the restriction is gone.
    Command e;
    e.request = {o, ActionKind::EndTurn};
    REQUIRE(c->submit(e).ok);
    to_actor(*c, o);
    CHECK(c->aggressive_step_ok(o, {c->pos(o).x + 1, c->pos(o).y}));
}

TEST_CASE("Aggressive: only aggressive creatures dash; no Dash without sight; shooting stays first when the Dash cannot reach") {
    const auto lib = lib_with({goblin_with("t.orc", {trait("aggressive", "aggressive")}), goblin_with("t.plain", {})});
    {   // a plain goblin at the same spot shoots, as before
        const auto ch = arena({{kFighter, {5, 5}}, {"t.plain", {15, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        to_actor(*c, ch.spawns[1].stable_id);
        const Command x = next_command(*c, ch.spawns[0].stable_id);
        CHECK(x.request.kind == ActionKind::Attack);
        CHECK_FALSE(x.request.aggressive);
        // and rules refuse an Aggressive Dash from it
        Command d;
        d.request = {ch.spawns[1].stable_id, ActionKind::Dash};
        d.request.as_bonus_action = true;
        d.request.aggressive = true;
        auto r = c->submit(d);
        CHECK_FALSE(r.ok);
        CHECK(r.error == "no Aggressive");
    }
    {   // too far to reach even with the Dash, the shortbow is legal: shoot (unchanged behaviour)
        const auto ch = arena({{kFighter, {2, 5}}, {"t.orc", {20, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        to_actor(*c, ch.spawns[1].stable_id);
        const Command x = next_command(*c, ch.spawns[0].stable_id);
        CHECK(x.request.kind == ActionKind::Attack);
        CHECK(x.request.attack_index == 1);
    }
    {   // a sight wall between them: no Dash (and no shot); it walks with its own movement
        std::vector<Vec2i> wall;
        for (int y = 0; y < 32; ++y) wall.push_back({10, y});
        const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {15, 5}}}, wall);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        to_actor(*c, ch.spawns[1].stable_id);
        const Command x = next_command(*c, ch.spawns[0].stable_id);
        CHECK(x.kind == CommandKind::Step);
    }
}

TEST_CASE("Reckless: ActionRequest::reckless is carried; advantage state folds under 0xAA, the flag under 0xB3; replay reproduces both") {
    const auto lib = lib_with({goblin_with("t.berserker", {trait("reckless", "reckless_attack")})});
    const auto ch = arena({{kFighter, {5, 5}}, {"t.berserker", {6, 5}}});
    const CombatantId f = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
    auto play = [&](ecs::Registry& reg, bool reckless) {
        auto c = Combat::create(reg, ch, lib, 33);
        REQUIRE(c);
        to_actor(*c, b);
        Command a;
        a.request = {b, ActionKind::Attack, f};
        a.request.attack_index = 0;
        a.request.reckless = reckless;
        auto r = c->submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (r.needs) REQUIRE(c->settle({}).ok);
        return std::move(*c);
    };
    ecs::Registry r0, r1, r2;
    Combat plain = play(r0, false), reck = play(r1, true);
    CHECK_FALSE(rules5e::find(plain.state(), b)->reckless_active);
    CHECK(rules5e::find(reck.state(), b)->reckless_active);   // attacks against it have advantage until its next turn
    CHECK(rules5e::has_leftover_state(reck.state()));
    CHECK_FALSE(rules5e::has_leftover_state(plain.state()));
    REQUIRE(reck.log().back().step.request.reckless);
    CHECK(reck.log_hash() != plain.log_hash());
    CHECK(reck.hash() != plain.hash());
    auto again = replay(r2, ch, lib, 33, reck.log());
    REQUIRE(again);
    CHECK(again->hash() == reck.hash());
    CHECK(again->log_hash() == reck.log_hash());
    CHECK(rules5e::find(again->state(), b)->reckless_active);
    // a second declaration in the same turn is refused by rules; a creature without the feature cannot declare it
    Command a2;
    a2.request = {b, ActionKind::Attack, f};
    a2.request.reckless = true;
    CHECK_FALSE(reck.submit(a2).ok);
    Command e;
    e.request = {b, ActionKind::EndTurn};
    REQUIRE(reck.submit(e).ok);
    to_actor(reck, f);
    Command fa;
    fa.request = {f, ActionKind::Attack, b};
    fa.request.reckless = true;
    auto fr = reck.submit(fa);
    CHECK_FALSE(fr.ok);
    CHECK(fr.error == "no Reckless Attack");
}

TEST_CASE("Aggressive: run_to_end logs the Dash; replay reproduces Combat::hash and Combat::log_hash") {
    const auto lib = lib_with({goblin_with("t.orc", {trait("aggressive", "aggressive")}, 30)}, 60);
    const auto ch = arena({{kFighter, {5, 5}}, {"t.orc", {16, 5}}});
    const CombatantId o = ch.spawns[1].stable_id;
    ecs::Registry r0, r1;
    auto c = Combat::create(r0, ch, lib, 5);
    REQUIRE(c);
    NoTarget idle;
    NearestEnemyPolicy nearest;
    // The fighter stands still for the first commands, so the orc has to close the distance itself.
    auto rr = run_to_end(*c, [&](CombatantId id) -> IActorPolicy* { return id == o ? static_cast<IActorPolicy*>(&nearest) : &idle; }, 40);
    CHECK(rr.error == "command limit reached");
    bool dashed = false;
    for (const auto& le : c->log()) dashed = dashed || (le.step.request.kind == ActionKind::Dash && le.step.request.aggressive);
    CHECK(dashed);
    auto again = replay(r1, ch, lib, 5, c->log());
    REQUIRE(again);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
    // The same fight plays on to its end from the replayed state without a refused command.
    auto rest = run_to_end(*again, [&](CombatantId) -> IActorPolicy* { return &nearest; });
    CHECK_MESSAGE(rest.ended, rest.error);
}
