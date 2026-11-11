// Legendary actions (SRD 5.1 p. 260). The window opens after another creature's turn, several
// options may be used in sequence, and a pass spends nothing. Turned and Slow do not block.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

#include "spell_util.hpp"

using namespace enc;

namespace {

LegendaryOption attack_opt(uint8_t cost = 1, uint8_t index = 0) {
    LegendaryOption o;
    o.cost = cost;
    o.use = LegendaryUse::Attack;
    o.attack_index = index;
    return o;
}
LegendaryOption move_opt(bool half, bool no_oa, uint8_t cost = 1) {
    LegendaryOption o;
    o.cost = cost;
    o.use = LegendaryUse::Move;
    o.half_speed = half;
    o.no_opportunity_attacks = no_oa;
    return o;
}
LegendaryOption spell_opt(uint32_t id, uint8_t cost = 1) {
    LegendaryOption o;
    o.cost = cost;
    o.use = LegendaryUse::Spell;
    o.ref_id = id;
    return o;
}
LegendaryOption check_opt() {
    LegendaryOption o;
    o.use = LegendaryUse::Check;
    return o;
}

void pool(Combatant& c, std::vector<LegendaryOption> opts, uint8_t n = 3) {
    c.la_max = c.la_cur = n;
    c.legendary = std::move(opts);
    c.attacks[0].to_hit = 40;   // the legendary attack hits
    c.attacks[0].damage = {1, 4, 0};
}

void order_as(EncounterState& s, std::initializer_list<CombatantId> ids) {
    s.order.clear();
    for (CombatantId id : ids) s.order.push_back({id, 15, 2});
    s.turn_index = 0;
}

ActionRequest la(CombatantId id, uint32_t option, CombatantId target = 0) {
    ActionRequest r{id, ActionKind::LegendaryAction, target};
    r.feature_id = option;
    return r;
}

bool offered(const EncounterState& s, CombatantId id) {
    return s.legendary_window.x != 0 && s.legendary_window.candidate == id;
}

}  // namespace

TEST_CASE("legendary: full at start, refill only on the creature's own turn, no window after its own turn") {
    Combatant x = pc(1), a = monster(2);
    pool(a, {attack_opt()});
    auto s = begin_encounter({x, a}, 1);
    order_as(s, {1, 2});
    CHECK(find(s, 2)->la_cur == 3);
    CHECK(count(step(s, {1, ActionKind::EndTurn}).events, EventKind::LegendaryActionsRegained) == 0);
    REQUIRE(offered(s, 2));
    REQUIRE(step(s, la(2, 1, 1)).ok);
    CHECK(find(s, 2)->la_cur == 2);
    // Passing closes the window and starts A's turn, which is the refill. Not a window after A's own turn.
    auto passed = step(s, la(2, 0));
    REQUIRE(passed.ok);
    CHECK(s.legendary_window.x == 0);
    CHECK(current_actor(s) == 2);
    bool regained = false;
    for (const auto& e : passed.events)
        if (e.kind == EventKind::LegendaryActionsRegained) {
            regained = true;
            CHECK(e.actor == 2);
            CHECK(e.a == 2);
            CHECK(e.b == 3);
        }
    CHECK(regained);
    CHECK(find(s, 2)->la_cur == 3);
    auto ended = step(s, {2, ActionKind::EndTurn});
    REQUIRE(ended.ok);
    CHECK(s.legendary_window.x == 0);
    CHECK(std::none_of(ended.events.begin(), ended.events.end(), [](const Event& e) {
        return e.kind == EventKind::LegendaryActionsRegained || e.kind == EventKind::LegendaryActionUsed;
    }));
}

TEST_CASE("legendary: cost, pass, and several uses at one turn end") {
    Combatant x = pc(1), a = monster(2);
    a.hp = a.hp_max = 40;
    pool(a, {attack_opt(1), attack_opt(2)}, 3);
    auto s = begin_encounter({x, a}, 1);
    order_as(s, {1, 2});
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    REQUIRE(offered(s, 2));
    auto too = step(s, la(2, 2, 1));   // cost 2 is legal
    REQUIRE(too.ok);
    CHECK(find(s, 2)->la_cur == 1);
    CHECK(std::any_of(too.events.begin(), too.events.end(), [](const Event& e) {
        return e.kind == EventKind::LegendaryActionUsed && e.a == 2 && e.b == 2 && e.c == 1 && e.target == 1;
    }));
    CHECK(offered(s, 2));   // still able to pay the cost-1 option
    auto cant = step(s, la(2, 2, 1));
    CHECK_FALSE(cant.ok);   // cost 2 with 1 left
    CHECK(find(s, 2)->la_cur == 1);
    const int hp = find(s, 1)->hp;
    CHECK(find(s, 2)->la_cur == 1);   // the cost-2 use left 1; the pass has not happened yet
    auto passed = step(s, la(2, 0));
    REQUIRE(passed.ok);
    // The pass spends nothing. It closes the window and starts A's turn, which refills from 1.
    CHECK(std::any_of(passed.events.begin(), passed.events.end(), [](const Event& e) {
        return e.kind == EventKind::LegendaryActionsRegained && e.actor == 2 && e.a == 1 && e.b == 3;
    }));
    CHECK(find(s, 2)->la_cur == 3);
    CHECK(find(s, 1)->hp == hp);
    CHECK(s.legendary_window.x == 0);   // queue finished, the turn advanced
    CHECK(current_actor(s) == 2);
}

TEST_CASE("legendary: two uses then the next candidate, poll_order ignored") {
    Combatant x = pc(1), a = monster(2), b = monster(3);
    a.team = 1;
    b.team = 1;
    a.hp = a.hp_max = 30;
    b.hp = b.hp_max = 30;
    pool(a, {attack_opt()}, 2);
    pool(b, {check_opt()}, 1);
    auto s = begin_encounter({x, a, b}, 3);
    order_as(s, {1, 2, 3});
    s.hooks.poll_order = [](const EncounterState&, const std::vector<CombatantId>& ids) {
        return std::vector<CombatantId>(ids.rbegin(), ids.rend());   // the reaction hook, not this window
    };
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    REQUIRE(offered(s, 2));   // initiative after X, not the reversed hook
    CHECK(s.legendary_window.queue.size() == 2);
    CHECK(s.legendary_window.queue[0] == 2);
    CHECK(s.legendary_window.queue[1] == 3);
    REQUIRE(step(s, la(2, 1, 1)).ok);
    REQUIRE(offered(s, 2));
    REQUIRE(step(s, la(2, 1, 1)).ok);   // pool empty, so the offer moves on
    REQUIRE(offered(s, 3));
    CHECK_FALSE(step(s, la(2, 0)).ok);   // not the creature being offered
    REQUIRE(step(s, la(3, 0)).ok);
    CHECK(s.legendary_window.x == 0);
    CHECK(current_actor(s) == 2);
}

TEST_CASE("legendary: lower id breaks an order tie") {
    Combatant x = pc(1), hi = monster(5), lo = monster(3);
    pool(hi, {check_opt()});
    pool(lo, {check_opt()});
    auto s = begin_encounter({x, hi, lo}, 1);
    s.order = {{1, 10, 0}};   // the other two are not in the order: the guard sorts by id
    s.turn_index = 0;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    REQUIRE(s.legendary_window.queue.size() == 2);
    CHECK(s.legendary_window.queue[0] == 3);
    CHECK(s.legendary_window.queue[1] == 5);
}

TEST_CASE("legendary: blocked by the incapacitated family, 0 HP, surprise and lethargy; not by Turned or Slow") {
    Combatant x = pc(1), a = monster(2);
    pool(a, {check_opt()});
    auto fresh = [&] {
        auto s = begin_encounter({x, a}, 1);
        order_as(s, {1, 2});
        return s;
    };
    {
        auto s = fresh();
        add_condition(mut(s, 2), Condition::Stunned);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(s.legendary_window.x == 0);
    }
    {
        auto s = fresh();
        mut(s, 2).hp = 0;
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(s.legendary_window.x == 0);
    }
    {
        auto s = fresh();
        mut(s, 2).surprised = true;
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(s.legendary_window.x == 0);
        // After its own first turn the flag is clear, so the next other turn end offers it.
        end_turns_until(s, 2);
        REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);   // surprised, so this is legal, and it is X
        CHECK_FALSE(find(s, 2)->surprised);
        end_turns_until(s, 1);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(offered(s, 2));
    }
    {
        auto s = fresh();
        mut(s, 2).lethargy = 1;
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(s.legendary_window.x == 0);
    }
    {
        auto s = fresh();
        ActiveSpellEffect e{};
        e.spell_id = 9;
        e.caster = 1;
        e.target = 2;
        e.condition = Condition::Frightened;
        e.save_ability = Ability::Wis;
        e.dc = 10;
        e.repeat_save = RepeatSave::None;
        e.control = uint8_t(ControlKind::Turned);
        s.effects.push_back(e);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(offered(s, 2));   // Turned does not block
    }
    {
        auto s = fresh();
        std::vector<Event> sink;
        ModifierSpec slow;
        slow.id = 70;
        slow.source = 70;
        slow.stat = ModStat::Slow;
        slow.applies_to = ModAppliesTo::Target;
        REQUIRE(apply_modifier(s, 1, 2, slow, sink));
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        CHECK(offered(s, 2));   // Slow does not block
    }
}

TEST_CASE("legendary attack and move open depth-0 reaction windows; no_opportunity_attacks does not") {
    Combatant x = pc(1, 80), a = monster(2);
    a.hp = a.hp_max = 80;
    pool(a, {attack_opt(), move_opt(false, false), move_opt(false, true)});
    x.spells_known = {kShield};
    x.spell_slots = x.spell_slots_max = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    x.spellcasting_ability = Ability::Int;
    x.caster_level = 1;
    EncounterHooks h = spell_hooks();
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&,
                           std::span<const ReactionOption>) { return ReactionDecision{0, true, 0}; };
    {
        auto s = begin_encounter({x, a}, 4, nullptr, h);
        order_as(s, {1, 2});
        set_policy(mut(s, 1), ReactionTrigger::BeingHit, ReactionPolicy::Ask);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        auto hit = step(s, la(2, 1, 1));
        REQUIRE(hit.ok);
        CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) {
            return e.kind == EventKind::ReactionWindowOpened && e.a == int(ReactionTrigger::BeingHit) && e.reaction_depth == 0;
        }));
        CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) { return e.kind == EventKind::AcBonus; }));
        CHECK_FALSE(find(s, 2)->reaction_used);
    }
    {
        auto s = begin_encounter({x, a}, 4, nullptr, h);
        order_as(s, {1, 2});
        set_policy(mut(s, 1), ReactionTrigger::BeingHit, ReactionPolicy::Never);
        ActionRequest ready{1, ActionKind::Ready, 2};
        ready.ready_kind = ActionKind::Attack;
        ready.attack_index = 0;
        ready.trigger_id = 1;
        s.hooks.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
            return e.kind == EventKind::AttackResolved && e.actor == 2;
        };
        REQUIRE(step(s, ready).ok);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        auto hit = step(s, la(2, 1, 1));
        REQUIRE(hit.ok);
        CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) {
            return e.kind == EventKind::ReadyTriggered;
        }));
        CHECK(std::any_of(hit.events.begin(), hit.events.end(), [](const Event& e) {
            return e.kind == EventKind::AttackResolved && e.actor == 2 && e.reaction_depth == 0;
        }));
    }
    {
        auto s = begin_encounter({x, a}, 4, nullptr, h);
        order_as(s, {1, 2});
        set_policy(mut(s, 1), ReactionTrigger::LeavingReach, ReactionPolicy::Auto);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        ActionRequest mv = la(2, 2);
        mv.move_ft = 15;
        mv.leaves_reach = true;
        auto moved = step(s, mv);
        REQUIRE(moved.ok);
        CHECK(std::any_of(moved.events.begin(), moved.events.end(), [](const Event& e) {
            return e.kind == EventKind::ReactionWindowOpened && e.a == int(ReactionTrigger::LeavingReach) &&
                   e.reaction_depth == 0;
        }));
        CHECK(std::any_of(moved.events.begin(), moved.events.end(), [](const Event& e) {
            return e.kind == EventKind::Moved && e.a == 15 && e.reaction_depth == 0;
        }));
        ActionRequest quiet = la(2, 3);
        quiet.move_ft = 10;
        quiet.leaves_reach = true;   // ignored: the option says it does not provoke
        auto no = step(s, quiet);
        REQUIRE(no.ok);
        CHECK(std::none_of(no.events.begin(), no.events.end(), [](const Event& e) {
            return e.kind == EventKind::ReactionWindowOpened && e.a == int(ReactionTrigger::LeavingReach);
        }));
    }
}

TEST_CASE("legendary: check_end inside the window does not advance the turn") {
    Combatant x = pc(1), a = monster(2), victim = monster(3);
    victim.team = 0;
    victim.hp = victim.hp_max = 1;
    x.team = 1;
    a.team = 1;
    pool(a, {attack_opt()});
    a.attacks[0].damage = {1, 8, 50};
    bool found = false;
    for (uint64_t seed = 1; seed < 40 && !found; ++seed) {
        auto s = begin_encounter({x, a, victim}, seed);
        order_as(s, {1, 2, 3});
        for (auto& c : s.combatants) c.reaction_policy.fill(ReactionPolicy::Never);
        const auto turn = s.turn_index;
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        auto kill = step(s, la(2, 1, 3));
        REQUIRE(kill.ok);
        if (find(s, 3)->hp > 0) continue;   // a natural 1 misses
        CHECK(s.ended);
        CHECK(s.turn_index == turn);
        CHECK(std::any_of(kill.events.begin(), kill.events.end(), [](const Event& e) { return e.kind == EventKind::EncounterEnded; }));
        CHECK(std::none_of(kill.events.begin(), kill.events.end(), [](const Event& e) { return e.kind == EventKind::TurnStarted; }));
        CHECK_FALSE(step(s, la(2, 0)).ok);
        found = true;
    }
    REQUIRE(found);
}

TEST_CASE("legendary spell: 1 action casting time, slot spent, bonus action and the bonus-action rule are not used") {
    auto book = srd_spellbook();
    SpellDef ba = *book->find(kMagicMissile);
    ba.content_id = "test.legendary.bonus_missile";
    ba.id = spell_id_of(ba.content_id);
    ba.casting_time = CastingTime::BonusAction;
    REQUIRE(book->add(ba));
    Combatant x = pc(1, 40), w = wizard(2, 1);
    w.hp = w.hp_max = 40;
    pool(w, {spell_opt(kMagicMissile), spell_opt(ba.id), spell_opt(kShield)});
    EncounterHooks h;
    h.spells = book;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    auto s = begin_encounter({x, w}, 5, nullptr, h);
    order_as(s, {1, 2});
    s.budget.bonus_action = false;   // already spent, on whoever's budget this is
    s.budget.action = false;
    const auto slots = find(s, 2)->spell_slots[1];
    const auto budget = s.budget;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK_FALSE(step(s, la(2, 2, 1)).ok);   // bonus-action casting time
    CHECK_FALSE(step(s, la(2, 3, 1)).ok);   // reaction casting time
    ActionRequest cast = la(2, 1, 1);
    cast.spell_level = 1;
    auto ok = step(s, cast);
    REQUIRE(ok.ok);
    CHECK(find(s, 2)->spell_slots[1] == slots - 1);
    CHECK(s.budget.action == budget.action);
    CHECK(s.budget.bonus_action == budget.bonus_action);
    CHECK(std::any_of(ok.events.begin(), ok.events.end(), [](const Event& e) { return e.kind == EventKind::SpellCast; }));
    CHECK(std::none_of(ok.events.begin(), ok.events.end(), [](const Event& e) {
        return e.kind == EventKind::FeatureTriggered;   // Slow's spell-loss roll is not applied
    }));
    // Slow on the caster still does not eat the spell.
    auto s2 = begin_encounter({x, w}, 5, nullptr, h);
    order_as(s2, {1, 2});
    std::vector<Event> sink;
    ModifierSpec slow;
    slow.id = 71;
    slow.source = 71;
    slow.stat = ModStat::Slow;
    slow.applies_to = ModAppliesTo::Target;
    REQUIRE(apply_modifier(s2, 1, 2, slow, sink));
    REQUIRE(step(s2, {1, ActionKind::EndTurn}).ok);
    ActionRequest again = la(2, 1, 1);
    again.spell_level = 1;
    auto slowed = step(s2, again);
    REQUIRE(slowed.ok);
    CHECK(std::any_of(slowed.events.begin(), slowed.events.end(), [](const Event& e) { return e.kind == EventKind::DamageDealt; }));
}

TEST_CASE("a form does not carry legendary actions") {
    Combatant host = pc(1);
    Combatant leg = monster(2);
    pool(leg, {attack_opt()}, 3);
    FormStats f;
    f.hp_max = 22;
    f.ac = 13;
    f.abilities = leg.abilities;
    f.speed_ft = leg.speed_ft;
    f.attacks = leg.attacks;
    f.creature_type = CreatureType::Beast;
    REQUIRE(apply_form(host, FormKind::Polymorph, f));
    CHECK(host.la_max == 0);
    CHECK(host.la_cur == 0);
    CHECK(host.legendary.empty());
    CHECK(leg.la_max == 3);
    CHECK(leg.legendary.size() == 1);
}

TEST_CASE("legendary check and move take no creature targets") {
    Combatant x = pc(1), a = monster(2);
    pool(a, {check_opt(), move_opt(false, false)});
    auto s = begin_encounter({x, a}, 1);
    order_as(s, {1, 2});
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    ActionRequest chk = la(2, 1);
    chk.targets = {1};
    auto bad = step(s, chk);
    CHECK_FALSE(bad.ok);
    CHECK(std::string(bad.error) == "legendary check or move takes no targets");
    CHECK(find(s, 2)->la_cur == 3);
    ActionRequest mv = la(2, 2);
    mv.move_ft = 5;
    mv.target = 1;
    auto bad_move = step(s, mv);
    CHECK_FALSE(bad_move.ok);
    CHECK(find(s, 2)->la_cur == 3);
}

// Polymorph replaces game statistics (SRD 5.1). Legendary actions and Legendary Resistance suspend with them
// and come back on revert at the remaining value, not refilled. Shapechanger and Wild Shape keep their own.
TEST_CASE("polymorph suspends legendary actions and Legendary Resistance") {
    const uint32_t kBlast = spell_id_of("test.spell#poly_blast");
    const uint32_t kLr = 880;
    SpellDef blast;
    blast.id = kBlast;
    blast.content_id = "test.spell#poly_blast";
    blast.level = 0;
    blast.range_ft = 60;
    blast.kind = SpellEffectKind::Save;
    blast.save_ability = Ability::Dex;
    blast.fixed_dc = 100;
    blast.on_save = OnSave::None;
    blast.dice = DiceExpr{1, 6, 0};
    blast.damage_type = DamageType::Fire;
    EncounterHooks h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(blast);
    h.spells = book;

    Combatant kept = monster(3);
    pool(kept, {attack_opt()}, 3);
    kept.la_cur = 1;
    kept.legendary_pool = 9;
    FormStats shape;
    shape.hp_max = 22;
    shape.ac = 13;
    shape.abilities = kept.abilities;
    shape.speed_ft = kept.speed_ft;
    shape.attacks = kept.attacks;
    shape.creature_type = CreatureType::Beast;
    REQUIRE(apply_form(kept, FormKind::Shapechanger, shape));
    CHECK(kept.la_max == 3);
    CHECK(kept.la_cur == 1);
    CHECK(kept.legendary_pool == 9);
    CHECK(kept.legendary.size() == 1);
    REQUIRE(revert_form(kept));
    CHECK(kept.la_cur == 1);
    CHECK_FALSE(kept.la_suspended);

    Combatant wild = monster(4);
    pool(wild, {attack_opt()}, 3);
    wild.la_cur = 2;
    wild.legendary_pool = 9;
    REQUIRE(apply_form(wild, FormKind::WildShape, shape));
    CHECK(wild.la_max == 3);
    CHECK(wild.la_cur == 2);
    CHECK(wild.legendary_pool == 9);
    REQUIRE(revert_form(wild));
    CHECK(wild.la_cur == 2);

    Combatant wiz = wizard(1);
    wiz.spells_known.push_back(kBlast);
    Combatant boss = monster(2);
    pool(boss, {attack_opt()}, 3);
    boss.legendary_pool = kLr;
    auto s = begin_encounter({wiz, boss}, 5, nullptr, h);
    order_as(s, {1, 2});
    mut(s, 2).la_cur = 2;   // spent before the spell; begin_encounter itself starts the pool full
    register_pool(s, 2, kLr, 3, 3);
    REQUIRE(apply_form(mut(s, 2), FormKind::Polymorph, shape));
    CHECK(find(s, 2)->form == FormKind::Polymorph);
    CHECK(find(s, 2)->la_max == 0);
    CHECK(find(s, 2)->la_cur == 0);
    CHECK(find(s, 2)->legendary.empty());
    CHECK(find(s, 2)->legendary_pool == 0);
    CHECK(find(s, 2)->la_suspended);
    CHECK(find(s, 2)->la_saved_cur == 2);
    CHECK(find(s, 2)->la_saved_pool == kLr);
    auto saved = step(s, cast(1, kBlast, 2));
    REQUIRE(saved.ok);
    CHECK(count(saved.events, EventKind::ResourceSpent) == 0);
    CHECK(find_pool(s, 2, kLr)->cur == 3);
    const Event* sv = nullptr;
    for (const auto& e : saved.events) if (e.kind == EventKind::SavingThrowRolled) sv = &e;
    REQUIRE(sv);
    CHECK((sv->c & 1) == 0);
    auto ended = step(s, {1, ActionKind::EndTurn});
    REQUIRE(ended.ok);
    CHECK_FALSE(offered(s, 2));
    CHECK(s.legendary_window.x == 0);
    CHECK(count(ended.events, EventKind::LegendaryActionsRegained) == 0);
    REQUIRE(revert_form(mut(s, 2)));
    CHECK(find(s, 2)->la_max == 3);
    CHECK(find(s, 2)->la_cur == 2);
    CHECK(find(s, 2)->legendary.size() == 1);
    CHECK(find(s, 2)->legendary_pool == kLr);
    CHECK_FALSE(find(s, 2)->la_suspended);
    CHECK(find_pool(s, 2, kLr)->cur == 3);
}
