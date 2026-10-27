// VERSION_BUMP_BATCH 7.2zj: Charm Person, Suggestion, Calm Emotions, Fly, Cone of Cold,
// Ice Storm, Greater Invisibility, Dispel Evil and Good, Greater Restoration, Geas.
// No new SIM_EVENTS tag (leftover_digest / sourced_digest sub-byte 0x60). 0xE3 stays free.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kCharm = spell_id_of("test.zj.charm_person");
const uint32_t kSuggest = spell_id_of("test.zj.suggestion");
const uint32_t kCalm = spell_id_of("test.zj.calm_emotions");
const uint32_t kFly = spell_id_of("test.zj.fly");
const uint32_t kCone = spell_id_of("test.zj.cone_of_cold");
const uint32_t kIce = spell_id_of("test.zj.ice_storm");
const uint32_t kInvis = spell_id_of("test.zj.greater_invisibility");
const uint32_t kDispel = spell_id_of("test.zj.dispel_evil_and_good");
const uint32_t kDispelMod = spell_id_of("test.zj.dispel_evil_and_good#modifier");
const uint32_t kRestore = spell_id_of("test.zj.greater_restoration");
const uint32_t kGeas = spell_id_of("test.zj.geas");
const uint32_t kGeasMinute = spell_id_of("test.zj.geas_minute");
const uint32_t kBolt = spell_id_of("test.zj.flat_bolt");

SpellDef charm_def(uint32_t id, const char* content, int8_t dc) {
    SpellDef d;
    d.id = id; d.content_id = content; d.level = 1; d.range_ft = 30; d.is_spell = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Wis; d.fixed_dc = dc;
    d.applies_condition = true; d.condition = Condition::Charmed;
    d.hostile_save_advantage = true; d.ends_on_ally_harm = true; d.caster_sees_target = true;
    d.target_types = bit(CreatureType::Humanoid); d.targets = 1; d.upcast_targets = 1; d.max_spread_ft = 30;
    d.duration = SpellDuration::Hours; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 600;
    return d;
}
SpellDef suggest_def() {
    SpellDef d = charm_def(kSuggest, "test.zj.suggestion", 30);
    d.level = 2; d.concentration = true; d.charm_immune = true; d.requires_hearing = true;
    d.duration_amount = 8; d.duration_rounds = 4800; d.target_types = 0; d.upcast_targets = 0;
    return d;
}
SpellDef calm_def() {
    SpellDef d;
    d.id = kCalm; d.content_id = "test.zj.calm_emotions"; d.level = 2; d.range_ft = 60; d.is_spell = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Cha; d.fixed_dc = 30;
    d.suppress_conditions = bit(Condition::Charmed) | bit(Condition::Frightened);
    d.concentration = true; d.target_types = bit(CreatureType::Humanoid);
    d.area = AreaShape::Sphere; d.area_size_ft = 20;
    d.duration = SpellDuration::Minutes; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 10;
    return d;
}
SpellDef fly_def() {
    SpellDef d;
    d.id = kFly; d.content_id = "test.zj.fly"; d.level = 3; d.range_ft = 5; d.is_spell = true;
    d.kind = SpellEffectKind::GrantFly; d.fly_ft = 60; d.concentration = true;
    d.targets = 1; d.upcast_targets = 1;
    d.duration = SpellDuration::Minutes; d.duration_amount = 10; d.expiry = 1; d.duration_rounds = 100;
    return d;
}
SpellDef cone_def() {
    SpellDef d;
    d.id = kCone; d.content_id = "test.zj.cone_of_cold"; d.level = 5; d.range_ft = 0; d.is_spell = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Con; d.fixed_dc = 30;
    d.dice = {8, 1, 0}; d.damage_type = DamageType::Cold; d.on_save = OnSave::Half; d.upcast_dice = 1;
    d.area = AreaShape::Cone; d.area_size_ft = 60;
    return d;
}
SpellDef ice_def(int8_t dc) {
    SpellDef d;
    d.id = kIce; d.content_id = "test.zj.ice_storm"; d.level = 4; d.range_ft = 300; d.is_spell = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = dc;
    d.dice = {2, 1, 0}; d.damage_type = DamageType::Bludgeoning; d.on_save = OnSave::Half; d.upcast_dice = 1;
    d.has_also_damage = true; d.also_dice = {4, 1, 0}; d.also_damage_type = DamageType::Cold;
    d.difficult_until = 1; d.area = AreaShape::Cylinder; d.area_size_ft = 20; d.area_height_ft = 40;
    d.zone.flags = kZoneDifficult; d.zone.shape = AreaShape::Cylinder; d.zone.size_ft = 20; d.zone.height_ft = 40;
    return d;
}
SpellDef invis_def() {
    SpellDef d;
    d.id = kInvis; d.content_id = "test.zj.greater_invisibility"; d.level = 4; d.range_ft = 5; d.is_spell = true;
    d.kind = SpellEffectKind::ApplyCondition; d.applies_condition = true; d.condition = Condition::Invisible;
    d.concentration = true; d.targets = 1; d.upcast_targets = 1;
    d.duration = SpellDuration::Minutes; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 10;
    return d;
}
SpellDef dispel_def() {
    SpellDef d;
    d.id = kDispel; d.content_id = "test.zj.dispel_evil_and_good"; d.level = 5; d.range_ft = 0; d.is_spell = true;
    d.kind = SpellEffectKind::ApplyModifier; d.concentration = true;
    d.modifier.stat = ModStat::AttackRoll; d.modifier.mode = RollMode::Disadvantage;
    d.modifier.applies_to = ModAppliesTo::Attackers; d.modifier.id = kDispelMod; d.modifier.source = kDispel;
    d.modifier.from_types = bit(CreatureType::Celestial) | bit(CreatureType::Elemental) | bit(CreatureType::Fey) |
                            bit(CreatureType::Fiend) | bit(CreatureType::Undead);
    d.modifier.concentration = true;
    return d;
}
SpellDef restore_def() {
    SpellDef d;
    d.id = kRestore; d.content_id = "test.zj.greater_restoration"; d.level = 5; d.range_ft = 5; d.is_spell = true;
    d.kind = SpellEffectKind::Restore;
    d.restore_options = {RestoreOption::Exhaustion, RestoreOption::Charmed, RestoreOption::Petrified,
                         RestoreOption::AbilityStr, RestoreOption::AbilityDex, RestoreOption::AbilityCon,
                         RestoreOption::AbilityInt, RestoreOption::AbilityWis, RestoreOption::AbilityCha,
                         RestoreOption::HitPointMaximum};
    return d;
}
SpellDef geas_def(bool minute) {
    SpellDef d = charm_def(minute ? kGeasMinute : kGeas, minute ? "test.zj.geas_minute" : "test.zj.geas", 30);
    d.level = 5; d.range_ft = 60; d.ends_on_restoration = true; d.ends_on_ally_harm = false;
    d.target_types = 0; d.upcast_targets = 0;
    d.duration_amount = 720; d.expiry = 0; d.duration_rounds = 0;   // 30 days do not fit the round counter
    if (minute) d.casting_time = CastingTime::Minute;
    return d;
}
SpellDef bolt_def() {
    SpellDef d;
    d.id = kBolt; d.content_id = "test.zj.flat_bolt"; d.level = 1; d.range_ft = 60; d.is_spell = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 30;
    d.dice = {0, 0, 5}; d.damage_type = DamageType::Force; d.on_save = OnSave::None;
    return d;
}

EncounterHooks zj_hooks(bool blind) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(charm_def(kCharm, "test.zj.charm_person", 30));
    SpellDef charm_ok = charm_def(spell_id_of("test.zj.charm_easy"), "test.zj.charm_easy", 1);
    book->add(charm_ok);
    book->add(suggest_def()); book->add(calm_def()); book->add(fly_def()); book->add(cone_def());
    book->add(ice_def(30)); book->add(ice_def(1));
    // ice_def(1) reused the same id. Replace with a distinct easy ice.
    book->erase(kIce);
    book->add(ice_def(30));
    SpellDef ice_easy = ice_def(1); ice_easy.id = spell_id_of("test.zj.ice_easy"); ice_easy.content_id = "test.zj.ice_easy";
    book->add(ice_easy);
    book->add(invis_def()); book->add(dispel_def()); book->add(restore_def());
    book->add(geas_def(false)); book->add(geas_def(true)); book->add(bolt_def());
    h.spells = book;
    if (blind) h.sight_facts = [](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f; f.line_of_sight = false; return f;
    };
    return h;
}

const uint32_t kCharmEasy = spell_id_of("test.zj.charm_easy");
const uint32_t kIceEasy = spell_id_of("test.zj.ice_easy");

Combatant wiz(CombatantId id, uint8_t team = 0) {
    auto c = wizard(id, team);
    c.hp = c.hp_max = 80;
    c.spells_known.insert(c.spells_known.end(),
                          {kCharm, kCharmEasy, kSuggest, kCalm, kFly, kCone, kIce, kIceEasy, kInvis, kDispel,
                           kRestore, kGeas, kGeasMinute, kBolt});
    c.spell_slots_max[4] = 2; c.spell_slots_max[5] = 2; c.spell_slots = c.spell_slots_max;
    never_react(c);
    return c;
}
Combatant foe(CombatantId id, uint8_t team = 1) {
    auto m = monster(id); m.team = team; m.hp = m.hp_max = 40; m.ac = 10; never_react(m); return m;
}
EncounterState begin(std::vector<Combatant> cs, uint64_t seed = 1, bool blind = false) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, zj_hooks(blind));
    end_turns_until(s, 1);
    return s;
}
int saves(const std::vector<Event>& ev) {
    int n = 0; for (const auto& e : ev) n += e.kind == EventKind::SavingThrowRolled; return n;
}
int natural_save(const std::vector<Event>& ev) {
    for (const auto& e : ev) if (e.kind == EventKind::SavingThrowRolled) return e.a;
    return -1;
}
const ActiveSpellEffect* effect_on(const EncounterState& s, uint32_t spell, CombatantId target) {
    for (const auto& e : s.effects) if (e.spell_id == spell && e.target == target) return &e;
    return nullptr;
}
int charmed_effects(const EncounterState& s, CombatantId target) {
    int n = 0;
    for (const auto& e : s.effects)
        if (e.target == target && !e.no_condition && e.condition == Condition::Charmed) ++n;
    return n;
}
ActionRequest choose(CombatantId a, uint32_t spell, CombatantId target, uint8_t level, uint8_t choice) {
    auto r = cast(a, spell, target, level); r.condition_choice = choice; return r;
}

}  // namespace

TEST_CASE("7.2zj kinds, harmful, and digests fold only when the new fields are set") {
    CHECK(uint8_t(SpellEffectKind::Descriptive) == 27);
    CHECK(uint8_t(SpellEffectKind::GrantFly) == 28);
    CHECK(uint8_t(SpellEffectKind::Restore) == 29);
    CHECK_FALSE(spell_harmful(fly_def()));
    CHECK_FALSE(spell_harmful(restore_def()));
    CHECK_FALSE(spell_harmful(invis_def()));
    CHECK_FALSE(spell_harmful(dispel_def()));
    CHECK(spell_harmful(charm_def(kCharm, "test.zj.charm_person", 30)));
    CHECK(spell_harmful(cone_def()));

    EncounterState plain;
    ActiveSpellEffect e{};
    e.spell_id = 1; e.caster = 1; e.target = 2; e.condition = Condition::Paralyzed;
    plain.effects.push_back(e);
    CHECK_FALSE(has_leftover_state(plain));
    const uint64_t before = leftover_digest(plain);
    plain.effects[0].suppress_count = 1;
    CHECK(has_leftover_state(plain));
    CHECK(leftover_digest(plain) != before);

    EncounterState src;
    src.sourced.push_back(SourcedCondition{Condition::Charmed, 2, 1, 0});
    const uint64_t sourced_zero = sourced_digest(src);
    src.sourced[0].suppress_count = 1;
    CHECK(sourced_digest(src) != sourced_zero);
}

TEST_CASE("7.2zj Charm Person: humanoid, advantage when fighting, ally harm, no round past an hour") {
    auto s = begin({wiz(1), foe(2)});
    mut(s, 2).creature_type = CreatureType::Beast;
    CHECK(std::string(step(s, cast(1, kCharm, 2, 1)).error) == "invalid creature type for this spell");
    mut(s, 2).creature_type = CreatureType::Humanoid;
    auto failed = step(s, cast(1, kCharm, 2, 1));
    REQUIRE(failed.ok);
    CHECK(saves(failed.events) == 1);
    CHECK(find(s, 2)->has(Condition::Charmed));
    const auto* ef = effect_on(s, kCharm, 2);
    REQUIRE(ef);
    CHECK(ef->timed.kind == Expiry::Rounds);
    CHECK(ef->timed.rounds_left == 600);
    CHECK(std::any_of(s.sourced.begin(), s.sourced.end(), [](const SourcedCondition& sc) {
        return sc.condition == Condition::Charmed && sc.target == 2 && sc.source == 1 && sc.suppress_count == 0;
    }));

    auto easy = begin({wiz(1), foe(2)});
    auto saved = step(easy, cast(1, kCharmEasy, 2, 1));
    REQUIRE(saved.ok);
    CHECK_FALSE(find(easy, 2)->has(Condition::Charmed));

    bool differed = false;
    for (uint64_t seed = 1; seed < 80 && !differed; ++seed) {
        auto hostile = begin({wiz(1), foe(2), foe(3)}, seed);
        auto friendly = begin({wiz(1), foe(2, 0), foe(3)}, seed);
        auto h = step(hostile, cast(1, kCharm, 2, 1));
        auto f = step(friendly, cast(1, kCharm, 2, 1));
        REQUIRE(h.ok); REQUIRE(f.ok);
        differed = natural_save(h.events) != natural_save(f.events);
    }
    CHECK(differed);

    auto harm = begin({wiz(1), foe(2), wiz(3)});
    REQUIRE(step(harm, cast(1, kCharm, 2, 1)).ok);
    mut(harm, 2).spells_known.push_back(kBolt);
    mut(harm, 2).spell_slots[1] = 1;
    mut(harm, 2).spellcasting_ability = Ability::Int;
    end_turns_until(harm, 2);
    harm.budget.action = true;
    REQUIRE(step(harm, cast(2, kBolt, 2, 1)).ok);   // self-damage: the attacker is null
    CHECK(find(harm, 2)->has(Condition::Charmed));
    end_turns_until(harm, 3);
    mut(harm, 3).attacks[0].to_hit = 30;
    mut(harm, 3).attacks[0].damage = DiceExpr{0, 0, 4};
    REQUIRE(step(harm, ActionRequest{3, ActionKind::Attack, 2}).ok);
    CHECK_FALSE(find(harm, 2)->has(Condition::Charmed));
    CHECK_FALSE(effect_on(harm, kCharm, 2));

    auto other = begin({wiz(1), foe(2), wiz(4, 2)});
    REQUIRE(step(other, cast(1, kCharm, 2, 1)).ok);
    end_turns_until(other, 4);
    other.budget.action = true;
    REQUIRE(step(other, cast(4, kBolt, 2, 1)).ok);   // another team
    CHECK(find(other, 2)->has(Condition::Charmed));
}

TEST_CASE("7.2zj Suggestion: charm immunity skips the save, deafness refuses, ally harm ends concentration") {
    auto immune = begin({wiz(1), foe(2)});
    mut(immune, 2).condition_immunities |= bit(Condition::Charmed);
    auto skipped = step(immune, cast(1, kSuggest, 2, 2));
    REQUIRE(skipped.ok);
    CHECK(saves(skipped.events) == 0);
    CHECK_FALSE(find(immune, 2)->has(Condition::Charmed));
    CHECK(find(immune, 1)->concentration_spell == 0);

    auto deaf = begin({wiz(1), foe(2)});
    add_condition(mut(deaf, 2), Condition::Deafened);
    CHECK(std::string(step(deaf, cast(1, kSuggest, 2, 2)).error) == "target cannot hear");
    CHECK(find(deaf, 1)->spell_slots[2] == find(deaf, 1)->spell_slots_max[2]);

    auto unseen = begin({wiz(1), foe(2)}, 1, true);
    CHECK(std::string(step(unseen, cast(1, kCharm, 2, 1)).error) == "caster cannot see the target");

    auto s = begin({wiz(1), foe(2), wiz(3)});
    REQUIRE(step(s, cast(1, kSuggest, 2, 2)).ok);
    CHECK(find(s, 1)->concentration_spell == kSuggest);
    end_turns_until(s, 3);
    mut(s, 3).attacks[0].to_hit = 30;
    mut(s, 3).attacks[0].damage = DiceExpr{0, 0, 4};
    REQUIRE(step(s, ActionRequest{3, ActionKind::Attack, 2}).ok);
    CHECK_FALSE(effect_on(s, kSuggest, 2));
    CHECK(find(s, 1)->concentration_spell == 0);
}

TEST_CASE("7.2zj Calm Emotions: suppress charmed and frightened, resume, and do not resume an expired charm") {
    auto s = begin({wiz(1), foe(2)});
    REQUIRE(step(s, cast(1, kCharm, 2, 1)).ok);
    s.budget.action = true;
    REQUIRE(step(s, cast(1, kCalm, 2, 2)).ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Charmed));
    const auto* calm = effect_on(s, kCalm, 2);
    REQUIRE(calm);
    CHECK(calm->no_condition);
    CHECK(calm->suppress_mask == (bit(Condition::Charmed) | bit(Condition::Frightened)));
    const auto* charm = effect_on(s, kCharm, 2);
    REQUIRE(charm);
    CHECK(charm->suppress_count == 1);
    CHECK(std::any_of(s.sourced.begin(), s.sourced.end(), [](const SourcedCondition& sc) {
        return sc.condition == Condition::Charmed && sc.suppress_count == 1;
    }));
    end_turns_until(s, 2);
    auto swing = step(s, ActionRequest{2, ActionKind::Attack, 1});
    CHECK(swing.ok);   // suppressed: charmed_by does not block the attack
    end_turns_until(s, 1);
    s.budget.action = true;
    REQUIRE(step(s, cast(1, kFly, 1, 3)).ok);   // a new concentration spell ends calm
    CHECK(find(s, 2)->has(Condition::Charmed));
    CHECK(effect_on(s, kCharm, 2));
    CHECK_FALSE(effect_on(s, kCalm, 2));

    auto gone = begin({wiz(1), foe(2)});
    REQUIRE(step(gone, cast(1, kCharm, 2, 1)).ok);
    for (auto& e : gone.effects) if (e.spell_id == kCharm) e.timed.rounds_left = 1;
    gone.budget.action = true;
    REQUIRE(step(gone, cast(1, kCalm, 2, 2)).ok);
    CHECK_FALSE(find(gone, 2)->has(Condition::Charmed));
    const int round = gone.round;
    for (int i = 0; i < 8 && gone.round == round; ++i) (void)step(gone, {current_actor(gone), ActionKind::EndTurn});
    CHECK(gone.round != round);
    CHECK_FALSE(effect_on(gone, kCharm, 2));
    CHECK_FALSE(find(gone, 2)->has(Condition::Charmed));
    end_turns_until(gone, 1);
    gone.budget.action = true;
    REQUIRE(step(gone, cast(1, kFly, 1, 3)).ok);
    CHECK_FALSE(find(gone, 2)->has(Condition::Charmed));

    auto fear = begin({wiz(1), foe(2)});
    add_condition(mut(fear, 2), Condition::Frightened);
    fear.sourced.push_back(SourcedCondition{Condition::Frightened, 2, 1, 0});
    REQUIRE(step(fear, cast(1, kCalm, 2, 2)).ok);
    CHECK_FALSE(find(fear, 2)->has(Condition::Frightened));
    CHECK(fear.sourced[0].suppress_count == 1);
}

TEST_CASE("7.2zj Fly: willing creatures, legal targets, and a mid-air revoke falls") {
    auto s = begin({wiz(1), foe(2), wiz(3)});
    CHECK(std::string(step(s, cast(1, kFly, 2, 3)).error) == "fly: the target is not willing");
    REQUIRE(step(s, cast(1, kFly, 1, 3)).ok);
    CHECK(find(s, 1)->granted_fly_ft == 60);
    CHECK(find(s, 1)->granted_fly_src == kFly);
    CHECK(effect_on(s, kFly, 1)->granted_fly == 60);
    s.budget.action = true;
    REQUIRE(step(s, cast(1, kFly, 3, 3)).ok);   // replacing concentration revokes the first grant
    CHECK(find(s, 1)->granted_fly_ft == 0);
    CHECK(find(s, 3)->granted_fly_ft == 60);

    auto aloft = begin({wiz(1), foe(2)});
    REQUIRE(step(aloft, cast(1, kFly, 1, 3)).ok);
    mut(aloft, 1).alt_ft = 30;
    aloft.budget.action = true;
    REQUIRE(step(aloft, cast(1, kSuggest, 2, 2)).ok);
    CHECK(find(aloft, 1)->granted_fly_ft == 0);
    CHECK(find(aloft, 1)->alt_ft == 0);

    auto listed = begin({wiz(1), foe(2), wiz(3)});
    std::vector<CombatantId> targets;
    for (const auto& r : legal_actions(listed))
        if (r.kind == ActionKind::CastSpell && r.spell_id == kFly && r.spell_level == 3) targets.push_back(r.target);
    CHECK(std::find(targets.begin(), targets.end(), 1) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), 3) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), 2) == targets.end());
}

TEST_CASE("7.2zj Cone of Cold kills without a frozen statue") {
    auto s = begin({wiz(1), foe(2)});
    mut(s, 2).hp = mut(s, 2).hp_max = 5;
    auto hit = step(s, cast(1, kCone, 2, 5));
    REQUIRE(hit.ok);
    CHECK(find(s, 2)->dead);
    CHECK_FALSE(find(s, 2)->has(Condition::Petrified));
}

TEST_CASE("7.2zj Ice Storm: two packets, cold does not upcast, difficult terrain until the caster's next turn ends") {
    auto s = begin({wiz(1), foe(2)});
    const int hp = find(s, 2)->hp;
    auto req = cast(1, kIce, 2, 4);
    req.has_area_origin = true; req.area_x = 3; req.area_y = 4;
    auto hit = step(s, req);
    REQUIRE(hit.ok);
    CHECK(find(s, 2)->hp == int16_t(hp - 6));   // 2d1 bludgeoning + 4d1 cold, failed save
    REQUIRE(s.zones.size() == 1);
    CHECK(s.zones[0].spell_id == kIce);
    CHECK_FALSE(s.zones[0].concentration);
    CHECK(s.zones[0].timed.kind == Expiry::EndOfCasterNextTurn);
    CHECK(s.zones[0].timed.rounds_left == 2);
    CHECK(s.zones[0].has_anchor_tile);
    CHECK(s.zones[0].anchor_x == 3);
    CHECK(s.zones[0].anchor_y == 4);
    CHECK(s.zones[0].size_ft == 20);
    bool flagged = false;
    for (const auto& e : hit.events)
        if (e.kind == EventKind::ZoneCreated && e.b == int32_t(kIce) && (e.c & int32_t(kZoneDifficult)) != 0) flagged = true;
    CHECK(flagged);
    (void)step(s, {1, ActionKind::EndTurn});
    CHECK(s.zones.size() == 1);
    CHECK(s.zones[0].timed.rounds_left == 1);
    end_turns_until(s, 1);
    CHECK(s.zones.size() == 1);
    (void)step(s, {1, ActionKind::EndTurn});
    CHECK(s.zones.empty());

    auto up = begin({wiz(1), foe(2)});
    const int before = find(up, 2)->hp;
    REQUIRE(step(up, cast(1, kIce, 2, 5)).ok);
    CHECK(find(up, 2)->hp == int16_t(before - 7));   // bludgeoning 3d1, cold still 4

    auto saved = begin({wiz(1), foe(2)});
    const int full = find(saved, 2)->hp;
    REQUIRE(step(saved, cast(1, kIceEasy, 2, 4)).ok);
    CHECK(find(saved, 2)->hp == int16_t(full - 3));   // half of 2 and half of 4
    CHECK(saved.zones.size() == 1);
}

TEST_CASE("7.2zj Greater Invisibility is not ended by an attack or a cast") {
    auto s = begin({wiz(1), wiz(3), foe(2)});
    REQUIRE(step(s, cast(1, kInvis, 3, 4)).ok);
    CHECK(find(s, 3)->has(Condition::Invisible));
    CHECK(find(s, 1)->concentration_spell == kInvis);
    end_turns_until(s, 3);
    mut(s, 3).attacks[0].to_hit = 30;
    mut(s, 3).attacks[0].damage = DiceExpr{0, 0, 1};
    REQUIRE(step(s, ActionRequest{3, ActionKind::Attack, 2}).ok);
    CHECK(find(s, 3)->has(Condition::Invisible));
    s.budget.action = true;
    REQUIRE(step(s, cast(3, kBolt, 2, 1)).ok);
    CHECK(find(s, 3)->has(Condition::Invisible));
    end_turns_until(s, 1);
    s.budget.action = true;
    REQUIRE(step(s, cast(1, kFly, 1, 3)).ok);
    CHECK_FALSE(find(s, 3)->has(Condition::Invisible));
    CHECK(find(s, 1)->concentration_spell == kFly);
}

TEST_CASE("7.2zj Dispel Evil and Good is disadvantage for the listed types only") {
    auto s = begin({wiz(1), foe(2)});
    REQUIRE(step(s, cast(1, kDispel, 1, 5)).ok);
    const ActiveModifier* row = nullptr;
    for (const auto& m : s.modifiers) if (m.spec.id == kDispelMod) row = &m;
    REQUIRE(row);
    CHECK(row->spec.applies_to == ModAppliesTo::Attackers);
    CHECK(row->spec.mode == RollMode::Disadvantage);
    CHECK(row->spec.stat == ModStat::AttackRoll);
    CHECK(row->target == 1);
    const uint32_t types = bit(CreatureType::Celestial) | bit(CreatureType::Elemental) | bit(CreatureType::Fey) |
                           bit(CreatureType::Fiend) | bit(CreatureType::Undead);
    CHECK(row->spec.from_types == types);

    auto fight = begin({wiz(1), foe(2)});
    REQUIRE(step(fight, cast(1, kDispel, 1, 5)).ok);
    mut(fight, 2).creature_type = CreatureType::Fiend;
    mut(fight, 2).attacks[0].to_hit = 30;
    mut(fight, 2).attacks[0].damage = DiceExpr{0, 0, 1};
    end_turns_until(fight, 2);
    const auto st = fight.rng.save();
    rng::Rng peek(0, 0); peek.load(st);
    const int a = peek.d(20), b = peek.d(20);
    auto swung = step(fight, ActionRequest{2, ActionKind::Attack, 1});
    REQUIRE(swung.ok);
    int natural = -1;
    for (const auto& e : swung.events) if (e.kind == EventKind::AttackResolved) natural = e.a;
    CHECK(natural == std::min(a, b));
}

TEST_CASE("7.2zj Greater Restoration: one option, refusals, and geas ends with any option") {
    auto empty = begin({wiz(1), foe(2)});
    CHECK(std::string(step(empty, choose(1, kRestore, 2, 5, 0)).error) == "bad restoration choice");
    CHECK(std::string(step(empty, choose(1, kRestore, 2, 5, 1)).error) == "restore: nothing to restore");
    CHECK(find(empty, 1)->spell_slots[5] == find(empty, 1)->spell_slots_max[5]);

    auto ex = begin({wiz(1), foe(2)});
    mut(ex, 2).exhaustion_level = 1;
    add_condition(mut(ex, 2), Condition::Exhaustion);
    REQUIRE(step(ex, choose(1, kRestore, 2, 5, 1)).ok);
    CHECK(find(ex, 2)->exhaustion_level == 0);
    CHECK_FALSE(find(ex, 2)->has(Condition::Exhaustion));

    auto two = begin({wiz(1), foe(2)});
    REQUIRE(step(two, cast(1, kCharm, 2, 1)).ok);
    two.budget.action = true;
    REQUIRE(step(two, cast(1, kCharm, 2, 1)).ok);
    CHECK(charmed_effects(two, 2) == 2);
    two.budget.action = true;
    REQUIRE(step(two, choose(1, kRestore, 2, 5, 2)).ok);
    CHECK(charmed_effects(two, 2) == 1);
    CHECK(find(two, 2)->has(Condition::Charmed));

    auto ability = begin({wiz(1), foe(2)});
    mut(ability, 2).abilities.score[std::size_t(Ability::Str)] = 8;
    mut(ability, 2).ability_reduction[std::size_t(Ability::Str)] = 2;
    REQUIRE(step(ability, choose(1, kRestore, 2, 5, 4)).ok);   // ability_str is index 3, choice 4
    CHECK(find(ability, 2)->abilities[Ability::Str] == 10);
    CHECK(find(ability, 2)->ability_reduction[std::size_t(Ability::Str)] == 0);

    auto hp = begin({wiz(1), foe(2)});
    const int cur = find(hp, 2)->hp;
    mut(hp, 2).hp_max_reduction = 5;
    REQUIRE(step(hp, choose(1, kRestore, 2, 5, 10)).ok);
    CHECK(find(hp, 2)->hp_max_reduction == 0);
    CHECK(find(hp, 2)->hp == cur);

    auto geas = begin({wiz(1), foe(2)});
    REQUIRE(step(geas, cast(1, kGeas, 2, 5)).ok);
    CHECK(find(geas, 2)->has(Condition::Charmed));
    mut(geas, 2).exhaustion_level = 1;
    add_condition(mut(geas, 2), Condition::Exhaustion);
    geas.budget.action = true;
    REQUIRE(step(geas, choose(1, kRestore, 2, 5, 1)).ok);
    CHECK_FALSE(effect_on(geas, kGeas, 2));
    CHECK_FALSE(find(geas, 2)->has(Condition::Charmed));

    auto listed = begin({wiz(1), foe(2), wiz(3)});
    mut(listed, 2).exhaustion_level = 1;
    mut(listed, 3).exhaustion_level = 1;
    bool foe_listed = false, ally_listed = false, other = false;
    for (const auto& r : legal_actions(listed)) {
        if (r.spell_id != kRestore) continue;
        if (r.condition_choice == 1 && r.target == 2) foe_listed = true;
        if (r.condition_choice == 1 && r.target == 3) ally_listed = true;
        if (r.condition_choice != 1) other = true;
    }
    CHECK(foe_listed);
    CHECK(ally_listed);
    CHECK_FALSE(other);

    auto minute = begin({wiz(1), foe(2)});
    CHECK(std::string(step(minute, cast(1, kGeasMinute, 2, 5)).error) == "casting time too long for an encounter");
    for (const auto& r : legal_actions(minute)) CHECK(r.spell_id != kGeasMinute);

    auto stays = begin({wiz(1), foe(2), wiz(3)});
    REQUIRE(step(stays, cast(1, kGeas, 2, 5)).ok);
    end_turns_until(stays, 3);
    mut(stays, 3).attacks[0].to_hit = 30;
    mut(stays, 3).attacks[0].damage = DiceExpr{0, 0, 4};
    REQUIRE(step(stays, ActionRequest{3, ActionKind::Attack, 2}).ok);
    CHECK(effect_on(stays, kGeas, 2));
    CHECK(find(stays, 2)->has(Condition::Charmed));

    auto obey = begin({wiz(1), foe(2), wiz(3)});
    REQUIRE(step(obey, cast(1, kGeas, 2, 5)).ok);
    const int before = find(obey, 3)->hp;
    end_turns_until(obey, 2);
    mut(obey, 2).attacks[0].to_hit = 30;
    mut(obey, 2).attacks[0].damage = DiceExpr{0, 0, 4};
    REQUIRE(step(obey, ActionRequest{2, ActionKind::Attack, 3}).ok);
    CHECK(find(obey, 3)->hp == int16_t(before - 4));   // no 5d10 psychic
    CHECK(find(obey, 2)->has(Condition::Charmed));
}
