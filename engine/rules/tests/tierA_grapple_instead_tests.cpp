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

TEST_CASE("B5 no_damage: a hit deals no damage and does not set the grapple_instead bit") {
    Attack tend{"tendril", 40, {0, 0, 0}, DamageType::Bludgeoning, 50, false, false};
    tend.grapple_no_damage = true;
    Combatant a = monster(1);
    a.attacks = {tend};
    a.team = 1;
    Combatant b = pc(2, 80);
    b.team = 0;
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = begin_encounter({a, b}, seed);
        end_turns_until(s, 1);
        ActionRequest q{1, ActionKind::Attack, 2, 0};
        q.target_within_5ft = true;
        const int hp0 = mut(s, 2).hp;
        auto out = step(s, q);
        const Event* e = nullptr;
        for (const auto& ev : out.events) if (ev.kind == EventKind::AttackResolved) e = &ev;
        if (!out.ok || !e || !(e->c & 1)) continue;
        CHECK_FALSE((e->c & 8) != 0);
        CHECK(count(out.events, EventKind::DamageDealt) == 0);
        CHECK(mut(s, 2).hp == hp0);
        auto s2 = begin_encounter({a, b}, seed);
        end_turns_until(s2, 1);
        q.grapple_instead = true;
        auto refused = step(s2, q);
        CHECK_FALSE(refused.ok);
        CHECK(std::string(refused.error).find("instead_of_damage") != std::string::npos);
        return;
    }
    FAIL("no hitting seed");
}
