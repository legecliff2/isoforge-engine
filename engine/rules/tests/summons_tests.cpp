// R-G12 rules PR 2: summons (Conjure Animals, Find Familiar, Animate Dead; VERSION_BUMP_BATCH 7.2r,
// docs/proposals/SUMMONS_TRANSFORMS.md D1 / D2 / D4 / D10 / D14 / D15).
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kConjure = spell_id_of("test.conjure_animals");
const uint32_t kFamiliar = spell_id_of("test.find_familiar");
const uint32_t kAnimate = spell_id_of("test.animate_dead");

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef c; c.id = kConjure; c.content_id = "test.conjure_animals"; c.level = 3; c.concentration = true; c.range_ft = 60;
    c.kind = SpellEffectKind::Summon;
    c.summon = SummonSpec{8, SummonInitiative::Group, SummonControl::Verbal, true};
    book->add(c);
    SpellDef f; f.id = kFamiliar; f.content_id = "test.find_familiar"; f.level = 1; f.range_ft = 10;
    f.kind = SpellEffectKind::Summon;
    f.summon = SummonSpec{1, SummonInitiative::Own, SummonControl::Telepathic, true};
    book->add(f);
    SpellDef a; a.id = kAnimate; a.content_id = "test.animate_dead"; a.level = 3; a.range_ft = 10;
    a.kind = SpellEffectKind::Summon;
    a.summon = SummonSpec{4, SummonInitiative::Group, SummonControl::BonusAction, false};
    book->add(a);
    h.spells = book;
    h.build_summon = [](const EncounterState&, const Combatant&, const SpellDef&, uint8_t, uint8_t idx) -> std::optional<Combatant> {
        if (idx > 1) return std::nullopt;   // not in the CR band
        Combatant m = monster(0);
        m.hp = m.hp_max = int16_t(idx == 0 ? 11 : 26);
        m.abilities.score[std::size_t(Ability::Dex)] = idx == 0 ? 8 : 20;   // mixed stat blocks (D2 group slot)
        return m;
    };
    h.summon_choice_ok = [](const EncounterState&, const Combatant&, const SpellDef& d, uint8_t, const std::vector<uint8_t>& ar) {
        return d.id != kConjure || ar.size() != 3;   // Conjure Animals: 1, 2, 4 or 8 creatures
    };
    return h;
}
Combatant caster(CombatantId id) {
    Combatant c = wizard(id, 0);
    c.hp = c.hp_max = int16_t(60);
    c.spells_known = {kConjure, kFamiliar, kAnimate};
    never_react(c);
    return c;
}
Combatant foe(CombatantId id) { Combatant m = monster(id); m.hp = m.hp_max = int16_t(40); never_react(m); return m; }
EncounterState start(uint64_t seed = 3) { return begin_encounter({caster(1), foe(2), foe(3)}, seed, nullptr, hooks()); }
ActionRequest summon(CombatantId a, uint32_t spell, std::vector<uint8_t> ar, uint8_t lvl = 3) {
    ActionRequest r = cast(a, spell, a, lvl);
    r.summon_archetypes = std::move(ar);
    return r;
}
ActionRequest command(CombatantId a, uint8_t c, std::vector<CombatantId> ids = {}) {
    ActionRequest r{a, ActionKind::CommandSummons};
    r.command = c; r.command_ids = std::move(ids);
    return r;
}
// any request that runs a step without using the turn (the caster's action is spent by the cast)
ActionRequest poke(const EncounterState&) { ActionRequest r{1, ActionKind::Heal, 1}; r.amount = 1; return r; }
void set_totals(EncounterState& s, int16_t t) { for (auto& e : s.order) e.total = t; }
std::size_t slot_of(const EncounterState& s, CombatantId id) {
    for (std::size_t i = 0; i < s.order.size(); ++i) if (s.order[i].id == id) return i;
    return s.order.size();
}
std::vector<const Event*> all(const std::vector<Event>& ev, EventKind k) {
    std::vector<const Event*> out;
    for (const auto& e : ev) if (e.kind == k) out.push_back(&e);
    return out;
}
}  // namespace

TEST_CASE("summons: defaults, numbering and refusals") {
    Combatant c; CHECK(c.summoner == 0); CHECK_FALSE(c.gone); CHECK(c.commanded == 0);
    ActionRequest r; CHECK(r.summon_ids.empty()); CHECK(r.command == 0);
    CHECK(int(EventKind::Summoned) == 48); CHECK(int(EventKind::SummonEnded) == 49); CHECK(int(EventKind::Commanded) == 52);
    auto s = start();
    end_turns_until(s, 1);
    CHECK(step(s, summon(1, kConjure, {})).error == "summon: bad creature count");
    CHECK(step(s, summon(1, kConjure, {0, 0, 0, 0, 0, 0, 0, 0, 0})).error == "summon: bad creature count");
    CHECK(step(s, summon(1, kConjure, {0, 0, 0})).error == "summon: choice not allowed");
    CHECK(step(s, summon(1, kConjure, {2})).error == "summon not available");
    auto live = summon(1, kConjure, {0});
    live.summon_ids = {100};
    CHECK(step(s, live).error == "summon_ids: assigned by rules (leave empty)");
    auto tiles = summon(1, kConjure, {0, 0});
    tiles.summon_tiles = {ActionRequest::SummonTile{1, 2}};
    CHECK(step(s, tiles).error == "summon_tiles: one per creature");
    ActionRequest dash{1, ActionKind::Dash}; dash.summon_archetypes = {0};
    CHECK(step(s, dash).error == "summon: not a summon spell");
    ActionRequest dash2{1, ActionKind::Dash}; dash2.command = 1;
    CHECK(step(s, dash2).error == "command: CommandSummons only");
    CHECK(step(s, command(1, 1)).error == "command: no summons to command");
    CHECK(step(s, command(1, 3)).error == "command: 1 act or 2 defend");
    CHECK(find(s, 1)->spell_slots[3] == 2);   // nothing spent
    CHECK(s.summons == 0);
    CHECK_FALSE(has_summon_state(s));
    s.next_summon_id = 500;   // a seeded counter alone never folds 0xAF
    CHECK_FALSE(has_summon_state(s));
}

TEST_CASE("summons: placed after the current actor acts this round; ids from the counter; Summoned after SpellResolved") {
    auto s = start();
    end_turns_until(s, 1);
    s.next_summon_id = 500;
    set_totals(s, 100);   // every summon roll is lower: inserted after everyone
    const std::size_t ti = s.turn_index;
    auto r = step(s, summon(1, kFamiliar, {1}, 1));
    REQUIRE(r.ok);
    REQUIRE(r.logged);
    CHECK(r.logged->request.summon_ids == std::vector<CombatantId>{500});
    CHECK(s.next_summon_id == 501); CHECK(s.summons == 1);
    CHECK(s.turn_index == ti); CHECK(current_actor(s) == 1);
    CHECK(slot_of(s, 500) == s.order.size() - 1);
    const auto sm = all(r.events, EventKind::Summoned);
    REQUIRE(sm.size() == 1);
    CHECK(sm[0]->actor == 1); CHECK(sm[0]->target == 500); CHECK(sm[0]->a == int32_t(kFamiliar)); CHECK(sm[0]->b == 1);
    CHECK(sm[0]->c == s.order.back().total);
    CHECK(all(r.events, EventKind::SpellResolved).front() < sm[0]);
    const Combatant& f = *find(s, 500);
    CHECK(f.summoner == 1); CHECK(f.team == 0); CHECK(f.summon_source == kFamiliar); CHECK(f.summon_group == 500);
    CHECK(f.hp == 26); CHECK(f.summon_vanishes); CHECK_FALSE(f.summon_concentration);
    CHECK(find(s, 1)->concentration_spell == 0);
    CHECK(has_summon_state(s));
    end_turns_until(s, 500);   // D14: its slot is still ahead this round
    CHECK(current_actor(s) == 500);
    CHECK(s.round == 1);
    auto acts = step(s, {500, ActionKind::Dash});   // telepathic: acts on its own
    CHECK(acts.ok);
}

TEST_CASE("summons: inserted before the current actor: it keeps its turn, stamps are rewritten, the summon acts next round") {
    auto s = start();
    end_turns_until(s, 1);
    set_totals(s, -100);   // every summon roll is higher: inserted at the front
    const std::size_t ti = s.turn_index;
    const uint32_t now = (uint32_t(s.round) << 16) | uint32_t(ti);
    mut(s, 1).trigger_turn_stamp = now;
    mut(s, 2).trigger_turn_stamp = (uint32_t(s.round + 1) << 16) | uint32_t(ti);   // another round: untouched
    const uint16_t round = s.round;
    auto r = step(s, summon(1, kConjure, {0, 1}));
    REQUIRE(r.ok);
    const CombatantId first = r.logged->request.summon_ids.at(0);
    CHECK(first == 4);   // unseeded: above every id
    CHECK(s.turn_index == ti + 2);
    CHECK(current_actor(s) == 1);
    CHECK(find(s, 1)->trigger_turn_stamp == now + 2);
    CHECK(find(s, 2)->trigger_turn_stamp == ((uint32_t(round + 1) << 16) | uint32_t(ti)));
    CHECK(slot_of(s, 4) == 0); CHECK(slot_of(s, 5) == 1);   // D2: one group, consecutive by id
    CHECK(find(s, 5)->summon_group == 4);
    CHECK(s.order[0].total == s.order[1].total);
    CHECK(find(s, 1)->concentration_spell == kConjure);
    (void)step(s, {1, ActionKind::EndTurn});
    for (int i = 0; i < 10 && current_actor(s) != 4; ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
    CHECK(current_actor(s) == 4);
    CHECK(s.round == round + 1);
}

TEST_CASE("summons: uncommanded summons Dodge; a verbal command is free and lasts one turn") {
    auto s = start();
    end_turns_until(s, 1);
    set_totals(s, 100);
    REQUIRE(step(s, summon(1, kConjure, {0})).ok);
    const bool bonus = s.budget.bonus_action;
    auto c = step(s, command(1, 1));
    REQUIRE(c.ok);
    CHECK(s.budget.bonus_action == bonus);   // verbal: no action
    const auto cm = all(c.events, EventKind::Commanded);
    REQUIRE(cm.size() == 1); CHECK(cm[0]->target == 0); CHECK(cm[0]->a == 1);
    CHECK(find(s, 4)->commanded == 1);
    end_turns_until(s, 4);
    REQUIRE(current_actor(s) == 4);
    CHECK(step(s, {4, ActionKind::Dash}).ok);   // commanded: acts
    REQUIRE(step(s, {4, ActionKind::EndTurn}).ok);
    CHECK(find(s, 4)->commanded == 0);
    end_turns_until(s, 4);
    REQUIRE(current_actor(s) == 4);
    CHECK(step(s, {4, ActionKind::Dash}).error == "summon not commanded: Dodge only");
    const auto la = legal_actions(s);
    REQUIRE(la.size() == 2);
    CHECK(la[0].kind == ActionKind::Dodge); CHECK(la[1].kind == ActionKind::EndTurn);
    CHECK(step(s, {4, ActionKind::Dodge}).ok);
}

TEST_CASE("summons: Animate Dead: one initiative group per cast; the command costs the bonus action") {
    auto s = start(5);
    end_turns_until(s, 1);
    set_totals(s, 100);
    auto r = step(s, summon(1, kAnimate, {0, 0, 1}));
    REQUIRE(r.ok);
    CHECK(all(r.events, EventKind::Summoned).size() == 3);
    const std::size_t p = slot_of(s, 4);
    CHECK(slot_of(s, 5) == p + 1); CHECK(slot_of(s, 6) == p + 2);
    for (CombatantId id : {4u, 5u, 6u}) CHECK(find(s, id)->summon_group == 4);
    for (CombatantId id : {5u, 6u}) { CHECK(s.order[slot_of(s, id)].dex_mod == s.order[p].dex_mod); CHECK(s.order[slot_of(s, id)].total == s.order[p].total); }
    CHECK(find(s, 1)->concentration_spell == 0);
    auto la = legal_actions(s);
    CHECK(std::any_of(la.begin(), la.end(), [](const ActionRequest& q) { return q.kind == ActionKind::CommandSummons; }));
    auto c = step(s, command(1, 1, {4, 6}));
    REQUIRE(c.ok);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(all(c.events, EventKind::Commanded).size() == 2);
    CHECK(find(s, 4)->commanded == 1); CHECK(find(s, 5)->commanded == 0);
    CHECK(step(s, command(1, 1, {5})).error == "no bonus action available");
    CHECK(step(s, command(1, 1, {2})).error == "command: not your summon");
    CHECK(step(s, command(1, 1, {5, 5})).error == "command: duplicate id");
    la = legal_actions(s);
    CHECK_FALSE(std::any_of(la.begin(), la.end(), [](const ActionRequest& q) { return q.kind == ActionKind::CommandSummons; }));
    mut(s, 6).hp = 0;   // not vanishing: it drops like a monster, no SummonEnded
    auto e = step(s, poke(s));
    REQUIRE(e.ok);
    CHECK(all(e.events, EventKind::SummonEnded).empty());
}

TEST_CASE("summons: concentration end and 0 HP end the summons") {
    auto s = start();
    end_turns_until(s, 1);
    set_totals(s, 100);
    REQUIRE(step(s, summon(1, kConjure, {0, 0, 0, 0})).ok);
    mut(s, 5).hp = 0;   // vanishes at 0 HP
    mut(s, 5).concentration_spell = 4242;   // its own concentration ends the usual way
    auto v = step(s, poke(s));
    REQUIRE(v.ok);
    auto ve = all(v.events, EventKind::SummonEnded);
    REQUIRE(ve.size() == 1); CHECK(ve[0]->target == 5); CHECK(ve[0]->b == 3);
    const auto vc = all(v.events, EventKind::ConcentrationEnded);
    REQUIRE(vc.size() == 1); CHECK(vc[0]->actor == 5); CHECK(vc[0]->a == 4242); CHECK(vc[0] < ve[0]);
    CHECK(find(s, 5)->concentration_spell == 0);
    CHECK(find(s, 5)->gone); CHECK(find(s, 5)->dead);
    add_condition(mut(s, 1), Condition::Incapacitated);
    auto r = step(s, {1, ActionKind::EndTurn});
    REQUIRE(r.ok);
    const auto ce = all(r.events, EventKind::ConcentrationEnded);
    const auto se = all(r.events, EventKind::SummonEnded);
    REQUIRE(ce.size() == 1);
    REQUIRE(se.size() == 3);
    CHECK(se[0]->target == 4); CHECK(se[1]->target == 6); CHECK(se[2]->target == 7);
    for (const Event* e : se) { CHECK(e->b == 1); CHECK(e->actor == 1); CHECK(e->a == int32_t(kConjure)); CHECK(ce[0] < e); }
    for (int i = 0; i < 12; ++i) CHECK(current_actor(s) < 4);   // gone summons never take a turn
    for (int i = 0; i < 12; ++i) { (void)step(s, {current_actor(s), ActionKind::EndTurn}); CHECK(current_actor(s) < 4); }
}

TEST_CASE("summons: teardown at encounter end: SummonEnded b=5 ascending, then EncounterEnded") {
    auto s = start();
    end_turns_until(s, 1);
    set_totals(s, 100);
    REQUIRE(step(s, summon(1, kAnimate, {0, 1})).ok);
    FormStats fs; fs.hp_max = 9; fs.ac = 12; fs.abilities.score = {10, 10, 10, 2, 10, 4};
    REQUIRE(apply_form(mut(s, 4), FormKind::Polymorph, fs));   // a transformed summon just ends: no Reverted
    mut(s, 4).form_caster = 1; mut(s, 4).form_source = 77; ++s.transforms;
    mut(s, 5).concentration_spell = 4343;   // a living summon's concentration ends through end_concentration
    mut(s, 2).dead = true; mut(s, 2).hp = 0;
    mut(s, 3).hp = 0; mut(s, 3).dead = true;
    auto r = step(s, poke(s));
    REQUIRE(r.ok);
    CHECK(s.ended);
    const auto se = all(r.events, EventKind::SummonEnded);
    REQUIRE(se.size() == 2);
    CHECK(all(r.events, EventKind::Reverted).empty());
    const auto tc = all(r.events, EventKind::ConcentrationEnded);
    REQUIRE(tc.size() == 1); CHECK(tc[0]->actor == 5); CHECK(tc[0]->a == 4343); CHECK(tc[0] < se[0]);
    CHECK(find(s, 5)->concentration_spell == 0);
    CHECK(find(s, 4)->form == FormKind::None);
    CHECK(se[0]->target == 4); CHECK(se[1]->target == 5);
    for (const Event* e : se) CHECK(e->b == 5);
    REQUIRE(r.teardown_begin.has_value());
    CHECK(&r.events[*r.teardown_begin] == se[0]);
    CHECK(r.events[*r.teardown_begin + 2].kind == EventKind::EncounterEnded);
}

TEST_CASE("summons: replay determinism, replayed ids must match the counter, and the 0xAF digest") {
    auto make = [] { auto s = start(11); s.next_summon_id = 300; return s; };
    auto a = make();
    auto b = make();
    ActionLog log;
    end_turns_until(a, 1);
    end_turns_until(b, 1);
    REQUIRE(step_logged(a, summon(1, kConjure, {0, 1}), log).ok);
    REQUIRE(step_logged(a, command(1, 1), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    CHECK(log[0].request.summon_ids == std::vector<CombatantId>{300, 301});
    auto bad = log[0];
    bad.request.summon_ids = {301, 302};
    auto b2 = b;
    CHECK(replay_step(b2, bad).error == "summon_ids: do not match the summon counter");
    bad.request.summon_ids.clear();
    CHECK(replay_step(b2, bad).error == "summon_ids: do not match the summon counter");
    for (const auto& ls : log) {
        auto r = replay_step(b, ls);
        REQUIRE(r.ok); CHECK_FALSE(r.desync);
    }
    CHECK(a.order.size() == b.order.size());
    for (std::size_t i = 0; i < a.order.size(); ++i) { CHECK(a.order[i].id == b.order[i].id); CHECK(a.order[i].total == b.order[i].total); }
    CHECK(a.turn_index == b.turn_index);
    CHECK(summons_digest(a) == summons_digest(b));
    auto c = make();
    end_turns_until(c, 1);
    const uint64_t d0 = summons_digest(c);
    REQUIRE(step(c, summon(1, kConjure, {0, 1})).ok);
    CHECK(summons_digest(c) != d0);
    const uint64_t d1 = summons_digest(c);
    REQUIRE(step(c, command(1, 1)).ok);
    CHECK(summons_digest(c) != d1);   // the command byte is in the state
}
