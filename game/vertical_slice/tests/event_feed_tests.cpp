#include <doctest/doctest.h>

#include <span>
#include <string>
#include <vector>

#include "ecs/registry.hpp"
#include "event_feed.hpp"
#include "session.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/file_chunk_provider.hpp"

using namespace eng;
using rules5e::EventKind;
using eng::sim::SimEvent;
using slice::FeedState;

namespace {
SimEvent rules(EventKind k, uint32_t actor, uint32_t target = 0, int a = 0, int b = 0, int c = 0, uint8_t depth = 0) {
    SimEvent e;
    e.kind = SimEvent::Kind::Rules;
    e.rules.kind = k; e.rules.actor = actor; e.rules.target = target;
    e.rules.a = a; e.rules.b = b; e.rules.c = c; e.rules.reaction_depth = depth;
    return e;
}
SimEvent moved(uint32_t actor, Vec2i t) {
    SimEvent e;
    e.kind = SimEvent::Kind::TileMoved;
    e.actor = actor;
    e.tile = t;
    return e;
}
std::string nm(uint32_t id) { return id == 1 ? "FIGHTER" : id == 2 ? "GOBLIN" : "?"; }
} // namespace

TEST_CASE("v3: TileMoved after the request's rules events still lands on the right actors") {
    // Fighter (1) steps; goblin (2) has a readied Move that fires inside the fighter's request
    // (depth 1, after ReadyTriggered). v3 puts both TileMoved after all rules events.
    const std::vector<SimEvent> block = {
        rules(EventKind::Moved, 1, 0, 5, 25, 0, 0),
        rules(EventKind::ReadyTriggered, 2, 1, 7, 0, 0, 1),
        rules(EventKind::Moved, 2, 0, 5, 0, 0, 1),
        rules(EventKind::AttackResolved, 1, 2, 12, 17, 1),
        moved(1, {11, 15}),
        moved(2, {19, 14}),
    };
    FeedState st;
    st.tiles[1] = {10, 15};
    st.tiles[2] = {20, 15};
    slice::apply_events(block, st, nm);
    CHECK(st.tiles[1] == Vec2i{11, 15});
    CHECK(st.tiles[2] == Vec2i{19, 14});   // moved during the fighter's turn
    CHECK(st.tile_moves == 2);
    REQUIRE(st.log.size() == 3);
    CHECK(st.log[0] == "GOBLIN READIED ACTION!");
    CHECK(st.log[1] == "GOBLIN MOVES (READIED)");
    CHECK(st.log[2] == "FIGHTER HITS GOBLIN (17)");
}

TEST_CASE("v4 spell kinds 35-38 are known and logged") {
    static_assert(sim::kSimEventSchemaVersion >= 7);
    std::vector<SimEvent> evs;
    evs.push_back(rules(EventKind::SavingThrowRolled, 2, 4, 9, 14, 1));
    evs.push_back(rules(EventKind::SavingThrowRolled, 2, 4, 3, 5, 0));
    evs.push_back(rules(EventKind::ConcentrationEnded, 1, 0, 77));
    evs.push_back(rules(EventKind::SpellEffectEnded, 2, 0, 77, -1));
    evs.push_back(rules(EventKind::SpellCheckRolled, 1, 0, 12, 15, 1));
    FeedState st;
    slice::apply_events(evs, st, nm);
    CHECK(st.skipped_unknown == 0);
    for (int k = 35; k <= 38; ++k) CHECK(slice::known_rules_kind(EventKind(k)));
    REQUIRE(st.log.size() == 5);
    CHECK(st.log[0] == "GOBLIN SAVES (14)");
    CHECK(st.log[1] == "GOBLIN FAILS SAVE (5)");
    CHECK(st.log[2] == "FIGHTER LOSES CONCENTRATION");
    CHECK(st.log[3] == "SPELL ENDS ON GOBLIN");
    CHECK(st.log[4] == "FIGHTER COUNTERS (15)");
}

TEST_CASE("v7 kinds 39-44 are known (not counted as unknown, not shown)") {
    std::vector<SimEvent> evs;
    for (int k = 39; k <= 44; ++k) evs.push_back(rules(EventKind(k), 1, 2, 1, 2, 3));
    FeedState st;
    slice::apply_events(evs, st, nm);
    CHECK(st.skipped_unknown == 0);
    CHECK(st.log.empty());
    for (int k = 39; k <= 44; ++k) CHECK(slice::known_rules_kind(EventKind(k)));
    CHECK(sim::kLastRulesEventKindV8 == EventKind::ActionUsed);
}

TEST_CASE("v9 zone kinds 45-47 are known (not counted as unknown, not shown)") {
    std::vector<SimEvent> evs;
    for (int k = 45; k <= 47; ++k) evs.push_back(rules(EventKind(k), 1, 0, 1, 2, 3));
    FeedState st;
    slice::apply_events(evs, st, nm);
    CHECK(st.skipped_unknown == 0);
    CHECK(st.log.empty());
    CHECK(sim::kLastRulesEventKindV9 == EventKind::ZoneEnded);
}

TEST_CASE("v10 summon / transform kinds 48-52 are known (not counted as unknown, not shown)") {
    std::vector<SimEvent> evs;
    for (int k = 48; k <= 52; ++k) evs.push_back(rules(EventKind(k), 1, 0, 1, 2, 3));
    FeedState st;
    slice::apply_events(evs, st, nm);
    CHECK(st.skipped_unknown == 0);
    CHECK(st.log.empty());
    CHECK(sim::kLastRulesEventKind == EventKind::Commanded);
}

TEST_CASE("unknown event kinds are skipped (above kLastRulesEventKind, future sim kinds)") {
    std::vector<SimEvent> evs;
    for (int k = 53; k <= 56; ++k) evs.push_back(rules(EventKind(k), 1, 2, 1, 2, 3));   // past v10's last kind (52)
    evs.push_back(rules(EventKind(250), 1));
    SimEvent future;
    future.kind = SimEvent::Kind(7);
    future.actor = 2;
    future.tile = {0, 0};
    evs.push_back(future);
    evs.push_back(rules(EventKind::CombatantDied, 2));
    FeedState st;
    st.tiles[2] = {20, 15};
    slice::apply_events(evs, st, nm);
    CHECK(st.skipped_unknown == 6);
    CHECK(st.tiles[2] == Vec2i{20, 15});   // the unknown kind did not move anyone
    REQUIRE(st.log.size() == 1);
    CHECK(st.log[0] == "GOBLIN DIES!");
    CHECK(slice::known_rules_kind(sim::kLastRulesEventKind));
    CHECK_FALSE(slice::known_rules_kind(EventKind(uint32_t(sim::kLastRulesEventKind) + 1)));
}

TEST_CASE("feeding a real fight's stream reproduces the sim's final tiles") {
    const std::string pack = std::string(SLICE_PACK_ROOT) + "/" + SLICE_PACK;   // load_pack wrapper resolves its root
    sim::ContentLibrary lib;
    REQUIRE(lib.load_pack(pack).empty());
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, pack + "/chunks/0_0_0.json"}});
    auto r = p.load({world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE(r.chunk);
    for (uint64_t seed : {1u, 2u, 3u, 4u}) {
        ecs::Registry reg;
        auto c = sim::Combat::create(reg, *r.chunk, lib, seed);
        REQUIRE(c);
        FeedState st;
        for (auto& sp : r.chunk->spawns) st.tiles[sp.stable_id] = sp.tile;
        sim::NearestEnemyPolicy pol;
        auto rr = sim::run_to_end(*c, [&](sim::CombatantId) { return &pol; });
        REQUIRE_MESSAGE(rr.ended, rr.error);
        slice::apply_events(c->events(), st, [](uint32_t) { return std::string("X"); });
        for (auto& cb : c->state().combatants) CHECK(st.tiles[cb.id] == c->pos(cb.id));
        CHECK(st.skipped_unknown == 0);
    }
}

TEST_CASE("NeedsDecision: a paused request adds nothing to the fed stream; completion does") {
    sim::ContentLibrary lib;
    REQUIRE(lib.load_pack(std::string(SLICE_PACK_ROOT) + "/" + SLICE_PACK).empty());
    world::Chunk ch{};
    ch.coord = {5, 5, 0};
    ch.origin = world::Chunk::Origin::Handcrafted;
    ch.palette = {"", "floor"};
    for (auto& cell : ch.layers[0]) cell.tile = 1;
    ch.spawns.push_back({"srd51.character.fighter_1", Vec2i{5, 5}, world::spawn_stable_id(ch.coord, 0), ""});
    ch.spawns.push_back({"srd51.monster.goblin", Vec2i{6, 5}, world::spawn_stable_id(ch.coord, 1), ""});
    ch.content_hash = world::compute_content_hash(ch);
    const uint32_t fighter = ch.spawns[0].stable_id, goblin = ch.spawns[1].stable_id;

    ecs::Registry reg;
    auto c = sim::Combat::create(reg, ch, lib, 7);
    REQUIRE(c);
    for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != goblin; ++i) {
        sim::Command e;
        e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
        REQUIRE(c->submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c->state()) == goblin);

    FeedState st;
    st.tiles[fighter] = {5, 5};
    st.tiles[goblin] = {6, 5};
    size_t seen = 0;
    auto drain = [&] {
        slice::apply_events(std::span<const SimEvent>(c->events()).subspan(seen), st, nm);
        seen = c->events().size();
    };
    drain();
    const auto log_before = st.log;

    sim::Command step;
    step.kind = sim::CommandKind::Step;
    step.to = {7, 5};   // leaves the fighter's reach -> opportunity attack window
    auto r = c->submit(step);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(r.status == sim::StepStatus::NeedsDecision);
    drain();
    CHECK(st.log == log_before);                 // nothing provisional reached the viewer
    CHECK(st.tiles[goblin] == Vec2i{6, 5});
    CHECK(st.tile_moves == 0);

    auto r2 = c->decide({r.needs->window_seq, true, 0});
    REQUIRE_MESSAGE(r2.ok, r2.error);
    REQUIRE(r2.status == sim::StepStatus::Done);
    drain();
    CHECK(st.log != log_before);                 // the completed request's block arrived at once
    CHECK(st.tiles[goblin] == c->pos(goblin));   // moved or not, the feed agrees with the sim
    CHECK(st.tiles[fighter] == c->pos(fighter));
}
