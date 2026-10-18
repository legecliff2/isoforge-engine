#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "assets/content_migrations.hpp"

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
    auto errs = lib.load(sample_set());
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

bool has_event(const Combat& c, rules5e::EventKind k, CombatantId actor) {
    for (auto& ev : c.events())
        if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == k && ev.rules.actor == actor) return true;
    return false;
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

TEST_CASE("leaving reach: submit pauses with NeedsDecision; decide() resumes => opportunity attack; log replays") {
    auto lib = pack_content();
    auto chunk = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, chunk, lib, 7);
    REQUIRE(c);
    const CombatantId fighter = chunk.spawns[0].stable_id, goblin = chunk.spawns[1].stable_id;
    advance_to(*c, goblin);
    // Direct opportunity attacks are refused (rules5e #11).
    Command oa;
    oa.request = {goblin, ActionKind::OpportunityAttack, fighter};
    CHECK_FALSE(c->submit(oa).ok);
    const std::size_t log0 = c->log().size(), ev0 = c->events().size();
    Command step;
    step.kind = CommandKind::Step;
    step.to = {7, 5};
    auto r = c->submit(step);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(r.status == StepStatus::NeedsDecision);
    REQUIRE(r.needs);
    CHECK(r.needs->reactor == fighter);
    CHECK(r.needs->trigger_actor == goblin);
    CHECK(r.needs->trigger == ReactionTrigger::LeavingReach);
    REQUIRE_FALSE(r.needs->options.empty());
    // Paused: the pending window is rules state; nothing applied, nothing logged.
    REQUIRE(c->pending());
    CHECK(c->pending()->window_seq == r.needs->window_seq);
    CHECK(c->pos(goblin) == Vec2i{6, 5});
    CHECK(c->log().size() == log0);
    CHECK(c->events().size() == ev0);
    CHECK_FALSE(rules5e::find(c->state(), fighter)->reaction_used);
    // Anything but a decision is refused while paused; a wrong window seq is refused.
    Command e;
    e.request = {goblin, ActionKind::EndTurn};
    CHECK_FALSE(c->submit(e).ok);
    CHECK_FALSE(c->decide({r.needs->window_seq + 1, true, 0}).ok);
    auto r2 = c->decide({r.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(r2.ok, r2.error);
    CHECK(r2.status == StepStatus::Done);
    CHECK_FALSE(c->pending());
    REQUIRE(r2.reactions.size() == 1);
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
    // One log entry: rules' LoggedStep (Move + decision) + the destination tile.
    REQUIRE(c->log().size() == log0 + 1);
    CHECK(c->log().back().step.request.kind == ActionKind::Move);
    CHECK(c->log().back().step.request.leaves_reach);
    CHECK(c->log().back().step.reactions.size() == 1);
    REQUIRE(c->log().back().to);
    CHECK(*c->log().back().to == Vec2i{7, 5});
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, chunk, lib, 7, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
    // Tampered decision: replay follows the log and diverges (or desyncs).
    auto bad = c->log();
    for (auto& le : bad)
        for (auto& rr : le.step.reactions) rr.decision.react = false;
    ecs::Registry reg3;
    auto rep3 = replay(reg3, chunk, lib, 7, bad, &err);
    const bool diverged = !rep3 || rep3->hash() != c->hash();
    CHECK(diverged);
}

TEST_CASE("sync settle() == async decide(); decline; poll-order hook; no window when no reach is left") {
    auto lib = pack_content();
    auto chunk = arena({{kFighter, 5, 5}, {kGoblin, 6, 5}, {kFighter, 7, 6}});
    const CombatantId f1 = chunk.spawns[0].stable_id, goblin = chunk.spawns[1].stable_id, f2 = chunk.spawns[2].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, chunk, lib, 3);
    REQUIRE(c);
    std::vector<CombatantId> seen_eligible;
    c->set_poll_order([&](const rules5e::EncounterState& s, const std::vector<CombatantId>& el) {
        seen_eligible = el;
        return default_poll_order(s, el);
    });
    advance_to(*c, goblin);
    Command step;
    step.kind = CommandKind::Step;
    step.to = {6, 3};   // illegal: not adjacent
    CHECK_FALSE(c->submit(step).ok);
    step.to = {6, 4};   // f1 (5,5)->(6,4) dist 1: still in reach. f2 (7,6)->(6,4) dist 2: leaves
    auto r = c->submit(step);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(r.needs);
    CHECK(r.needs->reactor == f2);
    CHECK(seen_eligible == std::vector<CombatantId>{f2});
    std::vector<CombatantId> polled;
    auto r2 = c->settle([&](const Combat&, const NeedsDecision& n) {
        polled.push_back(n.reactor);
        return ReactionDecision{n.window_seq, false, 0};
    });
    REQUIRE_MESSAGE(r2.ok, r2.error);
    CHECK(polled == std::vector<CombatantId>{f2});
    CHECK(has_event(*c, rules5e::EventKind::ReactionDeclined, f2));
    CHECK(c->pos(goblin) == Vec2i{6, 4});
    CHECK_FALSE(rules5e::find(c->state(), f2)->reaction_used);
    // Same thing answered asynchronously gives the same state.
    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, chunk, lib, 3);
    advance_to(*c2, goblin);
    auto a1 = c2->submit(step);
    REQUIRE(a1.needs);
    REQUIRE(c2->decide({a1.needs->window_seq, false, 0}).ok);
    CHECK(c2->hash() == c->hash());
    // No window when nobody's reach is left.
    const std::size_t n = c->events().size();
    if (c->state().budget.movement_ft >= 5) {
        step.to = {5, 4};   // f1 dist 1, f2 dist 2 (already outside)
        auto r3 = c->submit(step);
        REQUIRE(r3.ok);
        CHECK(r3.status == StepStatus::Done);
        for (std::size_t i = n; i < c->events().size(); ++i)
            CHECK(c->events()[i].rules.kind != rules5e::EventKind::ReactionWindowOpened);
    }
    (void)f1;
}

TEST_CASE("teams: overrides.team names map to ids by byte order; spawn-order independent; fallbacks") {
    auto lib = pack_content();
    auto with_teams = [](std::vector<std::pair<std::string, std::string>> spawns) {   // (archetype, overrides)
        std::vector<std::tuple<std::string, int, int>> s;
        for (std::size_t i = 0; i < spawns.size(); ++i) s.push_back({spawns[i].first, int(2 + 2 * i), 2});
        auto ch = arena(s);
        for (std::size_t i = 0; i < spawns.size(); ++i) ch.spawns[i].overrides_json = spawns[i].second;
        return ch;
    };
    auto team_of = [](const Combat& c, CombatantId id) { return rules5e::find(c.state(), id)->team; };
    {   // fallback: characters "party", monsters "monsters"; "monsters" < "party"
        auto ch = with_teams({{kFighter, ""}, {kGoblin, ""}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 1);
        REQUIRE(c);
        CHECK(c->team_names() == std::vector<std::string>{"monsters", "party"});
        CHECK(team_of(*c, ch.spawns[0].stable_id) == 1);
        CHECK(team_of(*c, ch.spawns[1].stable_id) == 0);
        CHECK(reg.get<Team>(c->entity(ch.spawns[0].stable_id))->team == 1);   // ECS mirrors
    }
    {   // explicit names; reversed spawn order gives the same name -> id mapping
        auto a = with_teams({{kFighter, R"({"team":"zeta"})"}, {kGoblin, R"({"team":"alpha"})"}, {kGoblin, R"({"team":"mid_1"})"}});
        auto b = with_teams({{kGoblin, R"({"team":"mid_1"})"}, {kGoblin, R"({"team":"alpha"})"}, {kFighter, R"({"team":"zeta"})"}});
        ecs::Registry ra, rb;
        auto ca = Combat::create(ra, a, lib, 1);
        auto cb = Combat::create(rb, b, lib, 1);
        REQUIRE(ca);
        REQUIRE(cb);
        CHECK(ca->team_names() == std::vector<std::string>{"alpha", "mid_1", "zeta"});
        CHECK(cb->team_names() == ca->team_names());
        CHECK(team_of(*ca, a.spawns[0].stable_id) == 2);
        CHECK(team_of(*ca, a.spawns[1].stable_id) == 0);
        CHECK(team_of(*cb, b.spawns[0].stable_id) == 1);
        CHECK(team_of(*cb, b.spawns[2].stable_id) == 2);
    }
    {   // same team => not hostile; other overrides keys ignored
        auto ch = with_teams({{kFighter, R"({"team":"party"})"}, {kGoblin, R"({"team":"party","note":1})"}, {kGoblin, ""}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 1);
        REQUIRE(c);
        CHECK_FALSE(c->hostile(ch.spawns[0].stable_id, ch.spawns[1].stable_id));
        CHECK(c->hostile(ch.spawns[0].stable_id, ch.spawns[2].stable_id));
    }
    for (const char* bad : {R"({"team":"Party"})", R"({"team":""})", R"({"team":3})", R"({"team":"9x"})"}) {
        CAPTURE(bad);
        auto ch = with_teams({{kFighter, bad}, {kGoblin, ""}});
        ecs::Registry reg;
        std::string err;
        CHECK_FALSE(Combat::create(reg, ch, lib, 1, &err));
        CHECK(err.find("overrides.team") != std::string::npos);
    }
}

TEST_CASE("character@3: v1 -> v2 -> v3 chain; pre-@3 effects inert; behavior.ai is read") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    CHECK(reg.check_chains().empty());
    const std::string fighter = kPack + "/characters/fighter_1.json";
    auto raw = assets::Json::parse(std::ifstream(fighter));
    CHECK(raw["$schema"] == "character@3");
    auto cur = reg.load_file(fighter);   // already current: unchanged
    REQUIRE_MESSAGE(cur.ok, cur.error);
    CHECK(cur.current.version == 4);
    auto raw4 = raw;   // the on-disk @3 file migrates 3 -> 4: schema tag and the descriptive-equipment marker
    raw4["$schema"] = "character@4";
    raw4["inert_mechanics"] = assets::Json::array({"equipment"});
    CHECK(cur.data == raw4);

    auto old_doc = raw;
    old_doc["$schema"] = "character@1";
    auto v1 = reg.load(old_doc);
    REQUIRE_MESSAGE(v1.ok, v1.error);
    CHECK(v1.current.type == "character");
    CHECK(v1.current.version == 4);
    CHECK(v1.data["$schema"] == "character@4");
    auto expect = raw4;
    for (auto& f : expect["features"]) f["effect"]["inert"] = true;   // 2 -> 3 marks effects inert
    CHECK(v1.data == expect);
    CHECK_FALSE(assets::behavior_ai_script(v1.data));
    {
        ContentLibrary old_lib;
        auto d = v1.data;
        d["id"] = "test.character.fighter_v1";
        REQUIRE_FALSE(old_lib.add(d, "character"));
        const Archetype* a = old_lib.find("test.character.fighter_v1");
        REQUIRE(a);
        CHECK(a->inert_features == std::vector<std::string>{"fighting_style_dueling (damage_bonus)",
                                                            "second_wind (self_heal)"});
        CHECK(a->base.attacks[0].damage == rules5e::DiceExpr{1, 8, 3});   // no Dueling applied
    }

    ContentLibrary lib;
    auto doc = v1.data;
    doc["id"] = "test.character.npc_ally";
    doc["behavior"] = {{"ai", "scripts/ai/skirmisher.luau"}};
    REQUIRE_FALSE(lib.add(doc, "character"));
    const Archetype* a = lib.find("test.character.npc_ally");
    REQUIRE(a);
    REQUIRE(a->ai);
    CHECK(*a->ai == "scripts/ai/skirmisher.luau");
    CHECK(a->base.is_pc);
    auto pack = pack_content();
    CHECK_FALSE(pack.find(kFighter)->ai);   // sample fighter has no behavior => built-in policy
    CHECK(pack.find(kGoblin)->ai);
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
    CHECK(c->submit(atk).error == "target out of reach");
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
