// Headless vertical slice: sample.vertical_slice goblin (Luau behavior.ai skirmisher) vs fighter
// (built-in NearestEnemyPolicy) on chunk 0_0_0, run to completion from a seed. Any archetype
// with behavior.ai (monster@2 / character@2) gets a LuauActorPolicy; others the built-in policy.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "scratch_dir.hpp"
#include "script_host/luau_policy.hpp"
#include "sim/driver.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;

namespace {
const std::string kPack = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/sample.vertical_slice";

struct Outcome {
    uint64_t hash = 0, log_hash = 0, event_digest = 0;
    std::size_t commands = 0, events = 0;
    std::vector<sim::LogEntry> log;
    std::map<std::string, std::size_t> luau_turns;   // archetype -> choose_target calls
    bool goblin_alive = false;
    std::size_t ai_calls = 0;
    std::size_t windows = 0, reactions = 0;
};

world::Chunk chunk(const std::string& pack = kPack) {
    world::HandcraftedProvider p({{world::ChunkCoord{0, 0, 0}, pack + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    return *r.chunk;
}

// Counts choose_target calls per policy.
class CountingPolicy final : public sim::IActorPolicy {
public:
    explicit CountingPolicy(sim::IActorPolicy& inner) : inner_(inner) {}
    std::optional<sim::CombatantId> choose_target(const sim::Combat& c, sim::CombatantId self) override {
        ++calls;
        return inner_.choose_target(c, self);
    }
    sim::ReactionDecision decide_reaction(const sim::Combat& c, const sim::NeedsDecision& n) override {
        return inner_.decide_reaction(c, n);
    }
    std::size_t calls = 0;
private:
    sim::IActorPolicy& inner_;
};

Outcome fight(uint64_t seed, const std::string& pack_dir = kPack) {
    // One ordered PackSet feeds both content and scripts (the game path).
    const auto set = assets::PackSet::from_pack_dir(pack_dir);
    REQUIRE_MESSAGE(set.ok(), (set.ok() ? "" : set.error_strings()[0]));
    sim::ContentLibrary lib;
    auto errs = lib.load(set);
    REQUIRE(errs.empty());
    script::ScriptHost host(1);
    auto pack = script::load_pack_scripts(host, set);
    REQUIRE_MESSAGE(pack.ok, (pack.errors.empty() ? "" : pack.errors[0]));
    REQUIRE(pack.ai_by_archetype.count("srd51.monster.goblin"));
    std::map<std::string, std::unique_ptr<script::LuauActorPolicy>> luau;
    std::map<std::string, std::unique_ptr<CountingPolicy>> counted;
    for (const auto& [id, a] : lib.all())
        if (a.ai) {
            REQUIRE_MESSAGE(pack.ai_by_archetype.count(id), id);
            // behavior.ai resolves within the archetype's own pack (script id pack_id:path).
            REQUIRE(pack.scripts.count(*a.ai_script_id()));
            CHECK(pack.scripts.at(*a.ai_script_id()) == pack.ai_by_archetype.at(id));
            luau[id] = std::make_unique<script::LuauActorPolicy>(host, pack.ai_by_archetype.at(id));
            counted[id] = std::make_unique<CountingPolicy>(*luau[id]);
        }
    sim::NearestEnemyPolicy builtin;

    const auto ch = chunk(pack_dir);
    ecs::Registry reg;
    auto c = sim::Combat::create(reg, ch, lib, seed);
    REQUIRE(c);
    auto policy_for = [&](sim::CombatantId id) -> sim::IActorPolicy* {
        const sim::Archetype* a = c->archetype(id);
        if (a && a->ai) return counted.at(a->id).get();   // behavior.ai => Luau
        return &builtin;                                   // no behavior => built-in policy
    };
    auto rr = sim::run_to_end(*c, policy_for);
    REQUIRE_MESSAGE(rr.ended, rr.error);
    for (const auto& [id, p] : luau) CHECK_MESSAGE(p->last_error().empty(), id);
    Outcome o;
    o.hash = c->hash();
    o.log_hash = c->log_hash();
    o.event_digest = sim::event_stream_hash(c->events());
    o.commands = c->log().size();
    o.events = c->events().size();
    o.log = c->log();
    o.ai_calls = host.log().size();
    for (const auto& [id, p] : counted) o.luau_turns[id] = p->calls;
    for (const auto& e : c->events())
        if (e.kind == sim::SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::ReactionWindowOpened) ++o.windows;
    for (const auto& e : c->log()) o.reactions += e.step.reactions.size();
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
    MESSAGE("seed 2026: hash=" << a.hash << " log=" << a.log_hash << " commands=" << a.commands << " events=" << a.events << " windows=" << a.windows << " reactions=" << a.reactions);
    CHECK(a.hash == 5530183380127477598ull);   // feature uses folded into the state hash (log + event digests unchanged)
    CHECK(a.log_hash == 10047098813104974020ull);
    MESSAGE("event digest (schema v" << sim::kSimEventSchemaVersion << ")=" << a.event_digest);
    CHECK(a.event_digest == 14305360421686639532ull);   // v4 (v3 was 83810279896348715; version only)
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
    bool tampered = false;
    for (auto& e : bad)
        if (!e.step.reactions.empty() && !tampered) { e.step.reactions[0].decision.react = !e.step.reactions[0].decision.react; tampered = true; }
    if (tampered) {
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

TEST_CASE("vertical slice: a character@2 with behavior.ai is driven by Luau") {
    namespace fs = std::filesystem;
    const ScratchDir scratch("vs_character_v2");
    // Copy of the live root; behavior.ai resolves within the character's own pack, so the
    // sample pack gets its own copy of the script (key sample.vertical_slice:scripts/ai/...).
    fs::copy(fs::path(kPack).parent_path(), scratch.path, fs::copy_options::recursive);
    const fs::path tmp = scratch.path / "sample.vertical_slice";
    fs::create_directories(tmp / "scripts/ai");
    fs::copy_file(scratch.path / "srd51.core/scripts/ai/skirmisher.luau", tmp / "scripts/ai/skirmisher.luau");
    const fs::path fj = tmp / "characters" / "fighter_1.json";
    auto doc = assets::Json::parse(std::ifstream(fj));
    CHECK(doc["$schema"] == "character@3");
    doc["behavior"] = {{"ai", "scripts/ai/skirmisher.luau"}};
    std::ofstream(fj) << doc.dump(2);
    const Outcome o = fight(5, tmp.string());
    REQUIRE(o.luau_turns.count("srd51.character.fighter_1"));
    CHECK(o.luau_turns.at("srd51.character.fighter_1") > 0);
    CHECK(o.luau_turns.at("srd51.monster.goblin") > 0);
    CHECK(fight(5, tmp.string()).hash == o.hash);
}
