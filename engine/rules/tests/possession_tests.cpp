// Possession engine (design v0.1 D1-D8, D11-D13). No monster content.
// The ghost's initiative slot spends the body's action economy. The body's slot is bookkeeping.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kPoss = spell_id_of("test.possession");
const uint32_t kPounce = spell_id_of("test.possession.pounce");

Attack derived_sword() {
    Attack a{"longsword", 7, {1, 8, 3}, DamageType::Slashing, 5, false, false};
    a.pb_in_to_hit = 2;   // Str +3, PB +2, proficient
    a.ability_used = int8_t(Ability::Str);
    return a;
}
Attack printed_sword() {
    Attack a{"scimitar", 4, {1, 6, 2}, DamageType::Slashing, 5, false, false};
    a.pb_in_to_hit = -1;
    return a;
}

Combatant ghost_of(CombatantId id) {
    auto c = monster(id);
    c.team = 1;
    c.creature_type = CreatureType::Undead;
    c.abilities.score = {6, 14, 10, 16, 12, 15};   // Int 16, Wis 12, Cha 15
    c.proficiency_bonus = 2;
    c.attacks = {printed_sword()};
    c.reaction_policy.fill(ReactionPolicy::Never);
    return c;
}
Combatant host_of(CombatantId id) {
    auto c = pc(id, 30);
    c.team = 0;
    c.abilities.score = {16, 12, 14, 8, 10, 9};
    c.proficiency_bonus = 2;
    c.save_proficiencies = bit(Ability::Str) | bit(Ability::Con);
    c.skill_proficiencies = bit(Skill::Athletics);
    c.attacks = {derived_sword()};
    c.speed_ft = 30;
    c.reaction_policy.fill(ReactionPolicy::Never);
    return c;
}
Combatant ally_of(CombatantId id) {
    auto c = pc(id, 20);
    c.team = 0;
    c.is_pc = false;
    c.uses_death_saves = false;
    c.attacks = {derived_sword()};
    c.reaction_policy.fill(ReactionPolicy::Never);
    return c;
}

ModifierSpec marker(uint32_t id, ModStat st) {
    ModifierSpec m; m.id = id; m.source = id; m.stat = st; m.applies_to = ModAppliesTo::Target; return m;
}

void link(EncounterState& s, CombatantId ghost, CombatantId body) {
    std::vector<Event> ev;
    REQUIRE(begin_possession(s, ghost, body, kPoss, 24, 1, ev).empty());
    CHECK(std::any_of(ev.begin(), ev.end(), [&](const Event& e) {
        return e.kind == EventKind::Possessed && e.actor == ghost && e.target == body;
    }));
}

}  // namespace

TEST_CASE("turn_body: the ghost's slot spends the body's action; the body's slot is bookkeeping") {
    auto s = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 1, nullptr, spell_hooks());
    link(s, 2, 1);
    CHECK(mut(s, 1).abilities[Ability::Int] == 16);
    CHECK(mut(s, 1).save_proficiencies == 0);
    CHECK(mut(s, 1).skill_proficiencies == 0);
    CHECK((mut(s, 1).condition_immunities & bit(Condition::Charmed)) != 0);
    CHECK(mut(s, 1).possessed_by == 2);
    CHECK(mut(s, 2).possessing == 1);

    end_turns_until(s, 2);
    CHECK(turn_body(s) == 1);
    CHECK(current_actor(s) == 2);
    CHECK(s.budget.action);
    auto hit = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK(hit.ok);
    CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1 && e.b == e.a + 5;   // 7 - PB 2
    }));
    auto ghost_attack = step(s, ActionRequest{2, ActionKind::Attack, 3, 0});
    CHECK_FALSE(ghost_attack.ok);
    CHECK(ghost_attack.error == "possessing: the ghost uses the body's statistics");
    auto body_end = step(s, ActionRequest{1, ActionKind::EndTurn});
    CHECK_FALSE(body_end.ok);

    auto ghost_end = step(s, ActionRequest{2, ActionKind::EndTurn});
    CHECK(ghost_end.ok);
    CHECK(std::any_of(ghost_end.events.begin(), ghost_end.events.end(), [](const Event& e) {
        return e.kind == EventKind::TurnEnded && e.actor == 2;
    }));

    end_turns_until(s, 1);
    CHECK(current_actor(s) == 1);
    CHECK(turn_body(s) == 1);
    CHECK_FALSE(s.budget.action);
    auto during = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK_FALSE(during.ok);
    CHECK(during.error == "possessed: no control of its body");
    auto book = step(s, ActionRequest{1, ActionKind::EndTurn});
    CHECK(book.ok);
    int body_ends = 0, ghost_ends = 0;
    for (const Event& e : book.events) {
        if (e.kind != EventKind::TurnEnded) continue;
        body_ends += e.actor == 1;
        ghost_ends += e.actor == 2;
    }
    CHECK(body_ends == 1);
    CHECK(ghost_ends == 0);
}

TEST_CASE("Haste, Slow, Command and moved_at_least_ft follow the body on the ghost's turn") {
    Trigger pounce; pounce.id = kPounce; pounce.on = TriggerOn::Hit; pounce.limit = TriggerLimit::OncePerTurn;
    Predicate pred; pred.kind = PredicateKind::MovedAtLeastFt; pred.a = 20;
    pounce.when = {pred};
    RiderEffect extra; extra.kind = RiderKind::ExtraDamage; extra.dice = {1, 4, 0}; extra.same_as_weapon = true;
    pounce.effects = {extra};

    auto host = host_of(1);
    host.triggers = {pounce};
    host.attacks[0].to_hit = 40;
    host.attacks[0].pb_in_to_hit = 2;
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 4, nullptr, spell_hooks());
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 1, 1, marker(7100, ModStat::Haste), sink));
    link(s, 2, 1);
    if (current_actor(s) == 2) REQUIRE(step(s, ActionRequest{2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    CHECK(s.budget.haste_action);
    ActionRequest haste_atk{1, ActionKind::Attack, 3, 0};
    haste_atk.haste_action = true;
    auto hr = step(s, haste_atk);
    CHECK(hr.ok);
    CHECK_FALSE(s.budget.haste_action);
    CHECK(s.budget.action);   // the body's own action is still there

    // Slow replaces Haste for the next ghost turn.
    for (auto& m : s.modifiers) if (m.spec.stat == ModStat::Haste) m.spec.stat = ModStat::Slow;
    step(s, ActionRequest{2, ActionKind::EndTurn});
    end_turns_until(s, 2);
    auto first = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK(first.ok);
    auto second = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK_FALSE(second.ok);
    CHECK(second.error == "slowed: one attack per turn");

    // Command on the body restricts the ghost's turn.
    step(s, ActionRequest{2, ActionKind::EndTurn});
    ActiveSpellEffect cmd{};
    cmd.spell_id = 9; cmd.caster = 3; cmd.target = 1; cmd.no_condition = true;
    cmd.control = uint8_t(ControlKind::Command); cmd.command_word = uint8_t(CommandWord::Halt);
    s.effects.push_back(cmd);
    end_turns_until(s, 2);
    auto halted = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK_FALSE(halted.ok);
    CHECK(halted.error == "command: the turn ends");
    s.effects.clear();

    // 20 ft of the body's movement on the ghost's turn fires the rider.
    step(s, ActionRequest{2, ActionKind::EndTurn});
    end_turns_until(s, 2);
    s.budget.movement_ft = 60;
    s.budget.action = true;
    for (int i = 0; i < 4; ++i) {
        ActionRequest m{1, ActionKind::Move}; m.move_ft = 5; m.move_tiles = 1;
        REQUIRE(step(s, m).ok);
    }
    auto pounced = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK(pounced.ok);
    CHECK(count(pounced.events, EventKind::FeatureTriggered) == 1);
}

TEST_CASE("a printed attack is refused; a derived attack drops the proficiency bonus") {
    auto host = host_of(1);
    host.attacks = {printed_sword(), derived_sword()};
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 2, nullptr, spell_hooks());
    link(s, 2, 1);
    end_turns_until(s, 2);
    auto refused = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "possessed: printed attack has no proficiency split");
    auto ok = step(s, ActionRequest{1, ActionKind::Attack, 3, 1});
    CHECK(ok.ok);
}

TEST_CASE("link ends the body's concentration and grapples, and the end restores the overlay") {
    auto s = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 5, nullptr, spell_hooks());
    mut(s, 1).concentration_spell = 42;
    std::vector<Event> ev;
    REQUIRE(apply_sourced_condition(s, 1, 3, Condition::Grappled, 10, ev));
    CHECK(mut(s, 3).has(Condition::Grappled));
    ev.clear();
    REQUIRE(begin_possession(s, 2, 1, kPoss, 24, 1, ev).empty());
    CHECK(mut(s, 1).concentration_spell == 0);
    CHECK(std::any_of(ev.begin(), ev.end(), [](const Event& e) { return e.kind == EventKind::ConcentrationEnded; }));
    CHECK_FALSE(mut(s, 3).has(Condition::Grappled));

    end_turns_until(s, 2);
    auto ended = step(s, [&] {
        ActionRequest r{2, ActionKind::Revert};
        r.as_bonus_action = true;
        return r;
    }());
    CHECK(ended.ok);
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 2 && e.actor == 2 && e.target == 1;
    }));
    CHECK(mut(s, 1).possessed_by == 0);
    CHECK(mut(s, 1).abilities[Ability::Int] == 8);
    CHECK(mut(s, 1).save_proficiencies == (bit(Ability::Str) | bit(Ability::Con)));
    CHECK(s.budget.bonus_action == false);
}

TEST_CASE("a possessed body does not count as conscious for its team, and its side is the ghost's") {
    auto s = begin_encounter({host_of(1), ghost_of(2)}, 6, nullptr, spell_hooks());
    CHECK(side_of(s, mut(s, 1)) == 0);
    link(s, 2, 1);
    CHECK(side_of(s, mut(s, 1)) == 1);
    CHECK(mut(s, 1).team == 0);
    auto outcome = default_end_condition(s);
    REQUIRE(outcome);
    CHECK(outcome->reason == EndReason::TeamNoConscious);
}

TEST_CASE("non-proficient armor on a possessed body blocks spells and is not applied when nobody is possessed") {
    auto host = host_of(1);
    host.armor_proficiency = uint8_t(1 | 2 | 8);   // light, medium, shields. No heavy.
    host.worn_armor = 4;
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 7, nullptr, spell_hooks());
    CHECK_FALSE(nonproficient_armor(mut(s, 1)));   // worn heavy, not possessed: the six clerics stay proficient
    link(s, 2, 1);
    CHECK(nonproficient_armor(mut(s, 1)));
    end_turns_until(s, 2);
    ActionRequest cast{1, ActionKind::CastSpell, 1};
    cast.spell_id = 1;
    auto refused = step(s, cast);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "possessed: the body's spells are not available");
}

TEST_CASE("end_summon clears a form lock after it ends a possession link") {
    auto drive_to_end = [](EncounterState& s) {
        if (current_actor(s) == 3) REQUIRE(step(s, ActionRequest{3, ActionKind::EndTurn}).ok);
        mut(s, 3).hp = 0;
        mut(s, 3).dead = true;
        const CombatantId slot = current_actor(s);
        REQUIRE(slot != 3);
        return step(s, ActionRequest{slot, ActionKind::EndTurn});
    };

    // The summon is the ghost. It leaves play, so it is not placed, and its lock is still cleared.
    auto ghost = ghost_of(2);
    ghost.summoner = 1;
    ghost.form_lock_zones = {9, 12};
    auto s = begin_encounter({host_of(1), ghost, ally_of(3)}, 11, nullptr, spell_hooks());
    s.summons = 1;
    link(s, 2, 1);
    auto ended = drive_to_end(s);
    CHECK(ended.ok);
    CHECK(s.ended);
    CHECK(mut(s, 2).gone);
    CHECK(mut(s, 2).form_lock_zones.empty());
    CHECK(mut(s, 2).possessing == 0);
    CHECK(mut(s, 1).possessed_by == 0);
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.actor == 2 && e.target == 1 && e.a == 5;
    }));
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::SummonEnded && e.target == 2;
    }));
    CHECK(s.possession_exits.empty());

    // The summon is the body. The ghost is placed, and the body's lock is still cleared.
    auto body = host_of(1);
    body.summoner = 3;
    body.form_lock_zones = {4};
    auto s2 = begin_encounter({body, ghost_of(2), ally_of(3)}, 12, nullptr, spell_hooks());
    s2.summons = 1;
    link(s2, 2, 1);
    auto ended2 = drive_to_end(s2);
    CHECK(ended2.ok);
    CHECK(s2.ended);
    CHECK(mut(s2, 1).gone);
    CHECK(mut(s2, 1).form_lock_zones.empty());
    CHECK(mut(s2, 2).possessing == 0);
    CHECK(std::any_of(ended2.events.begin(), ended2.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.actor == 2 && e.target == 1 && e.a == 1;
    }));
    REQUIRE(s2.possession_exits.size() == 1);
    CHECK(s2.possession_exits[0].ghost == 2);
    CHECK(s2.possession_exits[0].body == 1);
}

TEST_CASE("class features are refused on a possessed body; a race feature that is not a proficiency stays") {
    auto host = host_of(1);
    host.possession_blocked_features = {1};
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 8, nullptr, spell_hooks());
    link(s, 2, 1);
    end_turns_until(s, 2);
    ActionRequest use{1, ActionKind::UseFeature};
    use.feature_id = 1;
    auto blocked = step(s, use);
    CHECK_FALSE(blocked.ok);
    CHECK(blocked.error == "possessed: class features are not available");
}
