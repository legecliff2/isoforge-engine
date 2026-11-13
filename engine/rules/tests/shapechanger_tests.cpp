// Shapechange B1-B7, rules side (shapechange-contract v0.1 §5). Content loading is the sim test.
// One HP pool, a same-feature form switch (Reverted b = 3 then Transformed), death revert (b = 4),
// and Moonbeam's forced revert (b = 6) plus the form lock. No new FormKind, ActionKind, or EventKind.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kChange = 9101;
const uint32_t kOther = 9102;
const uint32_t kBeam = spell_id_of("test.shape.moonbeam");
const uint32_t kBeamSoft = spell_id_of("test.shape.moonbeam_soft");
const uint32_t kBlast = spell_id_of("test.shape.blast");
const uint32_t kHold = spell_id_of("test.shape.hold");
const uint32_t kPoly = spell_id_of("test.shape.poly");

struct Geo {
    std::set<std::pair<uint32_t, CombatantId>> inside;
};

EncounterHooks hooks(const std::shared_ptr<Geo>& geo) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef beam; beam.id = kBeam; beam.content_id = "test.shape.moonbeam"; beam.level = 2; beam.concentration = true;
    beam.kind = SpellEffectKind::Zone; beam.range_ft = 120; beam.fixed_dc = 30; beam.components = kComponentV | kComponentS;
    beam.zone.shape = AreaShape::Cylinder; beam.zone.size_ft = 5; beam.zone.height_ft = 40;
    ZoneTriggerRow enter; enter.on = ZoneTrigger::Enter; enter.group = 1;
    enter.effect.kind = SpellEffectKind::Save; enter.effect.save_ability = Ability::Con;
    enter.effect.dice = {1, 4, 0}; enter.effect.damage_type = DamageType::Radiant; enter.effect.on_save = OnSave::Half;
    enter.shapechanger_disadvantage = true;
    beam.zone.triggers = {enter};
    book->add(beam);
    SpellDef soft = beam; soft.id = kBeamSoft; soft.content_id = "test.shape.moonbeam_soft"; soft.fixed_dc = 1;
    book->add(soft);
    SpellDef blast; blast.id = kBlast; blast.content_id = "test.shape.blast"; blast.level = 1; blast.range_ft = 60;
    blast.kind = SpellEffectKind::Save; blast.save_ability = Ability::Dex; blast.fixed_dc = 30;
    blast.dice = {0, 0, 40}; blast.damage_type = DamageType::Force; blast.on_save = OnSave::None;
    blast.components = kComponentV | kComponentS;
    book->add(blast);
    SpellDef hold; hold.id = kHold; hold.content_id = "test.shape.hold"; hold.level = 1; hold.concentration = true;
    hold.kind = SpellEffectKind::ApplyCondition; hold.applies_condition = true; hold.condition = Condition::Invisible;
    hold.range_ft = 0; hold.components = kComponentV | kComponentS;
    book->add(hold);
    SpellDef poly; poly.id = kPoly; poly.content_id = "test.shape.poly"; poly.level = 4; poly.concentration = true;
    poly.kind = SpellEffectKind::Polymorph; poly.range_ft = 60; poly.fixed_dc = 30; poly.components = kComponentV | kComponentS;
    book->add(poly);
    h.spells = book;
    h.feature_spec = [](const EncounterState&, const Combatant&, uint32_t id) -> std::optional<FeatureSpec> {
        if (id != kChange && id != kOther) return std::nullopt;
        FeatureSpec f; f.cost = 1; f.form = uint8_t(id == kChange ? FormKind::Shapechanger : FormKind::WildShape);
        f.form_choices = 2;
        return f;
    };
    h.build_form = [](const EncounterState&, const Combatant& c, FormKind k, uint32_t, uint8_t idx) -> std::optional<FormStats> {
        if (k == FormKind::Polymorph) {
            FormStats f; f.abilities = c.abilities; f.hp_max = 8; f.ac = 12; f.speed_ft = 40; return f;
        }
        if (k == FormKind::WildShape && idx == 0) {
            FormStats f; f.abilities = c.abilities; f.hp_max = 11; f.ac = 13; f.speed_ft = 40; return f;
        }
        FormStats f;
        f.abilities = c.abilities; f.ac = c.ac; f.speed_ft = c.speed_ft; f.size = c.size;
        f.fly_ft = c.fly_ft; f.swim_ft = c.swim_ft; f.climb_ft = c.climb_ft; f.burrow_ft = c.burrow_ft; f.hover = c.hover;
        f.attacks = c.attacks; f.creature_type = c.creature_type;
        f.save_proficiencies = c.save_proficiencies; f.skill_proficiencies = c.skill_proficiencies;
        f.resistances = c.resistances; f.ability_reduction = c.ability_reduction;
        f.set_senses = true; f.darkvision_ft = c.darkvision_ft;
        if (idx == 0) { f.ac = static_cast<int8_t>(c.ac + 1); f.speed_ft = 40; f.fly_ft = 60; f.darkvision_ft = 60; }
        else { f.speed_ft = 30; f.fly_ft = 0; f.climb_ft = 30; f.size = Size::Large; f.darkvision_ft = 0; }
        return f;
    };
    h.zone_contains = [geo](const EncounterState&, uint32_t z, CombatantId c, bool) {
        return geo->inside.count({z, c}) > 0;
    };
    return h;
}

Combatant shifter(CombatantId id, bool fact) {
    Combatant c = monster(id);
    c.team = 1; c.hp = c.hp_max = 20; c.ac = 11; c.speed_ft = 30; c.fly_ft = 30;
    c.shapechanger = fact; c.abilities.score[std::size_t(Ability::Str)] = 16;
    c.save_proficiencies = bit(Ability::Con); c.resistances = bit(DamageType::Fire);
    c.legendary_pool = 7; c.la_max = 3; c.la_cur = 2;
    c.darkvision_ft = 0;
    never_react(c);
    return c;
}
Combatant caster(CombatantId id) {
    Combatant c = wizard(id, 0);
    c.hp = c.hp_max = 40;
    c.spell_slots_max = {0, 4, 4, 2, 2, 0, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    c.spells_known = {kBeam, kBeamSoft, kBlast, kHold, kPoly};
    never_react(c);
    return c;
}
EncounterState start(std::vector<Combatant> cs, const std::shared_ptr<Geo>& geo, uint64_t seed = 3) {
    return begin_encounter(std::move(cs), seed, nullptr, hooks(geo));
}
ActionRequest change(CombatantId a, uint8_t form, uint32_t feature = kChange) {
    ActionRequest r{a, ActionKind::UseFeature};
    r.feature_id = feature; r.form_archetype = form;
    return r;
}
const Event* first_of(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
int index_of(const std::vector<Event>& ev, EventKind k) {
    for (int i = 0; i < int(ev.size()); ++i) if (ev[std::size_t(i)].kind == k) return i;
    return -1;
}
bool lock_is(const EncounterState& s, CombatantId id, std::vector<uint32_t> zones) {
    std::sort(zones.begin(), zones.end());
    const Combatant* c = find(s, id);
    return c && c->form_lock_zones == zones;
}

}  // namespace

TEST_CASE("shapechanger: one HP pool, and the declared overrides are the only statistics that move") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo);
    end_turns_until(s, 2);
    mut(s, 2).ability_reduction[std::size_t(Ability::Str)] = 2;
    mut(s, 2).abilities.score[std::size_t(Ability::Str)] = 14;
    mut(s, 2).la_cur = 2;   // turn start refills the pool; the form must not touch what remains
    auto r = step(s, change(2, 1));
    REQUIRE(r.ok);
    const Combatant& c = *find(s, 2);
    CHECK(c.form == FormKind::Shapechanger);
    CHECK(c.hp == 20); CHECK(c.hp_max == 20);
    CHECK(c.abilities[Ability::Str] == 14);
    CHECK(c.ability_reduction[std::size_t(Ability::Str)] == 2);
    CHECK(c.save_proficiencies == bit(Ability::Con));
    CHECK(c.resistances == bit(DamageType::Fire));
    CHECK(c.legendary_pool == 7); CHECK(c.la_max == 3); CHECK(c.la_cur == 2);
    CHECK(c.ac == 12); CHECK(c.speed_ft == 40); CHECK(c.fly_ft == 60); CHECK(c.darkvision_ft == 60);
    mut(s, 2).hp = 11;
    end_turns_until(s, 1);
    end_turns_until(s, 2);
    mut(s, 2).la_cur = 2;
    auto back = step(s, change(2, 2));   // B4: still this feature, one action
    REQUIRE(back.ok);
    CHECK(find(s, 2)->hp == 11); CHECK(find(s, 2)->hp_max == 20);
    CHECK(find(s, 2)->fly_ft == 0); CHECK(find(s, 2)->climb_ft == 30); CHECK(find(s, 2)->darkvision_ft == 0);
    CHECK(find(s, 2)->legendary_pool == 7); CHECK(find(s, 2)->la_cur == 2);
    CHECK(find(s, 2)->ability_reduction[std::size_t(Ability::Str)] == 2);
}

TEST_CASE("shapechanger B4: hybrid to wolf is Reverted 3 then Transformed; anything else still refuses") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo);
    end_turns_until(s, 2);
    auto first = step(s, change(2, 1));
    REQUIRE(first.ok);
    CHECK(first_of(first.events, EventKind::Reverted) == nullptr);
    CHECK(index_of(first.events, EventKind::FeatureUsed) < index_of(first.events, EventKind::Transformed));
    CHECK(step(s, change(2, 2, kOther)).error == "already transformed");
    CHECK(step(s, change(2, 1)).error == "already in that form");   // same form, before the spent action is considered
    CHECK_FALSE(s.budget.action);
    CHECK(find(s, 2)->form == FormKind::Shapechanger);
    end_turns_until(s, 1); end_turns_until(s, 2);
    auto same = step(s, change(2, 1));
    CHECK(same.error == "already in that form");
    CHECK(same.events.empty());
    CHECK(s.budget.action);
    CHECK(find(s, 2)->form_archetype == 1);
    auto sw = step(s, change(2, 2));
    REQUIRE(sw.ok);
    const int rev = index_of(sw.events, EventKind::Reverted), tr = index_of(sw.events, EventKind::Transformed);
    REQUIRE(rev >= 0); REQUIRE(tr > rev);
    CHECK(sw.events[std::size_t(rev)].b == 3);
    CHECK(sw.events[std::size_t(tr)].c == int32_t(FormKind::Shapechanger));
    CHECK(find(s, 2)->form_archetype == 2);
    CHECK(step(s, {2, ActionKind::Revert}).error == "no action available");
}

TEST_CASE("shapechanger: Revert spends the action, and death reverts with reason 4 before CombatantDied") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo);
    end_turns_until(s, 2);
    REQUIRE(step(s, change(2, 1)).ok);
    CHECK(step(s, {2, ActionKind::Revert}).error == "no action available");
    end_turns_until(s, 1); end_turns_until(s, 2);
    auto rv = step(s, {2, ActionKind::Revert});
    REQUIRE(rv.ok);
    CHECK(first_of(rv.events, EventKind::Reverted)->b == 3);
    CHECK(find(s, 2)->form == FormKind::None);
    CHECK_FALSE(s.budget.action);
}

TEST_CASE("shapechanger: a monster that dies in a form reverts before CombatantDied") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo);
    end_turns_until(s, 2);
    REQUIRE(step(s, change(2, 1)).ok);
    end_turns_until(s, 1);
    auto hit = step(s, cast(1, kBlast, 2, 1));
    REQUIRE(hit.ok);
    const int rev = index_of(hit.events, EventKind::Reverted), died = index_of(hit.events, EventKind::CombatantDied);
    REQUIRE(rev >= 0); REQUIRE(died > rev);
    CHECK(hit.events[std::size_t(rev)].b == 4);
    CHECK(find(s, 2)->form == FormKind::None);
    CHECK(find(s, 2)->dead);
}

TEST_CASE("Moonbeam B7: a failed save in a Shapechanger form reverts with reason 6 and locks the light") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo, 9);
    end_turns_until(s, 1);
    auto casted = step(s, [&] {
        ActionRequest r{1, ActionKind::CastSpell, 0};
        r.spell_id = kBeam; r.spell_level = 2; r.has_zone_dest = true; r.zone_x = 3; r.zone_y = 4;
        return r;
    }());
    REQUIRE(casted.ok);
    REQUIRE(s.zones.size() == 1);
    const uint32_t zone = s.zones[0].zone_id;
    end_turns_until(s, 2);
    REQUIRE(step(s, change(2, 1)).ok);
    end_turns_until(s, 1);
    end_turns_until(s, 2);
    geo->inside.insert({zone, 2});
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5; mv.move_tiles = 1; mv.zones_entered = {zone};
    auto entered = step(s, mv);
    REQUIRE(entered.ok);
    const int rev = index_of(entered.events, EventKind::Reverted);
    REQUIRE(rev >= 0);
    CHECK(entered.events[std::size_t(rev)].b == 6);
    CHECK(find(s, 2)->form == FormKind::None);
    CHECK(lock_is(s, 2, {zone}));
    const bool action_before = s.budget.action;
    auto refused = step(s, change(2, 1));
    CHECK(refused.error == "can't assume a different form until you leave the light");
    CHECK(s.budget.action == action_before);
    CHECK(find(s, 2)->form == FormKind::None);
    geo->inside.erase({zone, 2});
    ActionRequest out{2, ActionKind::Move};
    out.move_ft = 5; out.move_tiles = 1; out.zones_left = {zone};
    REQUIRE(step(s, out).ok);
    CHECK(lock_is(s, 2, {}));
    REQUIRE(step(s, change(2, 2)).ok);
    CHECK(find(s, 2)->form == FormKind::Shapechanger);
}

TEST_CASE("Moonbeam B7: a failed save in the true form locks without a revert, and a success sets no lock") {
    auto geo = std::make_shared<Geo>();
    auto soft = start({caster(1), shifter(2, true)}, geo, 9);
    end_turns_until(soft, 1);
    ActionRequest easy{1, ActionKind::CastSpell, 0};
    easy.spell_id = kBeamSoft; easy.spell_level = 2; easy.has_zone_dest = true; easy.zone_x = 3; easy.zone_y = 4;
    REQUIRE(step(soft, easy).ok);
    const uint32_t soft_zone = soft.zones[0].zone_id;
    end_turns_until(soft, 2);
    geo->inside.insert({soft_zone, 2});
    ActionRequest in_soft{2, ActionKind::Move};
    in_soft.move_ft = 5; in_soft.move_tiles = 1; in_soft.zones_entered = {soft_zone};
    auto saved = step(soft, in_soft);
    REQUIRE(saved.ok);
    const Event* sv = first_of(saved.events, EventKind::SavingThrowRolled);
    REQUIRE(sv);
    CHECK(sv->c == 1);
    CHECK(first_of(saved.events, EventKind::Reverted) == nullptr);
    CHECK(find(soft, 2)->form == FormKind::None);
    CHECK(lock_is(soft, 2, {}));
    REQUIRE(step(soft, change(2, 1)).ok);
    CHECK(find(soft, 2)->form == FormKind::Shapechanger);

    geo->inside.clear();
    auto s = start({caster(1), shifter(2, true)}, geo, 9);
    end_turns_until(s, 1);
    ActionRequest beam{1, ActionKind::CastSpell, 0};
    beam.spell_id = kBeam; beam.spell_level = 2; beam.has_zone_dest = true; beam.zone_x = 3; beam.zone_y = 4;
    REQUIRE(step(s, beam).ok);
    const uint32_t zone = s.zones[0].zone_id;
    end_turns_until(s, 2);
    CHECK(find(s, 2)->form == FormKind::None);
    geo->inside.insert({zone, 2});
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5; mv.move_tiles = 1; mv.zones_entered = {zone};
    auto entered = step(s, mv);
    REQUIRE(entered.ok);
    CHECK(first_of(entered.events, EventKind::Reverted) == nullptr);
    CHECK(find(s, 2)->form == FormKind::None);
    CHECK(lock_is(s, 2, {zone}));
    const bool action_before = s.budget.action;
    auto refused = step(s, change(2, 1));
    CHECK(refused.error == "can't assume a different form until you leave the light");
    CHECK(s.budget.action == action_before);
    CHECK(find(s, 2)->form == FormKind::None);
    geo->inside.erase({zone, 2});
    ActionRequest out{2, ActionKind::Move};
    out.move_ft = 5; out.move_tiles = 1; out.zones_left = {zone};
    REQUIRE(step(s, out).ok);
    CHECK(lock_is(s, 2, {}));
    REQUIRE(step(s, change(2, 1)).ok);
    CHECK(find(s, 2)->form == FormKind::Shapechanger);
}

TEST_CASE("Moonbeam B7: death clears a lock the beam would otherwise keep") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true)}, geo, 9);
    end_turns_until(s, 1);
    ActionRequest beam{1, ActionKind::CastSpell, 0};
    beam.spell_id = kBeam; beam.spell_level = 2; beam.has_zone_dest = true; beam.zone_x = 3; beam.zone_y = 4;
    REQUIRE(step(s, beam).ok);
    const uint32_t zone = s.zones[0].zone_id;
    end_turns_until(s, 2);
    mut(s, 2).hp = 1;
    geo->inside.insert({zone, 2});
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5; mv.move_tiles = 1; mv.zones_entered = {zone};
    auto entered = step(s, mv);
    REQUIRE(entered.ok);
    CHECK(first_of(entered.events, EventKind::Reverted) == nullptr);
    CHECK(find(s, 2)->dead);
    CHECK(lock_is(s, 2, {}));
}

TEST_CASE("Moonbeam B7: failing two beams stays locked until every beam is left") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true), caster(4)}, geo, 9);
    end_turns_until(s, 1);
    ActionRequest beam{1, ActionKind::CastSpell, 0};
    beam.spell_id = kBeam; beam.spell_level = 2; beam.has_zone_dest = true; beam.zone_x = 3; beam.zone_y = 4;
    REQUIRE(step(s, beam).ok);
    end_turns_until(s, 4);
    ActionRequest beam2{4, ActionKind::CastSpell, 0};
    beam2.spell_id = kBeam; beam2.spell_level = 2; beam2.has_zone_dest = true; beam2.zone_x = 6; beam2.zone_y = 4;
    REQUIRE(step(s, beam2).ok);
    REQUIRE(s.zones.size() == 2);
    const uint32_t zone_a = s.zones[0].zone_id, zone_b = s.zones[1].zone_id;
    end_turns_until(s, 2);
    geo->inside.insert({zone_a, 2});
    ActionRequest in_a{2, ActionKind::Move};
    in_a.move_ft = 5; in_a.move_tiles = 1; in_a.zones_entered = {zone_a};
    REQUIRE(step(s, in_a).ok);
    CHECK(lock_is(s, 2, {zone_a}));
    geo->inside.insert({zone_b, 2});
    ActionRequest in_b{2, ActionKind::Move};
    in_b.move_ft = 5; in_b.move_tiles = 1; in_b.zones_entered = {zone_b};
    REQUIRE(step(s, in_b).ok);
    CHECK(lock_is(s, 2, {zone_a, zone_b}));
    geo->inside.erase({zone_b, 2});
    ActionRequest out_b{2, ActionKind::Move};
    out_b.move_ft = 5; out_b.move_tiles = 1; out_b.zones_left = {zone_b};
    REQUIRE(step(s, out_b).ok);
    CHECK(lock_is(s, 2, {zone_a}));
    CHECK(step(s, change(2, 1)).error == "can't assume a different form until you leave the light");
    CHECK(s.budget.action);
    geo->inside.erase({zone_a, 2});
    ActionRequest out_a{2, ActionKind::Move};
    out_a.move_ft = 5; out_a.move_tiles = 1; out_a.zones_left = {zone_a};
    REQUIRE(step(s, out_a).ok);
    CHECK(lock_is(s, 2, {}));
    REQUIRE(step(s, change(2, 1)).ok);
    CHECK(find(s, 2)->form == FormKind::Shapechanger);
}

TEST_CASE("Moonbeam B7: the lock clears when the zone ends, and a creature without the fact is not reverted") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true), shifter(3, false)}, geo, 4);
    end_turns_until(s, 2);
    REQUIRE(step(s, change(2, 1)).ok);
    end_turns_until(s, 3);
    REQUIRE(step(s, change(3, 1)).ok);
    CHECK_FALSE(find(s, 3)->shapechanger);
    end_turns_until(s, 1);
    ActionRequest beam{1, ActionKind::CastSpell, 0};
    beam.spell_id = kBeam; beam.spell_level = 2; beam.has_zone_dest = true; beam.zone_x = 1; beam.zone_y = 1;
    REQUIRE(step(s, beam).ok);
    const uint32_t zone = s.zones[0].zone_id;
    end_turns_until(s, 2);
    geo->inside.insert({zone, 2});
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5; mv.move_tiles = 1; mv.zones_entered = {zone};
    REQUIRE(step(s, mv).ok);
    CHECK(lock_is(s, 2, {zone}));
    end_turns_until(s, 3);
    geo->inside.insert({zone, 3});
    ActionRequest mv3{3, ActionKind::Move};
    mv3.move_ft = 5; mv3.move_tiles = 1; mv3.zones_entered = {zone};
    auto oni = step(s, mv3);
    REQUIRE(oni.ok);
    CHECK(first_of(oni.events, EventKind::Reverted) == nullptr);
    CHECK(find(s, 3)->form == FormKind::Shapechanger);
    CHECK(lock_is(s, 3, {}));
    end_turns_until(s, 1);
    ActionRequest other{1, ActionKind::CastSpell, 1};
    other.spell_id = kHold; other.spell_level = 1;
    REQUIRE(step(s, other).ok);   // a new concentration spell ends Moonbeam
    CHECK(s.zones.empty());
    CHECK(lock_is(s, 2, {}));
}

TEST_CASE("Polymorph still refuses only the shapechanger fact (G16, no rule change)") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), shifter(2, true), shifter(3, false)}, geo);
    end_turns_until(s, 1);
    ActionRequest on_fact = cast(1, kPoly, 2, 4);
    on_fact.form_archetype = 1;
    CHECK(step(s, on_fact).error == "polymorph: target is a shapechanger");
    CHECK(find(s, 2)->form == FormKind::None);
    ActionRequest on_plain = cast(1, kPoly, 3, 4);
    on_plain.form_archetype = 1;
    REQUIRE(step(s, on_plain).ok);
    CHECK(find(s, 3)->form == FormKind::Polymorph);
}

TEST_CASE("Moonbeam disadvantage still reads only the shapechanger fact") {
    auto run = [](bool fact, int& natural, uint64_t& rng_after) {
        auto geo = std::make_shared<Geo>();
        auto s = start({caster(1), shifter(2, fact)}, geo, 11);
        end_turns_until(s, 1);
        ActionRequest beam{1, ActionKind::CastSpell, 0};
        beam.spell_id = kBeam; beam.spell_level = 2; beam.has_zone_dest = true; beam.zone_x = 2; beam.zone_y = 2;
        REQUIRE(step(s, beam).ok);
        const uint32_t zone = s.zones[0].zone_id;
        end_turns_until(s, 2);
        geo->inside.insert({zone, 2});
        ActionRequest mv{2, ActionKind::Move};
        mv.move_ft = 5; mv.move_tiles = 1; mv.zones_entered = {zone};
        auto entered = step(s, mv);
        REQUIRE(entered.ok);
        const Event* sv = first_of(entered.events, EventKind::SavingThrowRolled);
        REQUIRE(sv);
        natural = sv->a;
        rng_after = s.rng.save().state;
    };
    int plain = 0, shape = 0; uint64_t rp = 0, rs = 0;
    run(false, plain, rp);
    run(true, shape, rs);
    CHECK(shape <= plain);
    CHECK(rs != rp);
}
