#include <doctest/doctest.h>

#include <string>

#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";

ContentLibrary pack_content() {
    ContentLibrary lib;
    auto errs = lib.load_pack(kPack);
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    return lib;
}

world::Chunk sample_chunk() {
    world::HandcraftedProvider p({{world::ChunkCoord{0, 0, 0}, kPack + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    return *r.chunk;
}

// Open 32x32 floor; spawns given as (archetype, x, y). Optional wall/difficult tiles.
world::Chunk arena(std::vector<std::tuple<std::string, int, int>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        auto& [a, x, y] = spawns[i];
        c.spawns.push_back({a, Vec2i{x, y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

// End turns until `who` is the current actor.
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 4 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
} // namespace

TEST_CASE("parse_dice") {
    CHECK(parse_dice("1d6+2") == rules5e::DiceExpr{1, 6, 2});
    CHECK(parse_dice("2d8") == rules5e::DiceExpr{2, 8, 0});
    CHECK(parse_dice("1d4-1") == rules5e::DiceExpr{1, 4, -1});
    CHECK_FALSE(parse_dice("d6"));
    CHECK_FALSE(parse_dice("1x6"));
}

TEST_CASE("content: sample pack fighter and goblin become combatants") {
    auto lib = pack_content();
    const Archetype* g = lib.find(kGoblin);
    const Archetype* f = lib.find(kFighter);
    REQUIRE(g); REQUIRE(f);
    CHECK(g->base.ac == 15);
    CHECK(g->base.hp_max == 7);
    CHECK(g->base.speed_ft == 30);
    REQUIRE(g->base.attacks.size() == 2);
    CHECK(g->base.attacks[0].id == "scimitar");
    CHECK_FALSE(g->base.attacks[0].ranged);
    CHECK(g->base.attacks[1].ranged);
    CHECK(g->ai == "scripts/ai/skirmisher.luau");   // behavior.ai via monster 1->2 migration
    CHECK(f->base.is_pc);
    CHECK(f->base.uses_death_saves);
    CHECK(f->base.ac == 18);
    CHECK_FALSE(f->ai);
}

TEST_CASE("grid: walls block, ground required, deterministic paths") {
    const auto chunk = sample_chunk();
    Grid g(chunk);
    CHECK_FALSE(g.walkable({0, 0}));        // no ground
    CHECK(g.walkable({10, 15}));            // fighter spawn
    int walls = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (chunk.layers[2][std::size_t(y * 32 + x)].flags & world::tile_flags::kBlocksMove) {
                CHECK_FALSE(g.walkable({x, y}));
                ++walls;
            }
    CHECK(walls == 51);
    auto p1 = g.path({10, 15}, {{19, 15}}, {});
    auto p2 = g.path({10, 15}, {{19, 15}}, {});
    REQUIRE_FALSE(p1.empty());
    CHECK(p1 == p2);
    for (auto t : p1) CHECK(g.walkable(t));
    CHECK(g.path({10, 15}, {{10, 15}}, {}).empty());   // already there
}

TEST_CASE("grid: difficult terrain costs 10 ft and paths prefer to avoid it") {
    world::Chunk c = arena({});
    for (int y = 0; y < 32; ++y) c.layers[1][std::size_t(y * 32 + 5)].flags = world::tile_flags::kDifficult;
    for (int y = 0; y < 31; ++y) c.layers[2][std::size_t(y * 32 + 6)].flags = world::tile_flags::kBlocksMove;
    Grid g(c);
    CHECK(g.step_cost_ft({5, 3}) == 10);
    CHECK(g.step_cost_ft({4, 3}) == 5);
    auto p = g.path({4, 3}, {{7, 3}}, {});
    REQUIRE_FALSE(p.empty());
    CHECK(p.back() == Vec2i{7, 3});
    for (auto t : p) { const bool ok = t.x != 6 || t.y == 31; CHECK(ok); }   // only through the gap
}

namespace {
bool has_event(const Combat& c, rules5e::EventKind k, CombatantId actor) {
    for (auto& ev : c.events())
        if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == k && ev.rules.actor == actor) return true;
    return false;
}
} // namespace

TEST_CASE("leaving reach opens a rules5e window; sync decider reacts => opportunity attack; log replays") {
    auto lib = pack_content();
    auto chunk = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, chunk, lib, 7);
    REQUIRE(c);
    const CombatantId fighter = chunk.spawns[0].stable_id, goblin = chunk.spawns[1].stable_id;
    std::vector<NeedsDecision> asked;
    c->set_reaction_decider([&](const Combat& cc, const NeedsDecision& n) {
        asked.push_back(n);
        CHECK(cc.pos(goblin) == Vec2i{6, 5});   // polled before the mover leaves
        return ReactionDecision{n.window_seq, true, 0};
    });
    advance_to(*c, goblin);
    // Direct opportunity attacks are refused (rules5e #11).
    Command oa;
    oa.request = {goblin, ActionKind::OpportunityAttack, fighter};
    CHECK_FALSE(c->submit(oa).ok);
    Command step;
    step.kind = CommandKind::Step;
    step.to = {7, 5};
    auto r = c->submit(step);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(r.needs);   // sync adapter: answered inline
    REQUIRE(asked.size() == 1);
    CHECK(asked[0].reactor == fighter);
    CHECK(asked[0].trigger_actor == goblin);
    CHECK(asked[0].trigger == rules5e::ReactionTrigger::LeavingReach);
    REQUIRE_FALSE(asked[0].options.empty());
    REQUIRE(r.reactions.size() == 1);
    CHECK(r.reactions[0].window_seq == asked[0].window_seq);
    CHECK(has_event(*c, rules5e::EventKind::ReactionWindowOpened, goblin));
    CHECK(has_event(*c, rules5e::EventKind::ReactionTaken, fighter));
    CHECK(has_event(*c, rules5e::EventKind::AttackResolved, fighter));
    CHECK(rules5e::find(c->state(), fighter)->reaction_used);
    const auto* gob = rules5e::find(c->state(), goblin);
    if (rules5e::is_conscious(*gob)) CHECK(c->pos(goblin) == Vec2i{7, 5});
    else {
        CHECK(c->pos(goblin) == Vec2i{6, 5});   // dropped: move interrupted, no tile change
        CHECK(has_event(*c, rules5e::EventKind::MoveInterrupted, goblin));
    }
    // The log (commands + recorded decisions) replays exactly without a decider.
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, chunk, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    // Tampered decision: the log is authoritative, so replay follows it and diverges.
    auto bad = c->log();
    for (auto& e : bad)
        for (auto& rr : e.reactions) rr.decision.react = false;
    ecs::Registry reg3;
    auto rep3 = replay(reg3, chunk, lib, 7, bad, &err);
    const bool diverged = !rep3 || rep3->hash() != c->hash();
    CHECK(diverged);
}

TEST_CASE("declined reaction: move completes, no attack; only reactors actually left are polled") {
    auto lib = pack_content();
    auto chunk = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}, {kFighter, 7, 6}});
    ecs::Registry reg;
    auto c = Combat::create(reg, chunk, lib, 3);
    REQUIRE(c);
    const CombatantId f1 = chunk.spawns[0].stable_id, goblin = chunk.spawns[1].stable_id, f2 = chunk.spawns[2].stable_id;
    std::vector<CombatantId> polled;
    c->set_reaction_decider([&](const Combat&, const NeedsDecision& n) {
        polled.push_back(n.reactor);
        return ReactionDecision{n.window_seq, false, 0};
    });
    advance_to(*c, goblin);
    Command step;
    step.kind = CommandKind::Step;
    step.to = {6, 3};   // illegal: not adjacent
    CHECK_FALSE(c->submit(step).ok);
    step.to = {6, 4};   // f1 (5,5)->(6,4) dist 1: still in reach. f2 (7,6)->(6,4) dist 2: leaves
    auto r = c->submit(step);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(polled == std::vector<CombatantId>{f2});
    CHECK(has_event(*c, rules5e::EventKind::ReactionDeclined, f2));
    CHECK(c->pos(goblin) == Vec2i{6, 4});
    CHECK_FALSE(rules5e::find(c->state(), f2)->reaction_used);
    // No window when nobody's reach is left.
    polled.clear();
    const std::size_t n = c->events().size();
    if (c->state().budget.movement_ft >= 5) {
        step.to = {5, 4};   // f1 dist 1, f2 dist 2 (already outside)
        REQUIRE(c->submit(step).ok);
        CHECK(polled.empty());
        for (std::size_t i = n; i < c->events().size(); ++i)
            CHECK(c->events()[i].rules.kind != rules5e::EventKind::ReactionWindowOpened);
    }
    (void)f1;
}

TEST_CASE("step validation: walls, occupied tiles, movement budget; attacks need reach") {
    auto lib = pack_content();
    auto chunk = arena({{kFighter, 5, 5}, {kGoblin, 20, 20}});
    chunk.layers[2][std::size_t(5 * 32 + 6)].flags = world::tile_flags::kBlocksMove;
    ecs::Registry reg;
    auto c = Combat::create(reg, chunk, lib, 1);
    REQUIRE(c);
    const CombatantId f = chunk.spawns[0].stable_id, g = chunk.spawns[1].stable_id;
    advance_to(*c, f);
    Command s;
    s.kind = CommandKind::Step;
    s.to = {6, 5};
    CHECK(c->submit(s).error == "tile not walkable");
    Command atk;
    atk.request = {f, ActionKind::Attack, g};
    CHECK(c->submit(atk).error == "target out of reach/range");
    Command mv;
    mv.request = {f, ActionKind::Move};
    mv.request.move_ft = 5;
    CHECK_FALSE(c->submit(mv).ok);   // raw Move is sim's job
    // 30 ft = 6 steps, the 7th fails.
    Vec2i p{5, 5};
    for (int i = 0; i < 6; ++i) {
        s.to = {p.x, p.y + 1};
        REQUIRE(c->submit(s).ok);
        p = s.to;
    }
    s.to = {p.x, p.y + 1};
    CHECK(c->submit(s).error == "not enough movement");
    CHECK(reg.get<TilePos>(c->entity(f))->y == 11);   // ECS mirrors position
}

TEST_CASE("built-in policies: fighter vs goblin with NearestEnemyPolicy ends; replay from log matches") {
    auto lib = pack_content();
    auto chunk = sample_chunk();
    NearestEnemyPolicy nearest;
    for (uint64_t seed : {1u, 2u, 3u}) {
        ecs::Registry reg;
        auto c = Combat::create(reg, chunk, lib, seed);
        REQUIRE(c);
        auto rr = run_to_end(*c, [&](CombatantId) { return &nearest; });
        REQUIRE_MESSAGE(rr.ended, rr.error);
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, chunk, lib, seed, c->log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->hash() == c->hash());
        CHECK(rep->ended());
    }
}
