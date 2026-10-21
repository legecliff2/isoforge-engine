// VERSION_BUMP_BATCH 7.2zc: spell and feature gap batch 1 (T&C agreed shapes). Disciple of Life (Combatant::heal_bonus_*),
// Invisibility's ends_on (attack / cast), and the generic mark of Hunter's Mark / Hex (ModifierSpec::mark_scope /
// chosen_ability, ActiveModifier::mark, SpellDef::move_mark, ActionRequest::mark_ability / move_mark), plus replay determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kInvis = spell_id_of("test.spell.invisibility");
const uint32_t kInvisCast = spell_id_of("test.spell.invisibility_cast_only");
const uint32_t kMark = spell_id_of("test.spell.hunters_mark");
const uint32_t kHex = spell_id_of("test.spell.hex");
const uint32_t kBolt = spell_id_of("test.spell.bolt");
const uint32_t kHealFeature = spell_id_of("test.feature.heal");

SpellDef invisibility(uint32_t id, const char* cid, uint8_t ends) {
    SpellDef d; d.id = id; d.content_id = cid; d.level = 2; d.range_ft = 5; d.kind = SpellEffectKind::ApplyCondition;
    d.condition = Condition::Invisible; d.applies_condition = true; d.concentration = true; d.targets = 2;
    d.duration = SpellDuration::Hours; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 600; d.ends_on = ends;
    return d;
}
ModifierSpec mark_damage(uint32_t id, bool weapon_only, DamageType type, bool typed) {
    ModifierSpec m; m.id = id; m.stat = ModStat::Damage; m.die = DiceExpr{5, 1, 0}; m.applies_to = ModAppliesTo::Self;
    m.concentration = true; m.mark_scope = 1; m.damage_type = type; m.has_damage_type = typed;
    if (weapon_only) { Predicate p; p.kind = PredicateKind::WeaponAttack; m.when = {p}; }
    return m;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(invisibility(kInvis, "test.spell.invisibility", kEndsOnAttack | kEndsOnCast));
    book->add(invisibility(kInvisCast, "test.spell.invisibility_cast_only", kEndsOnCast));
    SpellDef hm; hm.id = kMark; hm.content_id = "test.spell.hunters_mark"; hm.level = 1; hm.range_ft = 90;
    hm.casting_time = CastingTime::BonusAction; hm.concentration = true; hm.duration = SpellDuration::Hours; hm.duration_amount = 1;
    hm.kind = SpellEffectKind::ApplyModifier; hm.modifier = mark_damage(spell_id_of("test.spell.hunters_mark#modifier"), true,
                                                                        DamageType::Force, false);
    hm.move_mark = 1;
    book->add(hm);
    SpellDef hx = hm; hx.id = kHex; hx.content_id = "test.spell.hex";
    hx.modifier = mark_damage(spell_id_of("test.spell.hex#modifier"), false, DamageType::Necrotic, true);
    SpellEffect chk; chk.kind = SpellEffectKind::ApplyModifier;
    chk.modifier.id = spell_id_of("test.spell.hex#modifier1"); chk.modifier.stat = ModStat::AbilityCheck;
    chk.modifier.mode = RollMode::Disadvantage; chk.modifier.applies_to = ModAppliesTo::Target; chk.modifier.mark_scope = 1;
    chk.modifier.chosen_ability = true;
    hx.extra_effects = {chk};
    book->add(hx);
    SpellDef bolt; bolt.id = kBolt; bolt.content_id = "test.spell.bolt"; bolt.level = 0; bolt.range_ft = 120;
    bolt.kind = SpellEffectKind::SpellAttack; bolt.dice = DiceExpr{1, 1, 0}; bolt.damage_type = DamageType::Fire; bolt.fixed_attack = 40;
    book->add(bolt);
    SpellDef feat = *book->find(kCureWounds); feat.id = kHealFeature; feat.content_id = "test.feature.heal"; feat.is_spell = false;
    book->add(feat);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id) {
    auto c = wizard(id);
    c.attacks = {Attack{"blade", 40, {1, 1, 2}, DamageType::Slashing, 5, false, false}};
    c.spells_known.insert(c.spells_known.end(), {kInvis, kInvisCast, kMark, kHex, kBolt, kCureWounds, kHealFeature});
    return c;
}
Combatant target(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 200; m.ac = 1;
    m.reaction_policy.fill(ReactionPolicy::Never);
    return m;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 7) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_req(uint32_t spell, CombatantId t, uint8_t lvl) {
    ActionRequest r{1, ActionKind::CastSpell, t}; r.spell_id = spell; r.spell_level = lvl; return r;
}
ActionRequest hit(CombatantId t) { return ActionRequest{1, ActionKind::Attack, t, 0, 0, false, true}; }
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
int healed(const std::vector<Event>& ev) {
    for (const auto& e : ev) if (e.kind == EventKind::Healed) return e.a;
    return -1;
}
void next_turn(EncounterState& s) { (void)step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1); }
const ActiveModifier* mod_of(const EncounterState& s, uint32_t id) {
    for (const auto& m : s.modifiers) if (m.spec.id == id) return &m;
    return nullptr;
}

}  // namespace

TEST_CASE("7.2zc heal_bonus (Disciple of Life): + flat + slot level per creature healed by a slot spell") {
    auto heal_with = [](bool bonus, bool per_level, uint32_t spell, uint8_t lvl, bool innate) {
        auto c = caster(1);
        c.has_heal_bonus = bonus; c.heal_bonus_flat = 2; c.heal_bonus_per_level = per_level;
        if (innate) { InnateSpell is; is.spell_id = spell; is.uses_max = 1; is.uses_left = 1; c.innate = {is}; }
        auto ally = pc(3, 200); ally.hp = 1;
        auto s = start({c, target(2), ally});
        auto rq = cast_req(spell, 3, lvl); rq.innate = innate;
        auto r = step(s, rq);
        REQUIRE(r.ok);
        return healed(r.events);
    };
    // same seed, same dice: the difference is the bonus
    CHECK(heal_with(true, true, kCureWounds, 1, false) - heal_with(false, true, kCureWounds, 1, false) == 3);
    CHECK(heal_with(true, true, kCureWounds, 3, false) - heal_with(false, true, kCureWounds, 3, false) == 5);
    CHECK(heal_with(true, false, kCureWounds, 3, false) - heal_with(false, false, kCureWounds, 3, false) == 2);
    CHECK(heal_with(true, true, kHealFeature, 1, false) == heal_with(false, true, kHealFeature, 1, false));   // not a spell
    CHECK(heal_with(true, true, kCureWounds, 1, true) == heal_with(false, true, kCureWounds, 1, true));       // innate: no slot
}

TEST_CASE("7.2zc ends_on: Invisibility survives its own cast, ends after the subject's attack; concentration ends with the last") {
    auto s = start({caster(1), target(2), pc(3, 30)});
    ActionRequest rq = cast_req(kInvis, 1, 2); rq.targets = {1, 3};
    auto r = step(s, rq);
    REQUIRE(r.ok);
    CHECK(s.combatants[0].has(Condition::Invisible));
    CHECK(s.combatants[0].concentration_spell == kInvis);
    next_turn(s);
    auto a = step(s, hit(2));
    REQUIRE(a.ok);
    CHECK_FALSE(s.combatants[0].has(Condition::Invisible));
    CHECK(mut(s, 3).has(Condition::Invisible));   // the other target keeps it
    CHECK(s.combatants[0].concentration_spell == kInvis);
    bool ended = false;
    for (const auto& e : a.events) ended = ended || (e.kind == EventKind::SpellEffectEnded && e.actor == 1 && e.a == int32_t(kInvis));
    CHECK(ended);
    // the ally attacks: its effect ends, nothing of the spell is left: the caster's concentration ends too
    end_turns_until(s, 3);
    auto b = step(s, ActionRequest{3, ActionKind::Attack, 2, 0, 0, false, true});
    REQUIRE(b.ok);
    CHECK_FALSE(mut(s, 3).has(Condition::Invisible));
    CHECK(s.combatants[0].concentration_spell == 0);
    int conc = 0; for (const auto& e : b.events) conc += e.kind == EventKind::ConcentrationEnded;
    CHECK(conc == 1);
}

TEST_CASE("7.2zc ends_on cast only: an attack keeps it, casting any spell ends it after the spell resolves") {
    auto s = start({caster(1), target(2)});
    ActionRequest rq = cast_req(kInvisCast, 1, 2); rq.targets = {1};
    REQUIRE(step(s, rq).ok);
    next_turn(s);
    REQUIRE(step(s, hit(2)).ok);
    CHECK(s.combatants[0].has(Condition::Invisible));
    next_turn(s);
    auto r = step(s, cast_req(kCureWounds, 1, 1));   // a heal on itself: any spell, whatever the target
    REQUIRE(r.ok);
    CHECK_FALSE(s.combatants[0].has(Condition::Invisible));
    CHECK(s.combatants[0].concentration_spell == 0);
    std::size_t resolved = 0, removed = 0;
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        if (r.events[i].kind == EventKind::SpellResolved) resolved = i;
        if (r.events[i].kind == EventKind::ConditionRemoved && r.events[i].a == int32_t(Condition::Invisible)) removed = i;
    }
    CHECK(removed > resolved);
}

TEST_CASE("7.2zc mark: Hunter's Mark adds its die to weapon hits on the marked creature only") {
    auto s = start({caster(1), target(2), target(3)});
    REQUIRE(step(s, cast_req(kMark, 2, 1)).ok);
    const ActiveModifier* m = mod_of(s, spell_id_of("test.spell.hunters_mark#modifier"));
    REQUIRE(m);
    CHECK(m->target == 1);
    CHECK(m->mark == 2);
    auto a = step(s, hit(2));
    REQUIRE(a.ok);
    CHECK(dealt_to(a.events, 2) >= 8);   // 3 (or 4 on a crit) + 5 (or 10)
    next_turn(s);
    auto b = step(s, hit(3));
    REQUIRE(b.ok);
    CHECK(dealt_to(b.events, 3) <= 4);
    next_turn(s);
    auto c = step(s, cast_req(kBolt, 2, 0));   // a spell attack: Hunter's Mark is weapon_attack only
    REQUIRE(c.ok);
    CHECK(dealt_to(c.events, 2) <= 2);
}

TEST_CASE("7.2zc mark: cast-time checks (one creature other than the caster, mark_ability only for a chosen ability)") {
    auto s = start({caster(1), target(2), target(3)});
    CHECK_FALSE(step(s, cast_req(kMark, 1, 1)).ok);
    auto two = cast_req(kMark, 2, 1); two.targets = {2, 3};
    CHECK_FALSE(step(s, two).ok);
    auto with_ability = cast_req(kMark, 2, 1); with_ability.mark_ability = 2;
    CHECK_FALSE(step(s, with_ability).ok);
    CHECK_FALSE(step(s, cast_req(kHex, 2, 1)).ok);
    auto hex_bad = cast_req(kHex, 2, 1); hex_bad.mark_ability = 7;
    CHECK_FALSE(step(s, hex_bad).ok);
    CHECK(s.budget.bonus_action);
    CHECK(s.combatants[0].spell_slots[1] == 4);
}

TEST_CASE("7.2zc mark: Hex marks any attack's damage and the chosen check; the mark moves after the creature drops to 0") {
    auto s = start({caster(1), target(2), target(3)});
    auto rq = cast_req(kHex, 2, 1); rq.mark_ability = 3;   // Con
    REQUIRE(step(s, rq).ok);
    const uint32_t dmg_id = spell_id_of("test.spell.hex#modifier"), chk_id = spell_id_of("test.spell.hex#modifier1");
    const ActiveModifier* chk = mod_of(s, chk_id);
    REQUIRE(chk);
    CHECK(chk->target == 2); CHECK(chk->mark == 2);
    CHECK(chk->spec.has_ability); CHECK(chk->spec.ability == Ability::Con);
    auto b = step(s, cast_req(kBolt, 2, 0));   // spell attacks count for Hex
    REQUIRE(b.ok);
    CHECK(dealt_to(b.events, 2) >= 6);
    next_turn(s);
    ActionRequest mv{1, ActionKind::CastSpell, 3}; mv.spell_id = kHex; mv.move_mark = true;
    CHECK_FALSE(step(s, mv).ok);   // the marked creature still stands
    mut(s, 2).hp = 0;
    auto bad = mv; bad.target = 2;
    CHECK_FALSE(step(s, bad).ok);   // not the old mark
    auto moved = step(s, mv);
    REQUIRE(moved.ok);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.combatants[0].spell_slots[1] == 3);   // the cast's slot only
    int expired = 0, applied = 0, casts = 0;
    for (const auto& e : moved.events) {
        expired += e.kind == EventKind::ModifierExpired && e.b == 10;
        applied += e.kind == EventKind::ModifierApplied;
        casts += e.kind == EventKind::SpellCast;
    }
    CHECK(expired == 2); CHECK(applied == 2); CHECK(casts == 0);
    REQUIRE(mod_of(s, dmg_id));
    CHECK(mod_of(s, dmg_id)->target == 1); CHECK(mod_of(s, dmg_id)->mark == 3);
    CHECK(mod_of(s, chk_id)->target == 3); CHECK(mod_of(s, chk_id)->mark == 3);
    CHECK(mod_of(s, chk_id)->spec.ability == Ability::Con);
    auto c = step(s, cast_req(kBolt, 3, 0));
    REQUIRE(c.ok);
    CHECK(dealt_to(c.events, 3) >= 6);
}

TEST_CASE("7.2zc move_mark: refused without SpellDef::move_mark, a mark, or concentration on the spell") {
    auto s = start({caster(1), target(2), target(3)});
    ActionRequest mv{1, ActionKind::CastSpell, 3}; mv.spell_id = kBolt; mv.move_mark = true;
    CHECK_FALSE(step(s, mv).ok);
    mv.spell_id = kMark;
    CHECK_FALSE(step(s, mv).ok);   // no concentration / mark yet
    REQUIRE(step(s, cast_req(kMark, 2, 1)).ok);
    mut(s, 2).hp = 0;
    next_turn(s);
    s.combatants[0].concentration_spell = 0;
    CHECK_FALSE(step(s, mv).ok);
    CHECK(s.budget.bonus_action);
}

TEST_CASE("7.2zc modifiers_digest folds the mark only when set") {
    auto s = start({caster(1), target(2), target(3)});
    REQUIRE(step(s, cast_req(kMark, 2, 1)).ok);
    const uint64_t marked = modifiers_digest(s);
    for (auto& m : s.modifiers) m.mark = 3;
    CHECK(modifiers_digest(s) != marked);
    for (auto& m : s.modifiers) { m.mark = 0; m.spec.mark_scope = 0; }
    const uint64_t plain = modifiers_digest(s);
    for (auto& m : s.modifiers) m.spec.mark_scope = 1;   // mark 0: nothing folded
    CHECK(modifiers_digest(s) == plain);
}

TEST_CASE("7.2zc replay: the same seed gives the same events (heal bonus, ends_on, mark and its move)") {
    auto run = [] {
        auto c = caster(1); c.has_heal_bonus = true; c.heal_bonus_flat = 2; c.heal_bonus_per_level = true;
        auto s = start({c, target(2), target(3)}, 31);
        std::vector<Event> all;
        auto put = [&](const StepResult& r) { all.insert(all.end(), r.events.begin(), r.events.end()); };
        auto hx = cast_req(kHex, 2, 1); hx.mark_ability = 5;
        put(step(s, hx));
        put(step(s, hit(2)));
        next_turn(s);
        mut(s, 2).hp = 0;
        ActionRequest mv{1, ActionKind::CastSpell, 3}; mv.spell_id = kHex; mv.move_mark = true;
        put(step(s, mv));
        put(step(s, hit(3)));
        next_turn(s);
        ActionRequest iv = cast_req(kInvis, 1, 2); iv.targets = {1};
        put(step(s, iv));
        next_turn(s);
        put(step(s, hit(3)));
        put(step(s, cast_req(kCureWounds, 1, 2)));
        return std::make_pair(all, modifiers_digest(s));
    };
    const auto a = run(), b = run();
    CHECK(a.second == b.second);
    REQUIRE(a.first.size() == b.first.size());
    for (std::size_t i = 0; i < a.first.size(); ++i) { CHECK(a.first[i].kind == b.first[i].kind); CHECK(a.first[i].a == b.first[i].a); }
}
