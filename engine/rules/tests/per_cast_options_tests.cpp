// R-G14: per-cast options (Metamagic, Sculpt Spells, cast-time damage type / condition choices).
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kFireball = spell_id_of("test.spell#fireball");
const uint32_t kOrb = spell_id_of("test.spell#chromatic_orb");
const uint32_t kBlindDeaf = spell_id_of("test.spell#blindness_deafness");
const uint32_t kLongHold = spell_id_of("test.spell#long_hold");
constexpr uint32_t kSorcery = 900;
constexpr uint8_t kAllMeta = 0xFF;

struct Seen { uint16_t range = 0; uint8_t components = 0; };

EncounterHooks hooks(const std::shared_ptr<Seen>& seen) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef fb; fb.id = kFireball; fb.content_id = "test.spell#fireball"; fb.level = 3; fb.range_ft = 150;
    fb.kind = SpellEffectKind::Save; fb.save_ability = Ability::Dex; fb.on_save = OnSave::Half; fb.dice = DiceExpr{8, 6, 0};
    fb.damage_type = DamageType::Fire; fb.area = AreaShape::Sphere; fb.area_size_ft = 20; fb.evocation = true; fb.upcast_dice = 1;
    fb.components = kComponentV | kComponentS | kComponentM;
    book->add(fb);
    SpellDef orb; orb.id = kOrb; orb.content_id = "test.spell#chromatic_orb"; orb.level = 1; orb.range_ft = 90;
    orb.kind = SpellEffectKind::SpellAttack; orb.dice = DiceExpr{3, 8, 0}; orb.fixed_attack = 30;
    orb.damage_choices = {DamageType::Acid, DamageType::Cold, DamageType::Fire, DamageType::Lightning, DamageType::Poison,
                          DamageType::Thunder};
    book->add(orb);
    SpellDef bd; bd.id = kBlindDeaf; bd.content_id = "test.spell#blindness_deafness"; bd.level = 2; bd.range_ft = 30;
    bd.kind = SpellEffectKind::Save; bd.save_ability = Ability::Con; bd.applies_condition = true; bd.fixed_dc = 30;
    bd.condition_choices = {Condition::Blinded, Condition::Deafened};
    bd.expiry = 1; bd.duration_rounds = 10; bd.upcast_targets = 1; bd.duration = SpellDuration::Minutes; bd.duration_amount = 1;
    book->add(bd);
    SpellDef lh; lh.id = kLongHold; lh.content_id = "test.spell#long_hold"; lh.level = 2; lh.range_ft = 60;
    lh.kind = SpellEffectKind::Save; lh.save_ability = Ability::Wis; lh.applies_condition = true; lh.condition = Condition::Paralyzed;
    lh.fixed_dc = 30; lh.expiry = 1; lh.duration_rounds = 10; lh.upcast_targets = 1;
    lh.duration = SpellDuration::Minutes; lh.duration_amount = 1;
    book->add(lh);
    h.spells = book;
    h.spell_target_ok = [seen](const EncounterState&, const Combatant&, const SpellDef& d, const Combatant&) {
        seen->range = d.range_ft; return true; };
    h.can_provide_components = [seen](const EncounterState&, const Combatant&, const SpellDef& d) {
        seen->components = d.components; return true; };
    return h;
}
Combatant sorcerer(CombatantId id) {
    auto c = wizard(id);
    c.abilities.score[std::size_t(Ability::Cha)] = int8_t(16);   // +3
    c.spellcasting_ability = Ability::Cha;
    c.metamagic_known = kAllMeta; c.sorcery_pool = kSorcery;
    c.spells_known = {kFireBolt, kCureWounds, kHoldPerson, kFireball, kOrb, kBlindDeaf, kLongHold};
    return c;
}
Combatant tough(CombatantId id, uint8_t team) { auto m = monster(id); m.team = team; m.hp = m.hp_max = int16_t(300); return m; }
EncounterState start(std::vector<Combatant> cs, const std::shared_ptr<Seen>& seen, uint64_t seed = 1, uint16_t points = 5) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(seen));
    register_pool(s, 1, kSorcery, 10, points);
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_req(uint32_t spell, uint8_t level, std::vector<CombatantId> targets) {
    ActionRequest r{1, ActionKind::CastSpell, targets.empty() ? CombatantId(0) : targets[0]};
    r.spell_id = spell; r.spell_level = level; r.targets = std::move(targets);
    return r;
}
uint16_t points(const EncounterState& s) { return find_pool(s, 1, kSorcery)->cur; }
const Event* save_of(const std::vector<Event>& ev, CombatantId id) {
    for (const auto& e : ev) if (e.kind == EventKind::SavingThrowRolled && e.actor == id) return &e;
    return nullptr;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
}  // namespace

TEST_CASE("metamagic costs, defaults") {
    CHECK(metamagic_cost(kMetaCareful, 3) == 1); CHECK(metamagic_cost(kMetaDistant, 3) == 1);
    CHECK(metamagic_cost(kMetaEmpowered, 3) == 1); CHECK(metamagic_cost(kMetaExtended, 3) == 1);
    CHECK(metamagic_cost(kMetaSubtle, 3) == 1); CHECK(metamagic_cost(kMetaQuickened, 3) == 2);
    CHECK(metamagic_cost(kMetaHeightened, 3) == 3); CHECK(metamagic_cost(kMetaTwinned, 0) == 1);
    CHECK(metamagic_cost(kMetaTwinned, 2) == 2); CHECK(metamagic_cost(kMetaEmpowered | kMetaQuickened, 1) == 3);
    ActionRequest r{1, ActionKind::CastSpell};
    CHECK(r.metamagic == 0); CHECK(r.spared.empty()); CHECK(r.heighten_target == 0);
    CHECK(r.damage_choice == 0); CHECK(r.condition_choice == 0);
    Combatant c{};
    CHECK(c.metamagic_known == 0); CHECK(c.sorcery_pool == 0); CHECK_FALSE(c.sculpt_spells);
    SpellDef d; CHECK_FALSE(d.evocation); CHECK(d.damage_choices.empty()); CHECK(d.condition_choices.empty());
}

TEST_CASE("per-cast options are validated and a refusal changes nothing") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1), pc(3)}, seen, 1, 2);
    auto refused = [&](const ActionRequest& req, std::string_view why) {
        const auto slots = find(s, 1)->spell_slots; const uint16_t p = points(s);
        const auto res = step(s, req);
        CHECK(find(s, 1)->spell_slots == slots); CHECK(points(s) == p); CHECK(s.budget.action);
        INFO(res.error);
        return !res.ok && res.error.find(why) != std::string_view::npos;
    };
    auto r = cast_req(kHoldPerson, 2, {2});
    r.metamagic = kMetaHeightened; r.heighten_target = 2;
    CHECK(refused(r, "sorcery points"));                                          // 3 points, 2 left
    r = cast_req(kHoldPerson, 2, {2}); r.metamagic = kMetaQuickened | kMetaSubtle;
    CHECK(refused(r, "one metamagic option"));                                          // two non-Empowered options
    r = cast_req(kHoldPerson, 2, {2}); r.heighten_target = 2;
    CHECK(refused(r, "needs Heightened"));                                          // heighten_target without Heightened
    r = cast_req(kFireBolt, 0, {2}); r.metamagic = kMetaCareful; r.spared = {3};
    CHECK(refused(r, "Careful Spell needs a saving throw"));                                          // Careful needs a save
    r = cast_req(kFireball, 3, {2, 3}); r.metamagic = kMetaCareful; r.spared = {3, 2, 1, 4};
    CHECK(refused(r, "Careful Spell: 1 to"));                                          // more than Cha mod (3)
    r = cast_req(kFireball, 3, {2, 3}); r.spared = {3};
    CHECK(refused(r, "Careful Spell or Sculpt"));                                          // no Sculpt Spells
    r = cast_req(kOrb, 1, {2});
    CHECK(refused(r, "choose a damage type"));                                          // a damage type must be chosen
    r.damage_choice = 7;
    CHECK(refused(r, "bad damage choice"));                                          // out of range
    r = cast_req(kFireBolt, 0, {2}); r.damage_choice = 1;
    CHECK(refused(r, "bad damage choice"));                                          // no choices on this spell
    r = cast_req(kHoldPerson, 2, {2}); r.metamagic = kMetaQuickened;
    mut(s, 1).metamagic_known = kMetaCareful;
    CHECK(refused(r, "not known"));                                          // option not known
    mut(s, 1).metamagic_known = kAllMeta; mut(s, 1).sorcery_pool = 0;
    CHECK(refused(r, "sorcery points"));                                          // no pool
    mut(s, 1).sorcery_pool = kSorcery;
    r = cast_req(kCureWounds, 1, {1}); r.metamagic = kMetaExtended;
    mut(s, 1).spells_known.push_back(kCureWounds);
    CHECK(refused(r, "Extended Spell"));                                          // instantaneous: nothing to extend
    r = cast_req(kHoldPerson, 2, {2}); r.metamagic = kMetaTwinned;
    CHECK(refused(r, "two different targets"));                                          // Twinned needs two targets
    for (auto& pl : s.pools) pl.cur = 10;
    r = cast_req(kFireball, 3, {2, 3}); r.metamagic = kMetaTwinned;
    CHECK(refused(r, "Twinned Spell needs a spell"));                                          // an area spell cannot be twinned
}

TEST_CASE("Careful Spell: chosen creatures succeed automatically (still half damage); 1 point") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1), tough(3, 0)}, seen);
    auto r = cast_req(kFireball, 3, {2, 3}); r.metamagic = kMetaCareful; r.spared = {3};
    const auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK(points(s) == 4);
    CHECK(count(out.events, EventKind::ResourceSpent) == 1);
    const Event* sv = save_of(out.events, 3);
    REQUIRE(sv);
    CHECK(sv->a == 0); CHECK(sv->c == 1);
    CHECK(dealt_to(out.events, 3) >= 4);   // half of 8d6
    REQUIRE(save_of(out.events, 2));
    CHECK(save_of(out.events, 2)->a >= 1);
}

TEST_CASE("Sculpt Spells: up to 1 + level creatures take no damage from an evocation; no points") {
    auto seen = std::make_shared<Seen>();
    auto w = sorcerer(1); w.sculpt_spells = true; w.metamagic_known = 0; w.sorcery_pool = 0;
    auto s = start({w, tough(2, 1), tough(3, 0), tough(4, 0)}, seen);
    auto r = cast_req(kFireball, 3, {2, 3, 4}); r.spared = {3, 4};
    const auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK(dealt_to(out.events, 3) == 0); CHECK(dealt_to(out.events, 4) == 0);
    CHECK(find(s, 3)->hp == 300);
    CHECK(save_of(out.events, 3)->c == 1);
    CHECK(dealt_to(out.events, 2) > 0);
    CHECK(count(out.events, EventKind::ResourceSpent) == 0);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto big = cast_req(kFireball, 3, {2, 3}); big.spared = {3, 4, 5, 6, 7};
    CHECK_FALSE(step(s, big).ok);                 // 5 > 1 + 3
    auto hold = cast_req(kHoldPerson, 2, {2}); hold.spared = {3};
    mut(s, 1).spells_known.push_back(kHoldPerson);
    CHECK_FALSE(step(s, hold).ok);                // not an evocation
}

TEST_CASE("Heightened Spell: disadvantage on the target's first save; 3 points") {
    int lower = 0;
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto seen = std::make_shared<Seen>();
        auto a = start({sorcerer(1), tough(2, 1)}, seen, seed);
        auto b = start({sorcerer(1), tough(2, 1)}, seen, seed);
        auto plain = cast_req(kFireball, 3, {2});
        auto high = plain; high.metamagic = kMetaHeightened; high.heighten_target = 2;
        const auto x = step(a, plain);
        const auto y = step(b, high);
        REQUIRE(x.ok); REQUIRE(y.ok);
        CHECK(points(b) == 2);
        const int nx = save_of(x.events, 2)->a, ny = save_of(y.events, 2)->a;
        CHECK(ny <= nx);
        lower += ny < nx;
    }
    CHECK(lower > 0);
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1), tough(3, 1)}, seen);
    auto r = cast_req(kFireball, 3, {2}); r.metamagic = kMetaHeightened; r.heighten_target = 3;
    CHECK_FALSE(step(s, r).ok);   // not a target
}

TEST_CASE("Quickened Spell: an action spell as a bonus action; 2 points") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1)}, seen);
    auto r = cast_req(kFireBolt, 0, {2}); r.metamagic = kMetaQuickened;
    REQUIRE(step(s, r).ok);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);
    CHECK(points(s) == 3);
    CHECK_FALSE(step(s, r).ok);   // bonus action spent
}

TEST_CASE("Twinned Spell: a single-target spell at two creatures; cost = slot level (1 for a cantrip)") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1), tough(3, 1)}, seen);
    auto r = cast_req(kLongHold, 2, {2, 3}); r.metamagic = kMetaTwinned;
    const auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK(save_of(out.events, 2)); CHECK(save_of(out.events, 3));
    CHECK(find(s, 2)->has(Condition::Paralyzed)); CHECK(find(s, 3)->has(Condition::Paralyzed));
    CHECK(points(s) == 3);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto up = cast_req(kLongHold, 3, {2, 3}); up.metamagic = kMetaTwinned;
    CHECK_FALSE(step(s, up).ok);   // at 3rd level it already targets two
    auto bolt = cast_req(kFireBolt, 0, {2, 3}); bolt.metamagic = kMetaTwinned;
    const auto b = step(s, bolt);
    REQUIRE(b.ok);
    CHECK(count(b.events, EventKind::AttackResolved) == 2);
    CHECK(points(s) == 2);
}

TEST_CASE("Distant, Subtle, Extended: range, components and duration seen by the hooks and effects") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1), pc(3)}, seen);
    auto heal = cast_req(kCureWounds, 1, {3}); heal.metamagic = kMetaDistant;
    REQUIRE(step(s, heal).ok);
    CHECK(seen->range == 30);   // touch -> 30 ft
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto bolt = cast_req(kFireBolt, 0, {2}); bolt.metamagic = kMetaDistant | kMetaEmpowered;
    REQUIRE(step(s, bolt).ok);
    CHECK(seen->range == 240);
    CHECK(points(s) == 2);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto quiet = cast_req(kFireball, 3, {2}); quiet.metamagic = kMetaSubtle;
    REQUIRE(step(s, quiet).ok);
    CHECK(seen->components == kComponentM);
    CHECK(points(s) == 1);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto hold = cast_req(kLongHold, 2, {2}); hold.metamagic = kMetaExtended;
    REQUIRE(step(s, hold).ok);
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].timed.kind == Expiry::Rounds);
    CHECK(s.effects[0].timed.rounds_left == 20);
    CHECK(points(s) == 0);
}

TEST_CASE("Empowered Spell: rerolls low damage dice; deterministic; combines with another option") {
    int differs = 0;
    for (uint64_t seed = 1; seed < 20; ++seed) {
        auto seen = std::make_shared<Seen>();
        auto a = start({sorcerer(1), tough(2, 1)}, seen, seed);
        auto b = start({sorcerer(1), tough(2, 1)}, seen, seed);
        auto c = start({sorcerer(1), tough(2, 1)}, seen, seed);
        auto plain = cast_req(kFireball, 3, {2});
        auto emp = plain; emp.metamagic = kMetaEmpowered | kMetaCareful; emp.spared = {2};
        REQUIRE(step(a, plain).ok);
        REQUIRE(step(b, emp).ok);
        REQUIRE(step(c, emp).ok);
        CHECK(b.rng.save().state == c.rng.save().state);
        CHECK(find(b, 2)->hp == find(c, 2)->hp);
        differs += a.rng.save().state != b.rng.save().state;
        CHECK(points(b) == 3);
    }
    CHECK(differs > 0);
}

TEST_CASE("cast-time choices: Chromatic Orb's damage type, Blindness/Deafness's condition") {
    int hits = 0;
    for (uint64_t seed = 1; seed < 20; ++seed) {
        auto seen = std::make_shared<Seen>();
        auto t = tough(2, 1); t.immunities = bit(DamageType::Cold);
        auto s = start({sorcerer(1), t, tough(3, 1)}, seen, seed);
        auto cold = cast_req(kOrb, 1, {2}); cold.damage_choice = 2;   // Cold
        const auto x = step(s, cold);
        REQUIRE(x.ok);
        CHECK(find(s, 2)->hp == 300);
        end_turns_until(s, 2); end_turns_until(s, 1);
        auto fire = cast_req(kOrb, 1, {2}); fire.damage_choice = 3;   // Fire
        const auto y = step(s, fire);
        REQUIRE(y.ok);
        bool hit = false;
        for (const auto& e : y.events) if (e.kind == EventKind::AttackResolved) hit = (e.c & 1) != 0;
        if (hit) { CHECK(find(s, 2)->hp < 300); ++hits; }
    }
    CHECK(hits > 0);
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1)}, seen);
    auto bd = cast_req(kBlindDeaf, 2, {2}); bd.condition_choice = 2;   // Deafened
    REQUIRE(step(s, bd).ok);
    CHECK(find(s, 2)->has(Condition::Deafened));
    CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].condition == Condition::Deafened);
}

TEST_CASE("legal_actions picks the first choice for spells with choices") {
    auto seen = std::make_shared<Seen>();
    auto s = start({sorcerer(1), tough(2, 1)}, seen);
    bool found = false;
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::CastSpell && r.spell_id == kOrb) { found = true; CHECK(r.damage_choice == 1); }
    CHECK(found);
}
