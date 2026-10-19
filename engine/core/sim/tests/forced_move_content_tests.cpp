// R-G9 sim wiring (rules #112, VERSION_BUMP_BATCH 7.2k hand-off a / b / d, FORCED_MOVEMENT.md §3): spell@1 forced_move /
// teleport effects load; sim resolves pushes on its grid (8-way direction, tile by tile, stop at the last free tile, farthest
// first) and self-teleports to a caller-chosen, re-validated destination; log_hash folds 0xB4 / 0xB2; everything replays.
#include <doctest/doctest.h>

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
using rules5e::EventKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kMage = "test.character.mage";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json spell(const std::string& name, int level, Json effects, Json extra = Json::object()) {
    Json j = {{"$schema", "spell@1"}, {"id", "test.spell." + name}, {"name", name}, {"level", level}, {"school", "evocation"},
              {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
              {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}}, {"effects", std::move(effects)}};
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return j;
}
Json push_rider(int feet) { return {{"kind", "forced_move"}, {"applies_to", "affected"}, {"mode", "push"}, {"feet", feet}}; }
Json shove_bolt_doc() {   // one target, a Dex save that always fails (DC 30), then a 10 ft push
    return spell("shove_bolt", 1, Json::array({{{"kind", "save"}, {"ability", "dex"}, {"damage", {{"dice", "1d1"}, {"type", "force"}}}, {"on_success", "none"}}, push_rider(10)}));
}
Json wave_doc() {         // a 5 ft sphere at range, then a 10 ft push for every failed save
    return spell("wave", 1, Json::array({{{"kind", "save"}, {"ability", "dex"}, {"damage", {{"dice", "1d1"}, {"type", "force"}}}, {"on_success", "none"}}, push_rider(10)}),
                 {{"area", {{"shape", "sphere"}, {"size_ft", 5}}}});
}
Json misty_doc() {
    return spell("misty", 2, Json::array({{{"kind", "teleport"}, {"max_feet", 30}}}),
                 {{"school", "conjuration"}, {"casting_time", {{"kind", "bonus_action"}}}, {"range", {{"kind", "self"}}}});
}
rules5e::SpellDef def_of(const Json& j) {
    auto r = rules5e_content::spell_from_json(j);
    INFO(r.error);
    REQUIRE(r.def);
    return *r.def;
}
std::string error_of(const Json& j) {
    auto r = rules5e_content::spell_from_json(j);
    CHECK_FALSE(r.def);
    return r.error;
}

ContentLibrary make_lib() {
    ContentLibrary lib;
    for (const Json& d : {shove_bolt_doc(), wave_doc(), misty_doc()}) REQUIRE_FALSE(lib.add_spell(d));
    Json m = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    m["$schema"] = "character@4";
    m["id"] = kMage;
    m["hit_points"]["average"] = 200;
    m["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"dc", 30}, {"slots", {{"1", 4}, {"2", 3}}},
                         {"spells", {"test.spell.shove_bolt", "test.spell.wave", "test.spell.misty"}}};
    REQUIRE_FALSE(lib.add(m, "character"));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = "test.monster.gob";
    g["hit_points"]["average"] = 300;
    REQUIRE_FALSE(lib.add(g, "monster"));
    return lib;
}
world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns) {
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
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns) : lib(&l) {
        chunk = arena(spawns);
        c = Combat::create(reg, chunk, l, 1);
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
    std::vector<rules5e::Event> moved(int32_t flag) const {
        std::vector<rules5e::Event> out;
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Moved && (e.rules.c & flag)) out.push_back(e.rules);
        return out;
    }
    int tile_moves(CombatantId who) const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::TileMoved && e.actor == who;
        return n;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, 1, c->log(), &err);
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

TEST_CASE("R-G9 loader: forced_move and teleport effects") {
    const auto sb = def_of(shove_bolt_doc());
    REQUIRE(sb.extra_effects.size() == 1);
    CHECK(sb.extra_effects[0].kind == rules5e::SpellEffectKind::ForcedMove);
    CHECK(sb.extra_effects[0].forced.mode == 1);
    CHECK(sb.extra_effects[0].forced.feet == 10);
    CHECK_FALSE(sb.extra_effects[0].forced.up_to);
    CHECK(sb.extra_effects[0].affected_only);
    const auto ms = def_of(misty_doc());
    CHECK(ms.kind == rules5e::SpellEffectKind::Teleport);
    CHECK(ms.teleport.max_feet == 30);
    CHECK(ms.teleport.needs_sight);
    Json up = shove_bolt_doc();
    up["effects"][1]["up_to"] = true; up["effects"][1]["mode"] = "pull"; up["effects"][1]["max_size"] = "large";
    const auto u = def_of(up);
    CHECK(u.extra_effects[0].forced.up_to);
    CHECK(u.extra_effects[0].forced.mode == 2);
    CHECK(u.extra_effects[0].forced.max_size == int8_t(rules5e::Size::Large));

    auto with = [](Json j, const char* k, Json v) { j["effects"][1][k] = std::move(v); return j; };
    CHECK(error_of(with(shove_bolt_doc(), "provokes_oa", true)).find("provokes_oa") != std::string::npos);
    CHECK(error_of(with(shove_bolt_doc(), "mode", "line")).find("push or pull") != std::string::npos);
    CHECK(error_of(with(shove_bolt_doc(), "feet", 7)).find("multiple of 5") != std::string::npos);
    CHECK(error_of(with(shove_bolt_doc(), "max_size", "colossal")).find("max_size") != std::string::npos);
    Json tp_extra = shove_bolt_doc();
    tp_extra["effects"][1] = {{"kind", "teleport"}, {"max_feet", 30}};
    CHECK(error_of(tp_extra).find("only allowed as the first effect") != std::string::npos);
    Json no_max = misty_doc();
    no_max["effects"][0].erase("max_feet");
    CHECK(error_of(no_max).find("max_feet") != std::string::npos);
}

TEST_CASE("R-G9 sim: a push walks tile by tile and stops at the last free tile; replay") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {5, 5}}, {"test.monster.gob", {6, 5}}, {"test.monster.gob", {8, 5}}});
    const CombatantId me = f.id(0), front = f.id(1), back = f.id(2);
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("test.spell.shove_bolt", 1, front))).ok);
    const auto mv = f.moved(kMovedForced);
    REQUIRE(mv.size() == 1);
    CHECK(mv[0].actor == front);
    CHECK(mv[0].target == me);
    CHECK(mv[0].a == 5);            // 10 ft requested, blocked after one tile by the creature behind
    CHECK(f.c->pos(front) == Vec2i{7, 5});
    CHECK(f.c->pos(back) == Vec2i{8, 5});
    CHECK(f.tile_moves(front) == 1);
    f.check_replay();
}

TEST_CASE("R-G9 sim: several pushes resolve farthest first, so the front creature is not blocked; diagonal push") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {5, 5}}, {"test.monster.gob", {8, 5}}, {"test.monster.gob", {9, 5}}, {"test.monster.gob", {6, 6}}});
    const CombatantId me = f.id(0), near = f.id(1), far = f.id(2), diag = f.id(3);
    f.advance_to(me);
    Command aim = f.cast_cmd("test.spell.wave", 1, 0);
    aim.request.targets.clear();
    aim.aim = true;
    aim.to = Vec2i{8, 5};
    REQUIRE(f.settle(f.c->submit(aim)).ok);
    const auto mv = f.moved(kMovedForced);
    REQUIRE(mv.size() == 2);
    CHECK(mv[0].actor == far);      // farthest from the caster first
    CHECK(mv[1].actor == near);
    CHECK(f.c->pos(far) == Vec2i{11, 5});
    CHECK(f.c->pos(near) == Vec2i{10, 5});
    f.check_replay();
    f.end_turn();
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("test.spell.shove_bolt", 1, diag))).ok);
    CHECK(f.c->pos(diag) == Vec2i{8, 8});   // 8-way: straight away along the diagonal
    f.check_replay();
}

TEST_CASE("R-G9 sim: Misty Step teleports to a re-validated destination; refusals change nothing; 0xB2 / 0xB4; replay") {
    const auto lib = make_lib();
    Fight f(lib, {{kMage, {5, 5}}, {"test.monster.gob", {7, 5}}});
    const CombatantId me = f.id(0), gob = f.id(1);
    f.advance_to(me);
    const auto* def = f.c->spell_def(spell_id_of("test.spell.misty"));
    REQUIRE(def);
    const auto tiles = f.c->legal_teleport_tiles(me, *def);
    REQUIRE_FALSE(tiles.empty());
    for (std::size_t i = 1; i < tiles.size(); ++i)   // row-major
        CHECK((tiles[i - 1].y < tiles[i].y || (tiles[i - 1].y == tiles[i].y && tiles[i - 1].x < tiles[i].x)));
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{7, 5}) == tiles.end());   // occupied
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{5, 5}) == tiles.end());   // its own tile
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{11, 5}) != tiles.end());  // 30 ft
    CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{12, 5}) == tiles.end());  // 35 ft

    auto to = [&](int x, int y) {
        Command c = f.cast_cmd("test.spell.misty", 2, me);
        c.request.has_teleport_dest = true; c.request.teleport_x = x; c.request.teleport_y = y;
        return c;
    };
    f.refused(f.cast_cmd("test.spell.misty", 2, me));   // no destination
    f.refused(to(12, 5));                               // beyond 30 ft
    f.refused(to(7, 5));                                // occupied
    REQUIRE(f.settle(f.c->submit(to(5, 11))).ok);
    CHECK(f.c->pos(me) == Vec2i{5, 11});
    const auto mv = f.moved(kMovedTeleport);
    REQUIRE(mv.size() == 1);
    CHECK(mv[0].actor == me);
    CHECK(mv[0].a == 30);
    CHECK(f.tile_moves(me) == 1);
    CHECK(f.c->log().back().step.request.teleport_feet == 30);   // sim fills the distance
    CHECK(f.c->state().budget.action);                            // a bonus action
    f.check_replay();
    // the destination is folded (0xB2): the same log with another legal destination hashes differently
    auto log = f.c->log();
    log.back().step.request.teleport_x = 4;   // (4, 11): 30 ft too
    ecs::Registry r2;
    auto other = replay(r2, f.chunk, lib, 1, log);
    REQUIRE(other);
    CHECK(other->log_hash() != f.c->log_hash());
    CHECK(other->pos(me) == Vec2i{4, 11});
    (void)gob;
}
