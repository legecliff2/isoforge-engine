// Combat::set_end_condition / set_scenario_end. An empty function restores the team policy,
// install_hooks() leaves the field alone, and a round cap ends with EndReason::Custom.
// The cap is replay input (ScenarioEnd); omitting it replays the log under the team rule.
#include <doctest/doctest.h>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EndReason;

namespace {
world::Chunk arena() {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {"srd51.character.fighter_1", "srd51.monster.goblin"};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + 3 * i), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

ContentLibrary library() {
    ContentLibrary lib;
    auto errs = lib.load(sample_set());
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    return lib;
}

void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    auto r = c.submit(e);
    REQUIRE_MESSAGE(r.ok, r.error);
}
}  // namespace

TEST_CASE("set_end_condition: an empty function restores the team policy and clears a recorded cap") {
    auto lib = library();
    const auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    c->set_scenario_end(ScenarioEnd{ScenarioEndKind::Teams, 1});
    c->set_end_condition({});
    CHECK(c->scenario_end().kind == ScenarioEndKind::Teams);
    CHECK(c->scenario_end().round_cap == 0);
    c->legal_actions();   // install_hooks()
    end_turn(*c);
    CHECK_FALSE(c->ended());
    CHECK_FALSE(c->state().outcome);
}

TEST_CASE("set_end_condition: install_hooks() leaves a custom policy in place") {
    auto lib = library();
    const auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    c->set_end_condition([](const rules5e::EncounterState& s) -> std::optional<rules5e::EncounterOutcome> {
        return rules5e::EncounterOutcome{EndReason::Custom, rules5e::pc_statuses(s), {}};
    });
    c->legal_actions();   // install_hooks() re-binds every other hook and must not replace this one
    end_turn(*c);
    REQUIRE(c->ended());
    REQUIRE(c->state().outcome);
    CHECK(c->state().outcome->reason == EndReason::Custom);
    CHECK(c->scenario_end().round_cap == 0);   // an arbitrary function is not replay input
}

TEST_CASE("set_scenario_end: round cap ends Custom, and replay matches only when given scenario_end()") {
    auto lib = library();
    const auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    c->set_scenario_end(ScenarioEnd{ScenarioEndKind::Teams, 1});
    c->legal_actions();   // install_hooks()
    for (int i = 0; i < 8 && !c->ended(); ++i) end_turn(*c);
    REQUIRE(c->ended());
    REQUIRE(c->state().outcome);
    CHECK(c->state().outcome->reason == EndReason::Custom);
    CHECK(c->state().round == 2);
    CHECK(c->scenario_end().round_cap == 1);

    ecs::Registry reg2;
    std::string err;
    auto again = replay(reg2, ch, lib, 3, c->log(), &err, c->surprised_at_start(), c->scenario_end());
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    REQUIRE(again->state().outcome);
    CHECK(again->state().outcome->reason == EndReason::Custom);

    ecs::Registry reg3;
    auto plain = replay(reg3, ch, lib, 3, c->log(), &err);
    REQUIRE_MESSAGE(plain, err);
    CHECK_FALSE(plain->ended());
    CHECK(plain->hash() != c->hash());
}
