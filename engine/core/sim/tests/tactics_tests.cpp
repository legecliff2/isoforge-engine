// Line of sight, ranged attacks (range + LOS + disadvantage facts), Ready / readied Move.
#include <doctest/doctest.h>

#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

world::Chunk arena(std::vector<std::tuple<std::string, int, int>> spawns, std::vector<Vec2i> walls = {},
                   uint8_t wall_flags = world::tile_flags::kBlocksMove | world::tile_flags::kBlocksSight) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = wall_flags;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        auto& [a, x, y] = spawns[i];
        c.spawns.push_back({a, Vec2i{x, y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 4 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
uint8_t ranged_index(const rules5e::Combatant& k) {
    for (uint8_t i = 0; i < k.attacks.size(); ++i) if (k.attacks[i].ranged) return i;
    FAIL("no ranged attack");
    return 0;
}
bool has(const Combat& c, EventKind k, CombatantId actor, std::size_t from = 0) {
    for (std::size_t i = from; i < c.events().size(); ++i)
        if (c.events()[i].kind == SimEvent::Kind::Rules && c.events()[i].rules.kind == k && c.events()[i].rules.actor == actor) return true;
    return false;
}
} // namespace

TEST_CASE("line of sight: walls block, endpoints don't, corners need both sides, symmetric") {
    auto ch = arena({}, {{5, 5}, {10, 9}, {11, 10}});
    Grid g(ch);
    CHECK(g.blocks_sight({5, 5}));
    CHECK_FALSE(g.blocks_sight({6, 5}));
    CHECK_FALSE(g.line_of_sight({3, 5}, {7, 5}));      // straight through the wall
    CHECK(g.line_of_sight({3, 4}, {7, 4}));
    CHECK(g.line_of_sight({5, 5}, {8, 5}));            // standing in/next to a blocker endpoint: visible
    CHECK(g.line_of_sight({4, 5}, {5, 5}));
    // (10,10)->(11,9) passes exactly through the corner shared by (10,9) and (11,10): both block.
    CHECK_FALSE(g.line_of_sight({10, 10}, {11, 9}));
    auto ch2 = arena({}, {{10, 9}});
    CHECK(Grid(ch2).line_of_sight({10, 10}, {11, 9}));   // only one side blocks: visible
    // Symmetry and determinism over a pseudo-random wall field.
    std::vector<Vec2i> walls;
    uint32_t r = 12345;
    for (int i = 0; i < 120; ++i) { r = r * 1103515245u + 12345u; walls.push_back({int(r >> 8) % 32, int(r >> 20) % 32}); }
    Grid gw(arena({}, walls));
    for (int i = 0; i < 400; ++i) {
        r = r * 1103515245u + 12345u;
        const Vec2i a{int(r >> 4) % 32, int(r >> 12) % 32}, b{int(r >> 18) % 32, int(r >> 25) % 32};
        CHECK(gw.line_of_sight(a, b) == gw.line_of_sight(b, a));
    }
    // blocks_move alone does not block sight.
    Grid gm(arena({}, {{5, 5}}, world::tile_flags::kBlocksMove));
    CHECK(gm.line_of_sight({3, 5}, {7, 5}));
}

TEST_CASE("ranged attacks: long range limit, LOS, disadvantage facts from positions") {
    auto l = lib();
    // Fighter crossbow 80/320 ft (16/64 tiles).
    auto ch = arena({{kFighter, 2, 2}, {kGoblin, 20, 2}, {kGoblin, 2, 30}}, {{10, 10}, {10, 11}, {10, 12}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 5);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g1 = ch.spawns[1].stable_id, g2 = ch.spawns[2].stable_id;
    advance_to(*c, f);
    const uint8_t xb = ranged_index(*rules5e::find(c->state(), f));
    CHECK(c->can_shoot(f, xb, g1));                   // 18 tiles: long range
    CHECK(c->ranged_disadvantage(f, xb, g1));
    Command a;
    a.request = {f, ActionKind::Attack, g1};
    a.request.attack_index = xb;
    auto r = c->submit(a);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(c->log().back().step.request.beyond_normal_range);
    CHECK_FALSE(c->log().back().step.request.ranged_hostile_within_5ft);
    CHECK(has(*c, EventKind::AttackResolved, f));
    // Melee weapons still need reach.
    Command m;
    m.request = {f, ActionKind::Attack, g2};
    m.request.attack_index = 0;
    CHECK(c->submit(m).error == "target out of reach");
    // Replay reproduces the facts.
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, l, 5, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());

    // LOS blocked by a wall line; beyond long range rejected.
    auto ch2 = arena({{kFighter, 5, 11}, {kGoblin, 15, 11}}, {{10, 10}, {10, 11}, {10, 12}});
    ecs::Registry reg3;
    auto c2 = Combat::create(reg3, ch2, l, 5);
    advance_to(*c2, ch2.spawns[0].stable_id);
    Command b;
    b.request = {ch2.spawns[0].stable_id, ActionKind::Attack, ch2.spawns[1].stable_id};
    b.request.attack_index = xb;
    CHECK(c2->submit(b).error == "no line of sight");

    // Hostile adjacent => disadvantage; normal range => none.
    auto ch3 = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}, {kGoblin, 12, 5}});
    ecs::Registry reg4;
    auto c3 = Combat::create(reg4, ch3, l, 5);
    const CombatantId f3 = ch3.spawns[0].stable_id;
    CHECK(c3->ranged_disadvantage(f3, xb, ch3.spawns[2].stable_id));
    auto ch4 = arena({{kFighter, 5, 5}, {kGoblin, 12, 5}});
    ecs::Registry reg5;
    auto c4 = Combat::create(reg5, ch4, l, 5);
    CHECK_FALSE(c4->ranged_disadvantage(ch4.spawns[0].stable_id, xb, ch4.spawns[1].stable_id));
}

TEST_CASE("Ready attack: fires when the target ends a move within reach") {
    auto l = lib();
    auto ch = arena({{kFighter, 5, 5}, {kGoblin, 8, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    advance_to(*c, f);
    Command bad;
    bad.request = {f, ActionKind::Ready, g};
    bad.request.ready_kind = ActionKind::Attack;
    bad.request.trigger_id = ready_trigger_id(ReadyTriggerKind::TargetWithin, 3);   // beyond 5 ft reach
    CHECK(c->submit(bad).error == "trigger beyond reach");
    Command rd = bad;
    rd.request.trigger_id = ready_trigger_id(ReadyTriggerKind::TargetWithin, 1);
    REQUIRE(c->submit(rd).ok);
    Command end;
    end.request = {f, ActionKind::EndTurn};
    REQUIRE(c->submit(end).ok);
    advance_to(*c, g);
    const std::size_t n = c->events().size();
    Command st;
    st.kind = CommandKind::Step;
    st.to = {7, 5};   // still 2 tiles away: no trigger
    REQUIRE(c->submit(st).ok);
    CHECK_FALSE(has(*c, EventKind::ReadyTriggered, f, n));
    st.to = {6, 5};   // adjacent: fires
    auto r = c->submit(st);
    REQUIRE(r.status == StepStatus::NeedsDecision);   // readier asked (Ask policy)
    CHECK(r.needs->trigger == ReactionTrigger::ReadiedTrigger);
    r = c->settle([](const Combat&, const NeedsDecision& nd) { return ReactionDecision{nd.window_seq, true, 0}; });
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(has(*c, EventKind::ReadyTriggered, f, n));
    CHECK(has(*c, EventKind::AttackResolved, f, n));
    CHECK(rules5e::find(c->state(), f)->reaction_used);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, l, 3, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
}

TEST_CASE("readied Move: moves the readier to its tile when a hostile comes close; leaving reach provokes") {
    auto l = lib();
    // Fighter readies a move to (2,5), triggered by any hostile ending within 3 tiles. Goblin B
    // stands next to the fighter, so the readied move leaves B's reach (LeavingReach for B).
    auto ch = arena({{kFighter, 5, 5}, {kGoblin, 10, 5}, {kGoblin, 5, 6}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 8);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id, ga = ch.spawns[1].stable_id, gb = ch.spawns[2].stable_id;
    advance_to(*c, f);
    Command rd;
    rd.request = {f, ActionKind::Ready};
    rd.request.ready_kind = ActionKind::Move;
    rd.request.trigger_id = ready_trigger_id(ReadyTriggerKind::HostileWithin, 3);
    rd.to = {5, 6};   // occupied
    CHECK_FALSE(c->submit(rd).ok);
    rd.to = {30, 30};  // beyond speed
    CHECK(c->submit(rd).error == "readied move longer than speed");
    rd.to = {2, 5};
    auto r0 = c->submit(rd);
    REQUIRE_MESSAGE(r0.ok, r0.error);
    REQUIRE(c->readied_destination(f));
    CHECK(c->log().back().step.request.move_ft == 15);
    CHECK(*c->log().back().to == Vec2i{2, 5});
    Command end;
    end.request = {f, ActionKind::EndTurn};
    REQUIRE(c->submit(end).ok);
    advance_to(*c, ga);
    const std::size_t n = c->events().size();
    Command st;
    st.kind = CommandKind::Step;
    st.to = {9, 5};   // 4 tiles: no trigger
    REQUIRE(c->submit(st).ok);
    CHECK(c->pos(f) == Vec2i{5, 5});
    st.to = {8, 5};   // 3 tiles: fires; the readied move leaves gb's reach -> gb is asked
    auto r = c->submit(st);
    REQUIRE_MESSAGE(r.ok, r.error);
    // 1st window: the readier is asked whether to fire (ReadiedTrigger, Ask policy).
    REQUIRE(r.status == StepStatus::NeedsDecision);
    CHECK(r.needs->trigger == ReactionTrigger::ReadiedTrigger);
    CHECK(r.needs->reactor == f);
    CHECK(r.needs->trigger_actor == ga);
    r = c->decide({r.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    // 2nd window: the readied move leaves gb's reach.
    REQUIRE(r.status == StepStatus::NeedsDecision);
    CHECK(r.needs->trigger == ReactionTrigger::LeavingReach);
    CHECK(r.needs->reactor == gb);
    CHECK(r.needs->trigger_actor == f);
    CHECK(c->pos(f) == Vec2i{5, 5});   // paused: nothing applied
    r = c->decide({r.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(r.status == StepStatus::Done);
    CHECK(has(*c, EventKind::ReadyTriggered, f, n));
    CHECK(has(*c, EventKind::AttackResolved, gb, n));   // opportunity attack on the readier
    CHECK(c->pos(ga) == Vec2i{8, 5});
    if (rules5e::is_conscious(*rules5e::find(c->state(), f))) CHECK(c->pos(f) == Vec2i{2, 5});
    CHECK_FALSE(c->readied_destination(f));
    // Both TileMoved events, mover first (order of their Moved events).
    std::vector<CombatantId> moved;
    for (std::size_t i = n; i < c->events().size(); ++i)
        if (c->events()[i].kind == SimEvent::Kind::TileMoved) moved.push_back(c->events()[i].actor);
    CHECK(moved.back() == f);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, l, 8, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->pos(f) == c->pos(f));
}

TEST_CASE("policy: shoots when melee is out of reach this turn; closes to melee otherwise") {
    auto l = lib();
    auto ch = arena({{kFighter, 2, 5}, {kGoblin, 20, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 1);
    const CombatantId f = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    advance_to(*c, f);
    Command cmd = next_command(*c, g);
    CHECK(cmd.kind == CommandKind::Rules);
    CHECK(cmd.request.kind == ActionKind::Attack);
    CHECK(rules5e::find(c->state(), f)->attacks[cmd.request.attack_index].ranged);
    auto ch2 = arena({{kFighter, 2, 5}, {kGoblin, 7, 5}});
    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, ch2, l, 1);
    advance_to(*c2, ch2.spawns[0].stable_id);
    CHECK(next_command(*c2, ch2.spawns[1].stable_id).kind == CommandKind::Step);
}

TEST_CASE("Move requests carry move_tiles (tiles, not terrain cost); other requests and log_hash are unaffected") {
    auto l = lib();
    auto ch = arena({{kFighter, 5, 5}, {kGoblin, 20, 20}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    const CombatantId f = ch.spawns[0].stable_id;
    advance_to(*c, f);
    Command st;
    st.kind = CommandKind::Step;
    st.to = {6, 5};
    REQUIRE(c->submit(st).ok);
    st.to = {7, 5};
    REQUIRE(c->submit(st).ok);
    Command dodge;
    dodge.request = {f, ActionKind::Dodge};
    REQUIRE(c->submit(dodge).ok);
    REQUIRE(c->log().size() >= 3);
    const auto& lg = c->log();
    CHECK(lg[lg.size() - 3].step.request.move_tiles == 1);
    CHECK(lg[lg.size() - 2].step.request.move_tiles == 1);
    CHECK(lg[lg.size() - 1].step.request.move_tiles == 0);
    // not folded into log_hash: clearing the field on a copy of the log leaves the hash unchanged
    const uint64_t h = c->log_hash();
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, l, 3, lg, &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == h);
    auto stripped = lg;
    for (auto& e : stripped) e.step.request.move_tiles = 0;
    ecs::Registry reg3;
    auto rep2 = replay(reg3, ch, l, 3, stripped, &err);
    REQUIRE_MESSAGE(rep2, err);
    CHECK(rep2->log_hash() == h);
}
