// Bound (CONTENT_CONVENTIONS §50). Half of the damage the wearer takes, rounded up, is transferred to the
// linked guardian while that guardian is within radius_ft. Not share_damage: the wearer keeps the rest, and
// the link does not end past the radius. No new event kind. The wearer id folds under Combat::hash 0xE5 in sim.
#include <doctest/doctest.h>

#include <memory>
#include <optional>

#include "encounter_util.hpp"

using namespace enc;

namespace {
Combatant body(CombatantId id, uint8_t team, int hp = 40) {
    auto c = monster(id);
    c.team = team;
    c.hp = c.hp_max = int16_t(hp);
    c.ac = 10;
    for (auto& p : c.reaction_policy) p = ReactionPolicy::Never;
    return c;
}
void arm(EncounterState& s, int16_t bonus, DamageType type) {
    auto& at = mut(s, 1).attacks[0];
    at.to_hit = 100;
    at.type = type;
    at.damage = {0, 0, bonus};
    at.extra_damage.clear();
}
bool attack_hit(const std::vector<Event>& ev) {
    for (const auto& e : ev)
        if (e.kind == EventKind::AttackResolved && (e.c & 1) != 0) return true;
    return false;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a;
    return n;
}
EncounterHooks hooks_at(const std::shared_ptr<int>& dist) {
    EncounterHooks h;
    if (dist)
        h.sight_facts = [dist](const EncounterState&, const Combatant&, const Combatant&) {
            SightFacts f;
            f.distance_ft = uint16_t(*dist < 0 ? 0 : *dist);
            return f;
        };
    return h;
}
struct Blow {
    EncounterState state;
    StepResult step;
};
std::optional<Blow> swing(std::vector<Combatant> cs, const std::shared_ptr<int>& dist, int16_t bonus, DamageType type,
                          uint64_t seed) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks_at(dist));
    arm(s, bonus, type);
    end_turns_until(s, 1);
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    if (!r.ok || !attack_hit(r.events)) return std::nullopt;
    return Blow{std::move(s), std::move(r)};
}
Blow until_blow(std::vector<Combatant> cs, const std::shared_ptr<int>& dist, int16_t bonus,
                DamageType type = DamageType::Slashing) {
    for (uint64_t seed = 1; seed < 80; ++seed) {
        auto hit = swing(cs, dist, bonus, type, seed);
        if (hit) return std::move(*hit);
    }
    FAIL("no seed landed the attack");
    return Blow{};
}
void link(Combatant& guardian, CombatantId wearer, uint16_t radius = 60) { guardian.bound = {radius, wearer}; }
}  // namespace

TEST_CASE("bound: within 60 feet, half rounded up moves to the guardian and the wearer keeps the rest") {
    auto dist = std::make_shared<int>(30);
    auto wear = body(2, 1);
    auto guard = body(3, 1);
    link(guard, 2);
    auto odd = until_blow({body(1, 0), wear, guard}, dist, 11);
    CHECK(dealt_to(odd.step.events, 2) == 5);
    CHECK(dealt_to(odd.step.events, 3) == 6);
    CHECK(find(odd.state, 2)->hp == 35);
    CHECK(find(odd.state, 3)->hp == 34);
    CHECK(find(odd.state, 3)->bound.wearer == 2);

    auto even = until_blow({body(1, 0), body(2, 1), guard}, dist, 10);
    CHECK(dealt_to(even.step.events, 2) == 5);
    CHECK(dealt_to(even.step.events, 3) == 5);

    auto one = until_blow({body(1, 0), body(2, 1), guard}, dist, 1);
    CHECK(dealt_to(one.step.events, 2) == 0);
    CHECK(dealt_to(one.step.events, 3) == 1);
    CHECK(find(one.state, 2)->hp == 40);
    CHECK(find(one.state, 3)->hp == 39);
}

TEST_CASE("bound: 60 feet transfers, farther does not, and the link stays for a later hit") {
    auto dist = std::make_shared<int>(60);
    auto guard = body(3, 1);
    link(guard, 2);
    auto edge = until_blow({body(1, 0), body(2, 1), guard}, dist, 11);
    CHECK(dealt_to(edge.step.events, 3) == 6);
    CHECK(dealt_to(edge.step.events, 2) == 5);

    *dist = 65;
    auto far = until_blow({body(1, 0), body(2, 1), guard}, dist, 11);
    CHECK(dealt_to(far.step.events, 2) == 11);
    CHECK(dealt_to(far.step.events, 3) == 0);
    CHECK(find(far.state, 2)->hp == 29);
    CHECK(find(far.state, 3)->hp == 40);
    CHECK(find(far.state, 3)->bound.wearer == 2);
    CHECK(find(far.state, 3)->bound.radius_ft == 60);

    for (uint64_t seed = 1; seed < 80; ++seed) {
        *dist = 65;
        auto s = begin_encounter({body(1, 0), body(2, 1), guard}, seed, nullptr, hooks_at(dist));
        arm(s, 10, DamageType::Slashing);
        end_turns_until(s, 1);
        auto first = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
        if (!first.ok || !attack_hit(first.events)) continue;
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        *dist = 30;
        end_turns_until(s, 1);
        arm(s, 10, DamageType::Slashing);
        auto second = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
        if (!second.ok || !attack_hit(second.events)) continue;
        CHECK(dealt_to(first.events, 3) == 0);
        CHECK(dealt_to(second.events, 3) == 5);
        CHECK(dealt_to(second.events, 2) == 5);
        CHECK(find(s, 3)->bound.wearer == 2);
        return;
    }
    FAIL("no seed landed both attacks");
}

TEST_CASE("bound: no sight hook does not transfer, and a dead guardian does not") {
    auto guard = body(3, 1);
    link(guard, 2);
    auto none = until_blow({body(1, 0), body(2, 1), guard}, nullptr, 10);
    CHECK(dealt_to(none.step.events, 2) == 10);
    CHECK(dealt_to(none.step.events, 3) == 0);
    CHECK(find(none.state, 3)->bound.wearer == 2);

    guard.hp = 0;
    guard.dead = true;
    auto dist = std::make_shared<int>(10);
    auto dead = until_blow({body(1, 0), body(2, 1), guard}, dist, 10);
    CHECK(dealt_to(dead.step.events, 2) == 10);
    CHECK(dealt_to(dead.step.events, 3) == 0);
}

TEST_CASE("bound: the transfer is the damage taken after the wearer's mitigation, before temporary hit points") {
    auto dist = std::make_shared<int>(30);
    auto wear = body(2, 1);
    wear.resistances = bit(DamageType::Fire);
    auto guard = body(3, 1);
    link(guard, 2);
    auto resisted = until_blow({body(1, 0), wear, guard}, dist, 11, DamageType::Fire);
    CHECK(dealt_to(resisted.step.events, 2) == 2);   // 11 halved down to 5, then floor remains
    CHECK(dealt_to(resisted.step.events, 3) == 3);   // ceil(5 / 2)
    CHECK(find(resisted.state, 2)->hp == 38);
    CHECK(find(resisted.state, 3)->hp == 37);

    wear.resistances = 0;
    wear.immunities = bit(DamageType::Fire);
    auto immune = until_blow({body(1, 0), wear, guard}, dist, 11, DamageType::Fire);
    CHECK(dealt_to(immune.step.events, 2) == 0);
    CHECK(dealt_to(immune.step.events, 3) == 0);
    CHECK(find(immune.state, 2)->hp == 40);
    CHECK(find(immune.state, 3)->hp == 40);

    wear.immunities = 0;
    wear.vulnerabilities = bit(DamageType::Fire);
    auto vuln = until_blow({body(1, 0), wear, guard}, dist, 5, DamageType::Fire);
    CHECK(dealt_to(vuln.step.events, 2) == 5);   // 5 doubled to 10, then half
    CHECK(dealt_to(vuln.step.events, 3) == 5);

    wear.vulnerabilities = 0;
    wear.temp_hp = 3;
    auto soaked = until_blow({body(1, 0), wear, guard}, dist, 11);
    CHECK(dealt_to(soaked.step.events, 2) == 5);
    CHECK(dealt_to(soaked.step.events, 3) == 6);
    CHECK(find(soaked.state, 2)->temp_hp == 0);
    CHECK(find(soaked.state, 2)->hp == 38);   // remainder 5, temp absorbed 3
    CHECK(find(soaked.state, 3)->hp == 34);
}

TEST_CASE("bound: the guardian's resistance does not reduce the integer, and its temporary hit points still absorb") {
    auto dist = std::make_shared<int>(30);
    auto guard = body(3, 1);
    link(guard, 2);
    guard.resistances = bit(DamageType::Fire);
    guard.temp_hp = 4;
    auto hit = until_blow({body(1, 0), body(2, 1), guard}, dist, 11, DamageType::Fire);
    CHECK(dealt_to(hit.step.events, 3) == 6);
    CHECK(find(hit.state, 3)->temp_hp == 0);
    CHECK(find(hit.state, 3)->hp == 38);   // 6 through resistance, 4 of them into temp HP
    CHECK(find(hit.state, 2)->hp == 35);
}

TEST_CASE("bound: a halving Damaged reaction is part of the damage taken, then half of that is transferred") {
    auto dist = std::make_shared<int>(30);
    auto wear = body(2, 1);
    wear.reaction_policy[std::size_t(ReactionTrigger::Damaged)] = ReactionPolicy::Auto;
    Reaction r;
    r.id = 1;
    r.window = ReactionTrigger::Damaged;
    r.kind = ReactionEffectKind::ReduceDamage;
    r.half = true;
    wear.reactions = {r};
    auto guard = body(3, 1);
    link(guard, 2);
    auto hit = until_blow({body(1, 0), wear, guard}, dist, 11);
    CHECK(dealt_to(hit.step.events, 2) == 2);   // 11 halved down to 5, then floor remains
    CHECK(dealt_to(hit.step.events, 3) == 3);
    CHECK(find(hit.state, 2)->reaction_used);
}

TEST_CASE("bound: transferring the whole point does not call a concentration save; a kept remainder does") {
    auto dist = std::make_shared<int>(30);
    auto wear = body(2, 1);
    wear.concentration_spell = 99;
    auto guard = body(3, 1);
    link(guard, 2);
    auto all = until_blow({body(1, 0), wear, guard}, dist, 1);
    CHECK(find(all.state, 2)->concentration_spell == 99);
    CHECK(count(all.step.events, EventKind::SavingThrowRolled) == 0);

    auto kept = until_blow({body(1, 0), wear, guard}, dist, 11);
    CHECK(count(kept.step.events, EventKind::SavingThrowRolled) == 1);
}

TEST_CASE("bound: a cycle stops, and a second guardian takes half of what remains") {
    auto dist = std::make_shared<int>(30);
    auto a = body(2, 1);
    auto b = body(3, 1);
    link(a, 3);
    link(b, 2);
    auto cyc = until_blow({body(1, 0), a, b}, dist, 10);
    CHECK(dealt_to(cyc.step.events, 2) == 5);
    CHECK(dealt_to(cyc.step.events, 3) == 5);
    CHECK(find(cyc.state, 2)->hp == 35);
    CHECK(find(cyc.state, 3)->hp == 35);

    auto g1 = body(3, 1);
    auto g2 = body(4, 1);
    link(g1, 2);
    link(g2, 2);
    auto both = until_blow({body(1, 0), body(2, 1), g1, g2}, dist, 10);
    CHECK(dealt_to(both.step.events, 3) == 5);
    CHECK(dealt_to(both.step.events, 4) == 3);
    CHECK(dealt_to(both.step.events, 2) == 2);
    CHECK(find(both.state, 2)->hp == 38);
}
