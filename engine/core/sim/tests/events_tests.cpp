// Locks the sim event stream contract (docs/SIM_EVENTS.md). If a check here fails, the change is
// a contract change: bump kSimEventSchemaVersion, update docs/SIM_EVENTS.md (changelog) and the
// goldens below, and tell event consumers (render/platform, UI).
#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";

ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load_pack(kPack).empty());
    return l;
}
world::Chunk sample_chunk() {
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, kPack + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    return *r.chunk;
}
} // namespace

TEST_CASE("event layout: schema version and numeric enum values are locked") {
    CHECK(kSimEventSchemaVersion == 3);
    CHECK(uint8_t(SimEvent::Kind::Rules) == 0);
    CHECK(uint8_t(SimEvent::Kind::TileMoved) == 1);
    // rules5e::EventKind as surfaced in SimEvent::rules (append-only; values are the contract).
    const std::vector<std::pair<EventKind, int>> kinds = {
        {EventKind::InitiativeRolled, 0}, {EventKind::TurnStarted, 1}, {EventKind::AttackResolved, 2},
        {EventKind::DamageDealt, 3}, {EventKind::ConditionAdded, 4}, {EventKind::CombatantDowned, 5},
        {EventKind::DeathSaveRolled, 6}, {EventKind::Stabilized, 7}, {EventKind::RegainedHp, 8},
        {EventKind::CombatantDied, 9}, {EventKind::Moved, 10}, {EventKind::Dashed, 11},
        {EventKind::Disengaged, 12}, {EventKind::Dodged, 13}, {EventKind::StoodUp, 14},
        {EventKind::TurnEnded, 15}, {EventKind::EncounterEnded, 16}, {EventKind::Healed, 17},
        {EventKind::ConditionRemoved, 18}, {EventKind::MedicineCheck, 19}, {EventKind::Helped, 20},
        {EventKind::Hid, 21}, {EventKind::Readied, 22}, {EventKind::ReadyTriggered, 23},
        {EventKind::ReadyExpired, 24}, {EventKind::CheckRolled, 25}, {EventKind::ReactionWindowOpened, 26},
        {EventKind::ReactionTaken, 27}, {EventKind::ReactionDeclined, 28}, {EventKind::MoveInterrupted, 29},
        {EventKind::SpellCast, 30}, {EventKind::SpellCountered, 31}, {EventKind::SpellResolved, 32},
        {EventKind::AcBonus, 33}, {EventKind::FeatureUsed, 34}};   // v2: FeatureUsed
    CHECK(int(kLastRulesEventKind) == kinds.back().second);
    // Every rules kind must be listed above: a new kind fails here until the table (and version) is updated.
    CHECK(rules5e::kEventKindCount == kinds.size());
    CHECK(int(kLastRulesEventKind) + 1 == int(rules5e::kEventKindCount));
    for (auto [k, v] : kinds) CHECK(int(k) == v);
    CHECK(uint8_t(rules5e::ReactionTrigger::LeavingReach) == 0);
    CHECK(uint8_t(rules5e::ReactionTrigger::BeingHit) == 1);
    CHECK(uint8_t(rules5e::ReactionTrigger::SpellCast) == 2);
    CHECK(uint8_t(rules5e::ReactionTrigger::ReadiedTrigger) == 3);
    // The digest covers the version and every field.
    std::vector<SimEvent> one(1);
    const uint64_t base = event_stream_hash(one);
    auto changed = [&](auto mut) { auto v = one; mut(v[0]); return event_stream_hash(v) != base; };
    CHECK(changed([](SimEvent& e) { e.kind = SimEvent::Kind::TileMoved; }));
    CHECK(changed([](SimEvent& e) { e.rules.kind = EventKind::TurnStarted; }));
    CHECK(changed([](SimEvent& e) { e.rules.actor = 1; }));
    CHECK(changed([](SimEvent& e) { e.rules.target = 1; }));
    CHECK(changed([](SimEvent& e) { e.rules.a = 1; }));
    CHECK(changed([](SimEvent& e) { e.rules.b = 1; }));
    CHECK(changed([](SimEvent& e) { e.rules.c = 1; }));
    CHECK(changed([](SimEvent& e) { e.rules.reaction_depth = 1; }));
    CHECK(changed([](SimEvent& e) { e.actor = 1; }));
    CHECK(changed([](SimEvent& e) { e.tile.x = 1; }));
    CHECK(changed([](SimEvent& e) { e.tile.y = 1; }));
}

TEST_CASE("event ordering: opening, per-request blocks, TileMoved after Moved, turns bracket actions") {
    auto l = lib();
    auto ch = sample_chunk();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 1);
    REQUIRE(c);
    NearestEnemyPolicy p;
    REQUIRE(run_to_end(*c, [&](CombatantId) { return &p; }).ended);
    const auto& ev = c->events();
    REQUIRE(ev.size() > 4);
    // Opening: one InitiativeRolled per combatant, then the first TurnStarted.
    std::size_t i = 0;
    while (i < ev.size() && ev[i].rules.kind == EventKind::InitiativeRolled) ++i;
    CHECK(i == c->state().combatants.size());
    CHECK(ev[i].rules.kind == EventKind::TurnStarted);
    // v3: every TileMoved is preceded (in its request block) by a rules Moved of the same actor.
    for (std::size_t k = 0; k < ev.size(); ++k)
        if (ev[k].kind == SimEvent::Kind::TileMoved) {
            bool found = false;
            for (std::size_t j = k; j-- > 0;) {
                if (ev[j].kind == SimEvent::Kind::TileMoved && ev[j].actor == ev[k].actor) break;
                if (ev[j].kind == SimEvent::Kind::Rules && ev[j].rules.kind == EventKind::Moved &&
                    ev[j].rules.actor == ev[k].actor) { found = true; break; }
            }
            CHECK(found);
        }
    // Exactly one EncounterEnded, and it is last.
    std::size_t ends = 0;
    for (const auto& e : ev) ends += e.rules.kind == EventKind::EncounterEnded && e.kind == SimEvent::Kind::Rules;
    CHECK(ends == 1);
    CHECK(ev.back().rules.kind == EventKind::EncounterEnded);
    // Non-Rules events carry a zero rules payload; Rules events carry no tile.
    for (const auto& e : ev)
        if (e.kind == SimEvent::Kind::TileMoved) CHECK(e.rules.actor == 0);
        else CHECK(e.actor == 0);
}

TEST_CASE("event goldens: same seed => same stream; locked digests") {
    auto l = lib();
    auto ch = sample_chunk();
    auto run = [&](uint64_t seed) {
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, seed);
        REQUIRE(c);
        NearestEnemyPolicy p;
        REQUIRE(run_to_end(*c, [&](CombatantId) { return &p; }).ended);
        return std::pair{event_stream_hash(c->events()), c->events().size()};
    };
    const auto a = run(1), b = run(1);
    CHECK(a == b);
    CHECK(run(2).first != a.first);
    MESSAGE("seed 1: events=" << a.second << " digest=" << a.first);
    CHECK(a.first == 5379229055340337023ull);   // GOLDEN v3 (fighter now opens with the crossbow: 7 events)
}
