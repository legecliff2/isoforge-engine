// R-G12 rules PR 1: transforms (Wild Shape, Polymorph, shapechanger forms; VERSION_BUMP_BATCH 7.2q,
// docs/proposals/SUMMONS_TRANSFORMS.md D7 / D8 / D12 / D13 / D15).
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kWildShape = 9001;
const uint32_t kChange = 9002;   // a shapechanger's change-shape feature
const uint32_t kPolymorph = spell_id_of("test.polymorph");
uint32_t blast_id(int n) { return spell_id_of("test.blast#" + std::to_string(n)); }

FormStats wolf() {
    FormStats f;
    f.abilities.score = {12, 15, 12, 3, 12, 6};
    f.hp_max = 11; f.ac = 13; f.speed_ft = 40; f.proficiency_bonus = 2;
    f.skill_proficiencies = 0x8; f.size = Size::Medium; f.creature_type = CreatureType::Beast;
    f.attacks = {Attack{"bite", 4, {2, 4, 2}, DamageType::Piercing, 5, false, false}};
    f.blocked_features = {kChange};
    return f;
}
FormStats ape() { FormStats f = wolf(); f.hp_max = 30; f.ac = 12; f.abilities.score = {16, 14, 14, 6, 12, 7}; return f; }

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    for (int n : {5, 15, 30, 66}) {   // fixed damage, a save nobody makes: deterministic damage
        SpellDef b; b.id = blast_id(n); b.content_id = "test.blast#" + std::to_string(n); b.level = 1;
        b.kind = SpellEffectKind::Save; b.save_ability = Ability::Con; b.fixed_dc = 30; b.range_ft = 60;
        b.dice = DiceExpr{0, 0, int16_t(n)}; b.damage_type = DamageType::Force; b.on_save = OnSave::None;
        book->add(b);
    }
    SpellDef p; p.id = kPolymorph; p.content_id = "test.polymorph"; p.level = 4; p.concentration = true; p.range_ft = 60;
    p.kind = SpellEffectKind::Polymorph; p.fixed_dc = 30;
    book->add(p);
    h.spells = book;
    h.feature_spec = [](const EncounterState&, const Combatant&, uint32_t id) -> std::optional<FeatureSpec> {
        FeatureSpec f; f.cost = 1;
        if (id == kWildShape) { f.form = uint8_t(FormKind::WildShape); f.form_rounds = 2; f.form_choices = 2; return f; }
        if (id == kChange) { f.form = uint8_t(FormKind::Shapechanger); f.form_choices = 1; return f; }
        return std::nullopt;
    };
    h.build_form = [](const EncounterState&, const Combatant& c, FormKind k, uint32_t, uint8_t idx) -> std::optional<FormStats> {
        if (k == FormKind::Shapechanger) { FormStats f; f.abilities = c.abilities; f.ac = int8_t(c.ac + 1); f.size = Size::Small;
                                            f.attacks = c.attacks; f.speed_ft = 40; return f; }
        if (idx == 0) return wolf();
        if (idx == 1 && k != FormKind::Polymorph) return ape();
        return std::nullopt;   // not allowed (CR / level)
    };
    return h;
}
Combatant druid(CombatantId id) {
    Combatant c = pc(id, 28);
    c.abilities.score = {10, 12, 14, 13, 16, 10};
    c.save_proficiencies = bit(Ability::Wis); c.skill_proficiencies = 0x1;
    c.ac = 16; c.temp_hp = 0;
    c.spell_slots_max = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0}; c.spell_slots = c.spell_slots_max;
    c.spells_known = {blast_id(5)};
    return c;
}
Combatant caster(CombatantId id, uint8_t team) {
    Combatant c = wizard(id, team);
    c.hp = c.hp_max = int16_t(60);
    c.spell_slots_max = {0, 4, 3, 2, 1, 0, 0, 0, 0, 0}; c.spell_slots = c.spell_slots_max;
    c.spells_known = {blast_id(5), blast_id(15), blast_id(30), blast_id(66), kPolymorph};
    never_react(c);
    return c;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 3) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    register_feature(s, 1, kWildShape, 2, FeatureRecharge::ShortRest);
    return s;
}
ActionRequest shape(CombatantId a, uint8_t form = 1, uint32_t feature = kWildShape) {
    ActionRequest r{a, ActionKind::UseFeature};
    r.feature_id = feature; r.form_archetype = form;
    return r;
}
ActionRequest poly(CombatantId a, CombatantId t, uint8_t form = 1) {
    ActionRequest r = cast(a, kPolymorph, t, 4);
    r.form_archetype = form;
    return r;
}
const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
}  // namespace

TEST_CASE("transforms: defaults, tags and event numbering") {
    Combatant c;
    CHECK(c.form == FormKind::None); CHECK_FALSE(c.base.has_value()); CHECK(c.form_end_round == 0);
    ActionRequest r; CHECK(r.form_archetype == 0);
    CHECK(int(EventKind::Transformed) == 50); CHECK(int(EventKind::Reverted) == 51);
    auto s = start({druid(1), caster(2, 1)});
    CHECK_FALSE(has_summon_state(s));
    CHECK(step(s, {current_actor(s), ActionKind::Revert}).error == "not transformed");
}

TEST_CASE("transforms: Wild Shape replaces physical stats, keeps mental scores, proficiencies, temp HP and concentration") {
    auto s = start({druid(1), caster(2, 1)});
    end_turns_until(s, 1);
    mut(s, 1).temp_hp = 4; mut(s, 1).concentration_spell = 77;
    CHECK(step(s, shape(1, 0)).error == "form_archetype required");
    CHECK(step(s, shape(1, 3)).error == "bad form_archetype");
    auto r = step(s, shape(1, 1));
    REQUIRE(r.ok);
    const Event* t = first(r.events, EventKind::Transformed);
    REQUIRE(t); CHECK(t->actor == 1); CHECK(t->a == int32_t(kWildShape)); CHECK(t->b == 0); CHECK(t->c == int32_t(FormKind::WildShape));
    CHECK(first(r.events, EventKind::FeatureUsed) < t);
    const Combatant& d = *find(s, 1);
    CHECK(d.form == FormKind::WildShape);
    CHECK(d.hp == 11); CHECK(d.hp_max == 11); CHECK(d.ac == 13); CHECK(d.speed_ft == 40);
    CHECK(d.abilities[Ability::Str] == 12); CHECK(d.abilities[Ability::Wis] == 16); CHECK(d.abilities[Ability::Int] == 13);
    CHECK(d.save_proficiencies == bit(Ability::Wis)); CHECK(d.skill_proficiencies == (0x1u | 0x8u));
    CHECK(d.attacks.size() == 1); CHECK(d.attacks[0].id == "bite");   // gear melds: the beast's natural weapons only
    CHECK(d.temp_hp == 4);              // D8
    CHECK(d.concentration_spell == 77); // D7: concentration kept
    CHECK(d.base->hp == 28); CHECK(d.base->ac == 16);
    CHECK(has_summon_state(s));
    CHECK(feature_uses_left(s, 1, kWildShape) == 1);
    CHECK(step(s, cast(1, blast_id(5), 2, 1)).error == "cannot cast spells in this form");
    for (const auto& la : legal_actions(s)) CHECK(la.kind != ActionKind::CastSpell);
    CHECK(step(s, shape(1, 1)).error == "already transformed");
    CHECK(step(s, shape(1, 1, kChange)).error == "feature not usable in this form");   // D7: blocked_features
    bool listed = false;
    for (const auto& la : legal_actions(s)) if (la.kind == ActionKind::Revert) { listed = true; CHECK(la.as_bonus_action); }
    CHECK(listed);
    auto rv = step(s, {1, ActionKind::Revert});   // a bonus action
    REQUIRE(rv.ok);
    const Event* e = first(rv.events, EventKind::Reverted);
    REQUIRE(e); CHECK(e->b == 3); CHECK(e->c == 0); CHECK(e->a == int32_t(kWildShape));
    CHECK(find(s, 1)->form == FormKind::None); CHECK(find(s, 1)->hp == 28); CHECK(find(s, 1)->ac == 16);
    CHECK(find(s, 1)->abilities[Ability::Str] == 10); CHECK(find(s, 1)->attacks[0].id == "longsword");
    CHECK(find(s, 1)->temp_hp == 4);
    CHECK(step(s, {1, ActionKind::Revert}).error == "not transformed");
    CHECK(has_summon_state(s));   // 0xAF keeps folding once something transformed
}

TEST_CASE("transforms: Wild Shape uses (2 per short rest)") {
    auto s = start({druid(1), caster(2, 1)});
    end_turns_until(s, 1);
    const uint16_t r0 = s.round;
    REQUIRE(step(s, shape(1, 1)).ok);
    CHECK(find(s, 1)->form_end_round == uint32_t(r0) + 2);
    REQUIRE(step(s, {1, ActionKind::Revert}).ok);
    CHECK(step(s, shape(1, 2)).error == "no action available");
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, shape(1, 2)).ok);   // second use, the larger form
    CHECK(find(s, 1)->hp_max == 30);
    REQUIRE(step(s, {1, ActionKind::Revert}).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(step(s, shape(1, 1)).error == "feature has no uses left");
    recover_features(s, false);   // short rest
    CHECK(feature_uses_left(s, 1, kWildShape) == 2);
    CHECK(step(s, shape(1, 1)).ok);
}

TEST_CASE("transforms: duration expiry emits Reverted reason 0 at the creature's turn start") {
    auto s = start({druid(1), caster(2, 1)});
    end_turns_until(s, 1);
    REQUIRE(step(s, shape(1, 1)).ok);
    const uint32_t ends = find(s, 1)->form_end_round;
    std::vector<Event> all;
    for (int i = 0; i < 12 && find(s, 1)->form != FormKind::None; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        REQUIRE(r.ok);
        all.insert(all.end(), r.events.begin(), r.events.end());
    }
    const Event* e = first(all, EventKind::Reverted);
    REQUIRE(e); CHECK(e->b == 0); CHECK(e->actor == 1);
    CHECK(s.round == ends); CHECK(current_actor(s) == 1);
}

TEST_CASE("transforms: 0 HP in a form reverts and the excess carries over") {
    auto s = start({druid(1), caster(2, 1)});
    end_turns_until(s, 1);
    mut(s, 1).temp_hp = 3;
    REQUIRE(step(s, shape(1, 1)).ok);   // wolf, 11 HP, 3 temp
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto r = step(s, cast(2, blast_id(30), 1, 1));   // 30: 3 temp, 11 wolf, 16 over
    REQUIRE(r.ok);
    const Event* rv = first(r.events, EventKind::Reverted);
    REQUIRE(rv); CHECK(rv->b == 2); CHECK(rv->c == 16);
    std::vector<int32_t> dealt;
    for (const auto& e : r.events) if (e.kind == EventKind::DamageDealt && e.target == 1) dealt.push_back(e.a);
    REQUIRE(dealt.size() == 2);   // the form's share (3 temp + 11 HP), then only the carried overflow: no double count
    CHECK(dealt[0] == 14); CHECK(dealt[1] == 16); CHECK(dealt[0] + dealt[1] == 30);
    const Combatant& d = *find(s, 1);
    CHECK(d.form == FormKind::None); CHECK(d.hp == 12); CHECK(d.hp_max == 28); CHECK(d.temp_hp == 0);
    CHECK_FALSE(d.dead); CHECK_FALSE(d.has(Condition::Unconscious));
}

TEST_CASE("transforms: massive damage on revert uses the normal form's hp_max (D12)") {
    SUBCASE("overflow reaching 0 HP with leftover < hp_max: dying, not dead") {
        auto s = start({druid(1), caster(2, 1)});
        end_turns_until(s, 1);
        REQUIRE(step(s, shape(1, 1)).ok);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 2);
        REQUIRE(step(s, cast(2, blast_id(66), 1, 1)).ok == true);   // 66 - 11 = 55 over; 28 HP, 27 left over < 28
        CHECK(find(s, 1)->hp == 0); CHECK_FALSE(find(s, 1)->dead); CHECK(find(s, 1)->has(Condition::Unconscious));
        CHECK(find(s, 1)->form == FormKind::None);
    }
    SUBCASE("leftover >= the normal form's hp_max: instant death (the form's own max is not used)") {
        auto d = druid(1); d.hp = d.hp_max = int16_t(27);
        auto s = start({d, caster(2, 1)});
        end_turns_until(s, 1);
        REQUIRE(step(s, shape(1, 1)).ok);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 2);
        auto r = step(s, cast(2, blast_id(66), 1, 1));   // 55 over: 27 HP then 28 >= 27
        REQUIRE(r.ok);
        CHECK(find(s, 1)->dead);
        CHECK(count(r.events, EventKind::Reverted) == 1);
        CHECK(first(r.events, EventKind::Reverted) < first(r.events, EventKind::CombatantDied));
    }
}

TEST_CASE("transforms: Polymorph refusals (D13), the save, and spellcasting off") {
    auto s = start({druid(1), caster(2, 1), pc(3)});
    end_turns_until(s, 2);
    mut(s, 1).shapechanger = true;
    CHECK(step(s, poly(2, 1)).error == "polymorph: target is a shapechanger");
    mut(s, 1).shapechanger = false;
    make_dying(mut(s, 3));
    CHECK(step(s, poly(2, 3)).error == "polymorph: target has 0 hit points");
    CHECK(step(s, poly(2, 1, 0)).error == "form_archetype required");
    CHECK(step(s, poly(2, 1, 2)).error == "form not available");
    { auto r = cast(2, blast_id(5), 1, 1); r.form_archetype = 1; CHECK(step(s, r).error == "form_archetype: not a polymorph spell"); }
    { ActionRequest r{2, ActionKind::Dodge}; r.form_archetype = 1; CHECK(step(s, r).error == "form_archetype: UseFeature or CastSpell only"); }
    CHECK(find(s, 2)->spell_slots[4] == 1);   // nothing spent
    auto r = step(s, poly(2, 1));
    REQUIRE(r.ok);
    const Event* sv = first(r.events, EventKind::SavingThrowRolled);
    REQUIRE(sv); CHECK(sv->actor == 1); CHECK(sv->c == 0);   // unwilling: a failed Wisdom save
    const Event* t = first(r.events, EventKind::Transformed);
    REQUIRE(t); CHECK(t->c == int32_t(FormKind::Polymorph)); CHECK(t->a == int32_t(kPolymorph));
    const Combatant& d = *find(s, 1);
    CHECK(d.form == FormKind::Polymorph); CHECK(d.form_caster == 2);
    CHECK(d.abilities[Ability::Wis] == 12);   // Polymorph replaces the mental scores too
    CHECK(d.save_proficiencies == 0); CHECK(d.skill_proficiencies == 0x8u);
    CHECK(find(s, 2)->concentration_spell == kPolymorph);
    mut(s, 2).spell_slots[4] = 1;
    CHECK(step(s, poly(2, 1)).error == "polymorph: target already transformed");
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(step(s, {1, ActionKind::Revert}).error == "polymorph ends with the spell, not by choice");
    CHECK(step(s, cast(1, blast_id(5), 2, 1)).error == "cannot cast spells in this form");
}

TEST_CASE("transforms: a willing Polymorph target makes no save") {
    auto s = start({druid(1), caster(2, 0), monster(3)});
    end_turns_until(s, 2);
    auto r = step(s, poly(2, 1));
    REQUIRE(r.ok);
    CHECK(first(r.events, EventKind::SavingThrowRolled) == nullptr);
    CHECK(find(s, 1)->form == FormKind::Polymorph);
}

TEST_CASE("transforms: concentration ending reverts Polymorph; the target reverting ends the concentration") {
    SUBCASE("caster incapacitated: ConcentrationEnded, then Reverted b = 1") {
        auto s = start({druid(1), caster(2, 1)});
        end_turns_until(s, 2);
        REQUIRE(step(s, poly(2, 1)).ok);
        add_condition(mut(s, 2), Condition::Stunned);
        auto r = step(s, {2, ActionKind::EndTurn});
        REQUIRE(r.ok);
        const Event* ce = first(r.events, EventKind::ConcentrationEnded);
        const Event* rv = first(r.events, EventKind::Reverted);
        REQUIRE(ce); REQUIRE(rv);
        CHECK(ce < rv); CHECK(rv->b == 1); CHECK(rv->actor == 1);
        CHECK(find(s, 1)->form == FormKind::None); CHECK(find(s, 1)->hp == 28);
    }
    SUBCASE("the polymorphed creature drops to 0: Reverted b = 2, then the caster's concentration ends") {
        auto s = start({druid(1), caster(2, 1), caster(3, 1)});
        end_turns_until(s, 2);
        REQUIRE(step(s, poly(2, 1)).ok);
        REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
        end_turns_until(s, 3);
        auto r = step(s, cast(3, blast_id(15), 1, 1));   // wolf 11: 4 over
        REQUIRE(r.ok);
        const Event* rv = first(r.events, EventKind::Reverted);
        REQUIRE(rv); CHECK(rv->b == 2); CHECK(rv->c == 4);
        CHECK(find(s, 1)->hp == 24);
        CHECK(find(s, 2)->concentration_spell == 0);
        CHECK(count(r.events, EventKind::ConcentrationEnded) == 1);
    }
}

TEST_CASE("transforms: shapechanger keeps one HP pool and reverts on death") {
    auto m = monster(5); m.shapechanger = true; m.hp = m.hp_max = int16_t(20);
    const int8_t ac0 = m.ac;
    auto s = start({caster(1, 0), m});
    register_feature(s, 5, kChange, 0, FeatureRecharge::None);
    end_turns_until(s, 5);
    mut(s, 5).hp = 15;
    REQUIRE(step(s, shape(5, 1, kChange)).ok);
    CHECK(find(s, 5)->form == FormKind::Shapechanger); CHECK(find(s, 5)->hp == 15); CHECK(find(s, 5)->hp_max == 20);
    CHECK(find(s, 5)->size == Size::Small); CHECK(find(s, 5)->ac == ac0 + 1);
    REQUIRE(step(s, {5, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 5);
    bool listed = false;
    for (const auto& la : legal_actions(s)) if (la.kind == ActionKind::Revert) { listed = true; CHECK_FALSE(la.as_bonus_action); }
    CHECK(listed);
    REQUIRE(step(s, {5, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    auto a = step(s, cast(1, blast_id(30), 5, 1));   // a monster dies at 0 HP: Reverted b = 4 before CombatantDied
    REQUIRE(a.ok);
    CHECK(find(s, 5)->dead); CHECK(find(s, 5)->form == FormKind::None); CHECK(find(s, 5)->size == Size::Medium);
    const Event* rv = first(a.events, EventKind::Reverted);
    REQUIRE(rv); CHECK(rv->b == 4);
    CHECK(rv < first(a.events, EventKind::CombatantDied));
}

TEST_CASE("transforms: encounter end tears down every transform (D15, reason 5, before EncounterEnded)") {
    auto s = start({druid(1), monster(2), caster(3, 0)});
    end_turns_until(s, 1);
    REQUIRE(step(s, shape(1, 1)).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 3);
    auto r = step(s, cast(3, blast_id(30), 2, 1));   // the only enemy dies: the encounter ends
    REQUIRE(r.ok);
    REQUIRE(s.ended);
    REQUIRE(r.teardown_begin.has_value());
    const std::size_t k = *r.teardown_begin;
    REQUIRE(k + 1 < r.events.size());
    CHECK(r.events[k].kind == EventKind::Reverted); CHECK(r.events[k].b == 5); CHECK(r.events[k].c == 0); CHECK(r.events[k].actor == 1);
    CHECK(r.events[k + 1].kind == EventKind::EncounterEnded);
    CHECK(find(s, 1)->form == FormKind::None); CHECK(find(s, 1)->hp == 28);
    CHECK(has_summon_state(s));
    // no transform: no teardown block
    auto s2 = start({druid(1), monster(2), caster(3, 0)});
    end_turns_until(s2, 3);
    auto r2 = step(s2, cast(3, blast_id(30), 2, 1));
    REQUIRE(s2.ended);
    CHECK_FALSE(r2.teardown_begin.has_value());
    CHECK_FALSE(has_summon_state(s2));
}

TEST_CASE("transforms: replay determinism and the 0xAF digest") {
    auto make = [] { return start({druid(1), caster(2, 1)}, 11); };
    auto a = make();
    auto b = make();
    ActionLog log;
    end_turns_until(a, 1);
    end_turns_until(b, 1);
    REQUIRE(step_logged(a, shape(1, 1), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    REQUIRE(step_logged(a, poly(2, 1, 2), log).ok == false);   // invalid requests are not logged
    REQUIRE(step_logged(a, cast(2, blast_id(15), 1, 1), log).ok);
    for (const auto& ls : log) {
        auto r = replay_step(b, ls);
        REQUIRE(r.ok); CHECK_FALSE(r.desync);
    }
    CHECK(find(a, 1)->hp == find(b, 1)->hp);
    CHECK(find(a, 1)->form == find(b, 1)->form);
    CHECK(summons_digest(a) == summons_digest(b));
    CHECK(has_summon_state(a) == has_summon_state(b));
    auto c = make();
    end_turns_until(c, 1);
    const uint64_t d0 = summons_digest(c);
    REQUIRE(step(c, shape(1, 1)).ok);
    CHECK(summons_digest(c) != d0);
    auto e = make();
    end_turns_until(e, 1);
    REQUIRE(step(e, shape(1, 2)).ok);
    CHECK(summons_digest(e) != summons_digest(c));   // the form choice is in the state
}
