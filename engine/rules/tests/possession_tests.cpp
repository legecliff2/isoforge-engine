// Possession engine (design v0.1 D1-D8, D11-D13). No monster content.
// The ghost's initiative slot spends the body's action economy. The body's slot is bookkeeping.
#include <doctest/doctest.h>

#include <algorithm>
#include "rules5e/attack.hpp"
#include "rules5e/conditions.hpp"
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
    CHECK(std::any_of(ok.events.begin(), ok.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1 && e.b == e.a + 5;   // to_hit 7, drop PB 2
    }));
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

TEST_CASE("every worn armor and shield is non-proficient while possessed") {
    bool saw_lower = false;
    for (uint64_t seed = 1; seed < 30 && !saw_lower; ++seed) {
        auto host = host_of(1);
        host.armor_proficiency = 0xFF;
        host.worn_armor = 1;
        host.worn_shield = true;
        auto bare = host_of(1);
        auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, seed, nullptr, spell_hooks());
        CHECK_FALSE(nonproficient_armor(mut(s, 1)));
        link(s, 2, 1);
        CHECK(nonproficient_armor(mut(s, 1)));
        auto plain = begin_encounter({bare, ghost_of(2), ally_of(3)}, seed, nullptr, spell_hooks());
        link(plain, 2, 1);
        end_turns_until(s, 2);
        end_turns_until(plain, 2);
        auto armored = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
        auto open = step(plain, ActionRequest{1, ActionKind::Attack, 3, 0});
        REQUIRE(armored.ok);
        REQUIRE(open.ok);
        const Event* ahit = nullptr;
        const Event* ohit = nullptr;
        for (const auto& e : armored.events) if (e.kind == EventKind::AttackResolved) ahit = &e;
        for (const auto& e : open.events) if (e.kind == EventKind::AttackResolved) ohit = &e;
        REQUIRE(ahit);
        REQUIRE(ohit);
        CHECK(ahit->b <= ohit->b);
        if (ahit->b < ohit->b) saw_lower = true;
        if (seed == 1) {
            ActionRequest cast{1, ActionKind::CastSpell, 1};
            cast.spell_id = 1;
            auto refused = step(s, cast);
            CHECK_FALSE(refused.ok);
            CHECK(refused.error == "can't cast spells in armor you aren't proficient with");
        }
    }
    CHECK(saw_lower);
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
    ActionRequest race{1, ActionKind::UseFeature};
    race.feature_id = 2;
    auto stayed = step(s, race);
    CHECK(stayed.ok);
    CHECK(std::any_of(stayed.events.begin(), stayed.events.end(), [](const Event& e) {
        return e.kind == EventKind::FeatureUsed && e.a == 2;
    }));
}

namespace {
SpellDef possess_def(int dc) {
    SpellDef d;
    d.id = kPoss; d.content_id = "test.possession"; d.level = 0; d.is_spell = false;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Cha; d.fixed_dc = int8_t(dc);
    d.possess = true; d.targets = 1; d.range_ft = 5; d.target_types = bit(CreatureType::Humanoid);
    d.success_immunity_hours = 24; d.success_immunity_scope = 1;
    return d;
}
EncounterHooks poss_hooks(int dc) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    REQUIRE(b->add(possess_def(dc)));
    h.spells = b;
    return h;
}
Combatant caster_ghost(CombatantId id) {
    auto c = ghost_of(id);
    c.spells_known = {kPoss};
    return c;
}
bool has_kind(const std::vector<Event>& ev, EventKind k) {
    return std::any_of(ev.begin(), ev.end(), [&](const Event& e) { return e.kind == k; });
}
}  // namespace

TEST_CASE("a successful possession save and an ended possession each grant 24 hours of immunity") {
    auto host = host_of(1);
    host.abilities.score[std::size_t(Ability::Cha)] = 12;   // mod +1, so a DC 1 Charisma save always succeeds
    auto s = begin_encounter({host, caster_ghost(2), ally_of(3)}, 21, nullptr, poss_hooks(1));
    end_turns_until(s, 2);
    ActionRequest saved_req{2, ActionKind::CastSpell, 1}; saved_req.spell_id = kPoss;
    auto saved = step(s, saved_req);
    REQUIRE_MESSAGE(saved.ok, saved.error);
    CHECK(has_kind(saved.events, EventKind::SavingThrowRolled));
    CHECK_FALSE(has_kind(saved.events, EventKind::Possessed));
    CHECK(mut(s, 1).possessed_by == 0);
    REQUIRE(step(s, ActionRequest{2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    ActionRequest again_req{2, ActionKind::CastSpell, 1}; again_req.spell_id = kPoss;
    auto again = step(s, again_req);
    REQUIRE_MESSAGE(again.ok, again.error);
    CHECK_FALSE(has_kind(again.events, EventKind::SavingThrowRolled));
    CHECK_FALSE(has_kind(again.events, EventKind::Possessed));

    auto failed = begin_encounter({host_of(1), caster_ghost(2), caster_ghost(4), ally_of(3)}, 22, nullptr, poss_hooks(30));
    end_turns_until(failed, 2);
    ActionRequest took_req{2, ActionKind::CastSpell, 1}; took_req.spell_id = kPoss;
    auto took = step(failed, took_req);
    REQUIRE_MESSAGE(took.ok, took.error);
    CHECK(has_kind(took.events, EventKind::Possessed));
    auto ended = step(failed, [&] { ActionRequest r{2, ActionKind::Revert}; r.as_bonus_action = true; return r; }());
    REQUIRE(ended.ok);
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 2;
    }));
    REQUIRE(step(failed, ActionRequest{2, ActionKind::EndTurn}).ok);
    end_turns_until(failed, 2);
    ActionRequest retry_req{2, ActionKind::CastSpell, 1}; retry_req.spell_id = kPoss;
    auto retry = step(failed, retry_req);
    REQUIRE_MESSAGE(retry.ok, retry.error);
    CHECK_FALSE(has_kind(retry.events, EventKind::Possessed));
    end_turns_until(failed, 4);
    ActionRequest other_req{4, ActionKind::CastSpell, 1}; other_req.spell_id = kPoss;
    auto other = step(failed, other_req);
    REQUIRE_MESSAGE(other.ok, other.error);
    CHECK(has_kind(other.events, EventKind::Possessed));
    CHECK(mut(failed, 1).possessed_by == 4);
}

TEST_CASE("a PfEG ward spends the action and makes no save") {
    auto s = begin_encounter({host_of(1), caster_ghost(2), ally_of(3)}, 23, nullptr, poss_hooks(30));
    ActiveSpellEffect ward{};
    ward.spell_id = 77; ward.caster = 3; ward.target = 1; ward.blocks_possession = true;
    ward.immune_from_types = bit(CreatureType::Undead);
    s.effects.push_back(ward);
    end_turns_until(s, 2);
    CHECK(s.budget.action);
    ActionRequest ward_req{2, ActionKind::CastSpell, 1}; ward_req.spell_id = kPoss;
    auto warded = step(s, ward_req);
    REQUIRE_MESSAGE(warded.ok, warded.error);
    CHECK_FALSE(s.budget.action);
    CHECK_FALSE(has_kind(warded.events, EventKind::SavingThrowRolled));
    CHECK_FALSE(has_kind(warded.events, EventKind::Possessed));
    CHECK(mut(s, 1).possessed_by == 0);
}

TEST_CASE("possession ends for 0 HP, Turn Undead, and the possessor's death") {
    auto victim = host_of(1);
    victim.hp = victim.hp_max = 4;
    victim.ac = 1;
    auto striker = ally_of(3);
    striker.attacks[0].to_hit = 40;
    striker.attacks[0].damage = {1, 8, 20};
    auto down = begin_encounter({victim, ghost_of(2), striker}, 24, nullptr, spell_hooks());
    link(down, 2, 1);
    end_turns_until(down, 3);
    auto ended = step(down, ActionRequest{3, ActionKind::Attack, 1, 0});
    REQUIRE_MESSAGE(ended.ok, ended.error);
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 1 && e.actor == 2 && e.target == 1;
    }));

    SpellDef turn;
    turn.id = spell_id_of("test.turn_undead"); turn.content_id = "test.turn_undead"; turn.level = 0; turn.is_spell = false;
    turn.kind = SpellEffectKind::Save; turn.save_ability = Ability::Wis; turn.fixed_dc = 30;
    turn.control = ControlKind::Turned; turn.target_types = bit(CreatureType::Undead); turn.targets = 1; turn.range_ft = 60;
    EncounterHooks th = spell_hooks();
    auto book = std::make_shared<SpellBook>();
    REQUIRE(book->add(turn));
    th.spells = book;
    auto turned = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 25, nullptr, th);
    mut(turned, 3).spells_known = {turn.id};
    link(turned, 2, 1);
    end_turns_until(turned, 3);
    ActionRequest tu_req{3, ActionKind::CastSpell, 2}; tu_req.spell_id = turn.id;
    auto tu = step(turned, tu_req);
    REQUIRE_MESSAGE(tu.ok, tu.error);
    CHECK(std::any_of(tu.events.begin(), tu.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 3;
    }));
    CHECK(mut(turned, 2).possessing == 0);

    auto dead = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 26, nullptr, spell_hooks());
    link(dead, 2, 1);
    end_turns_until(dead, 3);
    mut(dead, 2).hp = 0;
    mut(dead, 2).dead = true;
    auto gone = step(dead, ActionRequest{3, ActionKind::EndTurn});
    REQUIRE_MESSAGE(gone.ok, gone.error);
    CHECK(std::any_of(gone.events.begin(), gone.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 5;
    }));
}

TEST_CASE("extra attack and a class trigger are off while the body is possessed") {
    auto host = host_of(1);
    host.attacks_per_action = 2;
    host.attacks[0].to_hit = 40;
    Trigger sneak;
    sneak.id = 9; sneak.on = TriggerOn::Hit; sneak.owner_id = 9; sneak.limit = TriggerLimit::OncePerTurn;
    RiderEffect extra; extra.kind = RiderKind::ExtraDamage; extra.dice = {1, 6, 0}; extra.same_as_weapon = true;
    sneak.effects = {extra};
    host.triggers = {sneak};
    Combatant::PossessionClassBase base;
    base.attacks_per_action = 1;
    base.attacks = host.attacks;
    base.ac = host.ac;
    base.speed_ft = host.speed_ft;
    host.possession_class_base = base;
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 27, nullptr, spell_hooks());
    link(s, 2, 1);
    CHECK(mut(s, 1).attacks_per_action == 1);
    CHECK(mut(s, 1).triggers.empty());
    end_turns_until(s, 2);
    auto hit = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK(hit.ok);
    CHECK(count(hit.events, EventKind::FeatureTriggered) == 0);
    auto second = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    CHECK_FALSE(second.ok);
    auto ended = step(s, [&] { ActionRequest r{2, ActionKind::Revert}; r.as_bonus_action = true; return r; }());
    REQUIRE(ended.ok);
    CHECK(mut(s, 1).attacks_per_action == 2);
    CHECK(mut(s, 1).triggers.size() == 1);
}

TEST_CASE("an unsupported host is refused") {
    auto host = host_of(1);
    host.possession_unsupported = true;
    auto s = begin_encounter({host, ghost_of(2)}, 28, nullptr, spell_hooks());
    std::vector<Event> ev;
    CHECK(begin_possession(s, 2, 1, kPoss, 24, 1, ev) == "possessed: unsupported host");
    host.possession_refuse = "possessed: unsupported host: improved_critical";
    s = begin_encounter({host, ghost_of(2)}, 28, nullptr, spell_hooks());
    CHECK(begin_possession(s, 2, 1, kPoss, 24, 1, ev) == "possessed: unsupported host: improved_critical");
}

TEST_CASE("turn-owner resets run on the possessor's turn, and the body's own turn resets after the link ends") {
    auto s = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 29, nullptr, spell_hooks());
    link(s, 2, 1);
    end_turns_until(s, 2);
    mut(s, 1).reaction_used = true;
    mut(s, 1).dodging = true;
    end_turns_until(s, 1);
    CHECK(current_actor(s) == 1);
    CHECK(mut(s, 1).reaction_used);
    CHECK(mut(s, 1).dodging);
    end_turns_until(s, 2);
    CHECK(current_actor(s) == 2);
    CHECK_FALSE(mut(s, 1).reaction_used);
    CHECK_FALSE(mut(s, 1).dodging);
    mut(s, 1).reaction_used = true;
    mut(s, 1).dodging = true;
    mut(s, 1).reaction_ac_bonus = 5;
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, true);
    end_turns_until(s, 1);
    CHECK(current_actor(s) == 1);
    CHECK_FALSE(mut(s, 1).reaction_used);
    CHECK_FALSE(mut(s, 1).dodging);
    CHECK(mut(s, 1).reaction_ac_bonus == 0);
}

TEST_CASE("a printed attack is not offered as an opportunity attack") {
    auto host = host_of(1);
    host.attacks = {printed_sword()};
    host.reaction_policy.fill(ReactionPolicy::Auto);
    auto mover = ally_of(3);
    mover.team = 0;
    auto s = begin_encounter({host, ghost_of(2), mover}, 30, nullptr, spell_hooks());
    link(s, 2, 1);
    end_turns_until(s, 3);
    ActionRequest mv{3, ActionKind::Move};
    mv.move_ft = 5; mv.move_tiles = 1; mv.leaves_reach = true;
    auto moved = step(s, mv);
    REQUIRE_MESSAGE(moved.ok, moved.error);
    CHECK_FALSE(mut(s, 1).reaction_used);
    CHECK(std::none_of(moved.events.begin(), moved.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1;
    }));
    host.attacks = {derived_sword()};
    auto offered = begin_encounter({host, ghost_of(2), mover}, 30, nullptr, spell_hooks());
    link(offered, 2, 1);
    end_turns_until(offered, 3);
    auto hit = step(offered, mv);
    REQUIRE_MESSAGE(hit.ok, hit.error);
    CHECK(mut(offered, 1).reaction_used);
    CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1;
    }));
}

TEST_CASE("an opportunity attack is not offered against a possessing ghost") {
    EncounterHooks h = spell_hooks();
    h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
        return e.kind == EventKind::Moved && e.actor == 3;
    };
    auto ghost = ghost_of(2);
    ghost.reaction_policy.fill(ReactionPolicy::Auto);
    auto foe = ally_of(3);
    foe.reaction_policy.fill(ReactionPolicy::Auto);
    foe.attacks = {derived_sword()};
    auto s = begin_encounter({host_of(1), ghost, foe}, 55, nullptr, h);
    end_turns_until(s, 2);
    ActionRequest ready{2, ActionKind::Ready};
    ready.ready_kind = ActionKind::Move;
    ready.move_ft = 15;
    ready.leaves_reach = true;
    ready.trigger_id = 9;
    REQUIRE(step(s, ready).ok);
    REQUIRE(mut(s, 2).has_readied);
    link(s, 2, 1);
    CHECK(mut(s, 2).has_readied);
    end_turns_until(s, 3);
    ActionRequest mv{3, ActionKind::Move};
    mv.move_ft = 5;
    mv.move_tiles = 1;
    auto fired = step(s, mv);
    REQUIRE_MESSAGE(fired.ok, fired.error);
    CHECK(std::any_of(fired.events.begin(), fired.events.end(), [](const Event& e) {
        return e.kind == EventKind::ReadyTriggered && e.actor == 2;
    }));
    CHECK(std::none_of(fired.events.begin(), fired.events.end(), [](const Event& e) {
        return e.kind == EventKind::ReactionWindowOpened && e.a == int32_t(ReactionTrigger::LeavingReach);
    }));
    CHECK_FALSE(mut(s, 3).reaction_used);
    CHECK(std::none_of(fired.events.begin(), fired.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 3;
    }));
}

TEST_CASE("a readied printed attack does not spend the reaction") {
    EncounterHooks h = spell_hooks();
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&, std::span<const ReactionOption>) {
        return ReactionDecision{0, true, 0};
    };
    h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
        return e.kind == EventKind::Moved && e.actor == 3;
    };
    auto host = host_of(1);
    host.attacks = {printed_sword()};
    host.reaction_policy.fill(ReactionPolicy::Auto);
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 31, nullptr, h);
    end_turns_until(s, 1);
    ActionRequest ready{1, ActionKind::Ready, 3};
    ready.ready_kind = ActionKind::Attack;
    ready.trigger_id = 3;
    ready.target_within_5ft = true;
    REQUIRE(step(s, ready).ok);
    REQUIRE(mut(s, 1).has_readied);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    std::vector<Event> began;
    REQUIRE(begin_possession(s, 2, 1, kPoss, 24, 1, began).empty());
    CHECK(count(began, EventKind::ReadyExpired) == 1);
    CHECK_FALSE(mut(s, 1).has_readied);
    end_turns_until(s, 2);
    REQUIRE(step(s, ready).ok);
    REQUIRE(mut(s, 1).has_readied);
    end_turns_until(s, 3);
    ActionRequest mv{3, ActionKind::Move};
    mv.move_ft = 5;
    auto fired = step(s, mv);
    REQUIRE_MESSAGE(fired.ok, fired.error);
    CHECK(count(fired.events, EventKind::ReactionWindowOpened) == 0);
    CHECK(mut(s, 1).has_readied);
    CHECK_FALSE(mut(s, 1).reaction_used);
    // The same ready fires when the attack carries its own bonus, so the filter is what kept the reaction.
    host.attacks = {derived_sword()};
    auto offered = begin_encounter({host, ghost_of(2), ally_of(3)}, 31, nullptr, h);
    link(offered, 2, 1);
    end_turns_until(offered, 2);
    REQUIRE(step(offered, ready).ok);
    end_turns_until(offered, 3);
    auto spent = step(offered, mv);
    REQUIRE_MESSAGE(spent.ok, spent.error);
    CHECK(count(spent.events, EventKind::ReactionWindowOpened) == 1);
    CHECK_FALSE(mut(offered, 1).has_readied);
    CHECK(mut(offered, 1).reaction_used);
}

TEST_CASE("evasion, reckless, archery, improved critical, great weapon fighting, and a class reaction are off, then restored") {
    auto host = host_of(1);
    host.evasion = true;
    host.reckless_attack = true;
    host.great_weapon_fighting = true;
    host.sculpt_spells = true;
    host.attack_bonus_ranged = 2;
    host.attack_bonus_melee = 1;
    host.crit_min = 19;
    host.metamagic_known = 1;
    host.has_heal_bonus = true;
    host.heal_bonus_flat = 2;
    host.heal_bonus_per_level = true;
    host.climb_ft = 30;
    Reaction deflect;
    deflect.id = 44;
    host.reactions = {deflect};
    Combatant::PossessionClassBase base;
    base.attacks = host.attacks;
    base.ac = host.ac;
    base.speed_ft = host.speed_ft;
    base.evasion = false;
    base.reckless_attack = false;
    base.great_weapon_fighting = false;
    base.sculpt_spells = false;
    base.attack_bonus_ranged = 0;
    base.attack_bonus_melee = 0;
    base.crit_min = 20;
    base.metamagic_known = 0;
    base.has_heal_bonus = false;
    base.heal_bonus_flat = 0;
    base.heal_bonus_per_level = false;
    base.climb_ft = 0;
    host.possession_class_base = base;
    host.possession_hp_bonus = 1;
    const int max0 = host.hp_max;
    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 32, nullptr, spell_hooks());
    link(s, 2, 1);
    CHECK_FALSE(mut(s, 1).evasion);
    CHECK_FALSE(mut(s, 1).reckless_attack);
    CHECK_FALSE(mut(s, 1).great_weapon_fighting);
    CHECK_FALSE(mut(s, 1).sculpt_spells);
    CHECK(mut(s, 1).attack_bonus_ranged == 0);
    CHECK(mut(s, 1).attack_bonus_melee == 0);
    CHECK(mut(s, 1).crit_min == 20);
    CHECK(mut(s, 1).metamagic_known == 0);
    CHECK_FALSE(mut(s, 1).has_heal_bonus);
    CHECK(mut(s, 1).reactions.empty());
    CHECK(mut(s, 1).climb_ft == 0);
    CHECK(mut(s, 1).hp_max == max0 - 1);
    CHECK(mut(s, 1).hp == max0 - 1);
    mut(s, 1).hp = int16_t(mut(s, 1).hp - 4);
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK(mut(s, 1).evasion);
    CHECK(mut(s, 1).reckless_attack);
    CHECK(mut(s, 1).great_weapon_fighting);
    CHECK(mut(s, 1).sculpt_spells);
    CHECK(mut(s, 1).attack_bonus_ranged == 2);
    CHECK(mut(s, 1).attack_bonus_melee == 1);
    CHECK(mut(s, 1).crit_min == 19);
    CHECK(mut(s, 1).metamagic_known == 1);
    CHECK(mut(s, 1).has_heal_bonus);
    CHECK(mut(s, 1).heal_bonus_flat == 2);
    REQUIRE(mut(s, 1).reactions.size() == 1);
    CHECK(mut(s, 1).reactions[0].id == 44);
    CHECK(mut(s, 1).climb_ft == 30);
    CHECK(mut(s, 1).hp_max == max0);
    CHECK(mut(s, 1).hp == max0 - 5);
}

TEST_CASE("draconic resilience changes the maximum only") {
    auto low = host_of(1);
    low.hp = 1;
    low.hp_max = 9;
    low.possession_hp_bonus = 1;
    auto s = begin_encounter({low, ghost_of(2), ally_of(3)}, 40, nullptr, spell_hooks());
    link(s, 2, 1);
    CHECK(mut(s, 1).hp_max == 8);
    CHECK(mut(s, 1).hp == 1);
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK(mut(s, 1).hp_max == 9);
    CHECK(mut(s, 1).hp == 1);

    auto down = host_of(1);
    down.hp_max = 9;
    down.possession_hp_bonus = 3;
    down.uses_death_saves = true;
    auto dropped = begin_encounter({down, ghost_of(2), ally_of(3)}, 41, nullptr, spell_hooks());
    link(dropped, 2, 1);
    mut(dropped, 1).hp = 0;
    add_condition(mut(dropped, 1), Condition::Unconscious);
    end_possession(dropped, 2, 1, ev, false);
    CHECK(mut(dropped, 1).hp == 0);
    CHECK(mut(dropped, 1).hp_max == 9);
    CHECK(mut(dropped, 1).has(Condition::Unconscious));

    auto killed = host_of(1);
    killed.summoner = 3;
    killed.hp_max = 12;
    killed.possession_hp_bonus = 4;
    killed.uses_death_saves = false;
    auto summon = begin_encounter({killed, ghost_of(2), ally_of(3)}, 42, nullptr, spell_hooks());
    summon.summons = 1;
    link(summon, 2, 1);
    const int during = mut(summon, 1).hp;
    mut(summon, 1).hp = 0;
    mut(summon, 1).dead = true;
    end_turns_until(summon, 2);
    auto gone = step(summon, ActionRequest{2, ActionKind::EndTurn});
    REQUIRE_MESSAGE(gone.ok, gone.error);
    CHECK(std::any_of(gone.events.begin(), gone.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 1;
    }));
    CHECK(mut(summon, 1).hp == 0);
    CHECK(mut(summon, 1).hp_max == 12);
    CHECK(during > 0);

    auto floored = host_of(1);
    floored.hp = 1;
    floored.hp_max = 2;
    floored.possession_hp_bonus = 5;
    auto floor = begin_encounter({floored, ghost_of(2), ally_of(3)}, 52, nullptr, spell_hooks());
    link(floor, 2, 1);
    CHECK(mut(floor, 1).hp_max == 1);
    CHECK(mut(floor, 1).hp == 1);
    end_possession(floor, 2, 2, ev, false);
    CHECK(mut(floor, 1).hp_max == 2);
    CHECK(mut(floor, 1).hp == 1);
}

TEST_CASE("a class feature already running is suspended for the link and resumed after") {
    EncounterHooks h = spell_hooks();
    ModifierSpec rage;
    rage.id = 77;
    rage.source = 77;
    rage.stat = ModStat::Resistance;
    rage.has_damage_type = true;
    rage.damage_type = DamageType::Bludgeoning;
    h.feature_modifiers = [&](const EncounterState&, const Combatant&, uint32_t feature_id) {
        return feature_id == 1 ? std::vector<ModifierSpec>{rage} : std::vector<ModifierSpec>{};
    };
    auto host = host_of(1);
    host.possession_blocked_features = {1};
    host.ac = 10;
    auto striker = ally_of(3);
    striker.attacks[0].to_hit = 40;
    striker.attacks[0].damage = {0, 0, 8};
    striker.attacks[0].type = DamageType::Bludgeoning;
    striker.attacks_per_action = 2;
    auto s = begin_encounter({host, ghost_of(2), striker}, 43, nullptr, h);
    ActiveModifier row;
    row.spec = rage;
    row.caster = 1;
    row.target = 1;
    s.modifiers.push_back(row);
    link(s, 2, 1);
    CHECK(s.modifiers[0].possession_held);
    CHECK((mut(s, 1).mod_resist & bit(DamageType::Bludgeoning)) == 0);
    end_turns_until(s, 3);
    const int before = mut(s, 1).hp;
    REQUIRE(step(s, ActionRequest{3, ActionKind::Attack, 1, 0}).ok);
    CHECK(before - mut(s, 1).hp == 8);
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK_FALSE(s.modifiers[0].possession_held);
    CHECK((mut(s, 1).mod_resist & bit(DamageType::Bludgeoning)) != 0);
    const int mid = mut(s, 1).hp;
    REQUIRE(step(s, ActionRequest{3, ActionKind::Attack, 1, 0}).ok);
    CHECK(mid - mut(s, 1).hp == 4);
}

TEST_CASE("Sacred Weapon's attack bonus is suspended for the link and resumed after") {
    EncounterHooks h = spell_hooks();
    ModifierSpec weapon;
    weapon.id = 81;
    weapon.source = 81;
    weapon.stat = ModStat::AttackRoll;
    weapon.bonus = 2;
    h.feature_modifiers = [&](const EncounterState&, const Combatant&, uint32_t feature_id) {
        return feature_id == 1 ? std::vector<ModifierSpec>{weapon} : std::vector<ModifierSpec>{};
    };
    auto host = host_of(1);
    host.possession_blocked_features = {1};
    host.attacks[0].to_hit = 40;
    host.attacks[0].pb_in_to_hit = 0;
    host.attacks[0].damage = {0, 0, 1};
    auto foe = ally_of(3);
    foe.ac = 10;
    auto s = begin_encounter({host, ghost_of(2), foe}, 53, nullptr, h);
    ActiveModifier row;
    row.spec = weapon;
    row.caster = 1;
    row.target = 1;
    s.modifiers.push_back(row);
    CHECK(modifier_flat(s, 1, ModStat::AttackRoll) == 2);
    link(s, 2, 1);
    CHECK(s.modifiers[0].possession_held);
    CHECK(modifier_flat(s, 1, ModStat::AttackRoll) == 0);
    end_turns_until(s, 2);
    auto held = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    REQUIRE_MESSAGE(held.ok, held.error);
    CHECK(std::any_of(held.events.begin(), held.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1 && e.b == e.a + 40;
    }));
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    REQUIRE(step(s, ActionRequest{2, ActionKind::EndTurn}).ok);
    CHECK_FALSE(s.modifiers[0].possession_held);
    CHECK(modifier_flat(s, 1, ModStat::AttackRoll) == 2);
    end_turns_until(s, 1);
    auto back = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    REQUIRE_MESSAGE(back.ok, back.error);
    CHECK(std::any_of(back.events.begin(), back.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1 && e.b == e.a + 42;
    }));
}

TEST_CASE("Rage's damage bonus is suspended for the link and resumed after") {
    EncounterHooks h = spell_hooks();
    ModifierSpec rage;
    rage.id = 82;
    rage.source = 82;
    rage.stat = ModStat::Damage;
    rage.bonus = 2;
    rage.scope = 1;
    rage.scope_ability = 1;
    h.feature_modifiers = [&](const EncounterState&, const Combatant&, uint32_t feature_id) {
        return feature_id == 1 ? std::vector<ModifierSpec>{rage} : std::vector<ModifierSpec>{};
    };
    auto host = host_of(1);
    host.possession_blocked_features = {1};
    host.attacks[0].to_hit = 40;
    host.attacks[0].pb_in_to_hit = 0;
    host.attacks[0].damage = {0, 0, 4};
    host.attacks[0].ability_used = int8_t(Ability::Str);
    auto foe = ally_of(3);
    foe.ac = 10;
    auto s = begin_encounter({host, ghost_of(2), foe}, 54, nullptr, h);
    ActiveModifier row;
    row.spec = rage;
    row.caster = 1;
    row.target = 1;
    s.modifiers.push_back(row);
    CHECK(modifier_flat(s, 1, ModStat::Damage) == 2);
    link(s, 2, 1);
    CHECK(s.modifiers[0].possession_held);
    CHECK(modifier_flat(s, 1, ModStat::Damage) == 0);
    end_turns_until(s, 2);
    const int before = mut(s, 3).hp;
    REQUIRE(step(s, ActionRequest{1, ActionKind::Attack, 3, 0}).ok);
    CHECK(before - mut(s, 3).hp == 4);
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    REQUIRE(step(s, ActionRequest{2, ActionKind::EndTurn}).ok);
    CHECK_FALSE(s.modifiers[0].possession_held);
    CHECK(modifier_flat(s, 1, ModStat::Damage) == 2);
    end_turns_until(s, 1);
    const int mid = mut(s, 3).hp;
    auto again = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    REQUIRE_MESSAGE(again.ok, again.error);
    CHECK(mid - mut(s, 3).hp == 6);
}

TEST_CASE("charm and fright on the host are suspended, then resume") {
    auto s = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 44, nullptr, spell_hooks());
    add_condition(mut(s, 1), Condition::Charmed);
    add_condition(mut(s, 1), Condition::Frightened);
    link(s, 2, 1);
    CHECK_FALSE(mut(s, 1).has(Condition::Charmed));
    CHECK_FALSE(mut(s, 1).has(Condition::Frightened));
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK(mut(s, 1).has(Condition::Charmed));
    CHECK(mut(s, 1).has(Condition::Frightened));
}

TEST_CASE("a linked creature is not refilled with legendary actions") {
    auto host = host_of(1);
    host.la_max = 3;
    host.la_cur = 1;
    auto ghost = ghost_of(2);
    ghost.la_max = 2;
    ghost.la_cur = 0;
    auto s = begin_encounter({host, ghost, ally_of(3)}, 45, nullptr, spell_hooks());
    link(s, 2, 1);
    mut(s, 1).la_cur = 1;
    mut(s, 2).la_cur = 0;
    end_turns_until(s, 1);
    CHECK(mut(s, 1).la_cur == 1);
    end_turns_until(s, 2);
    CHECK(mut(s, 2).la_cur == 0);
}

TEST_CASE("destroy undead ends the link and kills the ghost") {
    SpellDef turn;
    turn.id = spell_id_of("test.destroy_undead");
    turn.content_id = "test.destroy_undead";
    turn.level = 0;
    turn.is_spell = false;
    turn.kind = SpellEffectKind::Save;
    turn.save_ability = Ability::Wis;
    turn.fixed_dc = 30;
    turn.control = ControlKind::Turned;
    turn.target_types = bit(CreatureType::Undead);
    turn.targets = 1;
    turn.range_ft = 60;
    turn.destroy_cr8_max = 8;
    EncounterHooks h = spell_hooks();
    auto book = std::make_shared<SpellBook>();
    REQUIRE(book->add(turn));
    h.spells = book;
    auto ghost = ghost_of(2);
    ghost.cr8 = 0;
    auto s = begin_encounter({host_of(1), ghost, ally_of(3)}, 46, nullptr, h);
    mut(s, 3).spells_known = {turn.id};
    link(s, 2, 1);
    end_turns_until(s, 3);
    ActionRequest req{3, ActionKind::CastSpell, 2};
    req.spell_id = turn.id;
    auto ended = step(s, req);
    REQUIRE_MESSAGE(ended.ok, ended.error);
    CHECK(std::any_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::PossessionEnded && e.a == 3;
    }));
    CHECK(mut(s, 2).dead);
    CHECK(mut(s, 2).possessing == 0);
}

TEST_CASE("a possessing ghost cannot be attacked, and a form revert while linked keeps the true form") {
    auto s = begin_encounter({host_of(1), ghost_of(2), ally_of(3)}, 47, nullptr, spell_hooks());
    link(s, 2, 1);
    end_turns_until(s, 3);
    auto denied = step(s, ActionRequest{3, ActionKind::Attack, 2, 0});
    CHECK_FALSE(denied.ok);
    CHECK(denied.error == "can't target a possessing ghost");

    auto shaped = host_of(1);
    shaped.attacks_per_action = 2;
    shaped.ac = 18;
    shaped.attacks[0].id = "true-sword";
    Combatant::PossessionClassBase base;
    base.attacks_per_action = 1;
    base.ac = 14;
    base.ac_delta = 4;
    base.attacks = shaped.attacks;
    base.speed_ft = shaped.speed_ft;
    shaped.possession_class_base = base;
    FormStats beast;
    beast.ac = 11;
    beast.speed_ft = 40;
    beast.attacks_per_action = 1;
    Attack bite = printed_sword();
    bite.id = "beast-bite";
    beast.attacks = {bite};
    REQUIRE(apply_form(shaped, FormKind::Shapechanger, beast));
    auto formed = begin_encounter({shaped, ghost_of(2), ally_of(3)}, 48, nullptr, spell_hooks());
    link(formed, 2, 1);
    CHECK(mut(formed, 1).attacks[0].id == "beast-bite");
    REQUIRE(revert_form(mut(formed, 1)));
    CHECK(mut(formed, 1).form == FormKind::None);
    CHECK(mut(formed, 1).attacks_per_action == 1);
    CHECK(mut(formed, 1).ac == 14);
    CHECK(mut(formed, 1).attacks[0].id == "true-sword");
    std::vector<Event> ev;
    end_possession(formed, 2, 2, ev, false);
    CHECK(mut(formed, 1).attacks_per_action == 2);
    CHECK(mut(formed, 1).ac == 18);
    CHECK(mut(formed, 1).attacks[0].id == "true-sword");
}

TEST_CASE("a possessed body is incapacitated for Dodge and Grapple, and the possessor can still attack") {
    auto host = host_of(1);
    host.dodging = true;
    host.attacks[0].grapple_instead_ok = true;
    host.attacks[0].to_hit = 40;
    auto foe = ally_of(3);
    foe.ac = 10;
    auto s = begin_encounter({host, ghost_of(2), foe}, 49, nullptr, spell_hooks());
    link(s, 2, 1);
    AttackContext ctx;
    ctx.target_within_5ft = true;
    CHECK(attack_roll_mode(mut(s, 3), mut(s, 1), ctx) == RollMode::Normal);
    CHECK(save_roll_mode(mut(s, 1), Ability::Dex) == RollMode::Normal);
    end_turns_until(s, 2);
    auto dodge = step(s, ActionRequest{1, ActionKind::Dodge});
    CHECK_FALSE(dodge.ok);
    CHECK(dodge.error == "incapacitated");
    ActionRequest grab{1, ActionKind::Attack, 3};
    grab.special = 1;
    grab.target_within_5ft = true;
    auto grappled = step(s, grab);
    CHECK_FALSE(grappled.ok);
    CHECK(grappled.error == "incapacitated");
    ActionRequest instead{1, ActionKind::Attack, 3, 0};
    instead.grapple_instead = true;
    instead.target_within_5ft = true;
    auto swapped = step(s, instead);
    CHECK_FALSE(swapped.ok);
    CHECK(swapped.error == "incapacitated");
    auto swing = step(s, ActionRequest{1, ActionKind::Attack, 3, 0});
    REQUIRE_MESSAGE(swing.ok, swing.error);
    CHECK(std::any_of(swing.events.begin(), swing.events.end(), [](const Event& e) {
        return e.kind == EventKind::AttackResolved && e.actor == 1;
    }));
    const auto offered = legal_actions(s);
    CHECK(std::none_of(offered.begin(), offered.end(), [](const ActionRequest& r) {
        return r.kind == ActionKind::Dodge || r.grapple_instead;
    }));
}

TEST_CASE("werebear linked in human form keeps the bear's climb, triggers, reactions, and bonus attacks") {
    // The authored werebear's bear form has climb 30 and no triggers, reactions, or bonus attacks.
    // Those three lists are put_class_base's other writes, so this fixture gives the bear its own
    // and the human form a different set. Unlink must not copy the human set onto the bear.
    auto host = host_of(1);
    host.creature_type = CreatureType::Humanoid;
    host.ac = 10;
    host.speed_ft = 30;
    host.climb_ft = 0;
    host.size = Size::Medium;
    host.hp = host.hp_max = 135;
    host.attacks = {Attack{"greataxe", 7, {1, 12, 4}, DamageType::Slashing, 5, false, false}};
    Trigger human_tr; human_tr.id = 11;
    Reaction human_rx; human_rx.id = 21;
    BonusAttack human_ba; human_ba.id = 31;
    host.triggers = {human_tr};
    host.reactions = {human_rx};
    host.bonus_attacks = {human_ba};
    const int int0 = host.abilities[Ability::Int];
    const int wis0 = host.abilities[Ability::Wis];
    const int cha0 = host.abilities[Ability::Cha];

    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 50, nullptr, spell_hooks());
    link(s, 2, 1);

    FormStats bear;
    bear.ac = 11;
    bear.speed_ft = 40;
    bear.climb_ft = 30;
    bear.size = Size::Large;
    bear.hp_max = 135;
    bear.attacks = {Attack{"bite", 7, {2, 10, 4}, DamageType::Piercing, 5, false, false},
                    Attack{"claw", 7, {2, 8, 4}, DamageType::Slashing, 5, false, false}};
    Trigger bear_tr; bear_tr.id = 12;
    Reaction bear_rx; bear_rx.id = 22;
    BonusAttack bear_ba; bear_ba.id = 32;
    bear.triggers = {bear_tr};
    bear.reactions = {bear_rx};
    bear.bonus_attacks = {bear_ba};
    REQUIRE(apply_form(mut(s, 1), FormKind::Shapechanger, bear));

    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK(mut(s, 1).form == FormKind::Shapechanger);
    CHECK(mut(s, 1).climb_ft == 30);
    CHECK(mut(s, 1).speed_ft == 40);
    CHECK(mut(s, 1).ac == 11);
    CHECK(mut(s, 1).size == Size::Large);
    REQUIRE(mut(s, 1).attacks.size() == 2);
    CHECK(mut(s, 1).attacks[0].id == "bite");
    CHECK(mut(s, 1).attacks[1].id == "claw");
    REQUIRE(mut(s, 1).triggers.size() == 1);
    CHECK(mut(s, 1).triggers[0].id == 12);
    REQUIRE(mut(s, 1).reactions.size() == 1);
    CHECK(mut(s, 1).reactions[0].id == 22);
    REQUIRE(mut(s, 1).bonus_attacks.size() == 1);
    CHECK(mut(s, 1).bonus_attacks[0].id == 32);

    REQUIRE(revert_form(mut(s, 1)));
    CHECK(mut(s, 1).form == FormKind::None);
    CHECK(mut(s, 1).climb_ft == 0);
    CHECK(mut(s, 1).speed_ft == 30);
    CHECK(mut(s, 1).ac == 10);
    CHECK(mut(s, 1).size == Size::Medium);
    CHECK(mut(s, 1).hp_max == 135);
    REQUIRE(mut(s, 1).attacks.size() == 1);
    CHECK(mut(s, 1).attacks[0].id == "greataxe");
    CHECK(mut(s, 1).attacks[0].to_hit == 7);
    REQUIRE(mut(s, 1).triggers.size() == 1);
    CHECK(mut(s, 1).triggers[0].id == 11);
    REQUIRE(mut(s, 1).reactions.size() == 1);
    CHECK(mut(s, 1).reactions[0].id == 21);
    REQUIRE(mut(s, 1).bonus_attacks.size() == 1);
    CHECK(mut(s, 1).bonus_attacks[0].id == 31);
    CHECK(mut(s, 1).abilities[Ability::Int] == int0);
    CHECK(mut(s, 1).abilities[Ability::Wis] == wis0);
    CHECK(mut(s, 1).abilities[Ability::Cha] == cha0);
}

TEST_CASE("a class host unlinked in a form reverts to the true AC, attacks, and hit point maximum") {
    // Unit level. No pack character is driven through a form and an unlink; the printed sorcerer
    // is only checked at load. Draconic Resilience is the hit-point maximum (3), and the AC and
    // the longsword are the class deltas. Polymorph is the form.
    auto host = host_of(1);
    host.ac = 16;
    host.attacks[0].to_hit = 7;
    host.attacks[0].damage.bonus = 5;
    host.hp = host.hp_max = 20;
    host.possession_hp_bonus = 3;
    Combatant::PossessionClassBase stripped;
    stripped.attacks = host.attacks;
    stripped.ac = 13;
    stripped.ac_delta = 3;
    stripped.speed_ft = host.speed_ft;
    stripped.attack_hit_delta = {2};
    stripped.attack_dmg_delta = {1};
    host.possession_class_base = stripped;

    auto s = begin_encounter({host, ghost_of(2), ally_of(3)}, 51, nullptr, spell_hooks());
    link(s, 2, 1);
    CHECK(mut(s, 1).ac == 13);
    CHECK(mut(s, 1).attacks[0].to_hit == 5);
    CHECK(mut(s, 1).attacks[0].damage.bonus == 4);
    CHECK(mut(s, 1).hp_max == 17);
    CHECK(mut(s, 1).hp == 17);

    FormStats beast;
    beast.ac = 11;
    beast.hp_max = 30;
    beast.speed_ft = 40;
    beast.attacks_per_action = 1;
    beast.attacks = {Attack{"beast-bite", 4, {1, 8, 2}, DamageType::Piercing, 5, false, false}};
    REQUIRE(apply_form(mut(s, 1), FormKind::Polymorph, beast));
    CHECK(mut(s, 1).ac == 11);
    CHECK(mut(s, 1).attacks[0].id == "beast-bite");
    CHECK(mut(s, 1).hp_max == 30);

    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, false);
    CHECK(mut(s, 1).form == FormKind::Polymorph);
    CHECK(mut(s, 1).ac == 11);
    CHECK(mut(s, 1).attacks[0].id == "beast-bite");
    CHECK(mut(s, 1).attacks[0].to_hit == 4);
    CHECK(mut(s, 1).hp_max == 30);
    REQUIRE(mut(s, 1).base);
    CHECK(mut(s, 1).base->hp_max == 20);

    REQUIRE(revert_form(mut(s, 1)));
    CHECK(mut(s, 1).form == FormKind::None);
    CHECK(mut(s, 1).ac == 16);
    REQUIRE(mut(s, 1).attacks.size() == 1);
    CHECK(mut(s, 1).attacks[0].id == "longsword");
    CHECK(mut(s, 1).attacks[0].to_hit == 7);
    CHECK(mut(s, 1).attacks[0].damage.bonus == 5);
    CHECK(mut(s, 1).hp_max == 20);
    CHECK(mut(s, 1).hp == 17);
}
