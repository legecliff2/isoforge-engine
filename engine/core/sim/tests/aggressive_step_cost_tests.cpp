// fix/aggressive-step-cost: Combat::aggressive_step_ok prices a step like the Step itself (plan_step -> step_cost_ft: move
// modes, spell zones, another creature's space), so the creature's own movement after an Aggressive Dash runs out exactly
// where normal movement would. Water for a non-swimmer (10 ft), a difficult zone (Spike Growth, 10 ft), a swimmer (5 ft).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kMage = "test.character.ascmage";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json orc(const std::string& id, const char* speed) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["speed"] = Json::parse(speed);
    m["traits"] = Json::array({{{"id", "aggressive"}, {"name", "aggressive"}, {"description", "x"}, {"effect", {{"kind", "aggressive"}}}}});
    return m;
}
ContentLibrary make_lib() {
    ContentLibrary lib;
    Json sp = load_json("tests/validate/fixtures/zones/spells/spike_growth.json");
    sp["id"] = "testasc.spell.spike";
    sp["effects"][0]["zone"]["size_ft"] = 5;   // radius 1 tile
    { auto e = lib.add_spell(sp); INFO(e.value_or("")); REQUIRE_FALSE(e); }
    Json m = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    m["$schema"] = "character@4";
    m["id"] = kMage;
    m["hit_points"]["average"] = 2000;
    m["spellcasting"] = {{"ability", "int"}, {"caster_level", 9}, {"dc", 30}, {"slots", {{"1", 4}, {"2", 3}}},
                         {"spells", {"testasc.spell.spike"}}};
    REQUIRE_FALSE(lib.add(m, "character"));
    REQUIRE_FALSE(lib.add(orc("t.orc", R"({"walk":30})"), "monster"));
    REQUIRE_FALSE(lib.add(orc("t.swimorc", R"({"walk":30,"swim":30})"), "monster"));
    return lib;
}
world::Chunk arena(const std::string& who, const std::vector<Vec2i>& water) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i p : water) c.layers[0][std::size_t(p.y * 32 + p.x)].flags = world::tile_flags::kWater;
    c.spawns.push_back({kMage, Vec2i{2, 5}, world::spawn_stable_id(c.coord, 0), ""});
    c.spawns.push_back({who, Vec2i{14, 5}, world::spawn_stable_id(c.coord, 1), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::string& who, const std::vector<Vec2i>& water) : chunk(arena(who, water)), lib(&l) {
        c = Combat::create(reg, chunk, l, 5);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != who; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(settle(c->submit(e)).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    SubmitResult step(Vec2i to) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = to;
        return settle(c->submit(s));
    }
    void dash(CombatantId who) {
        Command d;
        d.request = {who, ActionKind::Dash};
        d.request.as_bonus_action = true;
        d.request.aggressive = true;
        auto r = settle(c->submit(d));
        REQUIRE_MESSAGE(r.ok, r.error);
        REQUIRE(c->state().budget.movement_ft == 60);   // 30 of its own, 30 from the Dash
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, 5, c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
// After the Dash the orc walks away: one dry step (5) and two dearer steps (`dear` each). Its own 30 ft are spent when
// 5 + 2 * dear exceeds 25; then a further step away must be refused and a step toward the mage allowed.
void walk_away(Fight& f, CombatantId o, int dear, bool third_allowed) {
    REQUIRE(f.step({15, 5}).ok);
    REQUIRE(f.step({16, 5}).ok);
    REQUIRE(f.step({17, 5}).ok);
    CHECK(f.c->state().budget.movement_ft == 60 - 5 - 2 * dear);
    CHECK(f.c->aggressive_step_ok(o, {18, 5}) == third_allowed);
    CHECK(f.c->aggressive_step_ok(o, {16, 5}));
    auto r = f.step({18, 5});
    CHECK(r.ok == third_allowed);
    if (!third_allowed) CHECK(r.error.find("aggressive") != std::string::npos);
}
}  // namespace

TEST_CASE("Aggressive Dash step cost: water for a non-swimmer costs 10 ft, so its own movement runs out like normal movement") {
    const auto lib = make_lib();
    Fight f(lib, "t.orc", {{16, 5}, {17, 5}, {18, 5}});
    const CombatantId o = f.id(1);
    f.advance_to(o);
    f.dash(o);
    walk_away(f, o, 10, false);   // 5 + 10 + 10 = 25: 5 left of its own, the next water step costs 10
    f.check_replay();
}

TEST_CASE("Aggressive Dash step cost: a swimmer pays 5 ft in water, unchanged") {
    const auto lib = make_lib();
    Fight f(lib, "t.swimorc", {{16, 5}, {17, 5}, {18, 5}});
    const CombatantId o = f.id(1);
    f.advance_to(o);
    f.dash(o);
    walk_away(f, o, 5, true);     // 5 + 5 + 5 = 15: 15 of its own left
    f.check_replay();
}

TEST_CASE("Aggressive Dash step cost: a difficult zone (Spike Growth) costs 10 ft per step") {
    const auto lib = make_lib();
    Fight f(lib, "t.orc", {});
    const CombatantId me = f.id(0), o = f.id(1);
    f.advance_to(me);
    Command cast;
    cast.request = {me, ActionKind::CastSpell, me};
    cast.request.spell_id = rules5e::spell_id_of("testasc.spell.spike");
    cast.request.spell_level = 2;
    cast.request.targets = {me};
    cast.request.has_zone_dest = true; cast.request.zone_x = 17; cast.request.zone_y = 5;   // difficult over x 16..18
    { auto r = f.settle(f.c->submit(cast)); REQUIRE_MESSAGE(r.ok, r.error); }
    f.advance_to(o);
    f.dash(o);
    walk_away(f, o, 10, false);
    f.check_replay();
}
