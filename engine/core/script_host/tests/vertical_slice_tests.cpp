// Headless vertical slice: sample.vertical_slice goblin (Luau behavior.ai skirmisher) vs fighter
// (built-in NearestEnemyPolicy) on chunk 0_0_0, run to completion from a seed.
#include <doctest/doctest.h>

#include <set>
#include <string>

#include "script_host/luau_policy.hpp"
#include "sim/driver.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";

struct Outcome {
    uint64_t hash = 0, log_hash = 0;
    std::size_t commands = 0, events = 0;
    std::vector<sim::Command> log;
    bool goblin_alive = false;
    std::size_t ai_calls = 0;
};

world::Chunk chunk() {
    world::HandcraftedProvider p({{world::ChunkCoord{0, 0, 0}, kPack + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    return *r.chunk;
}

Outcome fight(uint64_t seed) {
    sim::ContentLibrary lib;
    auto errs = lib.load_pack(kPack);
    REQUIRE(errs.empty());
    script::ScriptHost host(1);
    auto pack = script::load_pack_scripts(host, kPack);
    REQUIRE_MESSAGE(pack.ok, (pack.errors.empty() ? "" : pack.errors[0]));
    REQUIRE(pack.ai_by_archetype.count("srd51.monster.goblin"));
    script::LuauActorPolicy goblin_ai(host, pack.ai_by_archetype.at("srd51.monster.goblin"));
    sim::NearestEnemyPolicy builtin;

    const auto ch = chunk();
    ecs::Registry reg;
    auto c = sim::Combat::create(reg, ch, lib, seed);
    REQUIRE(c);
    auto policy_for = [&](sim::CombatantId id) -> sim::IActorPolicy* {
        const sim::Archetype* a = c->archetype(id);
        if (a && a->ai) return &goblin_ai;   // monsters with behavior.ai use Luau
        return &builtin;                     // fighter: built-in policy (no AI content for PCs)
    };
    auto rr = sim::run_to_end(*c, policy_for);
    REQUIRE_MESSAGE(rr.ended, rr.error);
    CHECK(goblin_ai.last_error().empty());
    Outcome o;
    o.hash = c->hash();
    o.log_hash = c->log_hash();
    o.commands = c->log().size();
    o.events = c->events().size();
    o.log = c->log();
    o.ai_calls = host.log().size();
    for (const auto& k : c->state().combatants)
        if (!k.is_pc) o.goblin_alive = !k.dead;
    return o;
}
} // namespace

TEST_CASE("vertical slice: goblin (Luau) vs fighter runs to completion; same seed => identical") {
    const Outcome a = fight(2026), b = fight(2026);
    CHECK(a.hash == b.hash);
    CHECK(a.log_hash == b.log_hash);
    CHECK(a.commands == b.commands);
    CHECK(a.ai_calls > 0);   // the skirmisher script chose the goblin's targets
    MESSAGE("seed 2026: hash=" << a.hash << " log=" << a.log_hash << " commands=" << a.commands << " events=" << a.events);
    CHECK(a.hash == 6687972637053972402ull);
    CHECK(a.log_hash == 214783171545905138ull);
}

TEST_CASE("vertical slice: the command log alone replays the fight bit-identically") {
    const Outcome a = fight(77);
    sim::ContentLibrary lib;
    REQUIRE(lib.load_pack(kPack).empty());
    ecs::Registry reg;
    std::string err;
    auto rep = sim::replay(reg, chunk(), lib, 77, a.log, &err);   // no scripts, no policies
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->ended());
    CHECK(rep->hash() == a.hash);
    CHECK(rep->log_hash() == a.log_hash);
    // Tampering with the log desyncs or changes the result.
    auto bad = a.log;
    for (auto& cmd : bad)
        if (cmd.kind == sim::CommandKind::Decide) { cmd.decision.react = !cmd.decision.react; break; }
    if (bad != a.log) {
        ecs::Registry reg2;
        auto rep2 = sim::replay(reg2, chunk(), lib, 77, bad, &err);
        const bool diverged = !rep2 || rep2->hash() != a.hash;
        CHECK(diverged);
    }
}

TEST_CASE("vertical slice: different seeds take different paths") {
    std::set<uint64_t> hashes;
    std::set<bool> winners;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const Outcome o = fight(seed);
        hashes.insert(o.hash);
        winners.insert(o.goblin_alive);
    }
    CHECK(hashes.size() >= 10);   // nearly all distinct
    MESSAGE("distinct outcomes: " << hashes.size() << ", winners seen: " << winners.size());
}
