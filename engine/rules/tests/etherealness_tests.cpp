// B19 Ghost Etherealness (SRD 5.1, CONTENT_CONVENTIONS §51). An action toggles Material ↔ Border Ethereal.
// The creature stays visible. It cannot affect or be affected across planes: a named target is refused
// before any roll, and an area skips with no roll. Same plane can. Specimen does not load ghost.json.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kToggle = spell_id_of("test.b19#etherealness");
const uint32_t kVisage = spell_id_of("test.b19#visage");
const uint32_t kBreath = spell_id_of("test.b19#breath");
const uint32_t kAura = spell_id_of("test.b19#aura");

SpellDef toggle() {
    SpellDef d;
    d.id = kToggle; d.content_id = "test.b19#etherealness"; d.is_spell = false; d.magical = false;
    d.kind = SpellEffectKind::ApplyCondition; d.ethereal_toggle = true; d.level = 0; d.range_ft = 0;
    return d;
}
SpellDef visage(int dc) {
    SpellDef g;
    g.id = kVisage; g.is_spell = false; g.magical = false; g.kind = SpellEffectKind::Save;
    g.save_ability = Ability::Wis; g.fixed_dc = int8_t(dc); g.level = 0;
    g.area = AreaShape::Sphere; g.area_size_ft = 60; g.range_ft = 0;
    g.applies_condition = true; g.condition = Condition::Frightened;
    g.repeat_save = RepeatSave::EndOfTurn;
    g.expiry = uint8_t(Expiry::Rounds); g.duration_rounds = 10;
    g.excluded_types = bit(CreatureType::Undead);
    g.target_sees_source = true;
    return g;
}
SpellDef breath() {
    SpellDef d;
    d.id = kBreath; d.is_spell = false; d.magical = false; d.kind = SpellEffectKind::Save;
    d.save_ability = Ability::Dex; d.fixed_dc = 30; d.level = 0;
    d.area = AreaShape::Sphere; d.area_size_ft = 15; d.range_ft = 0;
    d.dice = {1, 6, 0}; d.damage_type = DamageType::Fire; d.on_save = OnSave::Half;
    return d;
}
SpellDef aura() {
    SpellDef d;
    d.id = kAura; d.is_spell = false; d.magical = false; d.kind = SpellEffectKind::Zone;
    d.fixed_dc = 30; d.level = 0;
    d.zone.shape = AreaShape::Sphere; d.zone.anchor = ZoneAnchor::Creature; d.zone.size_ft = 10;
    ZoneTriggerRow row;
    row.on = ZoneTrigger::StartTurn; row.group = 1;
    row.effect.kind = SpellEffectKind::Save; row.effect.save_ability = Ability::Con;
    row.effect.applies_condition = true; row.effect.condition = Condition::Poisoned;
    row.condition_rounds = 1;
    d.zone.triggers = {row};
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(toggle());
    book->add(visage(30));
    book->add(breath());
    book->add(aura());
    h.spells = book;
    h.zone_contains = [](const EncounterState&, uint32_t, CombatantId id, bool) { return id == 1; };
    h.sight_facts = [](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f;
        f.line_of_sight = true;
        f.distance_ft = 10;
        return f;
    };
    return h;
}
Combatant body(CombatantId id, uint8_t team) {
    auto c = id == 1 ? pc(id, 40) : monster(id);
    c.team = team;
    c.hp = c.hp_max = 40;
    c.spells_known = {kToggle, kVisage, kBreath};
    for (auto& p : c.reaction_policy) p = ReactionPolicy::Never;
    return c;
}
EncounterState start(std::vector<Combatant> cs) {
    auto s = begin_encounter(std::move(cs), 1, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
int saves_by(const std::vector<Event>& ev, CombatantId who) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == EventKind::SavingThrowRolled && e.actor == who) ++n;
    return n;
}
bool rolled_attack(const std::vector<Event>& ev) {
    for (const auto& e : ev)
        if (e.kind == EventKind::AttackResolved) return true;
    return false;
}
int next_start_saves(EncounterState& s, CombatantId id) {
    int n = 0;
    if (current_actor(s) == id) {
        auto r = step(s, {id, ActionKind::EndTurn});
        REQUIRE(r.ok);
        n += count(r.events, EventKind::SavingThrowRolled);
    }
    for (int i = 0; i < 8 && current_actor(s) != id; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        REQUIRE(r.ok);
        n += count(r.events, EventKind::SavingThrowRolled);
    }
    REQUIRE(current_actor(s) == id);
    return n;
}
}  // namespace

TEST_CASE("B19: the action toggles Material, Border Ethereal, Material") {
    auto s = start({body(1, 0), body(2, 1)});
    CHECK(mut(s, 1).plane == Plane::Material);
    REQUIRE(step(s, cast(1, kToggle, 0)).ok);
    CHECK(mut(s, 1).plane == Plane::BorderEthereal);
    CHECK(s.budget.action == false);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    auto back = step(s, cast(1, kToggle, 1));
    REQUIRE_MESSAGE(back.ok, back.error);
    CHECK(mut(s, 1).plane == Plane::Material);
}

TEST_CASE("B19: a material attack on an ethereal creature is refused, not a miss") {
    auto s = start({body(1, 0), body(2, 1)});
    mut(s, 2).plane = Plane::BorderEthereal;
    mut(s, 1).attacks[0].to_hit = 100;
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    CHECK_FALSE(r.ok);
    CHECK(r.error == "can't affect a creature on the other plane");
    CHECK_FALSE(rolled_attack(r.events));
    CHECK(s.budget.action);
    CHECK(mut(s, 2).hp == 40);
}

TEST_CASE("B19: ethereal attacks ethereal, and material attacks material") {
    auto s = start({body(1, 0), body(2, 1)});
    mut(s, 1).plane = Plane::BorderEthereal;
    mut(s, 2).plane = Plane::BorderEthereal;
    mut(s, 1).attacks[0].to_hit = 100;
    mut(s, 1).attacks[0].damage = {0, 0, 5};
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    REQUIRE(r.ok);
    CHECK(rolled_attack(r.events));
    CHECK(mut(s, 2).hp < 40);
}

TEST_CASE("B19: a material area skips an ethereal creature and still rolls the material one") {
    auto s = start({body(1, 0), body(2, 1), body(3, 1)});
    mut(s, 3).plane = Plane::BorderEthereal;
    mut(s, 3).team = 1;
    auto r = step(s, [&] {
        auto q = cast(1, kBreath, 2);
        q.targets = {2, 3};
        return q;
    }());
    REQUIRE(r.ok);
    CHECK(saves_by(r.events, 2) == 1);
    CHECK(saves_by(r.events, 3) == 0);
    CHECK(mut(s, 3).hp == 40);
}

TEST_CASE("B19: ethereal Horrifying Visage does not hit a material creature") {
    auto s = start({body(1, 0), body(2, 1)});
    mut(s, 1).plane = Plane::BorderEthereal;
    mut(s, 1).creature_type = CreatureType::Undead;
    mut(s, 2).creature_type = CreatureType::Humanoid;
    auto r = step(s, cast(1, kVisage, 2));
    REQUIRE(r.ok);
    CHECK(saves_by(r.events, 2) == 0);
    CHECK_FALSE(mut(s, 2).has(Condition::Frightened));
    CHECK(mut(s, 2).aged_years == 0);
}

TEST_CASE("B19: ethereal Horrifying Visage does hit another ethereal creature") {
    auto s = start({body(1, 0), body(2, 1)});
    mut(s, 1).plane = Plane::BorderEthereal;
    mut(s, 2).plane = Plane::BorderEthereal;
    mut(s, 1).creature_type = CreatureType::Undead;
    mut(s, 2).creature_type = CreatureType::Humanoid;
    auto r = step(s, cast(1, kVisage, 2));
    REQUIRE(r.ok);
    CHECK(saves_by(r.events, 2) == 1);
    CHECK(mut(s, 2).has(Condition::Frightened));
}

TEST_CASE("B19: sight does not change when the subject is on the Border Ethereal") {
    Combatant viewer = body(1, 0);
    Combatant subject = body(2, 1);
    subject.plane = Plane::BorderEthereal;
    CHECK_FALSE(subject.has(Condition::Invisible));
    SightFacts facts;
    facts.line_of_sight = true;
    facts.distance_ft = 10;
    const Sight seen = derive_sight(viewer, facts);
    CHECK(seen.can_see);
    CHECK(seen.sense == Sense::Sight);
    auto s = start({viewer, subject});
    CHECK(mut(s, 2).plane == Plane::BorderEthereal);
    const Sight live = sight(s, 1, 2);
    CHECK(live.can_see);
    CHECK(live.perceives);
    CHECK_FALSE(mut(s, 2).has(Condition::Invisible));
}

TEST_CASE("B19: a grapple across planes is refused before the contest") {
    auto s = start({body(1, 0), body(2, 1)});
    mut(s, 2).plane = Plane::BorderEthereal;
    ActionRequest g{1, ActionKind::Attack, 2, 0};
    g.special = 1;
    g.target_within_5ft = true;
    auto r = step(s, g);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "can't affect a creature on the other plane");
    CHECK(count(r.events, EventKind::CheckRolled) == 0);
    CHECK(s.budget.action);
    CHECK_FALSE(mut(s, 2).has(Condition::Grappled));
}

TEST_CASE("B19: an aura on the Material Plane does not save an ethereal creature") {
    auto owner = body(2, 1);
    auto victim = body(1, 0);
    auto s = begin_encounter({victim, owner}, 1, nullptr, hooks());
    REQUIRE(add_feature_aura(s, 2, kAura) != 0);
    mut(s, 1).plane = Plane::BorderEthereal;
    CHECK(next_start_saves(s, 1) == 0);
    CHECK_FALSE(mut(s, 1).has(Condition::Poisoned));
    mut(s, 1).plane = Plane::Material;
    CHECK(next_start_saves(s, 1) == 1);
    CHECK(mut(s, 1).has(Condition::Poisoned));
}
