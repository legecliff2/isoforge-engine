// C-T TP1: tarrasque Swallow rules (contract v1.0 tests that need no map).
// The sim exit move, regurgitation placement and carry are TP2.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSwallow = spell_id_of("test.tarrasque#swallow");
const uint32_t kToad = spell_id_of("test.toad#swallow");
const uint32_t kBurst = spell_id_of("test.burst");

SpellDef swallow_spell(bool unlimited = true, bool all = true, uint16_t exit_ft = 30, bool ends_grapple = true) {
    SpellDef d;
    d.id = kSwallow; d.content_id = "test.tarrasque#swallow"; d.level = 0;
    d.kind = SpellEffectKind::ApplyCondition; d.is_spell = false; d.magical = false; d.components = 0;
    d.applies_condition = true; d.condition = Condition::Restrained; d.targets = 1;
    d.contain = 2; d.contain_dice = {16, 6, 0}; d.contain_damage_type = DamageType::Acid;
    d.contain_unlimited = unlimited; d.regurgitate_dc = all || exit_ft ? 20 : 0; d.regurgitate_damage = 60;
    d.regurgitate_all = all; d.contain_exit_ft = exit_ft; d.contain_ends_grapple = ends_grapple;
    return d;
}
SpellDef toad_spell() {
    SpellDef d = swallow_spell(false, false, 0, false);
    d.id = kToad; d.content_id = "test.toad#swallow";
    d.regurgitate_dc = 0; d.contain_dice = {3, 6, 0};
    return d;
}
SpellDef burst_spell() {
    SpellDef d;
    d.id = kBurst; d.content_id = "test.burst"; d.level = 0; d.is_spell = false; d.magical = false; d.components = 0;
    d.kind = SpellEffectKind::Save; d.area = AreaShape::Sphere; d.area_size_ft = 20;
    d.save_ability = Ability::Dex; d.fixed_dc = 30; d.on_save = OnSave::None;
    d.dice = {2, 6, 0}; d.damage_type = DamageType::Fire; d.targets = 4;
    return d;
}
EncounterHooks hooks_for(std::initializer_list<SpellDef> extra) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    for (const auto& d : extra) REQUIRE(b->add(d));
    h.spells = b;
    return h;
}
void silence(Combatant& c) {
    for (auto& p : c.reaction_policy) p = ReactionPolicy::Never;
}
Attack bite_row() { return Attack{"bite", 30, {4, 12, 10}, DamageType::Piercing, 10, false, false}; }
Attack swallow_row() {
    Attack a = bite_row();
    a.id = "swallow";
    a.target_requires_any = kTargetGateGrappledBySelf;
    a.target_max_size = int8_t(Size::Large);
    return a;
}
Trigger swallow_on_hit() {
    Trigger t; t.on = TriggerOn::Hit; t.id = kSwallow;
    Predicate p; p.kind = PredicateKind::AttackIs; p.a = 1;
    t.when.push_back(p);
    RiderEffect e; e.kind = RiderKind::ApplyCondition; e.spell_id = kSwallow;
    t.effects.push_back(e);
    return t;
}
Combatant tarrasque() {
    auto c = monster(1);
    silence(c);
    c.team = 0; c.hp = c.hp_max = 400; c.ac = 25; c.speed_ft = 40; c.size = Size::Gargantuan;
    c.attacks_per_action = 2;
    c.abilities.score[std::size_t(Ability::Con)] = 10;
    c.attacks = {bite_row(), swallow_row(), Attack{"claw", 30, {1, 8, 0}, DamageType::Slashing, 10, false, false}};
    c.triggers = {swallow_on_hit()};
    c.spells_known.push_back(kBurst);
    return c;
}
Combatant foe(CombatantId id, Size sz = Size::Medium, int hp = 400) {
    auto c = monster(id);
    silence(c);
    c.team = 1; c.hp = c.hp_max = int16_t(hp); c.ac = 10; c.size = sz; c.speed_ft = 30;
    c.uses_death_saves = false;
    return c;
}
Combatant ally() {
    auto c = monster(9);
    silence(c);
    c.team = 0; c.hp = c.hp_max = 40; c.ac = 10;
    return c;
}
void hold(EncounterState& s, CombatantId who) {
    SourcedCondition g;
    g.condition = Condition::Grappled; g.source = 1; g.target = who;
    g.via_attack = 0; g.exclusive = true; g.restrains = true; g.owns_restrained = true; g.escape_dc = 20;
    s.sourced.push_back(g);
    add_condition(mut(s, who), Condition::Grappled);
    if (!mut(s, who).has(Condition::Restrained)) add_condition(mut(s, who), Condition::Restrained);
}
EncounterState open(std::vector<Combatant> cs, uint64_t seed, const EncounterHooks& h) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, h);
    end_turns_until(s, 1);
    return s;
}
ActionRequest swing(CombatantId target, uint8_t index, uint8_t multi = 0) {
    ActionRequest r{1, ActionKind::Attack, target, index};
    r.multiattack = multi; r.target_within_5ft = true;
    return r;
}
const Event* attack_event(const std::vector<Event>& ev) {
    for (const auto& e : ev) if (e.kind == EventKind::AttackResolved) return &e;
    return nullptr;
}
int damage_of(const std::vector<Event>& ev, CombatantId target) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == target) n += e.a;
    return n;
}
bool swallowed(const EncounterState& s, CombatantId id) { return container_of(s, id) == 1; }
const ActiveSpellEffect* swallow_effect(const EncounterState& s, CombatantId id) {
    for (const auto& e : s.effects) if (e.contain == 2 && e.target == id) return &e;
    return nullptr;
}
bool lists_swallow(const EncounterState& s, CombatantId target) {
    for (const auto& q : legal_actions(s))
        if (q.kind == ActionKind::Attack && q.attack_index == 1 && q.target == target) return true;
    return false;
}

}  // namespace

TEST_CASE("T1-T2: swallow is refused when the target is not grappled or is too large, and nothing is spent") {
    auto h = hooks_for({swallow_spell(), burst_spell()});
    auto s = open({tarrasque(), foe(2), foe(3, Size::Huge), ally()}, 1, h);
    REQUIRE(s.budget.action);
    auto miss_gate = step(s, swing(2, 1));
    CHECK_FALSE(miss_gate.ok);
    CHECK(std::string(miss_gate.error) == "target must be grappled by the user");
    CHECK(s.budget.action);
    CHECK_FALSE(lists_swallow(s, 2));
    hold(s, 3);
    auto too_big = step(s, swing(3, 1));
    CHECK_FALSE(too_big.ok);
    CHECK(std::string(too_big.error) == "target is too large");
    CHECK(s.budget.action);
    CHECK_FALSE(lists_swallow(s, 3));
    CHECK_FALSE(swallowed(s, 2));
    CHECK_FALSE(swallowed(s, 3));
}

TEST_CASE("T1-T3: a hit deals the bite, swallows, and ends the grapple; a crit doubles dice only; a miss does nothing") {
    auto h = hooks_for({swallow_spell(), burst_spell()});
    int hits = 0, crits = 0;
    for (uint64_t seed = 1; seed < 80 && (hits == 0 || crits == 0); ++seed) {
        auto s = open({tarrasque(), foe(2), ally()}, seed, h);
        hold(s, 2);
        auto r = step(s, swing(2, 1));
        REQUIRE(r.ok);
        const Event* at = attack_event(r.events);
        REQUIRE(at);
        const bool hit = (at->c & 1) != 0, crit = (at->c & 2) != 0;
        if (!hit) continue;
        const int dealt = damage_of(r.events, 2);
        CHECK(dealt >= (crit ? 18 : 14));
        CHECK(dealt <= (crit ? 106 : 58));
        if (crit) { ++crits; CHECK(dealt <= 106); }
        else ++hits;
        REQUIRE(swallowed(s, 2));
        CHECK(mut(s, 2).has(Condition::Blinded));
        CHECK(mut(s, 2).has(Condition::Restrained));
        CHECK_FALSE(mut(s, 2).has(Condition::Grappled));
        CHECK(condition_source(s, 2, Condition::Grappled) == 0);
        CHECK_FALSE(attack_locked(s, 1, 0, 4));
        const int dealt_at = -1;
        int dmg_i = -1, blind_i = -1;
        for (int i = 0; i < int(r.events.size()); ++i) {
            if (r.events[i].kind == EventKind::DamageDealt && r.events[i].target == 2) dmg_i = i;
            if (r.events[i].kind == EventKind::ConditionAdded && r.events[i].a == int(Condition::Blinded)) blind_i = i;
        }
        CHECK(dealt_at == -1);
        CHECK(dmg_i >= 0);
        CHECK(blind_i > dmg_i);
        (void)dealt_at;
    }
    CHECK(hits > 0);
    CHECK(crits > 0);
    bool missed = false;
    for (uint64_t seed = 1; seed < 40 && !missed; ++seed) {
        auto s = open({tarrasque(), foe(2), ally()}, seed, h);
        mut(s, 1).attacks[1].to_hit = 0;
        mut(s, 2).ac = 30;
        hold(s, 2);
        auto r = step(s, swing(2, 1));
        REQUIRE(r.ok);
        const Event* at = attack_event(r.events);
        REQUIRE(at);
        if ((at->c & 1) != 0) continue;
        missed = true;
        CHECK_FALSE(swallowed(s, 2));
        CHECK(mut(s, 2).has(Condition::Grappled));
        CHECK(attack_locked(s, 1, 0, 3));
    }
    CHECK(missed);
}

TEST_CASE("T3: a target the bite kills is not swallowed") {
    auto h = hooks_for({swallow_spell()});
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = open({tarrasque(), foe(2, Size::Medium, 1), ally()}, seed, h);
        hold(s, 2);
        auto r = step(s, swing(2, 1));
        REQUIRE(r.ok);
        const Event* at = attack_event(r.events);
        REQUIRE(at);
        if ((at->c & 1) == 0) continue;
        CHECK(mut(s, 2).dead);
        CHECK_FALSE(swallowed(s, 2));
        CHECK(mut(s, 2).has(Condition::Grappled));
        return;
    }
    FAIL("no killing hit");
}

TEST_CASE("T4: restrained immunity still swallows; an existing blindness outlives the swallow") {
    auto h = hooks_for({swallow_spell(true, true, 0, true)});
    bool immune_hit = false;
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto immune = foe(2);
        immune.condition_immunities = bit(Condition::Restrained);
        auto s = open({tarrasque(), immune, ally()}, seed, h);
        hold(s, 2);
        mut(s, 2).condition_immunities = bit(Condition::Restrained);
        remove_condition(mut(s, 2), Condition::Restrained);
        auto r = step(s, swing(2, 1));
        REQUIRE(r.ok);
        if (!attack_event(r.events) || (attack_event(r.events)->c & 1) == 0) continue;
        REQUIRE(swallowed(s, 2));
        CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
        CHECK(mut(s, 2).has(Condition::Blinded));
        end_turns_until(s, 9);
        auto outside = step(s, ActionRequest{9, ActionKind::Attack, 2, 0});
        CHECK(std::string(outside.error) == "target has total cover");
        end_turns_until(s, 1);
        const auto* e = swallow_effect(s, 2);
        REQUIRE(e);
        CHECK(e->no_condition);
        immune_hit = true;
        break;
    }
    REQUIRE(immune_hit);
    auto already = foe(2);
    add_condition(already, Condition::Blinded);
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = open({tarrasque(), already, ally()}, seed, h);
        hold(s, 2);
        auto r = step(s, swing(2, 1));
        REQUIRE(r.ok);
        if (!attack_event(r.events) || (attack_event(r.events)->c & 1) == 0) continue;
        REQUIRE(swallow_effect(s, 2));
        CHECK_FALSE(swallow_effect(s, 2)->has_also);
        mut(s, 1).hp = 0; mut(s, 1).dead = true;
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
        CHECK_FALSE(swallowed(s, 2));
        CHECK(mut(s, 2).has(Condition::Blinded));
        return;
    }
    FAIL("no hit on an already blinded target");
}

TEST_CASE("T5: two creatures are swallowed, each takes its own acid, outsiders cannot reach them, the swallower can") {
    auto h = hooks_for({swallow_spell(), burst_spell()});
    EncounterState s = open({tarrasque(), foe(2), foe(3), ally()}, 1, h);
    bool both = false;
    for (uint64_t seed = 1; seed < 60 && !both; ++seed) {
        s = open({tarrasque(), foe(2), foe(3), ally()}, seed, h);
        hold(s, 2);
        auto a = step(s, swing(2, 1));
        REQUIRE(a.ok);
        if (!swallowed(s, 2)) continue;
        hold(s, 3);
        auto b = step(s, swing(3, 1));
        REQUIRE(b.ok);
        if (!swallowed(s, 3)) continue;
        both = true;
    }
    REQUIRE(both);
    end_turns_until(s, 9);
    auto outside = step(s, ActionRequest{9, ActionKind::Attack, 2, 0});
    CHECK(std::string(outside.error) == "target has total cover");
    mut(s, 9).spells_known.push_back(kBurst);
    const int16_t hp2 = mut(s, 2).hp, hp3 = mut(s, 3).hp;
    auto area = cast(9, kBurst, 2, 0); area.targets = {2, 3};
    REQUIRE(step(s, area).ok);
    CHECK(mut(s, 2).hp == hp2);
    CHECK(mut(s, 3).hp == hp3);
    auto inner = cast(1, kBurst, 2, 0); inner.targets = {2, 3};
    end_turns_until(s, 1);
    const int16_t before2 = mut(s, 2).hp, before3 = mut(s, 3).hp;
    REQUIRE(step(s, inner).ok);
    CHECK(mut(s, 2).hp < before2);
    CHECK(mut(s, 3).hp < before3);
    const int16_t acid2 = mut(s, 2).hp, acid3 = mut(s, 3).hp;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(mut(s, 2).hp < acid2);
    CHECK(mut(s, 3).hp < acid3);
}

TEST_CASE("T6: one creature's 60 regurgitates all; 35 each does not; the counter resets; the save is not magical") {
    auto h = hooks_for({swallow_spell()});
    EncounterState s = open({tarrasque(), foe(2), foe(3), ally()}, 1, h);
    bool both = false;
    for (uint64_t seed = 1; seed < 60 && !both; ++seed) {
        s = open({tarrasque(), foe(2), foe(3), ally()}, seed, h);
        hold(s, 2);
        if (!step(s, swing(2, 1)).ok || !swallowed(s, 2)) continue;
        hold(s, 3);
        if (!step(s, swing(3, 1)).ok || !swallowed(s, 3)) continue;
        both = true;
    }
    REQUIRE(both);
    end_turns_until(s, 2);
    for (auto& e : s.effects) if (e.contain == 2) e.container_damage = 35;
    auto quiet = step(s, {2, ActionKind::EndTurn});
    REQUIRE(quiet.ok);
    CHECK(count(quiet.events, EventKind::SavingThrowRolled) == 0);
    CHECK(swallowed(s, 2));
    CHECK(swallowed(s, 3));
    for (const auto& e : s.effects) if (e.contain == 2) CHECK(e.container_damage == 0);
    end_turns_until(s, 2);
    for (auto& e : s.effects) if (e.contain == 2 && e.target == 2) { e.container_damage = 60; e.regurgitate_dc = 30; }
    RollTrait mr; mr.roll = TraitRoll::Save; mr.mode = RollMode::Advantage; mr.when = kWhenSourceMagical;
    mr.save_abilities = uint8_t(bit(Ability::Con));
    mut(s, 1).roll_traits.push_back(mr);
    auto failed = step(s, {2, ActionKind::EndTurn});
    REQUIRE(failed.ok);
    const Event* save = nullptr;
    for (const auto& e : failed.events) if (e.kind == EventKind::SavingThrowRolled && e.actor == 1) save = &e;
    REQUIRE(save);
    CHECK(save->c == 0);
    CHECK_FALSE(swallowed(s, 2));
    CHECK_FALSE(swallowed(s, 3));
    CHECK(mut(s, 2).has(Condition::Prone));
    CHECK(mut(s, 3).has(Condition::Prone));
    REQUIRE(s.contain_exits.size() == 2);
    CHECK(s.contain_exits[0].kind == 2);
    CHECK(s.contain_exits[1].kind == 2);
    CHECK(s.contain_exits[0].creature != s.contain_exits[1].creature);
}

TEST_CASE("T8: the corpse drops only restrained; a short move is refused; the exit spends exit_ft") {
    auto h = hooks_for({swallow_spell()});
    EncounterState s = open({tarrasque(), foe(2), ally()}, 1, h);
    bool in = false;
    for (uint64_t seed = 1; seed < 40 && !in; ++seed) {
        s = open({tarrasque(), foe(2), ally()}, seed, h);
        hold(s, 2);
        if (!step(s, swing(2, 1)).ok || !swallowed(s, 2)) continue;
        in = true;
    }
    REQUIRE(in);
    const int16_t hp = mut(s, 2).hp;
    mut(s, 1).hp = 0; mut(s, 1).dead = true;
    REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    REQUIRE(swallow_effect(s, 2));
    CHECK(swallow_effect(s, 2)->no_condition);
    CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
    CHECK(mut(s, 2).has(Condition::Blinded));
    CHECK_FALSE(mut(s, 2).has(Condition::Prone));
    CHECK(s.contain_exits.empty());
    end_turns_until(s, 9);
    auto covered = step(s, ActionRequest{9, ActionKind::Attack, 2, 0});
    CHECK(std::string(covered.error) == "target has total cover");
    mut(s, 2).speed_ft = 30;
    end_turns_until(s, 2);
    CHECK(mut(s, 2).hp == hp);
    auto ordinary = step(s, ActionRequest{2, ActionKind::Move, 0, 0, 5});
    CHECK_FALSE(ordinary.ok);
    CHECK(std::string(ordinary.error) == "inside a corpse: exit with exit_ft of movement");
    CHECK(swallowed(s, 2));
    const int16_t budget = s.budget.movement_ft;
    auto exit = step(s, ActionRequest{2, ActionKind::Move, 0, 0, 30});
    REQUIRE(exit.ok);
    CHECK(s.budget.movement_ft == int16_t(budget - 30));
    CHECK_FALSE(swallowed(s, 2));
    CHECK_FALSE(mut(s, 2).has(Condition::Blinded));
    CHECK(mut(s, 2).has(Condition::Prone));
    REQUIRE(s.contain_exits.size() == 1);
    CHECK(s.contain_exits[0] == ContainExit{1, 2, 4});
}

TEST_CASE("T8: a speed of 25 needs a Dash before the 30 ft exit") {
    auto h = hooks_for({swallow_spell()});
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto slow = foe(2);
        slow.speed_ft = 25;
        auto s = open({tarrasque(), slow, ally()}, seed, h);
        hold(s, 2);
        if (!step(s, swing(2, 1)).ok || !swallowed(s, 2)) continue;
        mut(s, 1).hp = 0; mut(s, 1).dead = true;
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
        mut(s, 2).speed_ft = 25;
        end_turns_until(s, 2);
        auto denied = step(s, ActionRequest{2, ActionKind::Move, 0, 0, 30});
        CHECK_FALSE(denied.ok);
        CHECK(std::string(denied.error) == "not enough movement");
        CHECK(swallowed(s, 2));
        REQUIRE(step(s, {2, ActionKind::Dash}).ok);
        auto exit = step(s, ActionRequest{2, ActionKind::Move, 0, 0, 30});
        REQUIRE(exit.ok);
        CHECK_FALSE(swallowed(s, 2));
        CHECK(s.contain_exits.size() == 1);
        CHECK(s.contain_exits[0].kind == 4);
        return;
    }
    FAIL("no swallow for the slow exit");
}

TEST_CASE("T10: the bite slot accepts swallow, and so does the lead copy; another index is refused") {
    auto h = hooks_for({swallow_spell()});
    auto s = open({tarrasque(), foe(2), ally()}, 1, h);
    hold(s, 2);
    AttackSequence plain{"plain", {0}, {}};
    plain.alternates.push_back({0, 1});
    AttackSequence led = plain;
    led.id = "plain.lead";
    led.after_action = 99;
    mut(s, 1).multiattacks = {plain, led};
    auto bad = step(s, swing(2, 2, 1));
    CHECK_FALSE(bad.ok);
    CHECK(std::string(bad.error) == "attack out of multiattack sequence");
    CHECK(s.budget.action);
    auto ok = step(s, swing(2, 1, 1));
    REQUIRE(ok.ok);
    CHECK_FALSE(s.budget.action);
    auto s2 = open({tarrasque(), foe(2), ally()}, 3, h);
    hold(s2, 2);
    mut(s2, 1).multiattacks = {plain, led};
    s2.budget.action = false;
    s2.budget.sequence = 2;
    s2.budget.sequence_pos = 0;
    s2.budget.attacks_remaining = 1;
    auto led_bad = step(s2, swing(2, 2));
    CHECK_FALSE(led_bad.ok);
    CHECK(std::string(led_bad.error) == "attack out of multiattack sequence");
    auto led_ok = step(s2, swing(2, 1));
    REQUIRE(led_ok.ok);
}

TEST_CASE("T11: chomp costs 2, and chomp bite is attack_locked") {
    auto h = hooks_for({swallow_spell()});
    auto s = open({tarrasque(), foe(2), foe(3), ally()}, 1, h);
    LegendaryOption chomp_bite; chomp_bite.cost = 2; chomp_bite.use = LegendaryUse::Attack; chomp_bite.attack_index = 0;
    LegendaryOption chomp_sw = chomp_bite; chomp_sw.attack_index = 1;
    LegendaryOption stride; stride.cost = 1; stride.use = LegendaryUse::Move; stride.half_speed = true;
    mut(s, 1).legendary = {chomp_bite, chomp_sw, stride};
    mut(s, 1).la_max = 3; mut(s, 1).la_cur = 1;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    const CombatantId next = current_actor(s);
    REQUIRE(next != 1);
    REQUIRE(step(s, {next, ActionKind::EndTurn}).ok);
    REQUIRE(s.legendary_window.candidate == 1);
    ActionRequest bite{1, ActionKind::LegendaryAction, 2}; bite.feature_id = 1;
    auto poor = step(s, bite);
    CHECK_FALSE(poor.ok);
    CHECK(std::string(poor.error) == "not enough legendary actions");
    CHECK(mut(s, 1).la_cur == 1);
    mut(s, 1).la_cur = 3;
    hold(s, 2);
    ActionRequest other = bite; other.target = 3;
    auto locked = step(s, other);
    CHECK_FALSE(locked.ok);
    CHECK(std::string(locked.error) == "that attack holds a grappled creature: it can't attack another target");
    CHECK(mut(s, 1).la_cur == 3);
    bool listed = false;
    for (const auto& q : legal_actions(s))
        if (q.kind == ActionKind::LegendaryAction && q.feature_id == 1 && q.target == 3) listed = true;
    CHECK_FALSE(listed);
}

TEST_CASE("T2/T10a: a warded swallow cannot redirect onto a creature that fails the gate or the size cap") {
    auto h = hooks_for({swallow_spell()});
    auto s = open({tarrasque(), foe(2), foe(3), foe(4, Size::Huge), ally()}, 1, h);
    hold(s, 2);
    ActiveSpellEffect w{kSwallow, 9, 2, Condition::Warded, Ability::Wis, 40, RepeatSave::None, false};
    w.no_condition = false; w.dc = 40;
    s.effects.push_back(w);
    add_condition(mut(s, 2), Condition::Warded);
    auto alts = ward_alternate_targets(s, 1, 2, 0, 1);
    bool alt_ungrappled = false, alt_huge = false;
    for (CombatantId id : alts) {
        alt_ungrappled = alt_ungrappled || id == 3;
        alt_huge = alt_huge || id == 4;
    }
    CHECK_FALSE(alt_ungrappled);
    CHECK_FALSE(alt_huge);
    auto req = swing(2, 1);
    req.ward_alternate = 3;
    auto refused = step(s, req);
    CHECK_FALSE(refused.ok);
    CHECK(std::string(refused.error) == "ward_alternate: not a legal alternate target");
    CHECK(s.budget.action);
    CHECK_FALSE(swallowed(s, 2));
    auto lost = step(s, swing(2, 1));
    REQUIRE(lost.ok);
    CHECK_FALSE(s.budget.action);
    CHECK_FALSE(swallowed(s, 2));
    CHECK_FALSE(swallowed(s, 3));
}

TEST_CASE("T4/T11 toad: one at a time, no exit_ft, freed prone at death, immunity still swallows") {
    auto h = hooks_for({toad_spell()});
    Combatant toad = monster(1);
    silence(toad);
    toad.team = 0; toad.hp = toad.hp_max = 80; toad.attacks_per_action = 2; toad.attacks = {bite_row()};
    toad.attacks[0].to_hit = 30;
    toad.triggers.clear();
    Trigger t; t.on = TriggerOn::Hit;
    Predicate p; p.kind = PredicateKind::AttackIs; p.a = 0;
    t.when.push_back(p);
    RiderEffect e; e.kind = RiderKind::ApplyCondition; e.spell_id = kToad;
    t.effects.push_back(e);
    toad.triggers = {t};
    bool once = false;
    for (uint64_t seed = 1; seed < 40 && !once; ++seed) {
        auto s = open({toad, foe(2), foe(3), ally()}, seed, h);
        if (!step(s, swing(2, 0)).ok || !swallowed(s, 2)) continue;
        hold(s, 3);
        REQUIRE(step(s, swing(3, 0)).ok);
        CHECK_FALSE(swallowed(s, 3));
        CHECK(swallowed(s, 2));
        mut(s, 1).hp = 0; mut(s, 1).dead = true;
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
        CHECK_FALSE(swallowed(s, 2));
        CHECK(mut(s, 2).has(Condition::Prone));
        REQUIRE(s.contain_exits.size() == 1);
        CHECK(s.contain_exits[0].kind == 1);
        once = true;
    }
    REQUIRE(once);
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto immune = foe(2);
        immune.condition_immunities = bit(Condition::Restrained);
        auto s = open({toad, immune, ally()}, seed, h);
        auto r = step(s, swing(2, 0));
        REQUIRE(r.ok);
        if (!attack_event(r.events) || (attack_event(r.events)->c & 1) == 0) continue;
        REQUIRE(swallowed(s, 2));
        CHECK_FALSE(mut(s, 2).has(Condition::Restrained));
        CHECK(mut(s, 2).has(Condition::Blinded));
        return;
    }
    FAIL("immune toad swallow never hit");
}
