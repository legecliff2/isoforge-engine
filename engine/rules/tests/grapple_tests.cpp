// R-G8: conditions with a source, grapple / shove / escape, drag, size limits.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

Combatant strong(CombatantId id) { auto c = pc(id, 200); c.team = 0; return c; }
Combatant foe(CombatantId id, Size sz = Size::Medium) {
    auto m = monster(id); m.hp = m.hp_max = 500; m.size = sz; m.team = 1; return m;
}
EncounterState start(Combatant a, Combatant b, uint64_t seed = 1) {
    auto s = begin_encounter({std::move(a), std::move(b)}, seed, nullptr, spell_hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest special(uint8_t kind, CombatantId actor, CombatantId target, bool near = true) {
    ActionRequest r{actor, ActionKind::Attack, target};
    r.special = kind; r.target_within_5ft = near;
    return r;
}
std::vector<const Event*> checks(const std::vector<Event>& ev) {
    std::vector<const Event*> out;
    for (const auto& e : ev) if (e.kind == EventKind::CheckRolled) out.push_back(&e);
    return out;
}
bool has_event(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return true;
    return false;
}
}  // namespace

TEST_CASE("defaults and size limits") {
    ActionRequest r{1, ActionKind::Attack};
    CHECK(r.special == 0);
    EncounterState s;
    CHECK(s.sourced.empty());
    CHECK_FALSE(has_sourced_conditions(s));
    CHECK(size_within_one(Size::Medium, Size::Medium));
    CHECK(size_within_one(Size::Medium, Size::Large));
    CHECK_FALSE(size_within_one(Size::Medium, Size::Huge));
    CHECK(size_within_one(Size::Huge, Size::Tiny));
    CHECK_FALSE(size_within_one(Size::Small, Size::Huge));
}

TEST_CASE("grapple: Athletics against the better of Athletics / Acrobatics; the attacker must win outright") {
    int won = 0, lost = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        const auto r = step(s, special(1, 1, 2));
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        CHECK(c[0]->actor == 1); CHECK(c[1]->actor == 2);
        const bool grappled = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Grappled) == grappled);
        CHECK(has_event(r.events, EventKind::ConditionAdded) == grappled);
        CHECK((c[1]->c & 1) == int(!grappled));
        if (grappled) {
            ++won;
            CHECK(condition_source(s, 2, Condition::Grappled) == 1);
            REQUIRE(s.sourced.size() == 1);
            CHECK(s.sourced[0].escape_dc == 0);
            CHECK(effective_speed(s.combatants[1]) == 0);
            CHECK(grappled_by(s, 1) == std::vector<CombatantId>{2});
            CHECK(has_sourced_conditions(s));
        } else { ++lost; CHECK(s.sourced.empty()); }
        CHECK_FALSE(s.budget.action);
    }
    CHECK(won > 0); CHECK(lost > 0);
}

TEST_CASE("grapple and shove refuse: too large, out of reach, no such target, charmed, no action") {
    auto s = start(strong(1), foe(2, Size::Huge));
    CHECK_FALSE(step(s, special(1, 1, 2)).ok);   // Huge is two sizes above Medium
    CHECK_FALSE(step(s, special(2, 1, 2)).ok);
    CHECK(s.budget.action);
    auto t = start(strong(1), foe(2, Size::Large));
    CHECK_FALSE(step(t, special(1, 1, 2, false)).ok);   // not within 5 ft
    CHECK_FALSE(step(t, special(1, 1, 1)).ok);          // self
    CHECK_FALSE(step(t, special(1, 1, 9)).ok);
    CHECK_FALSE(step(t, special(7, 1, 2)).ok);          // bad kind
    CHECK(step(t, special(1, 1, 2)).ok);                // Large is one size up: allowed
    CHECK_FALSE(step(t, special(1, 1, 2)).ok);          // the action is spent
    auto u = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(apply_sourced_condition(u, 2, 1, Condition::Charmed, 0, sink));
    CHECK_FALSE(step(u, special(1, 1, 2)).ok);
    CHECK(u.budget.action);
    ActionRequest attack{1, ActionKind::Attack, 2}; attack.target_within_5ft = true;
    CHECK_FALSE(step(u, attack).ok);                    // a charmed creature cannot attack its charmer
}

TEST_CASE("grapple and shove replace one attack of the Attack action (Extra Attack)") {
    auto c = strong(1); c.attacks_per_action = 2;
    auto s = start(c, foe(2));
    REQUIRE(step(s, special(1, 1, 2)).ok);
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.attacks_remaining == 1);
    REQUIRE(step(s, special(2, 1, 2)).ok);
    CHECK(s.budget.attacks_remaining == 0);
    CHECK_FALSE(step(s, special(2, 1, 2)).ok);
}

TEST_CASE("shove: prone, or pushed 5 ft (a Moved event for the target)") {
    int prone = 0, pushed = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        auto r = step(s, special(2, 1, 2));
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        const bool win = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Prone) == win);
        prone += win;
        auto t = start(strong(1), foe(2), seed);
        auto q = step(t, special(3, 1, 2));
        const auto d = checks(q.events);
        REQUIRE(d.size() == 2);
        const bool win2 = d[0]->b > d[1]->b;
        const Event* mv = nullptr;
        for (const auto& e : q.events) if (e.kind == EventKind::Moved) mv = &e;
        CHECK((mv != nullptr) == win2);
        if (mv) { CHECK(mv->actor == 2); CHECK(mv->target == 1); CHECK(mv->a == 5); ++pushed; }
        CHECK(t.sourced.empty());
    }
    CHECK(prone > 0); CHECK(pushed > 0);
}

TEST_CASE("escape: against the recorded DC, or a contest against the grappler") {
    int freed = 0, held = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 1, 2, 12, sink));
        CHECK(s.sourced[0].escape_dc == 12);
        end_turns_until(s, 2);
        const auto r = step(s, special(4, 2, 1));
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 1);
        const bool free = c[0]->b >= 12;
        CHECK(s.combatants[1].has(Condition::Grappled) == !free);
        CHECK(has_event(r.events, EventKind::ConditionRemoved) == free);
        CHECK(s.sourced.empty() == free);
        (free ? freed : held)++;
    }
    CHECK(freed > 0); CHECK(held > 0);
    freed = held = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 1, 2, 0, sink));
        end_turns_until(s, 2);
        const auto r = step(s, special(4, 2, 0));   // any grappler
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        CHECK(c[0]->actor == 2); CHECK(c[1]->actor == 1);
        const bool free = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Grappled) == !free);
        (free ? freed : held)++;
    }
    CHECK(freed > 0); CHECK(held > 0);
}

TEST_CASE("escape refusals") {
    auto s = start(strong(1), foe(2));
    end_turns_until(s, 2);
    CHECK_FALSE(step(s, special(4, 2, 1)).ok);   // not grappled
    s.combatants[1].conditions |= bit(Condition::Grappled);   // grappled by something with no recorded source
    CHECK_FALSE(step(s, special(4, 2, 1)).ok);
    auto r = special(4, 2, 1); r.dc = 10;
    CHECK(step(s, r).ok);
    CHECK_FALSE(s.budget.action);
    auto t = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(t, 1, 2, 10, sink));
    end_turns_until(t, 2);
    CHECK_FALSE(step(t, special(4, 2, 7)).ok);   // not held by creature 7
    CHECK(t.budget.action);
}

TEST_CASE("a grapple ends when the grappler is incapacitated or dies") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(s, 1, 2, 0, sink));
    s.combatants[0].conditions |= bit(Condition::Stunned);
    std::vector<Event> ev;
    prune_sourced(s, ev);
    CHECK_FALSE(s.combatants[1].has(Condition::Grappled));
    CHECK(s.sourced.empty());
    CHECK(has_event(ev, EventKind::ConditionRemoved));
    // a second holder keeps the condition
    auto t = start(strong(1), foe(2));
    REQUIRE(apply_sourced_condition(t, 1, 2, Condition::Grappled, 0, sink));
    t.combatants.push_back(foe(3));
    REQUIRE(apply_sourced_condition(t, 3, 2, Condition::Grappled, 0, sink));
    t.combatants[0].conditions |= bit(Condition::Stunned);
    prune_sourced(t, ev);
    CHECK(t.combatants[1].has(Condition::Grappled));
    CHECK(t.sourced.size() == 1);
    CHECK(t.sourced[0].source == 3);
    // an entry whose condition has gone is dropped quietly
    t.combatants[1].conditions = 0;
    prune_sourced(t, ev);
    CHECK(t.sourced.empty());
}

TEST_CASE("moving a grappled creature costs double unless it is two sizes smaller") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(s, 1, 2, 0, sink));
    const int before = s.budget.movement_ft;
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 10;
    REQUIRE(step(s, mv).ok);
    CHECK(before - s.budget.movement_ft == 20);
    auto t = start(strong(1), foe(2, Size::Tiny));
    REQUIRE(grapple(t, 1, 2, 0, sink));
    const int b2 = t.budget.movement_ft;
    REQUIRE(step(t, mv).ok);
    CHECK(b2 - t.budget.movement_ft == 10);
    auto u = start(strong(1), foe(2));
    const int b3 = u.budget.movement_ft;
    REQUIRE(step(u, mv).ok);
    CHECK(b3 - u.budget.movement_ft == 10);
}

TEST_CASE("grapple() refuses a too-large, immune, dead or self target and leaves no entry") {
    auto s = start(strong(1), foe(2, Size::Gargantuan));
    std::vector<Event> ev;
    CHECK_FALSE(grapple(s, 1, 2, 0, ev));
    CHECK_FALSE(grapple(s, 1, 1, 0, ev));
    CHECK_FALSE(grapple(s, 1, 9, 0, ev));
    CHECK(s.sourced.empty());
    auto t = start(strong(1), foe(2));
    t.combatants[1].condition_immunities |= bit(Condition::Grappled);
    CHECK_FALSE(grapple(t, 1, 2, 0, ev));
    CHECK(ev.empty());
    auto u = start(strong(1), foe(2));
    u.combatants[1].dead = true;
    CHECK_FALSE(grapple(u, 1, 2, 0, ev));
}

TEST_CASE("sourced_digest folds every field and is order-stable") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, 12, ev));
    const uint64_t d1 = sourced_digest(s);
    CHECK(d1 != sourced_digest(EncounterState{}));
    CHECK(sourced_digest(s) == d1);
    REQUIRE(grapple(s, 1, 2, 13, ev));   // same key: the DC is updated, not duplicated
    CHECK(s.sourced.size() == 1);
    CHECK(sourced_digest(s) != d1);
}

// ---- Druid gap G: grapple rider restrains / exclusive / max_grapples (monster@5 on_hit) ----
namespace {
EncounterState start4(uint64_t seed = 1) {
    auto s = begin_encounter({strong(1), foe(2), foe(3), foe(4)}, seed, nullptr, spell_hooks());
    end_turns_until(s, 1);
    return s;
}
GrappleRider rider(int dc, bool restrains, bool exclusive, uint8_t max = 0) {
    GrappleRider g; g.escape_dc = int16_t(dc); g.restrains = restrains; g.exclusive = exclusive; g.max_grapples = max; return g;
}
bool offers_attack(const EncounterState& s, CombatantId target, uint8_t attack_index) {
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::Attack && r.special == 0 && r.target == target && r.attack_index == attack_index) return true;
    return false;
}
}  // namespace

TEST_CASE("grapple rider: a plain rider is exactly grapple() (no appended fields set)") {
    auto s = start4();
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, rider(12, false, false), 0, ev));
    REQUIRE(s.sourced.size() == 1);
    CHECK(s.sourced[0].via_attack == 0xFF);
    CHECK_FALSE(s.sourced[0].restrains);
    CHECK_FALSE(s.sourced[0].exclusive);
    CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
    CHECK_FALSE(attack_locked(s, 1, 0, 3));
}

TEST_CASE("grapple rider: restrains while grappled; exclusive blocks the attack on other targets") {
    auto s = start4();
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, rider(12, true, true), 0, ev));   // crocodile bite: grappled (DC 12), restrained, can't bite another
    CHECK(mut(s, 2).has(Condition::Grappled));
    CHECK(mut(s, 2).has(Condition::Restrained));
    REQUIRE(s.sourced.size() == 1);
    CHECK(s.sourced[0].via_attack == 0);
    CHECK(s.sourced[0].owns_restrained);
    CHECK(attack_locked(s, 1, 0, 3));
    CHECK_FALSE(attack_locked(s, 1, 0, 2));   // the held creature may still be bitten
    CHECK_FALSE(attack_locked(s, 2, 0, 3));
    CHECK(offers_attack(s, 2, 0));
    CHECK_FALSE(offers_attack(s, 3, 0));
    ActionRequest other{1, ActionKind::Attack, 3};
    other.target_within_5ft = true;
    const auto r = step(s, other);
    CHECK_FALSE(r.ok);
    CHECK(s.budget.action);   // refused before anything is spent
    ActionRequest held{1, ActionKind::Attack, 2};
    held.target_within_5ft = true;
    CHECK(step(s, held).ok);
}

TEST_CASE("grapple rider: Restrained clears on escape, and only on a successful one") {
    int freed = 0, kept = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start4(seed);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 1, 2, rider(12, true, true), 0, sink));
        end_turns_until(s, 2);
        const auto r = step(s, special(4, 2, 1));
        REQUIRE(r.ok);
        const bool free = !mut(s, 2).has(Condition::Grappled);
        CHECK(mut(s, 2).has(Condition::Restrained) == !free);
        if (free) {
            CHECK(s.sourced.empty());
            CHECK_FALSE(attack_locked(s, 1, 0, 3));
            int removed = 0;
            for (const auto& e : r.events) removed += e.kind == EventKind::ConditionRemoved && e.actor == 2;
            CHECK(removed == 2);   // Restrained and Grappled
        }
        (free ? freed : kept)++;
    }
    CHECK(freed > 0); CHECK(kept > 0);
}

TEST_CASE("grapple rider: the grapple ending (incapacitated grappler, out of reach) ends the restraint it added only") {
    auto s = start4();
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, rider(12, true, false), 0, ev));
    mut(s, 1).conditions |= bit(Condition::Stunned);
    prune_sourced(s, ev);
    CHECK_FALSE(mut(s, 2).has(Condition::Grappled));
    CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
    // already restrained by something else: the grapple does not own it and leaves it when it ends
    auto t = start4();
    mut(t, 2).conditions |= bit(Condition::Restrained);
    REQUIRE(grapple(t, 1, 2, rider(12, true, false), 0, ev));
    CHECK_FALSE(t.sourced[0].owns_restrained);
    mut(t, 1).conditions |= bit(Condition::Stunned);
    prune_sourced(t, ev);
    CHECK_FALSE(mut(t, 2).has(Condition::Grappled));
    CHECK(mut(t, 2).has(Condition::Restrained));
    // two restraining grapplers: the first to let go hands the restraint to the other
    auto u = begin_encounter({strong(1), foe(2), strong(5)}, 1, nullptr, spell_hooks());
    mut(u, 5).team = 0;
    REQUIRE(grapple(u, 1, 2, rider(12, true, false), 0, ev));
    REQUIRE(grapple(u, 5, 2, rider(12, true, false), 0, ev));
    mut(u, 1).conditions |= bit(Condition::Stunned);
    prune_sourced(u, ev);
    CHECK(mut(u, 2).has(Condition::Restrained));
    REQUIRE(u.sourced.size() == 1);
    CHECK(u.sourced[0].owns_restrained);
    mut(u, 5).conditions |= bit(Condition::Stunned);
    prune_sourced(u, ev);
    CHECK_FALSE(mut(u, 2).has(Condition::Restrained));
}

TEST_CASE("grapple rider: max_grapples per attack (giant crab claws); past the limit nothing is grappled") {
    auto s = start4();
    std::vector<Event> ev;
    const GrappleRider claw = rider(11, false, false, 2);   // two claws, each grapples one target
    REQUIRE(grapple(s, 1, 2, claw, 0, ev));
    REQUIRE(grapple(s, 1, 3, claw, 0, ev));
    const std::size_t before = ev.size();
    CHECK_FALSE(grapple(s, 1, 4, claw, 0, ev));
    CHECK(ev.size() == before);
    CHECK_FALSE(mut(s, 4).has(Condition::Grappled));
    CHECK(s.sourced.size() == 2);
    CHECK(grapple(s, 1, 2, claw, 0, ev));   // re-grappling one already held is not a third
    CHECK(s.sourced.size() == 2);
    CHECK_FALSE(attack_locked(s, 1, 0, 4));   // not exclusive: the claw still attacks others
    CHECK(grapple(s, 1, 4, claw, 1, ev));      // another attack has its own count
}

TEST_CASE("grapple rider: max_size skips a larger creature and is not an extended field") {
    GrappleRider g = rider(14, false, false);
    g.max_size = int8_t(Size::Large);   // Chuul pincer: Large or smaller
    CHECK_FALSE(g.extended());
    std::vector<Event> ev;
    auto huge = start(strong(1), foe(2, Size::Huge));
    mut(huge, 1).size = Size::Large;   // Huge is one size up, so size_within_one would allow it
    CHECK_FALSE(grapple(huge, 1, 2, g, 0, ev));
    CHECK(ev.empty());
    CHECK_FALSE(mut(huge, 2).has(Condition::Grappled));
    auto fit = start(strong(1), foe(2, Size::Large));
    mut(fit, 1).size = Size::Large;
    REQUIRE(grapple(fit, 1, 2, g, 0, ev));
    CHECK(mut(fit, 2).has(Condition::Grappled));
    CHECK(fit.sourced[0].via_attack == 0xFF);   // size-only: the 0xDB fold stays off
    auto med = start(strong(1), foe(2, Size::Medium));
    REQUIRE(grapple(med, 1, 2, g, 0, ev));
    CHECK(mut(med, 2).has(Condition::Grappled));
    // the cap still composes with restrains: a Large target (over Medium) gains neither condition
    GrappleRider held = rider(14, true, false);
    held.max_size = int8_t(Size::Medium);   // Otyugh tentacle
    auto oty = start(strong(1), foe(2, Size::Large));
    mut(oty, 1).size = Size::Large;
    CHECK_FALSE(grapple(oty, 1, 2, held, 0, ev));
    CHECK_FALSE(mut(oty, 2).has(Condition::Grappled));
    CHECK_FALSE(mut(oty, 2).has(Condition::Restrained));
}

TEST_CASE("save_effect grappled_by_self: only creatures the user is grappling, before the action is spent") {
    auto book = srd_spellbook();
    SpellDef tent;
    tent.content_id = "test.tentacles";
    tent.id = spell_id_of(tent.content_id);
    tent.kind = SpellEffectKind::Save;
    tent.is_spell = false;
    tent.magical = false;
    tent.fixed_dc = 30;
    tent.save_ability = Ability::Con;
    tent.applies_condition = true;
    tent.condition = Condition::Poisoned;
    tent.repeat_save = RepeatSave::EndOfTurn;
    tent.expiry = uint8_t(Expiry::Rounds);
    tent.duration_rounds = 10;
    tent.target_requires_any = kTargetGateGrappledBySelf;
    tent.targets = 2;   // Tentacle Slam: one cast may name each grappled creature
    tent.range_ft = 30;
    book->add(tent);
    auto hooks = spell_hooks();
    hooks.spells = book;
    Combatant user = strong(1);
    user.spells_known.push_back(tent.id);
    auto s = begin_encounter({user, foe(2), foe(3)}, 1, nullptr, hooks);
    end_turns_until(s, 1);
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, 13, ev));
    bool saw_held = false, saw_free = false;
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::CastSpell && r.spell_id == tent.id) {
            saw_held = saw_held || r.target == 2;
            saw_free = saw_free || r.target == 3;
        }
    CHECK(saw_held);
    CHECK_FALSE(saw_free);
    ActionRequest bad{1, ActionKind::CastSpell, 3};
    bad.spell_id = tent.id;
    bad.targets = {3};
    const auto refused = step(s, bad);
    CHECK_FALSE(refused.ok);
    CHECK(std::string(refused.error).find("grappled by the user") != std::string::npos);
    CHECK(s.budget.action);
    ActionRequest mixed{1, ActionKind::CastSpell, 2};
    mixed.spell_id = tent.id;
    mixed.targets = {2, 3};   // one creature the user is not grappling refuses the whole cast
    CHECK_FALSE(step(s, mixed).ok);
    CHECK(s.budget.action);
    REQUIRE(grapple(s, 1, 3, 13, ev));
    ActionRequest both{1, ActionKind::CastSpell, 2};
    both.spell_id = tent.id;
    both.targets = {2, 3};
    const auto hit = step(s, both);
    REQUIRE(hit.ok);
    CHECK_FALSE(s.budget.action);
    CHECK(mut(s, 2).has(Condition::Poisoned));
    CHECK(mut(s, 3).has(Condition::Poisoned));
}
