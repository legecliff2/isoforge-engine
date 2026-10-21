// VERSION_BUMP_BATCH 7.2zd: spell batch 2 (T&C agreed shapes). Sanctuary (Condition::Warded, SpellDef::has_ward, ends_on
// harmful_cast, ActionRequest::ward_alternate, ward_alternate_targets), Preserve Life (FeatureSpec::split_*, check_heal_split,
// ActionRequest::heal_split), Protection from Evil and Good (ModAppliesTo::Attackers + ModifierSpec::from_types,
// SpellEffectKind::ConditionImmunityFrom) and Aid (SpellEffectKind::MaxHpBonus), plus replay determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSanct = spell_id_of("test.spell.sanctuary");
const uint32_t kPfeg = spell_id_of("test.spell.protection_from_evil_and_good");
const uint32_t kAid = spell_id_of("test.spell.aid");
const uint32_t kScare = spell_id_of("test.spell.scare");
const uint32_t kBall = spell_id_of("test.spell.ball");
const uint32_t kPreserve = spell_id_of("test.feature.preserve_life");
const uint32_t kChannel = spell_id_of("test.pool.channel_divinity");
const uint32_t kPfegMod = spell_id_of("test.spell.protection_from_evil_and_good#modifier");

SpellDef sanctuary(int8_t dc) {
    SpellDef d; d.id = kSanct; d.content_id = "test.spell.sanctuary"; d.level = 1; d.range_ft = 30;
    d.casting_time = CastingTime::BonusAction; d.kind = SpellEffectKind::ApplyCondition; d.condition = Condition::Warded;
    d.applies_condition = true; d.duration = SpellDuration::Rounds; d.duration_amount = 10; d.expiry = 1; d.duration_rounds = 10;
    d.ends_on = kEndsOnAttack | kEndsOnHarmfulCast; d.has_ward = true; d.ward_save = Ability::Wis; d.fixed_dc = dc;
    return d;
}
EncounterHooks hooks(int8_t ward_dc = 100) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(sanctuary(ward_dc));
    SpellDef p; p.id = kPfeg; p.content_id = "test.spell.protection_from_evil_and_good"; p.level = 1; p.range_ft = 5;
    p.concentration = true; p.duration = SpellDuration::Rounds; p.duration_amount = 100; p.expiry = 1; p.duration_rounds = 100;
    p.kind = SpellEffectKind::ApplyModifier;
    p.modifier.id = kPfegMod; p.modifier.stat = ModStat::AttackRoll; p.modifier.mode = RollMode::Disadvantage;
    p.modifier.applies_to = ModAppliesTo::Attackers; p.modifier.from_types = bit(CreatureType::Fiend) | bit(CreatureType::Undead);
    SpellEffect imm; imm.kind = SpellEffectKind::ConditionImmunityFrom;
    imm.immune_conditions = bit(Condition::Charmed) | bit(Condition::Frightened);
    imm.immune_from_types = bit(CreatureType::Fiend) | bit(CreatureType::Undead);
    p.extra_effects = {imm};
    book->add(p);
    SpellDef a; a.id = kAid; a.content_id = "test.spell.aid"; a.level = 2; a.range_ft = 30; a.targets = 3;
    a.duration = SpellDuration::Rounds; a.duration_amount = 4800; a.expiry = 1; a.duration_rounds = 4800;
    a.kind = SpellEffectKind::MaxHpBonus; a.hp_bonus_flat = 5; a.hp_bonus_per_slot = 5;
    book->add(a);
    SpellDef sc; sc.id = kScare; sc.content_id = "test.spell.scare"; sc.level = 1; sc.range_ft = 60;
    sc.kind = SpellEffectKind::Save; sc.save_ability = Ability::Wis; sc.fixed_dc = 100; sc.applies_condition = true;
    sc.condition = Condition::Frightened; sc.duration = SpellDuration::Minutes; sc.duration_amount = 1;
    book->add(sc);
    SpellDef ball; ball.id = kBall; ball.content_id = "test.spell.ball"; ball.level = 1; ball.range_ft = 150;
    ball.kind = SpellEffectKind::Save; ball.save_ability = Ability::Dex; ball.fixed_dc = 100; ball.dice = DiceExpr{1, 1, 0};
    ball.damage_type = DamageType::Fire; ball.area = AreaShape::Sphere; ball.area_size_ft = 20;
    book->add(ball);
    h.spells = book;
    h.feature_spec = [](const EncounterState&, const Combatant&, uint32_t f) -> std::optional<FeatureSpec> {
        if (f != kPreserve) return std::nullopt;
        FeatureSpec fs; fs.pool = kChannel; fs.pool_cost = 1; fs.cost = 1;
        fs.split_pool = 10; fs.split_range_ft = 30; fs.split_cap_half = true;
        fs.split_excluded = bit(CreatureType::Undead) | bit(CreatureType::Construct);
        return fs;
    };
    h.targets_within = [](const EncounterState&, const std::vector<CombatantId>& ids, int) {
        return std::find(ids.begin(), ids.end(), CombatantId(9)) == ids.end();   // creature 9 is always too far
    };
    return h;
}
Combatant caster(CombatantId id, uint8_t team = 0) {
    auto c = wizard(id, team);
    c.hp = c.hp_max = 40;
    c.attacks = {Attack{"blade", 40, {1, 1, 0}, DamageType::Slashing, 5, false, false}};
    c.spells_known.insert(c.spells_known.end(), {kSanct, kPfeg, kAid, kScare, kBall, kCureWounds});
    never_react(c);
    return c;
}
Combatant brute(CombatantId id, CreatureType type = CreatureType::Beast) {
    auto m = monster(id);
    m.hp = m.hp_max = 200; m.ac = 1; m.creature_type = type;
    m.attacks = {Attack{"claw", 40, {1, 1, 0}, DamageType::Slashing, 5, false, false}};
    never_react(m);
    return m;
}
EncounterState start(std::vector<Combatant> cs, int8_t ward_dc = 100, uint64_t seed = 7) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(ward_dc));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(CombatantId a, uint32_t spell, std::vector<CombatantId> ts, uint8_t lvl) {
    ActionRequest r{a, ActionKind::CastSpell, ts.empty() ? 0 : ts[0]}; r.spell_id = spell; r.spell_level = lvl;
    if (ts.size() > 1) r.targets = ts;
    return r;
}
ActionRequest attack(CombatantId a, CombatantId t, CombatantId alt = 0) {
    ActionRequest r{a, ActionKind::Attack, t, 0, 0, false, true}; r.ward_alternate = alt; return r;
}
const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
int ward_saves(const std::vector<Event>& ev) {
    int n = 0; for (const auto& e : ev) n += e.kind == EventKind::SavingThrowRolled && (e.c & 4) != 0; return n;
}
void to_turn(EncounterState& s, CombatantId id) { (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn}); end_turns_until(s, id); }

}  // namespace

TEST_CASE("7.2zd Sanctuary: a failed ward save retargets an attack to a legal alternate, or loses it") {
    auto s = start({caster(1), brute(2), caster(3), brute(4)});
    REQUIRE(step(s, cast_at(1, kSanct, {3}, 1)).ok);
    CHECK(mut(s, 3).has(Condition::Warded));
    to_turn(s, 2);
    CHECK(ward_alternate_targets(s, 2, 3, 0, 0) == std::vector<CombatantId>{1, 4});
    CHECK(std::string(step(s, attack(2, 3, 3)).error) == "ward_alternate: not a legal alternate target");
    CHECK(std::string(step(s, attack(2, 1, 4)).error) == "ward_alternate: the target is not warded");
    auto r = step(s, attack(2, 3, 1));
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    REQUIRE(first(r.events, EventKind::AttackResolved));
    CHECK(first(r.events, EventKind::AttackResolved)->target == 1);
    to_turn(s, 2);
    r = step(s, attack(2, 3));   // no alternate: the attack is lost (the action is spent)
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK(first(r.events, EventKind::AttackResolved) == nullptr);
    CHECK(!step(s, attack(2, 3)).ok);
    CHECK(mut(s, 3).has(Condition::Warded));
}

TEST_CASE("7.2zd Sanctuary: a successful save attacks the warded creature; the ward's own subject is not stopped") {
    auto s = start({caster(1), brute(2), caster(3)}, 1);
    REQUIRE(step(s, cast_at(1, kSanct, {3}, 1)).ok);
    to_turn(s, 2);
    auto r = step(s, attack(2, 3));
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK(first(r.events, EventKind::AttackResolved)->target == 3);
}

TEST_CASE("7.2zd Sanctuary: harmful targeted spells meet the ward; areas and allies' spells do not") {
    auto s = start({caster(1), caster(2, 1), caster(3)});
    REQUIRE(step(s, cast_at(1, kSanct, {3}, 1)).ok);
    to_turn(s, 2);
    auto r = step(s, cast_at(2, kScare, {3}, 1));   // lost: no target left, SpellResolved still closes the cast
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 1);
    CHECK(!mut(s, 3).has(Condition::Frightened));
    CHECK(first(r.events, EventKind::SpellResolved) != nullptr);
    to_turn(s, 2);
    auto alt = cast_at(2, kScare, {3}, 1); alt.ward_alternate = 1;
    r = step(s, alt);
    REQUIRE(r.ok);
    CHECK(mut(s, 1).has(Condition::Frightened));
    to_turn(s, 2);
    r = step(s, cast_at(2, kBall, {3}, 1));   // an area: no ward save
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 0);
    to_turn(s, 1);
    r = step(s, cast_at(1, kCureWounds, {3}, 1));   // an ally's spell: no ward save
    REQUIRE(r.ok);
    CHECK(ward_saves(r.events) == 0);
}

TEST_CASE("7.2zd Sanctuary ends on the subject's attack or harmful cast, not a helpful one") {
    auto s = start({caster(1), brute(2), caster(3)});
    REQUIRE(step(s, cast_at(1, kSanct, {3}, 1)).ok);
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kCureWounds, {1}, 1)).ok);
    CHECK(mut(s, 3).has(Condition::Warded));
    to_turn(s, 3);
    REQUIRE(step(s, cast_at(3, kScare, {2}, 1)).ok);
    CHECK(!mut(s, 3).has(Condition::Warded));
    to_turn(s, 1);
    REQUIRE(step(s, cast_at(1, kSanct, {3}, 1)).ok);
    to_turn(s, 3);
    REQUIRE(step(s, attack(3, 2)).ok);
    CHECK(!mut(s, 3).has(Condition::Warded));
}

TEST_CASE("7.2zd Protection from Evil and Good: attackers of the listed types roll with disadvantage") {
    int lower = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        auto nat = [&](CreatureType ty, bool protect) {
            auto s = start({caster(1), brute(2, ty)}, 100, seed);
            if (protect) REQUIRE(step(s, cast_at(1, kPfeg, {1}, 1)).ok);
            auto& m = mut(s, 2); (void)m;
            to_turn(s, 2);
            auto r = step(s, attack(2, 1));
            REQUIRE(first(r.events, EventKind::AttackResolved));
            return first(r.events, EventKind::AttackResolved)->a;
        };
        const int plain = nat(CreatureType::Undead, false), undead = nat(CreatureType::Undead, true), beast = nat(CreatureType::Beast, true);
        CHECK(undead <= plain);
        CHECK(beast == plain);
        lower += undead < plain;
    }
    CHECK(lower > 0);
}

TEST_CASE("7.2zd Protection from Evil and Good: the row never touches the protected creature's own attacks") {
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        auto nat = [&](bool protect) {
            auto s = start({caster(1), brute(2, CreatureType::Undead)}, 100, seed);
            if (protect) { REQUIRE(step(s, cast_at(1, kPfeg, {1}, 1)).ok); to_turn(s, 1); }
            else to_turn(s, 1);
            auto r = step(s, attack(1, 2));
            return first(r.events, EventKind::AttackResolved)->a;
        };
        CHECK(nat(true) == nat(false));
    }
}

TEST_CASE("7.2zd condition_immunity_from: no charm or fear from the listed types while the spell lasts") {
    auto undead = caster(2, 1); undead.creature_type = CreatureType::Undead;
    auto beast = caster(4, 1); beast.creature_type = CreatureType::Beast;
    auto s = start({caster(1), undead, caster(3), beast});
    REQUIRE(step(s, cast_at(1, kPfeg, {3}, 1)).ok);
    to_turn(s, 2);
    REQUIRE(step(s, cast_at(2, kScare, {3}, 1)).ok);
    CHECK(!mut(s, 3).has(Condition::Frightened));
    to_turn(s, 4);
    REQUIRE(step(s, cast_at(4, kScare, {3}, 1)).ok);
    CHECK(mut(s, 3).has(Condition::Frightened));
}

TEST_CASE("7.2zd Aid: up to three targets, + 5 per slot level above 2; ends drop the maximum and clamp current HP") {
    auto down = caster(5); down.hp = 0; make_dying(down);
    auto s = start({caster(1), brute(2), caster(3), caster(4), down});
    CHECK(std::string(step(s, cast_at(1, kAid, {1, 3, 4, 5}, 2)).error) == "too many targets");
    auto r = step(s, cast_at(1, kAid, {1, 3, 5}, 3));
    REQUIRE(r.ok);
    CHECK(mut(s, 1).hp_max == 50); CHECK(mut(s, 1).hp == 50);
    CHECK(mut(s, 5).hp == 10); CHECK(!mut(s, 5).has(Condition::Unconscious));
    to_turn(s, 1);
    REQUIRE(step(s, cast_at(1, kAid, {1}, 2)).ok);   // a lower bonus keeps the higher one
    CHECK(mut(s, 1).hp_max == 50);
    mut(s, 3).hp = 20;
    for (auto& e : s.effects) if (e.max_hp_bonus > 0) e.timed.rounds_left = 1;
    to_turn(s, 1);
    to_turn(s, 1);
    CHECK(mut(s, 1).hp_max == 40); CHECK(mut(s, 1).hp == 40);
    CHECK(mut(s, 3).hp_max == 40); CHECK(mut(s, 3).hp == 20);
    CHECK(std::none_of(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.max_hp_bonus > 0; }));
}

TEST_CASE("7.2zd Preserve Life: check_heal_split gives the failing index and reason; a valid split heals and spends the pool") {
    auto hurt = [](CombatantId id, int hp, CreatureType ty = CreatureType::Humanoid) {
        auto c = caster(id); c.hp_max = 20; c.hp = int16_t(hp); c.creature_type = ty; return c;
    };
    auto s = start({caster(1), brute(2), hurt(3, 4), hurt(5, 9), hurt(6, 10), hurt(7, 2, CreatureType::Undead), hurt(9, 1)});
    register_pool(s, 1, kChannel, 1, 1);
    const FeatureSpec fs = *s.hooks.feature_spec(s, mut(s, 1), kPreserve);
    auto chk = [&](std::vector<HealShare> v) { return check_heal_split(s, mut(s, 1), fs, v); };
    auto is = [](HealSplitCheck c, int i, HealSplitReason r) { return c.index == i && c.reason == r; };
    CHECK(is(chk({{3, 6}, {5, 1}}), -1, HealSplitReason::None));
    CHECK(is(chk({{3, 6}, {5, 2}}), 1, HealSplitReason::OverCap));
    CHECK(is(chk({{6, 1}}), 0, HealSplitReason::OverCap));        // already at half: nothing
    CHECK(is(chk({{3, 1}, {7, 1}}), 1, HealSplitReason::ExcludedType));
    CHECK(is(chk({{9, 1}}), 0, HealSplitReason::OutOfRange));
    CHECK(is(chk({{3, 1}, {3, 1}}), 1, HealSplitReason::BadTarget));
    CHECK(is(chk({{3, 0}}), 0, HealSplitReason::BadTarget));
    FeatureSpec small = fs; small.split_pool = 6;
    CHECK(check_heal_split(s, mut(s, 1), small, {{3, 6}, {5, 1}}).index == 1);
    CHECK(check_heal_split(s, mut(s, 1), small, {{3, 6}, {5, 1}}).reason == HealSplitReason::OverPool);
    ActionRequest u{1, ActionKind::UseFeature}; u.feature_id = kPreserve;
    CHECK(std::string(step(s, u).error) == "heal_split required");
    u.heal_split = {{3, 6}, {5, 2}};
    CHECK(std::string(step(s, u).error) == "heal_split: over the cap (half the hit point maximum)");
    u.heal_split = {{3, 6}, {5, 1}};
    auto r = step(s, u);
    REQUIRE(r.ok);
    CHECK(mut(s, 3).hp == 10); CHECK(mut(s, 5).hp == 10);
    CHECK(find_pool(s, 1, kChannel)->cur == 0);
    to_turn(s, 1);
    CHECK(std::string(step(s, u).error) == "not enough resource");   // Channel Divinity spent
}

TEST_CASE("7.2zd modifiers_digest folds applies_to attackers rows only") {
    auto s = start({caster(1), brute(2)});
    REQUIRE(step(s, cast_at(1, kPfeg, {1}, 1)).ok);
    const uint64_t with = modifiers_digest(s);
    for (auto& m : s.modifiers) m.spec.from_types = 0;
    CHECK(modifiers_digest(s) != with);
}

TEST_CASE("7.2zd replay: the same seed gives the same events (ward, Aid, PfEG, split heal)") {
    auto run = [] {
        auto s = start({caster(1), brute(2, CreatureType::Fiend), caster(3)}, 100, 41);
        register_pool(s, 1, kChannel, 1, 1);
        std::vector<Event> all;
        auto put = [&](const StepResult& r) { all.insert(all.end(), r.events.begin(), r.events.end()); };
        put(step(s, cast_at(1, kSanct, {3}, 1)));
        put(step(s, cast_at(1, kPfeg, {1}, 1)));
        to_turn(s, 2);
        put(step(s, attack(2, 3, 1)));
        to_turn(s, 1);
        put(step(s, cast_at(1, kAid, {1, 3}, 2)));
        mut(s, 3).hp = 2;
        to_turn(s, 1);
        ActionRequest u{1, ActionKind::UseFeature}; u.feature_id = kPreserve; u.heal_split = {{3, 8}};
        put(step(s, u));
        return std::make_pair(all, modifiers_digest(s));
    };
    const auto a = run(), b = run();
    CHECK(a.second == b.second);
    REQUIRE(a.first.size() == b.first.size());
    for (std::size_t i = 0; i < a.first.size(); ++i) { CHECK(a.first[i].kind == b.first[i].kind); CHECK(a.first[i].a == b.first[i].a); }
}
