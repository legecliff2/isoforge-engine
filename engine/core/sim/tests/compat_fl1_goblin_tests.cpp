// Compat behaviour golden, goblin-focused: frozen pack tests/compat/fl1 (never edited) loaded by
// the current engine must keep the goblin's rules-level behaviour.
//
// POLICY: the pinned values below NEVER change (same policy as compat_fl1_tests.cpp; see
// docs/CONTENT_COMPAT.md). A failure means the engine changed what existing content means: gate
// the change behind a schema version + migration instead of re-pinning. Only an explicit,
// Director-approved compat break (with a new frozen pack) may change them.
//
// Design: a FIXED command script defined here drives both sides (no engine AI/policy): melee only
// (attack 0 when adjacent, else step toward the foe, else end turn). Ranged attacks are not used
// so that adding ranged/LOS rules (#34) cannot move these values. Nimble Escape is never
// requested, so its activation state cannot move these values; that it is inert for fl1
// (monster@1/@2) is asserted separately. Metrics are per-goblin (attacks, hits, damage dealt and
// taken, final hp) plus a digest of rules events present since sim v0 and final state; the sim
// event-schema version and additive event kinds are excluded. Values were computed on main at
// 19a372a (#30 merge), i.e. before the monster@3 gate. Seeds 1/2/2026 share the script with
// compat_fl1_tests.cpp, so their digests equal those pins (a cross-check).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "base/canonical_hash.hpp"
#include "sim/combat.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kFl1 = std::string(ENGINE_SOURCE_ROOT) + "/tests/compat/fl1/sample.vertical_slice";
const std::string kGoblinId = "srd51.monster.goblin";

const rules5e::Combatant* first_foe(const Combat& c, CombatantId self) {
    for (const auto& k : c.state().combatants)
        if (k.id != self && c.hostile(self, k.id) && !k.dead && rules5e::is_conscious(k)) return &k;
    return nullptr;
}

Command end_turn(CombatantId self) {
    Command e;
    e.request = {self, ActionKind::EndTurn};
    return e;
}

// Step toward the foe, else end turn. Do not change.
Command approach(const Combat& c, CombatantId self, Vec2i fp) {
    const auto& s = c.state();
    std::vector<Vec2i> goals;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) goals.push_back({fp.x + dx, fp.y + dy});
    const Vec2i mp = c.pos(self);
    const auto path = c.grid().path(mp, goals, c.occupied_except(self));
    if (path.empty() || c.grid().step_cost_ft(path[0]) > s.budget.movement_ft) return end_turn(self);
    Command st;
    st.kind = CommandKind::Step;
    st.to = path[0];
    return st;
}

// Frozen script (see header). Returns the primary command; `fallback` is used if it is rejected.
Command scripted(const Combat& c, bool is_goblin, Command* fallback) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    const auto* foe = first_foe(c, self);
    const auto* me = rules5e::find(s, self);
    *fallback = end_turn(self);
    if (!foe || !me || !rules5e::is_conscious(*me)) return end_turn(self);
    const Vec2i fp = c.pos(foe->id);
    const bool can_attack = s.budget.action || s.budget.attacks_remaining > 0;
    if (chebyshev(c.pos(self), fp) <= 1) {
        if (!can_attack) return end_turn(self);
        Command a;
        a.request = {self, ActionKind::Attack, foe->id};
        a.request.attack_index = 0;
        return a;
    }
    (void)is_goblin;
    return approach(c, self, fp);
}

struct GoblinResult {
    uint64_t digest = 0;
    uint32_t commands = 0, attacks = 0, hits = 0, dealt = 0, taken = 0;
    int32_t hp = 0;
    bool ended = false;
};

GoblinResult run(uint64_t seed) {
    ContentLibrary lib;
    auto errs = lib.load_pack(kFl1);
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, kFl1 + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    ecs::Registry reg;
    auto c = Combat::create(reg, *r.chunk, lib, seed);
    REQUIRE(c);
    CombatantId gob = 0;
    for (const auto& sp : r.chunk->spawns)
        if (sp.archetype == kGoblinId) gob = sp.stable_id;
    REQUIRE(gob != 0);
    GoblinResult out;
    while (!c->ended() && out.commands < 2000) {
        const bool is_goblin = rules5e::current_actor(c->state()) == gob;
        Command fb;
        auto res = c->submit(scripted(*c, is_goblin, &fb));
        if (!res.ok) res = c->submit(fb);
        REQUIRE_MESSAGE(res.ok, res.error);
        while (res.needs) res = c->decide({res.needs->window_seq, true, 0});   // always react
        ++out.commands;
    }
    out.ended = c->ended();
    CanonicalHasher h;
    CombatantId last_attacker = 0;
    for (const auto& e : c->events()) {
        if (e.kind != SimEvent::Kind::Rules) continue;
        const auto k = e.rules.kind;
        if (k != EventKind::InitiativeRolled && k != EventKind::TurnStarted && k != EventKind::Moved &&
            k != EventKind::AttackResolved && k != EventKind::DamageDealt && k != EventKind::CombatantDowned &&
            k != EventKind::CombatantDied && k != EventKind::DeathSaveRolled && k != EventKind::EncounterEnded)
            continue;
        h.byte(uint8_t(k));
        h.u32(e.rules.actor);
        h.u32(e.rules.target);
        h.i32(e.rules.a);
        h.i32(e.rules.b);
        h.i32(e.rules.c);
        if (k == EventKind::AttackResolved) {
            last_attacker = e.rules.actor;
            if (e.rules.actor == gob) {
                ++out.attacks;
                if (e.rules.c & 1) ++out.hits;
            }
        }
        if (k == EventKind::DamageDealt) {
            if (e.rules.target == gob) out.taken += uint32_t(e.rules.a);
            else if (last_attacker == gob) out.dealt += uint32_t(e.rules.a);
        }
    }
    for (const auto& k : c->state().combatants) {
        h.u32(k.id);
        h.i32(k.hp);
        h.byte(k.dead ? 1 : 0);
        const Vec2i p2 = c->pos(k.id);
        h.i32(p2.x);
        h.i32(p2.y);
        if (k.id == gob) out.hp = k.hp;
    }
    out.digest = h.value();
    return out;
}
} // namespace

TEST_CASE("compat fl1 goblin: frozen stat block as the engine sees it") {
    ContentLibrary lib;
    REQUIRE(lib.load_pack(kFl1).empty());
    const Archetype* g = lib.find(kGoblinId);
    REQUIRE(g);
    REQUIRE(g->base.attacks.size() == 2);
    CHECK(g->base.attacks[0].damage == rules5e::DiceExpr{1, 6, 2});   // scimitar
    CHECK(g->base.attacks[0].to_hit == 4);
    CHECK(g->base.attacks[1].damage == rules5e::DiceExpr{1, 6, 2});   // shortbow
    CHECK(g->base.attacks[1].to_hit == 4);
    CHECK(g->base.hp_max == 7);
    CHECK(g->base.ac == 15);
    // fl1 is monster@1: Nimble Escape is descriptive (inert via the monster@3 gate), as it was
    // when fl1 was frozen (before #30's feature hooks).
    CHECK(g->inert_features == std::vector<std::string>{"nimble_escape (bonus_action_grant)"});
    CHECK(g->base.bonus_action_grants == 0);
    CHECK(g->active.empty());
}

TEST_CASE("compat fl1 goblin: scripted seeded fights reproduce the frozen outcome (NEVER update these)") {
    struct Pin { uint64_t seed, digest; uint32_t commands, attacks, hits, dealt, taken; int32_t hp; };
    const Pin pins[] = {
        {1, 17752322533980382510ull, 13, 1, 0, 0, 11, 0},
        {2, 5403282979316985951ull, 23, 3, 1, 9, 17, 0},
        {3, 15477144339721690572ull, 13, 1, 1, 4, 8, 0},
        {7, 7905605568869462007ull, 21, 3, 1, 4, 7, 0},
        {2026, 5399669302866641243ull, 17, 2, 2, 14, 0, 7},
    };
    for (const Pin& p : pins) {
        CAPTURE(p.seed);
        const auto r = run(p.seed);
        MESSAGE("{" << p.seed << ", " << r.digest << "ull, " << r.commands << ", " << r.attacks << ", " << r.hits << ", "
                    << r.dealt << ", " << r.taken << ", " << r.hp << "},");
        CHECK(r.ended);
        CHECK(r.digest == p.digest);
        CHECK(r.commands == p.commands);
        CHECK(r.attacks == p.attacks);
        CHECK(r.hits == p.hits);
        CHECK(r.dealt == p.dealt);
        CHECK(r.taken == p.taken);
        CHECK(r.hp == p.hp);
    }
}
