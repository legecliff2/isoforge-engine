// Tier-A §27 (vampire spawn Claws, Lich rulings): ActionRequest::grapple_instead on an attack with Attack::grapple_instead_ok.
// A hit with the choice rolls no damage (a crit too), deals none, and marks AttackResolved c bit 3 (8) for the sim's grapple hook;
// extra_damage hit riders do not fire, other hit riders do. Without the rider the request is refused.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
Attack claws() { Attack a{"claws", 30, {2, 4, 3}, DamageType::Slashing, 5, false, false}; a.grapple_instead_ok = true; return a; }
const Attack kPlain{"slam", 30, {1, 6, 2}, DamageType::Bludgeoning, 5, false, false};

Combatant spawn(CombatantId id) {
    auto c = monster(id);
    c.attacks = {claws(), kPlain};
    return c;
}
EncounterState start(uint64_t seed) {
    auto s = begin_encounter({spawn(1), pc(2, 500)}, seed);
    mut(s, 1).team = 1; mut(s, 2).team = 0;
    end_turns_until(s, 1);
    return s;
}
ActionRequest claw(bool instead, uint8_t index = 0) {
    ActionRequest r{1, ActionKind::Attack, 2, index};
    r.target_within_5ft = true; r.grapple_instead = instead;
    return r;
}
const Event* resolved(const std::vector<Event>& ev) {
    for (const auto& e : ev) if (e.kind == EventKind::AttackResolved) return &e;
    return nullptr;
}
// the first seed whose claw roll is a hit with the wanted crit-ness (to_hit 30: only a natural 1 misses)
uint64_t seed_for(bool want_crit) {
    for (uint64_t seed = 1; seed < 2000; ++seed) {
        auto s = start(seed);
        auto r = step(s, claw(false));
        const Event* e = resolved(r.events);
        if (r.ok && e && (e->c & 1) && bool(e->c & 2) == want_crit) return seed;
    }
    return 0;
}
}  // namespace

TEST_CASE("grapple_instead: a hit deals no damage and sets the c & 8 bit; without it, damage and no bit") {
    const uint64_t seed = seed_for(false);
    REQUIRE(seed != 0);
    {
        auto s = start(seed);
        const int hp0 = mut(s, 2).hp;
        auto r = step(s, claw(true));
        REQUIRE(r.ok);
        const Event* e = resolved(r.events);
        REQUIRE(e);
        CHECK((e->c & 1));
        CHECK((e->c & 8));
        CHECK(count(r.events, EventKind::DamageDealt) == 0);
        CHECK(mut(s, 2).hp == hp0);
    }
    {
        auto s = start(seed);
        const int hp0 = mut(s, 2).hp;
        auto r = step(s, claw(false));
        REQUIRE(r.ok);
        const Event* e = resolved(r.events);
        REQUIRE(e);
        CHECK_FALSE((e->c & 8));
        CHECK(count(r.events, EventKind::DamageDealt) == 1);
        CHECK(mut(s, 2).hp < hp0);
        CHECK(hp0 - mut(s, 2).hp >= 5);   // 2d4+3
    }
}

TEST_CASE("grapple_instead: a crit grapples only (no dice to double)") {
    const uint64_t seed = seed_for(true);
    REQUIRE(seed != 0);
    auto s = start(seed);
    const int hp0 = mut(s, 2).hp;
    auto r = step(s, claw(true));
    REQUIRE(r.ok);
    const Event* e = resolved(r.events);
    REQUIRE(e);
    CHECK((e->c & 2));
    CHECK((e->c & 8));
    CHECK(mut(s, 2).hp == hp0);
}

TEST_CASE("grapple_instead: refused on an attack without the rider; legal_actions lists both variants") {
    auto s = start(3);
    auto r = step(s, claw(true, 1));
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("instead_of_damage") != std::string::npos);
    int with = 0, without = 0, plain_with = 0;
    for (const auto& q : legal_actions(s)) {
        if (q.kind != ActionKind::Attack || q.special != 0 || q.target != 2) continue;
        if (q.attack_index == 0) (q.grapple_instead ? with : without)++;
        if (q.attack_index == 1 && q.grapple_instead) ++plain_with;
    }
    CHECK(with == 1);
    CHECK(without == 1);
    CHECK(plain_with == 0);
}

TEST_CASE("grapple_instead: a miss is a miss either way") {
    for (uint64_t seed = 1; seed < 2000; ++seed) {
        auto s = start(seed);
        auto r = step(s, claw(true));
        const Event* e = resolved(r.events);
        if (!r.ok || !e || (e->c & 1)) continue;
        CHECK_FALSE((e->c & 8));
        CHECK(count(r.events, EventKind::DamageDealt) == 0);
        return;
    }
    FAIL("no missing seed found");
}
