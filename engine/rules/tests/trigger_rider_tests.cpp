// Row 9c: choice: ask riders through the HitRider reaction window (Divine Smite), spend (slot / pool),
// scale.per_slot_level, save_condition / apply_condition riders, FeatureTriggered outcome 2 (declined).
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSmite = spell_id_of("test.paladin#divine_smite#0");
const uint32_t kStun = spell_id_of("test.monk#stun#1");
const uint32_t kPoolId = spell_id_of("test.pool.ki");
const uint32_t kRiderSave = spell_id_of("test.rider#save");
const uint32_t kRiderApply = spell_id_of("test.rider#apply");

Predicate melee_pred() { Predicate p; p.kind = PredicateKind::Weapon; p.a = int16_t(WeaponIs::Melee); return p; }

Trigger smite(uint8_t choice) {
    Trigger t; t.id = kSmite; t.on = TriggerOn::Hit; t.choice = choice; t.when = {melee_pred()};
    RiderEffect sp; sp.kind = RiderKind::Spend; sp.slot = 10;
    RiderEffect dmg; dmg.kind = RiderKind::ExtraDamage; dmg.dice = DiceExpr{2, 8, 0}; dmg.type = DamageType::Radiant;
    dmg.slot_dice = DiceExpr{1, 8, 0}; dmg.slot_base = 1;
    t.effects = {sp, dmg};
    return t;
}
Combatant paladin(CombatantId id, std::vector<Trigger> trig, uint8_t l1 = 2, uint8_t l2 = 1, uint8_t l3 = 0) {
    auto c = pc(id, 50);
    c.attacks = {Attack{"sword", 40, {1, 4, 0}, DamageType::Piercing, 5, false, false}};
    c.spell_slots_max = {0, l1, l2, l3, 0, 0, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    c.triggers = std::move(trig);
    return c;
}
Combatant dummy(CombatantId id, bool immune_piercing = false) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = 10;
    m.reaction_policy.fill(ReactionPolicy::Never);
    if (immune_piercing) m.immunities = bit(DamageType::Piercing);
    return m;
}
EncounterHooks hooks_deciding(int pick, std::vector<std::vector<ReactionOption>>* seen = nullptr,
                              std::vector<SpellDef> extra = {}) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    for (auto& d : extra) book->add(d);
    h.spells = book;
    h.decide_reaction = [pick, seen](const EncounterState&, const ReactionWindow& w, const Combatant&,
                                     std::span<const ReactionOption> opts) {
        if (w.trigger == ReactionTrigger::HitRider && seen) seen->emplace_back(opts.begin(), opts.end());
        if (w.trigger != ReactionTrigger::HitRider || pick < 0) return ReactionDecision{w.seq, false, 0};
        return ReactionDecision{w.seq, true, uint8_t(pick)};
    };
    return h;
}
ActionRequest stab(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
int idx_of(const std::vector<Event>& ev, EventKind k, int from = 0) {
    for (int i = from; i < int(ev.size()); ++i) if (ev[std::size_t(i)].kind == k) return i;
    return -1;
}
bool landed(const StepResult& r) {
    const int i = idx_of(r.events, EventKind::AttackResolved);
    return i >= 0 && (r.events[std::size_t(i)].c & 1);
}
bool crit_of(const StepResult& r) {
    const int i = idx_of(r.events, EventKind::AttackResolved);
    return i >= 0 && (r.events[std::size_t(i)].c & 2);
}
int dealt(const std::vector<Event>& ev) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt) n += e.a; return n;
}
int outcome(const std::vector<Event>& ev, uint32_t id) {
    for (const auto& e : ev) if (e.kind == EventKind::FeatureTriggered && e.a == int32_t(id)) return e.c;
    return -1;
}
SpellDef rider_save(int dc) {
    SpellDef d; d.id = kRiderSave; d.content_id = "test.rider#save"; d.level = 0; d.range_ft = 5;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Str; d.fixed_dc = int8_t(dc); d.on_save = OnSave::None;
    d.applies_condition = true; d.condition = Condition::Stunned; d.is_spell = false; d.magical = false;
    d.expiry = 1; d.duration_rounds = 1;
    return d;
}
SpellDef rider_apply() {
    SpellDef d; d.id = kRiderApply; d.content_id = "test.rider#apply"; d.level = 0; d.range_ft = 5;
    d.kind = SpellEffectKind::ApplyCondition; d.condition = Condition::Prone; d.is_spell = false; d.magical = false;
    d.expiry = 1; d.duration_rounds = 1;
    return d;
}
}  // namespace

TEST_CASE("Divine Smite: ask opens a free HitRider window with one option per slot level; spend + scale") {
    int taken = 0;
    for (int pick = 0; pick < 2; ++pick) {
        for (uint64_t seed = 0; seed < 30; ++seed) {
            std::vector<std::vector<ReactionOption>> seen;
            auto s = begin_encounter({paladin(1, {smite(1)}), dummy(2, true)}, seed, nullptr, hooks_deciding(pick, &seen));
            end_turns_until(s, 1);
            auto r = step(s, stab(1, 2));
            REQUIRE(r.ok);
            if (!landed(r)) { CHECK(seen.empty()); continue; }
            REQUIRE(seen.size() == 1);
            REQUIRE(seen[0].size() == 2);                          // slots 1 and 2 (no 3rd-level slot)
            for (const auto& o : seen[0]) {
                CHECK(o.kind == ReactionOptionKind::Rider);
                CHECK(o.option_id == kSmite);
                CHECK_FALSE(o.uses_reaction);
            }
            CHECK(seen[0][0].slot_level == 1); CHECK(seen[0][1].slot_level == 2);
            const int opened = idx_of(r.events, EventKind::ReactionWindowOpened);
            const int took = idx_of(r.events, EventKind::ReactionTaken);
            const int ft = idx_of(r.events, EventKind::FeatureTriggered);
            const int dd = idx_of(r.events, EventKind::DamageDealt);
            CHECK(opened >= 0); CHECK(opened < took); CHECK(took < ft); CHECK(ft < dd);
            CHECK(r.events[std::size_t(opened)].a == int32_t(ReactionTrigger::HitRider));
            CHECK(r.events[std::size_t(took)].actor == 1);
            CHECK(r.events[std::size_t(ft)].c == 1);
            CHECK_FALSE(s.combatants[0].reaction_used);            // free
            const int level = pick + 1;
            CHECK(s.combatants[0].spell_slots[std::size_t(level)] == (level == 1 ? 1 : 0));
            const int mult = crit_of(r) ? 2 : 1;
            const int dice = (2 + (level - 1)) * mult;             // 2d8 + 1d8 per level above 1st; piercing is immune
            CHECK(dealt(r.events) >= dice);
            CHECK(dealt(r.events) <= dice * 8);
            ++taken;
        }
    }
    CHECK(taken > 0);
}

TEST_CASE("Divine Smite: declining costs nothing and emits outcome 2") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = begin_encounter({paladin(1, {smite(1)}), dummy(2)}, seed, nullptr, hooks_deciding(-1));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (!landed(r)) continue;
        CHECK(outcome(r.events, kSmite) == 2);
        CHECK(idx_of(r.events, EventKind::ReactionDeclined) >= 0);
        CHECK(s.combatants[0].spell_slots[1] == 2);
        CHECK(dealt(r.events) <= 8);                                // 1d4 (2d4 on a crit): no radiant dice
    }
}

TEST_CASE("Divine Smite: Auto policy takes option 0 (lowest slot); Never policy and no slots open no window") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto p = paladin(1, {smite(1)});
        p.reaction_policy[std::size_t(ReactionTrigger::HitRider)] = ReactionPolicy::Auto;
        auto s = begin_encounter({p, dummy(2)}, seed, nullptr, spell_hooks());   // no decide_reaction: Auto never pauses
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        REQUIRE(r.status == StepStatus::Done);
        if (landed(r)) {
            CHECK(outcome(r.events, kSmite) == 1);
            CHECK(s.combatants[0].spell_slots[1] == 1);
            CHECK(s.combatants[0].spell_slots[2] == 1);
        }
        auto n = paladin(1, {smite(1)});
        n.reaction_policy[std::size_t(ReactionTrigger::HitRider)] = ReactionPolicy::Never;
        auto s2 = begin_encounter({n, dummy(2)}, seed, nullptr, hooks_deciding(0));
        end_turns_until(s2, 1);
        auto r2 = step(s2, stab(1, 2));
        CHECK(idx_of(r2.events, EventKind::ReactionWindowOpened) < 0);
        CHECK(outcome(r2.events, kSmite) == -1);
        auto e = paladin(1, {smite(1)}, 0, 0, 0);
        auto s3 = begin_encounter({e, dummy(2)}, seed, nullptr, hooks_deciding(0));
        end_turns_until(s3, 1);
        auto r3 = step(s3, stab(1, 2));
        CHECK(idx_of(r3.events, EventKind::ReactionWindowOpened) < 0);   // cannot pay
        CHECK(outcome(r3.events, kSmite) == -1);
    }
}

TEST_CASE("Divine Smite: default Ask without a decider pauses with a Rider option; answering resumes") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = begin_encounter({paladin(1, {smite(1)}), dummy(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        REQUIRE(r.ok);
        if (r.status != StepStatus::NeedsDecision) continue;       // missed
        REQUIRE(r.needs);
        CHECK(r.needs->trigger == ReactionTrigger::HitRider);
        CHECK(r.needs->reactor == 1);
        REQUIRE(r.needs->options.size() == 2);
        CHECK(r.needs->options[0].kind == ReactionOptionKind::Rider);
        auto done = step(s, decision_request(*r.needs, true, 1));
        REQUIRE(done.ok);
        CHECK(done.status == StepStatus::Done);
        CHECK(s.combatants[0].spell_slots[2] == 0);
        CHECK(outcome(done.events, kSmite) == 1);
        return;
    }
    FAIL("no hit in 30 seeds");
}

TEST_CASE("ask triggers whose predicates fail open no window; content-free attackers never see HitRider") {
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto p = paladin(1, {smite(1)});
        p.attacks[0].ranged = true;                                // weapon: melee fails
        std::vector<std::vector<ReactionOption>> seen;
        auto s = begin_encounter({p, dummy(2)}, seed, nullptr, hooks_deciding(0, &seen));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        CHECK(seen.empty());
        CHECK(idx_of(r.events, EventKind::ReactionWindowOpened) < 0);
        auto q = paladin(1, {});
        auto s2 = begin_encounter({q, dummy(2)}, seed, nullptr, hooks_deciding(0, &seen));
        end_turns_until(s2, 1);
        auto r2 = step(s2, stab(1, 2));
        CHECK(seen.empty());
        CHECK(r2.status == StepStatus::Done);
    }
}

TEST_CASE("pool spend: an auto trigger pays its cost (ResourceSpent) and stops when the pool is dry") {
    Trigger t; t.id = kStun; t.on = TriggerOn::Hit;
    RiderEffect sp; sp.kind = RiderKind::Spend; sp.pool = kPoolId; sp.value = 2;
    RiderEffect dmg; dmg.kind = RiderKind::ExtraDamage; dmg.dice = DiceExpr{1, 6, 0}; dmg.type = DamageType::Force;
    t.effects = {sp, dmg};
    int hits = 0;
    for (uint64_t seed = 0; seed < 40 && hits < 3; ++seed) {
        auto s = begin_encounter({paladin(1, {t}), dummy(2)}, seed, nullptr, spell_hooks());
        register_pool(s, 1, kPoolId, 3, 3);
        end_turns_until(s, 1);
        s.budget.attacks_remaining = 2;
        auto r1 = step(s, stab(1, 2));
        if (!landed(r1)) continue;
        ++hits;
        CHECK(outcome(r1.events, kStun) == 1);
        const int rs = idx_of(r1.events, EventKind::ResourceSpent);
        REQUIRE(rs >= 0);
        CHECK(r1.events[std::size_t(rs)].a == int32_t(kPoolId));
        CHECK(r1.events[std::size_t(rs)].b == 2);
        CHECK(r1.events[std::size_t(rs)].c == 1);
        CHECK(rs < idx_of(r1.events, EventKind::FeatureTriggered));
        auto r2 = step(s, stab(1, 2));                              // 1 left < cost 2
        CHECK(outcome(r2.events, kStun) == -1);
        CHECK(find_pool(s, 1, kPoolId)->cur == 1);
    }
    CHECK(hits > 0);
}

TEST_CASE("save_condition / apply_condition riders resolve after the damage and not on a dead target") {
    auto save_rider = [&](bool ask) {
        Trigger t; t.id = kStun; t.on = TriggerOn::Hit; t.choice = ask ? 1 : 0;
        RiderEffect e; e.kind = RiderKind::SaveCondition; e.spell_id = kRiderSave;
        t.effects = {e};
        return t;
    };
    int checked = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        // DC 30: the save always fails -> Stunned; DC 1: always passes -> nothing.
        auto s = begin_encounter({paladin(1, {save_rider(false)}), dummy(2)}, seed, nullptr, spell_hooks());
        auto h = hooks_deciding(0, nullptr, {rider_save(30)});
        s = begin_encounter({paladin(1, {save_rider(false)}), dummy(2)}, seed, nullptr, h);
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (!landed(r)) continue;
        ++checked;
        CHECK(outcome(r.events, kStun) == 1);
        const int dd = idx_of(r.events, EventKind::DamageDealt);
        const int sv = idx_of(r.events, EventKind::SavingThrowRolled);
        const int ca = idx_of(r.events, EventKind::ConditionAdded);
        CHECK(dd >= 0); CHECK(sv > dd); CHECK(ca > sv);
        CHECK(s.combatants[1].has(Condition::Stunned));
        auto s2 = begin_encounter({paladin(1, {save_rider(false)}), dummy(2)}, seed, nullptr, hooks_deciding(0, nullptr, {rider_save(1)}));
        end_turns_until(s2, 1);
        auto r2 = step(s2, stab(1, 2));
        CHECK(idx_of(r2.events, EventKind::SavingThrowRolled) >= 0);
        CHECK_FALSE(s2.combatants[1].has(Condition::Stunned));
        // A kill: no save is rolled against the dead.
        auto s3 = begin_encounter({paladin(1, {save_rider(false)}), dummy(2)}, seed, nullptr, hooks_deciding(0, nullptr, {rider_save(30)}));
        end_turns_until(s3, 1);
        s3.combatants[1].hp = 1;
        auto r3 = step(s3, stab(1, 2));
        CHECK(idx_of(r3.events, EventKind::SavingThrowRolled) < 0);
    }
    CHECK(checked > 0);
}

TEST_CASE("apply_condition rider (Pounce prone): applied on a hit, with a declined ask nothing happens") {
    auto mk = [&](uint8_t choice) {
        Trigger t; t.id = kStun; t.on = TriggerOn::Hit; t.choice = choice;
        RiderEffect e; e.kind = RiderKind::ApplyCondition; e.spell_id = kRiderApply;
        t.effects = {e};
        return t;
    };
    int checked = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = begin_encounter({paladin(1, {mk(0)}), dummy(2)}, seed, nullptr, hooks_deciding(0, nullptr, {rider_apply()}));
        end_turns_until(s, 1);
        auto r = step(s, stab(1, 2));
        if (!landed(r)) continue;
        ++checked;
        CHECK(s.combatants[1].has(Condition::Prone));
        CHECK(idx_of(r.events, EventKind::DamageDealt) < idx_of(r.events, EventKind::ConditionAdded));
        auto s2 = begin_encounter({paladin(1, {mk(1)}), dummy(2)}, seed, nullptr, hooks_deciding(-1, nullptr, {rider_apply()}));
        end_turns_until(s2, 1);
        auto r2 = step(s2, stab(1, 2));
        CHECK(outcome(r2.events, kStun) == 2);
        CHECK_FALSE(s2.combatants[1].has(Condition::Prone));
    }
    CHECK(checked > 0);
    // Unknown spell id (or a def of the wrong kind): the trigger does not fire.
    auto s3 = begin_encounter({paladin(1, {mk(0)}), dummy(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s3, 1);
    CHECK(outcome(step(s3, stab(1, 2)).events, kStun) == -1);
}
