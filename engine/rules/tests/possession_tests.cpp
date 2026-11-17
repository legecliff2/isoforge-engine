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
}

TEST_CASE("turn-owner resets run on the possessor's turn and not twice after the link ends") {
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
    std::vector<Event> ev;
    end_possession(s, 2, 2, ev, true);
    end_turns_until(s, 1);
    CHECK(current_actor(s) == 1);
    CHECK(mut(s, 1).reaction_used);
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
}
