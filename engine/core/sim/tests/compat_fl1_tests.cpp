// Compat behaviour golden: frozen content pack tests/compat/fl1 (never edited) loaded by the
// current engine must behave exactly as it did when fl1 was frozen.
//
// POLICY: the pinned values below NEVER change. If this test fails, the engine changed what
// existing content means (a content-semantics regression). Fix the engine (gate the new
// behaviour behind a new schema version + migration, see docs/CONTENT_COMPAT.md); do not update
// the goldens. The only exception is an explicit, Director-approved compat break, recorded in
// docs/CONTENT_COMPAT.md together with a new frozen pack.
//
// Design: the fight is driven by a FIXED command script defined in this file (not by
// sim::run_to_end / engine AI policies), so policy/tactics changes cannot move it. The digest
// covers only rules-level outcomes (initiative, moves, attack rolls, damage, downs/deaths, end)
// with their numbers, plus final hp and positions; it excludes the sim event-schema version and
// additive event kinds, so event-stream versioning doesn't break it either. Values were
// computed on main at c09ea79 (#25 merge), i.e. before feature hooks (#30).
#include <doctest/doctest.h>

#include <string>

#include "base/canonical_hash.hpp"
#include "sim/combat.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kFl1 = std::string(ENGINE_SOURCE_ROOT) + "/tests/compat/fl1/sample.vertical_slice";

// Frozen script: current actor attacks the first conscious hostile (combatant order) with
// attack 0 when adjacent and an action is left; else steps along the shortest path toward it;
// else ends its turn. Melee only. Do not change.
Command scripted(const Combat& c) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    Command end;
    end.request = {self, ActionKind::EndTurn};
    const rules5e::Combatant* foe = nullptr;
    for (const auto& k : s.combatants)
        if (k.id != self && c.hostile(self, k.id) && !k.dead && rules5e::is_conscious(k)) { foe = &k; break; }
    const auto* me = rules5e::find(s, self);
    if (!foe || !me || !rules5e::is_conscious(*me)) return end;
    const Vec2i mp = c.pos(self), fp = c.pos(foe->id);
    if (chebyshev(mp, fp) <= 1) {
        if (!s.budget.action && s.budget.attacks_remaining == 0) return end;
        Command a;
        a.request = {self, ActionKind::Attack, foe->id};
        a.request.attack_index = 0;
        return a;
    }
    std::vector<Vec2i> goals;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) goals.push_back({fp.x + dx, fp.y + dy});
    const auto path = c.grid().path(mp, goals, c.occupied_except(self));
    if (path.empty() || c.grid().step_cost_ft(path[0]) > s.budget.movement_ft) return end;
    Command st;
    st.kind = CommandKind::Step;
    st.to = path[0];
    return st;
}

struct CompatResult { uint64_t digest = 0; uint32_t commands = 0, attacks = 0, damage_total = 0; bool ended = false; };

CompatResult run_fl1(uint64_t seed) {
    ContentLibrary lib;
    auto errs = lib.load_pack(kFl1);
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, kFl1 + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    ecs::Registry reg;
    auto c = Combat::create(reg, *r.chunk, lib, seed);
    REQUIRE(c);
    CompatResult out;
    while (!c->ended() && out.commands < 2000) {
        auto res = c->submit(scripted(*c));
        REQUIRE_MESSAGE(res.ok, res.error);
        while (res.needs) res = c->decide({res.needs->window_seq, true, 0});   // always react
        ++out.commands;
    }
    out.ended = c->ended();
    CanonicalHasher h;
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
        if (k == EventKind::AttackResolved) ++out.attacks;
        if (k == EventKind::DamageDealt) out.damage_total += uint32_t(e.rules.a);
    }
    for (const auto& k : c->state().combatants) {
        h.u32(k.id);
        h.i32(k.hp);
        h.byte(k.dead ? 1 : 0);
        const Vec2i p2 = c->pos(k.id);
        h.i32(p2.x);
        h.i32(p2.y);
    }
    out.digest = h.value();
    return out;
}
} // namespace

TEST_CASE("compat fl1: frozen content semantics (stat blocks as the engine sees them)") {
    ContentLibrary lib;
    REQUIRE(lib.load_pack(kFl1).empty());
    const Archetype* f = lib.find("srd51.character.fighter_1");
    REQUIRE(f);
    REQUIRE(f->base.attacks.size() >= 1);
    // fl1 bakes Dueling into the longsword: 1d8+5 is the effective damage, with no extra bonus.
    CHECK(f->base.attacks[0].damage == rules5e::DiceExpr{1, 8, 5});
    CHECK(f->base.attacks[0].to_hit == 5);
    const Archetype* g = lib.find("srd51.monster.goblin");
    REQUIRE(g);
    CHECK(g->base.attacks[0].damage == rules5e::DiceExpr{1, 6, 2});
}

TEST_CASE("compat fl1: scripted seeded fights reproduce the frozen outcome (NEVER update these)") {
    struct Pin { uint64_t seed, digest; uint32_t commands, attacks, damage; };
    const Pin pins[] = {
        {1, 17752322533980382510ull, 13, 2, 11},
        {2, 5403282979316985951ull, 23, 7, 26},
        {2026, 5399669302866641243ull, 17, 4, 14},
    };
    for (const Pin& p : pins) {
        CAPTURE(p.seed);
        const auto r = run_fl1(p.seed);
        MESSAGE("{" << p.seed << ", " << r.digest << "ull, " << r.commands << ", " << r.attacks << ", " << r.damage_total << "},");
        CHECK(r.ended);
        CHECK(r.digest == p.digest);
        CHECK(r.commands == p.commands);
        CHECK(r.attacks == p.attacks);
        CHECK(r.damage_total == p.damage);
    }
}
