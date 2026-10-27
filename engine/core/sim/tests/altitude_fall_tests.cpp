// Altitude PR4 (sim side): ledge falls deal falling damage (1d6 per 10 ft, rules RNG) and land prone; one kAltFall event;
// rules-side falls (prone flyer, revoke_fly) surface as AltitudeChanged kAltFall from complete(); replay determinism.
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
namespace tf = world::tile_flags;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json goblin(const std::string& id, const Json& speed) {
    std::ifstream f(kRoot + "/content/packs/srd51.core/monsters/goblin.json");
    REQUIRE(f.good());
    Json m = Json::parse(f);
    m["$schema"] = "monster@3";
    m["id"] = "test.monster." + id;
    m["speed"] = speed;
    m["hit_points"]["average"] = 60;
    return m;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE_FALSE(l.add(goblin("walker", Json::parse(R"({"walk":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("flyer", Json::parse(R"({"walk":30,"fly":60})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("swimmer", Json::parse(R"({"walk":30,"swim":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("burrower", Json::parse(R"({"walk":30,"burrow":20})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("climber", Json::parse(R"({"walk":30,"climb":30})")), "monster"));
    REQUIRE_FALSE(l.add(goblin("foe", Json::parse(R"({"walk":30})")), "monster"));
    return l;
}
struct Cell { Vec2i p; uint8_t flags = 0; uint8_t elevation = 0; };
world::Chunk arena(const std::string& who, const std::vector<Cell>& cells, bool ground, std::optional<Vec2i> ally = std::nullopt) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    c.elevation_is_ground = ground;
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (const Cell& k : cells) {
        auto& t = c.layers[0][std::size_t(k.p.y * 32 + k.p.x)];
        t.flags = k.flags;
        t.elevation = k.elevation;
    }
    c.spawns.push_back({"test.monster." + who, Vec2i{4, 4}, world::spawn_stable_id(c.coord, 0), R"({"team":"a"})"});
    c.spawns.push_back({"test.monster.foe", Vec2i{28, 28}, world::spawn_stable_id(c.coord, 1), R"({"team":"b"})"});
    if (ally) c.spawns.push_back({"test.monster.walker", *ally, world::spawn_stable_id(c.coord, 2), R"({"team":"a"})"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    ContentLibrary l = lib();
    std::optional<Combat> c;
    CombatantId me = 0;
    Fight(const std::string& who, const std::vector<Cell>& cells, bool ground, std::optional<Vec2i> ally = std::nullopt)
        : chunk(arena(who, cells, ground, ally)) {
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        me = chunk.spawns[0].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != me; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == me);
    }
    SubmitResult step(Vec2i to) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = to;
        return c->submit(s);
    }
    SubmitResult vertical(int16_t d) {
        Command s;
        s.kind = CommandKind::Step;
        s.to = c->pos(me);
        s.request.alt_to = d;
        return c->submit(s);
    }
    void next_turn() {   // end turns until it is `me` again
        for (int i = 0; i < 7; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
            if (rules5e::current_actor(c->state()) == me) return;
        }
        FAIL("me never got a turn");
    }
    uint8_t last_mode() const { return c->log().back().step.request.move_mode; }
    int alt() const { return rules5e::find(c->state(), me)->alt_ft; }
    int left() const { return c->state().budget.movement_ft; }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, l, 1, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
int falls_events(const Combat& c) {
    int n = 0;
    for (const auto& e : c.events()) n += (e.kind == SimEvent::Kind::AltitudeChanged && e.rules.b == kAltFall) ? 1 : 0;
    return n;
}
int cause_events(const Combat& c, uint8_t cause, std::size_t from = 0) {
    int n = 0;
    const auto& ev = c.events();
    for (std::size_t i = from; i < ev.size(); ++i)
        n += (ev[i].kind == SimEvent::Kind::AltitudeChanged && ev[i].rules.b == cause) ? 1 : 0;
    return n;
}
void end_current(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), rules5e::ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
// Shover at (4,4), target at (5,4), the push lands on (6,4). `high` is the ledge elevation of the two standing tiles.
struct Shove {
    ecs::Registry reg;
    world::Chunk chunk{};   // value-init: other layers must be zero, or a stale water flag zeroes ground height
    ContentLibrary l = lib();
    std::optional<Combat> c;
    CombatantId shover = 0, target = 0;
    Shove(const std::string& target_kind, uint8_t high, uint8_t dest_elev) {
        chunk.coord = {5, 5, 0};
        chunk.origin = world::Chunk::Origin::Handcrafted;
        chunk.palette = {"", "floor"};
        chunk.elevation_is_ground = true;
        for (auto& cell : chunk.layers[0]) cell.tile = 1;
        for (Vec2i p : {Vec2i{4, 4}, Vec2i{5, 4}})
            chunk.layers[0][std::size_t(p.y * 32 + p.x)].elevation = high;
        chunk.layers[0][std::size_t(4 * 32 + 6)].elevation = dest_elev;
        chunk.spawns.push_back({"test.monster.walker", Vec2i{4, 4}, world::spawn_stable_id(chunk.coord, 0), R"({"team":"a"})"});
        chunk.spawns.push_back({"test.monster." + target_kind, Vec2i{5, 4}, world::spawn_stable_id(chunk.coord, 1), R"({"team":"b"})"});
        chunk.content_hash = world::compute_content_hash(chunk);
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        shover = chunk.spawns[0].stable_id;
        target = chunk.spawns[1].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != shover; ++i) end_current(*c);
        REQUIRE(rules5e::current_actor(c->state()) == shover);
    }
    // Contested shove: repeat until the target is pushed to (6,4). Returns the event index the successful shove started at.
    std::size_t push() {
        for (int round = 0; round < 40; ++round) {
            for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != shover; ++i) end_current(*c);
            REQUIRE(rules5e::current_actor(c->state()) == shover);
            const std::size_t at = c->events().size();
            Command x;
            x.request = {shover, rules5e::ActionKind::Attack, target};
            x.request.special = 3;
            const SubmitResult r = c->submit(x);
            REQUIRE_MESSAGE(r.ok, r.error);
            c->settle(nullptr);
            if (c->pos(target) == Vec2i{6, 4}) return at;
            end_current(*c);
        }
        FAIL("shove never landed");
        return 0;
    }
    void replay() {
        ecs::Registry r2;
        std::string err;
        auto again = eng::sim::replay(r2, chunk, l, 1, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
}  // namespace

TEST_CASE("altitude PR4: a ledge fall deals 1d6 per 10 ft and lands prone (one kAltFall event, replays)") {
    Fight f("walker", {{{4, 4}, 0, 10}}, true);   // 50 ft drop: 5d6
    const auto* me = rules5e::find(f.c->state(), f.me);
    const int hp0 = me->hp;
    rng::Rng r = f.c->state().rng;
    int want = 0;
    for (int i = 0; i < 5; ++i) want += r.d(6);
    REQUIRE(f.step({5, 4}).ok);
    me = rules5e::find(f.c->state(), f.me);
    CHECK(f.alt() == 0);
    CHECK(me->hp == hp0 - want);
    CHECK(me->has(rules5e::Condition::Prone));
    CHECK(falls_events(*f.c) == 1);
    REQUIRE(f.c->falls().size() == 1);
    CHECK(f.c->falls()[0].fell_ft == 50);
    f.check_replay();
    f.next_turn();
    f.check_replay();
}

TEST_CASE("altitude PR4: a flyer that steps off a ledge does not fall; a 5 ft drop is a step") {
    Fight g("flyer", {{{4, 4}, 0, 10}}, true);
    const int hp0 = rules5e::find(g.c->state(), g.me)->hp;
    REQUIRE(g.step({5, 4}).ok);
    CHECK(g.alt() == 50);
    CHECK(rules5e::find(g.c->state(), g.me)->hp == hp0);
    CHECK(falls_events(*g.c) == 0);
    Fight w("walker", {{{4, 4}, 0, 1}}, true);
    const int hw = rules5e::find(w.c->state(), w.me)->hp;
    REQUIRE(w.step({5, 4}).ok);
    CHECK(rules5e::find(w.c->state(), w.me)->hp == hw);
    CHECK_FALSE(rules5e::find(w.c->state(), w.me)->has(rules5e::Condition::Prone));
}

TEST_CASE("altitude PR4: identical ledge falls hash identically (deterministic)") {
    Fight a("walker", {{{4, 4}, 0, 8}}, true), b("walker", {{{4, 4}, 0, 8}}, true);
    REQUIRE(a.step({5, 4}).ok);
    REQUIRE(b.step({5, 4}).ok);
    CHECK(a.c->hash() == b.c->hash());
    CHECK(a.c->log_hash() == b.c->log_hash());
    a.check_replay();
}

TEST_CASE("forced move off a ledge: a shove drops a walker and holds a flyer aloft") {
    Shove walker("walker", 4, 0);   // 20 ft ledge, dest at 0
    const std::size_t at = walker.push();
    CHECK(walker.c->pos(walker.target) == Vec2i{6, 4});
    CHECK(rules5e::find(walker.c->state(), walker.target)->alt_ft == 0);
    CHECK(rules5e::find(walker.c->state(), walker.target)->has(rules5e::Condition::Prone));
    REQUIRE(walker.c->falls().size() >= 1);
    CHECK(walker.c->falls().back().id == walker.target);
    CHECK(walker.c->falls().back().fell_ft == 20);
    CHECK(cause_events(*walker.c, kAltFall, at) == 1);
    CHECK(cause_events(*walker.c, kAltForced, at) == 0);
    walker.replay();

    Shove flyer("flyer", 4, 0);
    const std::size_t af = flyer.push();
    const auto* ft = rules5e::find(flyer.c->state(), flyer.target);
    CHECK(ft->alt_ft == 20);
    CHECK(flyer.c->abs_height_ft(flyer.target) == 20);
    CHECK(flyer.c->airborne(flyer.target));
    CHECK_FALSE(ft->has(rules5e::Condition::Prone));
    CHECK(flyer.c->falls().empty());
    CHECK(cause_events(*flyer.c, kAltFall, af) == 0);
    CHECK(cause_events(*flyer.c, kAltForced, af) == 1);
    flyer.replay();

    Shove aloft("flyer", 4, 0);   // already 10 ft up: absolute height (30 ft) is kept over the drop
    REQUIRE(aloft.c->set_altitude(aloft.shover, 10, kAltForced));   // same absolute height, so the shove is in reach
    REQUIRE(aloft.c->set_altitude(aloft.target, 10, kAltForced));
    const std::size_t aa = aloft.push();
    CHECK(rules5e::find(aloft.c->state(), aloft.target)->alt_ft == 30);
    CHECK(aloft.c->abs_height_ft(aloft.target) == 30);
    CHECK(aloft.c->airborne(aloft.target));
    CHECK(cause_events(*aloft.c, kAltFall, aa) == 0);
    CHECK(cause_events(*aloft.c, kAltForced, aa) == 1);

    Shove step("walker", 1, 0);   // 5 ft: a step, not a fall
    step.push();
    CHECK(rules5e::find(step.c->state(), step.target)->alt_ft == 0);
    CHECK_FALSE(rules5e::find(step.c->state(), step.target)->has(rules5e::Condition::Prone));
    CHECK(step.c->falls().empty());
    step.replay();

    Shove up("walker", 0, 4);   // shoved onto higher ground: stands there, alt stays 0
    up.push();
    CHECK(up.c->pos(up.target) == Vec2i{6, 4});
    CHECK(rules5e::find(up.c->state(), up.target)->alt_ft == 0);
    CHECK_FALSE(rules5e::find(up.c->state(), up.target)->has(rules5e::Condition::Prone));
    CHECK(up.c->falls().empty());
    CHECK(up.c->abs_height_ft(up.target) == 20);
    up.replay();
}

TEST_CASE("forced move off a ledge: a spell push drops a walker and holds a flyer aloft") {
    Json doc = {{"$schema", "spell@1"}, {"id", "test.spell.ledge_push"}, {"name", "ledge push"}, {"level", 1},
                {"school", "evocation"}, {"casting_time", {{"kind", "action"}}},
                {"range", {{"kind", "ranged"}, {"feet", 60}}}, {"components", {{"v", true}, {"s", true}}},
                {"duration", {{"kind", "instantaneous"}}},
                {"effects", Json::array({{{"kind", "save"}, {"ability", "dex"}, {"damage", {{"dice", "1d1"}, {"type", "force"}}}, {"on_success", "none"}},
                                          {{"kind", "forced_move"}, {"applies_to", "affected"}, {"mode", "push"}, {"feet", 5}}})}};
    auto loaded = rules5e_content::spell_from_json(doc);
    REQUIRE(loaded.def);
    ContentLibrary l = lib();
    REQUIRE_FALSE(l.add_spell(doc));
    Json m;
    {
        std::ifstream f(kRoot + "/content/packs/sample.vertical_slice/characters/fighter_1.json");
        REQUIRE(f.good());
        m = Json::parse(f);
    }
    m["$schema"] = "character@4";
    m["id"] = "test.character.ledge_mage";
    m["hit_points"]["average"] = 80;
    m["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"dc", 30}, {"slots", {{"1", 4}}},
                         {"spells", {"test.spell.ledge_push"}}};
    REQUIRE_FALSE(l.add(m, "character"));

    auto run = [&](const std::string& kind) {
        world::Chunk chunk{};
        chunk.coord = {5, 5, 0};
        chunk.origin = world::Chunk::Origin::Handcrafted;
        chunk.palette = {"", "floor"};
        chunk.elevation_is_ground = true;
        for (auto& cell : chunk.layers[0]) cell.tile = 1;
        for (Vec2i p : {Vec2i{3, 4}, Vec2i{4, 4}})
            chunk.layers[0][std::size_t(p.y * 32 + p.x)].elevation = 4;   // 20 ft; dest (5,4) stays 0
        chunk.spawns.push_back({"test.character.ledge_mage", Vec2i{3, 4}, world::spawn_stable_id(chunk.coord, 0), ""});
        chunk.spawns.push_back({"test.monster." + kind, Vec2i{4, 4}, world::spawn_stable_id(chunk.coord, 1), ""});
        chunk.content_hash = world::compute_content_hash(chunk);
        ecs::Registry reg;
        auto c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
        const CombatantId mage = chunk.spawns[0].stable_id, tgt = chunk.spawns[1].stable_id;
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != mage; ++i) end_current(*c);
        REQUIRE(rules5e::current_actor(c->state()) == mage);
        Command cmd;
        cmd.request = {mage, rules5e::ActionKind::CastSpell, tgt};
        cmd.request.spell_id = rules5e::spell_id_of("test.spell.ledge_push");
        cmd.request.spell_level = 1;
        cmd.request.targets = {tgt};
        const std::size_t at = c->events().size();
        auto r = c->submit(cmd);
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
        CHECK(c->pos(tgt) == Vec2i{5, 4});
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, l, 1, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());
        struct Out { int alt; bool prone; bool air; int fell; int falls; int forced; };
        const auto* t = rules5e::find(c->state(), tgt);
        int fell = 0;
        for (const auto& f : c->falls())
            if (f.id == tgt) fell = f.fell_ft;
        return Out{t->alt_ft, t->has(rules5e::Condition::Prone), c->airborne(tgt), fell,
                   cause_events(*c, kAltFall, at), cause_events(*c, kAltForced, at)};
    };
    const auto w = run("walker");
    CHECK(w.alt == 0);
    CHECK(w.prone);
    CHECK_FALSE(w.air);
    CHECK(w.fell == 20);
    CHECK(w.falls == 1);
    CHECK(w.forced == 0);
    const auto f = run("flyer");
    CHECK(f.alt == 20);
    CHECK_FALSE(f.prone);
    CHECK(f.air);
    CHECK(f.fell == 0);
    CHECK(f.falls == 0);
    CHECK(f.forced == 1);
}

TEST_CASE("forced move off a ledge: a climber falls the absolute drop, or lands within 5 ft") {
    Shove fell("climber", 0, 0);
    REQUIRE(fell.c->set_altitude(fell.shover, 20, kAltForced));   // same height, so the shove is within reach
    REQUIRE(fell.c->set_altitude(fell.target, 20, kAltForced));
    const std::size_t at = fell.push();
    CHECK(rules5e::find(fell.c->state(), fell.target)->alt_ft == 0);
    CHECK(rules5e::find(fell.c->state(), fell.target)->has(rules5e::Condition::Prone));
    REQUIRE_FALSE(fell.c->falls().empty());
    CHECK(fell.c->falls().back().fell_ft == 20);
    CHECK(cause_events(*fell.c, kAltFall, at) == 1);
    CHECK(cause_events(*fell.c, kAltForced, at) == 0);

    Shove land("climber", 0, 4);   // dest ground is the climber's absolute height
    REQUIRE(land.c->set_altitude(land.shover, 20, kAltForced));
    REQUIRE(land.c->set_altitude(land.target, 20, kAltForced));
    const std::size_t al = land.push();
    CHECK(land.c->pos(land.target) == Vec2i{6, 4});
    CHECK(rules5e::find(land.c->state(), land.target)->alt_ft == 0);
    CHECK_FALSE(rules5e::find(land.c->state(), land.target)->has(rules5e::Condition::Prone));
    CHECK(land.c->abs_height_ft(land.target) == 20);
    CHECK(land.c->falls().empty());
    CHECK(cause_events(*land.c, kAltFall, al) == 0);
    CHECK(cause_events(*land.c, kAltForced, al) == 1);
}
