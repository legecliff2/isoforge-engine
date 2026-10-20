// R-G11 zones sim wiring (rules #118, VERSION_BUMP_BATCH 7.2l hand-off a-f): zone_contains geometry, Move / readied Move /
// forced-move membership lists, zone_changes (aura owner's move, zone move), legal_zone_tiles and the destination checks,
// terrain overlays, log_hash 0xB7 and Combat::hash 0xAB; #142's creature facts; plus #144's deferred cases (a grapple broken by
// a push, a frightened creature's teleport).
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/events.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;
using rules5e::TriggerOn;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kMage = "test.character.zmage";
const char* kFighter = "test.character.zfighter";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json fixture(const char* name, const char* id) {
    Json j = load_json(std::string("tests/validate/fixtures/zones/spells/") + name + ".json");
    j["id"] = id;
    return j;
}
Json spike() {   // a 5 ft (radius 1 tile) Spike Growth: difficult, 2d4 per 5 ft moved inside
    Json j = fixture("spike_growth", "testz.spell.spike");
    j["effects"][0]["zone"]["size_ft"] = 5;
    return j;
}
Json moonbeam() { return fixture("moonbeam", "testz.spell.moonbeam"); }
Json guardians() {
    Json j = fixture("spirit_guardians", "testz.spell.guardians");
    j["effects"][0]["zone"]["size_ft"] = 5;
    j["effects"][0]["zone"].erase("needs_conscious_owner");
    return j;
}
Json simple(const std::string& name, int level, Json effects, Json extra = Json::object()) {
    Json j = {{"$schema", "spell@1"}, {"id", "testz.spell." + name}, {"name", name}, {"level", level}, {"school", "evocation"},
              {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
              {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}}, {"effects", std::move(effects)}};
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return j;
}
Json push_bolt() {   // Dex save (DC 30: always fails), then a 10 ft push
    return simple("push", 1, Json::array({{{"kind", "save"}, {"ability", "dex"}, {"damage", {{"dice", "1d1"}, {"type", "force"}}}, {"on_success", "none"}},
                                          {{"kind", "forced_move"}, {"applies_to", "affected"}, {"mode", "push"}, {"feet", 10}}}));
}
Json fear() {        // frightened, no save
    return simple("fear", 1, Json::array({{{"kind", "apply_condition"}, {"condition", "frightened"}}}),
                  {{"duration", {{"kind", "minutes"}, {"amount", 1}}}});
}
Json misty() {
    return simple("misty", 2, Json::array({{{"kind", "teleport"}, {"max_feet", 30}}}),
                  {{"casting_time", {{"kind", "bonus_action"}}}, {"range", {{"kind", "self"}}}});
}

ContentLibrary make_lib() {
    ContentLibrary lib;
    for (const Json& d : {spike(), moonbeam(), guardians(), push_bolt(), fear(), misty()}) {
        auto e = lib.add_spell(d);
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    Json m = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    m["$schema"] = "character@4";
    m["id"] = kMage;
    m["hit_points"]["average"] = 300;
    m["spellcasting"] = {{"ability", "int"}, {"caster_level", 9}, {"dc", 30}, {"slots", {{"1", 4}, {"2", 3}, {"3", 3}}},
                         {"spells", {"testz.spell.spike", "testz.spell.moonbeam", "testz.spell.guardians", "testz.spell.push",
                                     "testz.spell.fear", "testz.spell.misty"}}};
    REQUIRE_FALSE(lib.add(m, "character"));
    Json f = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    f["id"] = kFighter;
    f["abilities"]["str"] = 20;
    f["hit_points"]["average"] = 300;
    REQUIRE_FALSE(lib.add(f, "character"));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = "test.monster.zgob";
    g["hit_points"]["average"] = 400;
    g["abilities"]["str"] = 3; g["abilities"]["dex"] = 3;
    REQUIRE_FALSE(lib.add(g, "monster"));
    Json o = g;
    o["id"] = "test.monster.zodd";
    o["no_breath"] = true; o["shapechanger"] = true; o["evil"] = true;
    REQUIRE_FALSE(lib.add(o, "monster"));
    Json w = g;   // fix/zone-terrain-in-water: a goblin with a swim speed
    w["id"] = "test.monster.zswim";
    w["speed"] = Json::parse(R"({"walk":30,"swim":30})");
    REQUIRE_FALSE(lib.add(w, "monster"));
    return lib;
}
world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns, const std::vector<Vec2i>& water = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i p : water) c.layers[0][std::size_t(p.y * 32 + p.x)].flags = world::tile_flags::kWater;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    uint64_t seed;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns, uint64_t s = 1,
          const std::vector<Vec2i>& water = {}) : lib(&l), seed(s) {
        chunk = arena(spawns, water);
        c = Combat::create(reg, chunk, l, s);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(settle(c->submit(e)).ok);
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != who; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    Command cast_cmd(const char* sp, uint8_t level, CombatantId target) const {
        Command cmd;
        cmd.request = {rules5e::current_actor(c->state()), ActionKind::CastSpell, target};
        cmd.request.spell_id = spell_id_of(sp);
        cmd.request.spell_level = level;
        if (target) cmd.request.targets = {target};
        return cmd;
    }
    Command zone_at(const char* sp, uint8_t level, Vec2i t) const {
        Command cmd = cast_cmd(sp, level, rules5e::current_actor(c->state()));
        cmd.request.has_zone_dest = true; cmd.request.zone_x = t.x; cmd.request.zone_y = t.y;
        return cmd;
    }
    SubmitResult step(Vec2i to) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = to;
        return settle(c->submit(s));
    }
    int triggers(TriggerOn on, CombatantId subject) const {
        int n = 0;
        for (const auto& e : c->events())
            n += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered && e.rules.b == int32_t(on) &&
                 e.rules.target == subject;
        return n;
    }
    int count(EventKind k) const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k;
        return n;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, seed, c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
        for (const auto& k : c->state().combatants) CHECK(again->pos(k.id) == c->pos(k.id));
    }
    void refused(const Command& cmd) {
        const uint64_t h = c->hash(), lh = c->log_hash();
        const std::size_t n = c->log().size();
        CHECK_FALSE(settle(c->submit(cmd)).ok);
        CHECK(c->hash() == h);
        CHECK(c->log_hash() == lh);
        CHECK(c->log().size() == n);
    }
};
}  // namespace

TEST_CASE("R-G11 sim: #142 creature facts no_breath / shapechanger / evil reach rules5e") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 2}}, {"test.monster.zodd", {9, 9}}, {"test.monster.zgob", {12, 9}}});
    const auto& odd = *rules5e::find(f.c->state(), f.id(1));
    CHECK(odd.no_breath); CHECK(odd.shapechanger); CHECK(odd.evil);
    const auto& gob = *rules5e::find(f.c->state(), f.id(2));
    CHECK_FALSE(gob.no_breath); CHECK_FALSE(gob.shapechanger); CHECK_FALSE(gob.evil);
}

TEST_CASE("R-G11 sim: Spike Growth destination, difficult overlay, MoveWithin on a step and per 5 ft of a push; 0xAB; replay") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {"test.monster.zgob", {8, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    const auto* def = f.c->spell_def(spell_id_of("testz.spell.spike"));
    REQUIRE(def);
    const auto tiles = f.c->legal_zone_tiles(me, *def);
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{12, 5}) != tiles.end());
    for (std::size_t i = 1; i < tiles.size(); ++i)
        CHECK((tiles[i - 1].y < tiles[i].y || (tiles[i - 1].y == tiles[i].y && tiles[i - 1].x < tiles[i].x)));
    const uint64_t before = f.c->hash();
    f.refused(f.cast_cmd("testz.spell.spike", 2, me));        // a point zone needs a destination
    f.refused(f.zone_at("testz.spell.spike", 2, {2, 40}));    // beyond 150 ft / out of bounds
    REQUIRE(f.settle(f.c->submit(f.zone_at("testz.spell.spike", 2, {10, 5}))).ok);   // covers x 9..11
    CHECK(f.count(EventKind::ZoneCreated) == 1);
    CHECK(rules5e::has_zone_state(f.c->state()));
    CHECK(f.c->hash() != before);
    CHECK(f.c->step_cost_ft(gob, {8, 5}, {9, 5}) == 10);      // difficult
    CHECK(f.c->step_cost_ft(gob, {7, 5}, {8, 5}) == 5);
    f.check_replay();
    f.advance_to(gob);
    const int hp0 = f.c->state().combatants[1].hp;
    REQUIRE(f.step({9, 5}).ok);                                // enters: one MoveWithin
    CHECK(f.triggers(TriggerOn::ZoneMoveWithin, gob) == 1);
    CHECK(f.c->state().budget.movement_ft == rules5e::find(f.c->state(), gob)->speed_ft - 10);
    REQUIRE(f.step({10, 5}).ok);                               // within: another
    CHECK(f.triggers(TriggerOn::ZoneMoveWithin, gob) == 2);
    CHECK(rules5e::find(f.c->state(), gob)->hp < hp0);
    f.check_replay();
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("testz.spell.push", 1, gob))).ok);   // (10,5) -> (12,5): 5 ft within, then out
    CHECK(f.c->pos(gob) == Vec2i{12, 5});
    CHECK(f.triggers(TriggerOn::ZoneMoveWithin, gob) == 3);   // one more: (10,5) -> (11,5) inside
    f.check_replay();
}

TEST_CASE("R-G11 sim: Moonbeam moves (zone_move_ft, zone_changes, Enter on the moved-onto creature); 0xB7; replay") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {2, 5}}, {"test.monster.zgob", {10, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.zone_at("testz.spell.moonbeam", 2, {4, 9}))).ok);
    const uint32_t zid = f.c->state().zones.at(0).zone_id;
    f.end_turn();
    f.advance_to(me);
    auto mv = [&](Vec2i t) {
        Command c = f.cast_cmd("testz.spell.moonbeam", 2, me);
        c.request.zone_move = zid; c.request.has_zone_dest = true; c.request.zone_x = t.x; c.request.zone_y = t.y;
        return c;
    };
    f.refused(mv({30, 9}));   // beyond 60 ft of the anchor
    REQUIRE(f.settle(f.c->submit(mv({10, 6}))).ok);   // radius 1: covers the goblin at (10,5)
    const auto& logged = f.c->log().back().step.request;
    CHECK(logged.zone_move_ft == 30);
    REQUIRE(logged.zone_changes.size() == 1);
    CHECK(logged.zone_changes[0].creature == gob);
    CHECK(logged.zone_changes[0].entered);
    CHECK(f.count(EventKind::ZoneMoved) == 1);
    f.check_replay();
    auto log = f.c->log();   // the destination is a caller input (0xB7)
    log.back().step.request.zone_y = 7;
    ecs::Registry r2;
    auto other = replay(r2, f.chunk, lib, f.seed, log);
    if (other) CHECK(other->log_hash() != f.c->log_hash());
}

TEST_CASE("R-G11 sim: Spirit Guardians aura halves speed for others, not the excluded; aura owner's move reports zone_changes") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {5, 5}}, {"test.monster.zgob", {9, 5}}, {kFighter, {5, 7}}});
    const CombatantId me = f.id(0), gob = f.id(1), ally = f.id(2);
    f.advance_to(me);
    Command c = f.cast_cmd("testz.spell.guardians", 3, me);
    c.request.zone_excluded = {ally};
    f.refused(f.zone_at("testz.spell.guardians", 3, {5, 5}));   // an aura has no destination
    REQUIRE(f.settle(f.c->submit(c)).ok);
    CHECK(f.c->step_cost_ft(gob, {7, 5}, {6, 5}) == 10);        // halved speed inside (5 ft tile costs 10)
    CHECK(f.c->step_cost_ft(ally, {5, 7}, {5, 6}) == 5);        // excluded
    REQUIRE(f.step({6, 5}).ok);                                  // the owner steps toward the goblin: no change yet (radius 1)
    REQUIRE(f.step({7, 5}).ok);                                  // now (9,5) is within? distance 1 from (7,5)? no: 2
    REQUIRE(f.step({8, 5}).ok);                                  // goblin at (9,5) enters the aura through the owner's move
    const auto& q = f.c->log().back().step.request;
    REQUIRE(q.zone_changes.size() == 1);
    CHECK(q.zone_changes[0].creature == gob);
    CHECK(q.zone_changes[0].entered);
    f.check_replay();
}

TEST_CASE("R-G9 deferred (#144): a push that breaks a grapple ends it") {
    const auto lib = make_lib();
    bool done = false;
    for (uint64_t seed = 1; seed <= 30 && !done; ++seed) {
        Fight f(lib, {{kMage, {5, 7}}, {kFighter, {5, 5}}, {"test.monster.zgob", {6, 5}}}, seed);
        const CombatantId me = f.id(0), ftr = f.id(1), gob = f.id(2);
        f.advance_to(ftr);
        Command g;
        g.request = {ftr, ActionKind::Attack, gob};
        g.request.special = 1;   // grapple
        g.request.target_within_5ft = true;
        if (!f.settle(f.c->submit(g)).ok || !rules5e::find(f.c->state(), gob)->has(Condition::Grappled)) continue;
        f.advance_to(me);
        const int removed = f.count(EventKind::ConditionRemoved);
        REQUIRE(f.settle(f.c->submit(f.cast_cmd("testz.spell.push", 1, gob))).ok);   // away from (5,7): to (8,3)
        CHECK(f.c->distance_tiles(ftr, gob) > 1);
        CHECK_FALSE(rules5e::find(f.c->state(), gob)->has(Condition::Grappled));
        CHECK(f.count(EventKind::ConditionRemoved) > removed);
        f.check_replay();
        done = true;
    }
    CHECK(done);
}

TEST_CASE("R-G9 deferred (#144): a frightened creature cannot teleport closer to its frightener") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {10, 5}}, {kMage, {14, 5}}, {"test.monster.zgob", {28, 28}}});   // a far hostile keeps the encounter going
    const CombatantId scarer = f.id(0), me = f.id(1);
    f.advance_to(scarer);
    const auto fr = f.settle(f.c->submit(f.cast_cmd("testz.spell.fear", 1, me)));
    INFO(fr.error);
    REQUIRE(fr.ok);
    REQUIRE(rules5e::condition_source(f.c->state(), me, Condition::Frightened) == scarer);
    f.advance_to(me);
    const auto* def = f.c->spell_def(spell_id_of("testz.spell.misty"));
    REQUIRE(def);
    const auto tiles = f.c->legal_teleport_tiles(me, *def);
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{12, 5}) == tiles.end());   // closer
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{14, 7}) != tiles.end());   // same distance
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{18, 5}) != tiles.end());   // farther
    auto to = [&](int x, int y) {
        Command c = f.cast_cmd("testz.spell.misty", 2, me);
        c.request.has_teleport_dest = true; c.request.teleport_x = x; c.request.teleport_y = y;
        return c;
    };
    f.refused(to(12, 5));
    REQUIRE(f.settle(f.c->submit(to(18, 5))).ok);
    CHECK(f.c->pos(me) == Vec2i{18, 5});
    f.check_replay();
}

TEST_CASE("zone terrain in water: a difficult zone stacks with swimming for a non-swimmer (15 ft), once with a creature; replay") {
    const auto lib = make_lib();
    std::vector<Vec2i> pool;
    for (int x = 8; x <= 12; ++x) pool.push_back({x, 5});
    Fight f(lib, {{kMage, {2, 5}}, {"test.monster.zgob", {8, 5}}, {"test.monster.zgob", {9, 5}}, {"test.monster.zswim", {8, 6}}},
            1, pool);
    const CombatantId me = f.id(0), gob = f.id(1), sw = f.id(3);
    CHECK(f.c->step_cost_ft(gob, {9, 6}, {10, 5}) == 10);         // water, no swim speed
    CHECK(f.c->step_cost_ft(sw, {8, 6}, {9, 5}) == 5);            // swimmer in water
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.zone_at("testz.spell.spike", 2, {10, 5}))).ok);   // difficult over x 9..11
    CHECK(f.c->step_cost_ft(gob, {9, 6}, {10, 5}) == 15);         // 5 + 5 (swimming) + 5 (difficult)
    CHECK(f.c->step_cost_ft(gob, {8, 5}, {9, 5}, 1, true) == 15); // zone + a creature's space: difficult once
    CHECK(f.c->step_cost_ft(sw, {8, 6}, {9, 5}) == 10);           // swimmer: difficult only
    CHECK(f.c->step_cost_ft(gob, {7, 6}, {8, 6}) == 5);           // dry, outside the zone
    f.check_replay();
    f.advance_to(gob);
    REQUIRE(f.step({9, 5}).ok);                                   // water + zone + the ally's space: still 15
    CHECK(f.c->state().budget.movement_ft == rules5e::find(f.c->state(), gob)->speed_ft - 15);
    REQUIRE(f.step({10, 5}).ok);                                  // water + zone: 15
    CHECK(f.c->state().budget.movement_ft == rules5e::find(f.c->state(), gob)->speed_ft - 30);
    f.check_replay();
}
