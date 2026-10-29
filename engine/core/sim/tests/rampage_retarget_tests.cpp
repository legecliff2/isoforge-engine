// gnoll_rampage repro: FeatureTriggered (reduce_to_zero -> bonus_attack bite) leaves a grant, and the
// driver's sticky target is the creature that just dropped. run_to_end keeps that id for the rest of
// the turn (choose_target once per turn). A dead, missing, or not-living sticky target must spend the
// grant on another living hostile the bite reaches. No such foe leaves the grant unused. No new
// SIM_EVENTS tag: this is AI policy, the same Attack / as_bonus_action the grant already accepts.
// Giant hyena compiles the same bite grant; the driver path is the gnoll one.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kGnoll = "srd51.monster.gnoll";
const char* kHyena = "srd51.monster.giant_hyena";
const char* kGoblin = "srd51.monster.goblin";
const char* kFighter = "srd51.character.fighter_1";
const char* kFoe = "t.gnoll_rampage_foe";   // goblin, 1 HP, AC 5: the real gnoll bite kills on any hit but a natural 1
// A natural 1 still misses. These seeds' scripted bites hit (the REQUIRE says so).
constexpr uint64_t kDeadSeed = 1;
constexpr uint64_t kDownedSeed = 1;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

struct Spawn {
    const char* arch;
    int x, y;
    const char* team;   // "" = pack default (monsters / party)
};

world::Chunk arena(const std::vector<Spawn>& spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        const Spawn& sp = spawns[i];
        c.spawns.push_back({sp.arch, Vec2i{sp.x, sp.y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
        if (sp.team[0] != '\0') c.spawns.back().overrides_json = std::string("{\"team\":\"") + sp.team + "\"}";
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

int attack_named(const rules5e::Combatant& k, const char* id) {
    for (int i = 0; i < int(k.attacks.size()); ++i)
        if (k.attacks[std::size_t(i)].id == id) return i;
    return -1;
}

rules5e::Combatant& who(Combat& c, CombatantId id) {
    return const_cast<rules5e::Combatant&>(*rules5e::find(c.state(), id));
}
rules5e::EncounterState& state_of(Combat& c) { return const_cast<rules5e::EncounterState&>(c.state()); }

void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId id) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != id; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == id);
}

SubmitResult submit_settled(Combat& c, const Command& cmd) {
    auto r = c.submit(cmd);
    if (r.ok && r.status == StepStatus::NeedsDecision)
        r = c.settle([](const Combat&, const NeedsDecision& n) { return ReactionDecision{n.window_seq, false, 0}; });
    return r;
}

bool rampage_fired(const Combat& c, CombatantId owner, const char* arch) {
    const uint32_t id = rules5e::spell_id_of(std::string(arch) + "#rampage#0");
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered && e.rules.actor == owner &&
            uint32_t(e.rules.a) == id && e.rules.c == 1)
            return true;
    return false;
}

void expect_bite_grant(const ContentLibrary& lib, const char* id) {
    const Archetype* a = lib.find(id);
    REQUIRE(a);
    const int bite = attack_named(a->base, "bite");
    REQUIRE(bite >= 0);
    bool found = false;
    for (const auto& t : a->base.triggers) {
        if (t.on != rules5e::TriggerOn::ReduceToZero) continue;
        REQUIRE_FALSE(t.effects.empty());
        CHECK(t.effects[0].kind == rules5e::RiderKind::BonusAttack);
        CHECK(int(t.effects[0].value) == bite);
        found = true;
    }
    CHECK(found);
}

// Script the gnoll's real bite. `fragile` pokes the target to 1 HP of its existing maximum and the bite to
// to_hit 30, for a PC whose maximum must stay high enough that the hit downs them instead of killing them.
// That poke is not in the log, so those fights are not replayed.
void script_bite(Combat& c, CombatantId gnoll, CombatantId victim, bool fragile) {
    to_actor(c, gnoll);
    rules5e::Combatant& g = who(c, gnoll);
    const int bite = attack_named(g, "bite");
    REQUIRE(bite >= 0);
    if (fragile) {
        for (auto& at : g.attacks)
            if (!at.ranged) at.to_hit = 30;
        who(c, victim).hp = 1;
        who(c, victim).temp_hp = 0;
    }
    Command a;
    a.request = {gnoll, ActionKind::Attack, victim, uint8_t(bite)};
    const SubmitResult r = submit_settled(c, a);
    REQUIRE_MESSAGE(r.ok, r.error);
}

ContentLibrary open_lib() {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = kFoe;
    foe["hit_points"]["average"] = 1;
    foe["armor_class"]["value"] = 5;
    const auto err = lib.add(foe, "monster");
    REQUIRE_MESSAGE(!err, err.value_or(""));
    return lib;
}

template <class F>
void with_fight(const std::vector<Spawn>& spawns, uint64_t seed, F&& body) {
    ContentLibrary lib = open_lib();
    world::Chunk ch = arena(spawns);
    ecs::Registry reg;
    auto combat = Combat::create(reg, ch, lib, seed);
    REQUIRE(combat);
    body(*combat, ch, lib);
}

void expect_bonus_bite(const Command& cmd, CombatantId target, int bite) {
    CHECK(cmd.kind == CommandKind::Rules);
    CHECK(cmd.request.kind == ActionKind::Attack);
    CHECK(cmd.request.as_bonus_action);
    CHECK(cmd.request.bonus_option == 0);
    CHECK(cmd.request.attack_index == uint8_t(bite));
    CHECK(cmd.request.target == target);
}
}  // namespace

TEST_CASE("gnoll_rampage: gnoll and giant hyena compile reduce_to_zero into a bite grant") {
    ContentLibrary lib = open_lib();
    expect_bite_grant(lib, kGnoll);
    expect_bite_grant(lib, kHyena);
}

TEST_CASE("gnoll_rampage: a dead sticky target is retargeted onto a living adjacent foe") {
    // Two adjacent survivors (a distance tie -> lowest id) and one out of bite reach.
    const Spawn gnoll{kGnoll, 10, 10, ""};
    const Spawn victim{kFoe, 11, 10, "party"};
    const Spawn left{kGoblin, 9, 10, "party"};
    const Spawn down{kGoblin, 10, 11, "party"};
    const Spawn far{kGoblin, 10, 15, "party"};
    with_fight({gnoll, victim, left, down, far}, kDeadSeed, [](Combat& c, world::Chunk& ch, ContentLibrary& lib) {
        const CombatantId g = ch.spawns[0].stable_id, dead = ch.spawns[1].stable_id;
        const CombatantId a = ch.spawns[2].stable_id, b = ch.spawns[3].stable_id, far_id = ch.spawns[4].stable_id;
        script_bite(c, g, dead, false);
        REQUIRE_MESSAGE(who(c, dead).dead, "scripted bite missed (natural 1): choose another kDeadSeed");
        CHECK(rampage_fired(c, g, kGnoll));
        const int bite = attack_named(who(c, g), "bite");
        REQUIRE(bite >= 0);
        REQUIRE(c.state().budget.bonus_attack_grant == uint8_t(bite + 1));
        REQUIRE(c.state().budget.bonus_action);
        CHECK(c.distance_tiles(g, a) == 1);
        CHECK(c.distance_tiles(g, b) == 1);
        CHECK(c.distance_tiles(g, far_id) > 1);
        const CombatantId expect = a < b ? a : b;
        // The policy target is still the corpse (and a missing id). next_command does not re-pick.
        const Command sticky = next_command(c, dead);
        const Command missing = next_command(c, std::nullopt);
        const Command bogus = next_command(c, CombatantId{~0u});
        expect_bonus_bite(sticky, expect, bite);
        expect_bonus_bite(missing, expect, bite);
        expect_bonus_bite(bogus, expect, bite);
        const SubmitResult r = submit_settled(c, sticky);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(c.state().budget.bonus_attack_grant == 0);
        CHECK_FALSE(c.state().budget.bonus_action);
        CHECK(c.log().back().step.request.target == expect);
        CHECK(c.log().back().step.request.as_bonus_action);
        // The sticky id is still the corpse. The action is not retargeted; the turn ends.
        const Command after = next_command(c, dead);
        CHECK(after.request.kind == ActionKind::EndTurn);
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, ch, lib, kDeadSeed, c.log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->hash() == c.hash());
        CHECK(rep->log_hash() == c.log_hash());
    });
}

TEST_CASE("gnoll_rampage: no other legal bite target leaves the grant unused") {
    const Spawn gnoll{kGnoll, 10, 10, ""};
    const Spawn victim{kFoe, 11, 10, "party"};
    const Spawn far{kGoblin, 10, 15, "party"};
    with_fight({gnoll, victim, far}, kDeadSeed, [](Combat& c, world::Chunk& ch, ContentLibrary&) {
        const CombatantId g = ch.spawns[0].stable_id, dead = ch.spawns[1].stable_id;
        script_bite(c, g, dead, false);
        REQUIRE_MESSAGE(who(c, dead).dead, "scripted bite missed (natural 1): choose another kDeadSeed");
        CHECK(rampage_fired(c, g, kGnoll));
        const uint8_t grant = c.state().budget.bonus_attack_grant;
        REQUIRE(grant != 0);
        const Command sticky = next_command(c, dead);
        const Command missing = next_command(c, std::nullopt);
        CHECK(sticky.request.kind == ActionKind::EndTurn);
        CHECK(missing.request.kind == ActionKind::EndTurn);
        CHECK(c.state().budget.bonus_attack_grant == grant);
        CHECK(c.state().budget.bonus_action);
    });
    // A downed PC is not dead, and still not a living bite target. Out of reach of anyone else: unused.
    const Spawn fighter{kFighter, 11, 10, ""};
    with_fight({gnoll, fighter, far}, kDownedSeed, [](Combat& c, world::Chunk& ch, ContentLibrary&) {
        const CombatantId g = ch.spawns[0].stable_id, downed = ch.spawns[1].stable_id;
        to_actor(c, g);
        const int bite = attack_named(who(c, g), "bite");
        REQUIRE(bite >= 0);
        who(c, downed).hp = 0;
        REQUIRE_FALSE(who(c, downed).dead);
        REQUIRE_FALSE(rules5e::is_conscious(who(c, downed)));
        state_of(c).budget.bonus_attack_grant = uint8_t(bite + 1);
        state_of(c).budget.bonus_action = true;
        const Command cmd = next_command(c, downed);
        CHECK(cmd.request.kind == ActionKind::EndTurn);
        CHECK(c.state().budget.bonus_attack_grant == uint8_t(bite + 1));
        CHECK(c.state().budget.bonus_action);
    });
}

TEST_CASE("gnoll_rampage: a downed sticky target is not living, so the grant retargets") {
    const Spawn gnoll{kGnoll, 10, 10, ""};
    const Spawn fighter{kFighter, 11, 10, ""};
    const Spawn other{kGoblin, 10, 11, "party"};
    with_fight({gnoll, fighter, other}, kDownedSeed, [](Combat& c, world::Chunk& ch, ContentLibrary& lib) {
        const CombatantId g = ch.spawns[0].stable_id, downed = ch.spawns[1].stable_id, foe = ch.spawns[2].stable_id;
        script_bite(c, g, downed, true);
        REQUIRE_MESSAGE(who(c, downed).hp <= 0, "scripted bite missed (natural 1): choose another kDownedSeed");
        REQUIRE_FALSE(who(c, downed).dead);   // a PC at 0 HP is dying, not dead
        REQUIRE_FALSE(rules5e::is_conscious(who(c, downed)));
        CHECK(rampage_fired(c, g, kGnoll));
        const int bite = attack_named(who(c, g), "bite");
        REQUIRE(c.state().budget.bonus_attack_grant == uint8_t(bite + 1));
        const Command cmd = next_command(c, downed);
        expect_bonus_bite(cmd, foe, bite);
        const SubmitResult r = submit_settled(c, cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(c.state().budget.bonus_attack_grant == 0);
        CHECK_FALSE(c.state().budget.bonus_action);
        (void)lib;
    });
}

TEST_CASE("gnoll_rampage: a living sticky target out of bite reach is not replaced") {
    const Spawn gnoll{kGnoll, 10, 10, ""};
    const Spawn sticky{kGoblin, 10, 14, "party"};   // 4 tiles: the bite (5 ft) cannot reach
    const Spawn neighbor{kGoblin, 11, 10, "party"};
    with_fight({gnoll, sticky, neighbor}, kDeadSeed, [](Combat& c, world::Chunk& ch, ContentLibrary&) {
        const CombatantId g = ch.spawns[0].stable_id, focus = ch.spawns[1].stable_id, near = ch.spawns[2].stable_id;
        to_actor(c, g);
        const int bite = attack_named(who(c, g), "bite");
        REQUIRE(bite >= 0);
        REQUIRE(rules5e::is_conscious(who(c, focus)));
        REQUIRE(c.distance_tiles(g, focus) > 1);
        REQUIRE(c.distance_tiles(g, near) == 1);
        state_of(c).budget.bonus_attack_grant = uint8_t(bite + 1);
        state_of(c).budget.bonus_action = true;
        const Command cmd = next_command(c, focus);
        const bool attack = cmd.kind == CommandKind::Rules && cmd.request.kind == ActionKind::Attack;
        if (attack) CHECK_FALSE(cmd.request.as_bonus_action);
        CHECK(c.state().budget.bonus_attack_grant == uint8_t(bite + 1));
        CHECK(c.state().budget.bonus_action);
        if (attack) {
            CHECK(cmd.request.target == focus);
            CHECK(cmd.request.target != near);
        }
    });
}
