// Ward / mark follow-ups (Engine Core's #240 questions): Sanctuary's ward meets harmful spells from any caster but the warded
// creature (spell_harmful; areas still don't), Grapple / Shove meet it like attacks (kWardSpecialAttack alternates),
// meets_ward(state, request), and move_mark: listed by legal_actions once available, refused on the turn the mark dropped.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSanct = spell_id_of("test.ward.sanctuary");
const uint32_t kScare = spell_id_of("test.ward.scare");
const uint32_t kBall = spell_id_of("test.ward.ball");
const uint32_t kBless = spell_id_of("test.ward.bless");
const uint32_t kBane = spell_id_of("test.ward.bane");
const uint32_t kMark = spell_id_of("test.ward.hunters_mark");
const uint32_t kBolt = spell_id_of("test.ward.bolt");

SpellDef plain(uint32_t id, const char* cid) { SpellDef d; d.id = id; d.content_id = cid; d.level = 1; d.range_ft = 60; return d; }
EncounterHooks hooks(int8_t ward_dc) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef sa = plain(kSanct, "test.ward.sanctuary"); sa.casting_time = CastingTime::BonusAction;
    sa.kind = SpellEffectKind::ApplyCondition; sa.condition = Condition::Warded; sa.applies_condition = true;
    sa.duration = SpellDuration::Rounds; sa.duration_amount = 10; sa.expiry = 1; sa.duration_rounds = 10;
    sa.ends_on = kEndsOnAttack | kEndsOnHarmfulCast; sa.has_ward = true; sa.ward_save = Ability::Wis; sa.fixed_dc = ward_dc;
    book->add(sa);
    SpellDef sc = plain(kScare, "test.ward.scare"); sc.kind = SpellEffectKind::Save; sc.save_ability = Ability::Wis;
    sc.fixed_dc = 100; sc.applies_condition = true; sc.condition = Condition::Frightened;
    sc.duration = SpellDuration::Minutes; sc.duration_amount = 1;
    book->add(sc);
    SpellDef ball = plain(kBall, "test.ward.ball"); ball.kind = SpellEffectKind::Save; ball.save_ability = Ability::Dex;
    ball.fixed_dc = 100; ball.dice = DiceExpr{1, 1, 0}; ball.damage_type = DamageType::Fire; ball.area = AreaShape::Sphere;
    ball.area_size_ft = 20;
    book->add(ball);
    SpellDef bl = plain(kBless, "test.ward.bless"); bl.kind = SpellEffectKind::ApplyModifier; bl.concentration = true;
    bl.modifier.id = spell_id_of("test.ward.bless#modifier"); bl.modifier.stat = ModStat::AttackRoll; bl.modifier.die = DiceExpr{1, 4, 0};
    book->add(bl);
    SpellDef bn = bl; bn.id = kBane; bn.content_id = "test.ward.bane"; bn.modifier.id = spell_id_of("test.ward.bane#modifier");
    bn.modifier.die_mult = -1;
    book->add(bn);
    SpellDef hm = plain(kMark, "test.ward.hunters_mark"); hm.casting_time = CastingTime::BonusAction; hm.concentration = true;
    hm.duration = SpellDuration::Hours; hm.duration_amount = 1; hm.kind = SpellEffectKind::ApplyModifier; hm.move_mark = 1;
    hm.modifier.id = spell_id_of("test.ward.hunters_mark#modifier"); hm.modifier.stat = ModStat::Damage;
    hm.modifier.die = DiceExpr{1, 1, 0}; hm.modifier.applies_to = ModAppliesTo::Self; hm.modifier.concentration = true;
    hm.modifier.mark_scope = 1;
    book->add(hm);
    SpellDef bolt = plain(kBolt, "test.ward.bolt"); bolt.level = 0; bolt.kind = SpellEffectKind::SpellAttack;
    bolt.dice = DiceExpr{1, 1, 50}; bolt.damage_type = DamageType::Fire; bolt.fixed_attack = 40;
    book->add(bolt);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id, uint8_t team = 0) {
    auto c = wizard(id, team);
    c.hp = c.hp_max = 40;
    c.attacks = {Attack{"blade", 40, {1, 1, 0}, DamageType::Slashing, 5, false, false}};
    c.spells_known.insert(c.spells_known.end(), {kSanct, kScare, kBall, kBless, kBane, kMark, kBolt, kCureWounds});
    never_react(c);
    return c;
}
Combatant brute(CombatantId id, int16_t hp = 200) {
    auto m = monster(id);
    m.hp = m.hp_max = hp; m.ac = 1;
    never_react(m);
    return m;
}
EncounterState start(std::vector<Combatant> cs, int8_t ward_dc = 100) {
    auto s = begin_encounter(std::move(cs), 7, nullptr, hooks(ward_dc));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(CombatantId a, uint32_t spell, CombatantId t, uint8_t lvl = 1) {
    ActionRequest r{a, ActionKind::CastSpell, t}; r.spell_id = spell; r.spell_level = lvl; return r;
}
ActionRequest grapple(CombatantId a, CombatantId t, CombatantId alt = 0, uint8_t special = 1) {
    ActionRequest r{a, ActionKind::Attack, t, 0, 0, false, true}; r.special = special; r.ward_alternate = alt; return r;
}
int ward_saves(const std::vector<Event>& ev) {
    int n = 0; for (const auto& e : ev) n += e.kind == EventKind::SavingThrowRolled && (e.c & 4) != 0; return n;
}
void to_turn(EncounterState& s, CombatantId id) { (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn}); end_turns_until(s, id); }
int move_marks(const EncounterState& s) {
    const auto la = legal_actions(s);
    return int(std::count_if(la.begin(), la.end(), [](const ActionRequest& r) { return r.kind == ActionKind::CastSpell && r.move_mark; }));
}

}  // namespace

TEST_CASE("ward follow-ups: spell_harmful (damage, a save, a negative condition or modifier)") {
    auto h = hooks(100);
    CHECK(spell_harmful(*h.spells->find(kScare)));
    CHECK(spell_harmful(*h.spells->find(kBolt)));
    CHECK(spell_harmful(*h.spells->find(kBane)));
    CHECK_FALSE(spell_harmful(*h.spells->find(kBless)));
    CHECK_FALSE(spell_harmful(*h.spells->find(kSanct)));
    CHECK_FALSE(spell_harmful(*h.spells->find(kCureWounds)));
    CHECK_FALSE(spell_harmful(*h.spells->find(kMark)));   // its row is on the caster (applies_to self)
}

TEST_CASE("ward follow-ups: an ally's harmful spell meets the ward; anyone's helpful spell and areas do not") {
    auto s = start({caster(1), caster(2), caster(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kSanct, 3)).ok);
    to_turn(s, 2);
    auto r = step(s, cast_at(2, kScare, 3));   // same team, harmful: the ward save (failed: lost)
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK_FALSE(mut(s, 3).has(Condition::Frightened));
    to_turn(s, 2);
    r = step(s, cast_at(2, kBane, 3));   // a negative modifier
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    to_turn(s, 2);
    r = step(s, cast_at(2, kBless, 3));
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 0);
    to_turn(s, 2);
    r = step(s, cast_at(2, kBall, 3));
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 0);
    to_turn(s, 3);
    r = step(s, cast_at(3, kCureWounds, 3));   // the warded creature itself
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 0);
}

TEST_CASE("ward follow-ups: meets_ward for attacks, Grapple / Shove and harmful single-target spells") {
    auto s = start({caster(1), caster(2), caster(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kSanct, 3)).ok);
    to_turn(s, 4);
    std::size_t ward = s.effects.size();
    for (std::size_t i = 0; i < s.effects.size(); ++i) if (s.effects[i].condition == Condition::Warded) ward = i;
    REQUIRE(ward < s.effects.size());
    const ActionRequest atk{4, ActionKind::Attack, 3, 0, 0, false, true};
    CHECK(meets_ward(s, atk) == std::optional<std::size_t>(ward));
    CHECK(meets_ward(s, grapple(4, 3)) == std::optional<std::size_t>(ward));
    CHECK(meets_ward(s, grapple(4, 3, 0, 2)) == std::optional<std::size_t>(ward));
    CHECK_FALSE(meets_ward(s, grapple(4, 3, 0, 4)));   // Escape
    CHECK_FALSE(meets_ward(s, ActionRequest{4, ActionKind::Attack, 1, 0, 0, false, true}));
    CHECK(meets_ward(s, cast_at(4, kScare, 3)) == std::optional<std::size_t>(ward));
    CHECK(meets_ward(s, cast_at(2, kScare, 3)) == std::optional<std::size_t>(ward));   // any side
    CHECK_FALSE(meets_ward(s, cast_at(4, kBall, 3)));
    CHECK_FALSE(meets_ward(s, cast_at(4, kCureWounds, 3)));
    CHECK_FALSE(meets_ward(s, cast_at(3, kScare, 3)));   // the warded creature itself
    // side-agnostic, sorted alternates (attack and Grapple / Shove)
    CHECK(ward_alternate_targets(s, 4, 3, 0, 0) == std::vector<CombatantId>{1, 2});
    CHECK(ward_alternate_targets(s, 4, 3, 0, kWardSpecialAttack) == std::vector<CombatantId>{1, 2});
}

TEST_CASE("ward follow-ups: Grapple / Shove meet the ward like attacks (retarget or lose)") {
    auto s = start({caster(1), caster(2), caster(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kSanct, 3)).ok);
    to_turn(s, 4);
    CHECK(std::string(step(s, grapple(4, 1, 2)).error) == "ward_alternate: the target is not warded");
    CHECK(std::string(step(s, grapple(4, 3, 3)).error) == "ward_alternate: not a legal alternate target");
    auto r = step(s, grapple(4, 3));   // failed save, no alternate: lost
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK_FALSE(mut(s, 3).has(Condition::Grappled));
    CHECK_FALSE(step(s, grapple(4, 3)).ok);   // the attack was spent
    to_turn(s, 4);
    r = step(s, grapple(4, 3, 2, 2));   // Shove prone, retargeted
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK_FALSE(mut(s, 3).has(Condition::Prone));
}

TEST_CASE("mark follow-ups: move_mark is refused on the turn the mark dropped, listed on a later turn") {
    auto s = start({caster(1), brute(2, 5), brute(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kMark, 2)).ok);
    CHECK(move_marks(s) == 0);   // the mark still stands
    REQUIRE(step(s, cast_at(1, kBolt, 2, 0)).ok);   // drops it this turn
    REQUIRE(mut(s, 2).hp <= 0);
    ActionRequest mv{1, ActionKind::CastSpell, 3}; mv.spell_id = kMark; mv.move_mark = true;
    CHECK(move_marks(s) == 0);
    CHECK(std::string(step(s, mv).error) == "move_mark: the mark dropped this turn");
    to_turn(s, 1);
    CHECK(move_marks(s) == 2);   // creatures 3 and 4 (any living creature but the caster and the old mark)
    auto la = legal_actions(s);
    std::vector<CombatantId> ts;
    for (const auto& r : la) if (r.move_mark) { CHECK(r.spell_id == kMark); ts.push_back(r.target); }
    CHECK(ts == std::vector<CombatantId>{3, 4});
    REQUIRE(step(s, mv).ok);
    CHECK(move_marks(s) == 0);   // the bonus action is spent
}

TEST_CASE("ward follow-ups: Sanctuary's harmful_cast end follows the SRD: a spell that affects an enemy, harmful or not") {
    auto s = start({caster(1), caster(2), caster(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kSanct, 3)).ok);
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kBane, 2)).ok);    // affects only an ally: the ward stays
    CHECK(mut(s, 3).has(Condition::Warded));
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kMark, 4)).ok);    // Hunter's Mark: its rows are on the caster; the ward stays
    CHECK(mut(s, 3).has(Condition::Warded));
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kBless, 4)).ok);   // a helpful spell on an enemy: ends
    CHECK_FALSE(mut(s, 3).has(Condition::Warded));
    to_turn(s, 1);
    REQUIRE(step(s, cast_at(1, kSanct, 3)).ok);
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kCureWounds, 4)).ok);   // a heal on an enemy: ends
    CHECK_FALSE(mut(s, 3).has(Condition::Warded));
}
