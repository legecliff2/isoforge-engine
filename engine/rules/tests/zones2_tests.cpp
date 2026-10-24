// R-G11 rules PR 2: Silence, Stinking Cloud, Wall of Fire, Fog Cloud / Darkness flags, and the items PR 1 deferred (Moonbeam's
// shapechanger disadvantage, Spirit Guardians' evil-caster damage, the D7 feature-aura fields), per
// docs/proposals/ZONES_AURAS.md §2.10. Sim's geometry is faked as in zones_tests.cpp.
#include <algorithm>
#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSilence = spell_id_of("test.spell#silence");
const uint32_t kCloud = spell_id_of("test.spell#stinking_cloud");
const uint32_t kWall = spell_id_of("test.spell#wall_of_fire");
const uint32_t kFog = spell_id_of("test.spell#fog_cloud");
const uint32_t kDark = spell_id_of("test.spell#darkness");
const uint32_t kBeam = spell_id_of("test.spell#moonbeam_shape");
const uint32_t kGuard = spell_id_of("test.spell#guardians_evil");
const uint32_t kThunder = spell_id_of("test.spell#thunder");          // S only: 4d1 thunder, no save damage
const uint32_t kAura = spell_id_of("test.feature#aura_of_protection");
const uint32_t kStench = spell_id_of("test.feature#stench");

struct Geo {
    std::set<std::pair<uint32_t, CombatantId>> inside, whole, side;
};

ZoneTriggerRow row(ZoneTrigger on, SpellEffect e, uint8_t group = 1) {
    ZoneTriggerRow r; r.on = on; r.effect = e; r.group = group;
    return r;
}
SpellEffect save_fx(Ability ab, DiceExpr dice, DamageType dt, OnSave on) {
    SpellEffect e; e.kind = SpellEffectKind::Save; e.save_ability = ab; e.dice = dice; e.damage_type = dt; e.on_save = on;
    return e;
}
SpellEffect plain_fx(DiceExpr dice, DamageType dt) {   // no save: the damage always applies
    SpellEffect e; e.kind = SpellEffectKind::ApplyCondition; e.dice = dice; e.damage_type = dt;
    return e;
}
SpellDef zone_spell(uint32_t id, const char* cid, uint8_t level, bool conc) {
    SpellDef d; d.id = id; d.content_id = cid; d.level = level; d.concentration = conc; d.kind = SpellEffectKind::Zone;
    d.range_ft = 120; d.fixed_dc = 30; d.components = kComponentV | kComponentS;
    return d;
}

EncounterHooks hooks(const std::shared_ptr<Geo>& geo) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    {
        SpellDef d = zone_spell(kSilence, "test.spell#silence", 2, true);
        d.zone.shape = AreaShape::Sphere; d.zone.size_ft = 20; d.zone.flags = kZoneSilence;
        book->add(d);
    }
    {   // Stinking Cloud: completely within at the start of its turn: Con save or lose the action; poison / no breath succeed
        SpellDef d = zone_spell(kCloud, "test.spell#stinking_cloud", 3, true);
        d.zone.shape = AreaShape::Sphere; d.zone.size_ft = 20; d.zone.flags = kZoneHeavyObscured;
        ZoneTriggerRow r = row(ZoneTrigger::StartTurn, save_fx(Ability::Con, {0, 0, 0}, DamageType::Poison, OnSave::None), 0);
        r.completely_within = true; r.loses_action = true;
        r.auto_success_damage = bit(DamageType::Poison); r.auto_success_conditions = bit(Condition::Poisoned);
        r.auto_success_no_breath = true;
        d.zone.triggers = {r};
        book->add(d);
    }
    {   // Wall of Fire: Appear Dex half 5d1; ends its turn in the wall or within 10 ft of the side, or enters: 5d1, no save
        SpellDef d = zone_spell(kWall, "test.spell#wall_of_fire", 4, true);
        d.upcast_dice = 1;
        d.zone.shape = AreaShape::Line; d.zone.length_ft = 60; d.zone.height_ft = 20; d.zone.flags = kZoneOpaque;
        d.zone.ring_size_ft = 20; d.zone.sided = true; d.zone.side_ft = 10;
        ZoneTriggerRow end = row(ZoneTrigger::EndTurn, plain_fx({5, 1, 0}, DamageType::Fire), 0);
        end.region = 1;
        d.zone.triggers = {row(ZoneTrigger::Appear, save_fx(Ability::Dex, {5, 1, 0}, DamageType::Fire, OnSave::Half), 0), end,
                           row(ZoneTrigger::Enter, plain_fx({5, 1, 0}, DamageType::Fire))};
        book->add(d);
    }
    {
        SpellDef f = zone_spell(kFog, "test.spell#fog_cloud", 1, true);
        f.zone.shape = AreaShape::Sphere; f.zone.size_ft = 20; f.zone.upcast_size_ft = 20; f.zone.flags = kZoneHeavyObscured;
        book->add(f);
        SpellDef k = zone_spell(kDark, "test.spell#darkness", 2, true);
        k.zone.shape = AreaShape::Sphere; k.zone.size_ft = 15; k.zone.flags = kZoneMagicalDarkness;
        book->add(k);
    }
    {
        SpellDef d = zone_spell(kBeam, "test.spell#moonbeam_shape", 2, true);
        d.zone.shape = AreaShape::Cylinder; d.zone.size_ft = 5; d.zone.height_ft = 40; d.zone.move_cost = 1; d.zone.move_ft = 60;
        d.fixed_dc = 12;
        ZoneTriggerRow r = row(ZoneTrigger::Enter, save_fx(Ability::Con, {2, 1, 0}, DamageType::Radiant, OnSave::Half));
        r.shapechanger_disadvantage = true;
        d.zone.triggers = {r};
        book->add(d);
    }
    {
        SpellDef d = zone_spell(kGuard, "test.spell#guardians_evil", 3, true);
        d.range_ft = 0;
        d.zone.shape = AreaShape::Sphere; d.zone.size_ft = 15; d.zone.anchor = ZoneAnchor::Creature;
        ZoneTriggerRow r = row(ZoneTrigger::Enter, save_fx(Ability::Wis, {3, 1, 0}, DamageType::Radiant, OnSave::Half));
        r.has_evil_damage_type = true; r.evil_damage_type = DamageType::Necrotic;
        d.zone.triggers = {r};
        book->add(d);
    }
    {
        SpellDef t; t.id = kThunder; t.content_id = "test.spell#thunder"; t.level = 1; t.range_ft = 60; t.components = kComponentS;
        t.kind = SpellEffectKind::Save; t.save_ability = Ability::Con; t.dice = {4, 1, 0}; t.damage_type = DamageType::Thunder;
        t.fixed_dc = 30;
        book->add(t);
    }
    {   // a Paladin-style feature aura (D7 model): +Cha mod (min 1) to allies' saves, can't be frightened, needs a conscious owner
        SpellDef a = zone_spell(kAura, "test.feature#aura_of_protection", 0, false);
        a.is_spell = false; a.components = 0; a.range_ft = 0;
        a.zone.shape = AreaShape::Sphere; a.zone.size_ft = 10; a.zone.anchor = ZoneAnchor::Creature;
        a.zone.needs_conscious_owner = true; a.zone.aura_allies_only = true;
        a.zone.aura_modifier.id = 99; a.zone.aura_modifier.stat = ModStat::Save;
        a.zone.has_aura_bonus_ability = true; a.zone.aura_bonus_ability = Ability::Cha;
        a.zone.aura_immunity = bit(Condition::Frightened);
        book->add(a);
    }
    {   // ghast Stench (SRD 5.1): start of turn within 5 ft, DC 10 Con or poisoned until the start of its next turn; a success
        // gives 24 h immunity to this ghast's Stench. Works while the owner is alive (no needs_conscious_owner).
        SpellDef a = zone_spell(kStench, "test.feature#stench", 0, false);
        a.is_spell = false; a.components = 0; a.range_ft = 0; a.fixed_dc = 10;
        a.zone.shape = AreaShape::Sphere; a.zone.size_ft = 5; a.zone.anchor = ZoneAnchor::Creature;
        SpellEffect e = save_fx(Ability::Con, {0, 0, 0}, DamageType::Poison, OnSave::None);
        e.applies_condition = true; e.condition = Condition::Poisoned;
        ZoneTriggerRow r = row(ZoneTrigger::StartTurn, e);
        r.condition_until = 1; r.success_immunity_hours = 24;
        a.zone.triggers = {r};
        book->add(a);
    }
    h.spells = book;
    h.zone_contains = [geo](const EncounterState&, uint32_t z, CombatantId c, bool completely) {
        return completely ? geo->whole.count({z, c}) > 0 : geo->inside.count({z, c}) > 0;
    };
    h.zone_side_contains = [geo](const EncounterState&, uint32_t z, CombatantId c) { return geo->side.count({z, c}) > 0; };
    return h;
}

Combatant caster(CombatantId id, uint8_t team = 0) {
    auto w = wizard(id, team);
    w.spell_slots_max = {0, 4, 3, 3, 2, 1, 0, 0, 0, 0};
    w.spell_slots = w.spell_slots_max;
    w.spells_known = {kSilence, kCloud, kWall, kFog, kDark, kBeam, kGuard, kThunder, kHoldPerson, kFireBolt, kMagicMissile};
    never_react(w);
    return w;
}
Combatant dummy(CombatantId id, uint8_t team = 1) {
    auto m = monster(id);
    m.team = team;
    m.hp = m.hp_max = 300;
    never_react(m);
    return m;
}
EncounterState start(std::vector<Combatant> cs, const std::shared_ptr<Geo>& geo, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(geo));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_zone(CombatantId a, uint32_t spell, uint8_t level, bool dest = true) {
    ActionRequest r{a, ActionKind::CastSpell, 0};
    r.spell_id = spell; r.spell_level = level;
    if (dest) { r.has_zone_dest = true; r.zone_x = 3; r.zone_y = 4; }
    return r;
}
ActionRequest move(CombatantId a, std::vector<uint32_t> entered = {}, std::vector<uint32_t> left = {}) {
    ActionRequest r{a, ActionKind::Move};
    r.move_ft = 5; r.move_tiles = 1;
    r.zones_entered = std::move(entered); r.zones_left = std::move(left);
    return r;
}
std::vector<Event> of_kind(const std::vector<Event>& ev, EventKind k) {
    std::vector<Event> out;
    for (const auto& e : ev) if (e.kind == k) out.push_back(e);
    return out;
}
// Ends turns until `id` acts, collecting every event on the way.
std::vector<Event> run_until(EncounterState& s, CombatantId id) {
    std::vector<Event> all;
    for (int i = 0; i < 20 && current_actor(s) != id; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
    }
    return all;
}
int hp(const EncounterState& s, CombatantId id) { return find(s, id)->hp; }
// Ends the current turn, then runs until `id`'s next turn.
std::vector<Event> next_turn_of(EncounterState& s, CombatantId id) {
    auto r = step(s, {current_actor(s), ActionKind::EndTurn});
    std::vector<Event> all = r.events;
    auto more = run_until(s, id);
    all.insert(all.end(), more.begin(), more.end());
    return all;
}

}  // namespace

TEST_CASE("PR 2 defaults: appended fields keep PR 1 behaviour") {
    ZoneTriggerRow r;
    CHECK(r.region == 0); CHECK_FALSE(r.loses_action); CHECK(r.auto_success_damage == 0); CHECK_FALSE(r.shapechanger_disadvantage);
    CHECK_FALSE(r.has_evil_damage_type);
    ZoneSpec z;
    CHECK(z.upcast_size_ft == 0); CHECK(z.ring_size_ft == 0); CHECK_FALSE(z.sided); CHECK(z.aura_modifier.id == 0);
    CHECK(z.aura_immunity == 0); CHECK_FALSE(z.aura_allies_only);
    ActionRequest q{1, ActionKind::CastSpell};
    CHECK_FALSE(q.has_zone_end); CHECK(q.zone_side == 0); CHECK_FALSE(q.zone_ring);
    TurnBudget b; CHECK_FALSE(b.action_lost);
    Combatant c = monster(1);
    CHECK_FALSE(c.no_breath); CHECK_FALSE(c.shapechanger); CHECK_FALSE(c.evil);
    ActiveZone a; CHECK(a.size_ft == 0); CHECK(a.side == 0); CHECK_FALSE(a.feature);
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    CHECK_FALSE(in_silence(s, 1));
}

TEST_CASE("Silence: deafened while completely inside, no verbal components, thunder immunity; ends with concentration") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), caster(2, 1), dummy(3), dummy(4)}, geo);
    geo->inside = {{1, 2}, {1, 4}}; geo->whole = {{1, 2}};   // 4 is only partly inside
    auto r = step(s, cast_zone(1, kSilence, 2));
    REQUIRE_MESSAGE(r.ok, r.error);
    const auto added = of_kind(r.events, EventKind::ConditionAdded);
    REQUIRE(added.size() == 1);
    CHECK(added[0].actor == 2); CHECK(added[0].a == int32_t(Condition::Deafened));
    CHECK(find(s, 2)->has(Condition::Deafened)); CHECK_FALSE(find(s, 4)->has(Condition::Deafened));
    CHECK(of_kind(r.events, EventKind::ZoneCreated)[0].c == kZoneSilence);
    CHECK(in_silence(s, 2)); CHECK_FALSE(in_silence(s, 4)); CHECK_FALSE(in_silence(s, 1));
    // thunder: no damage inside, full damage outside (the caster is outside the zone)
    const int h2 = hp(s, 2), h3 = hp(s, 3);
    next_turn_of(s, 1);
    REQUIRE(step(s, cast(1, kThunder, 2, 1)).ok);
    CHECK(hp(s, 2) == h2);
    next_turn_of(s, 1);
    REQUIRE(step(s, cast(1, kThunder, 3, 1)).ok);
    CHECK(hp(s, 3) == h3 - 4);
    // the caster inside: no Fire Bolt / Magic Missile (V); nothing is rolled
    end_turns_until(s, 2);
    const auto before = s.rng.save();
    auto fb = step(s, cast(2, kFireBolt, 1));
    CHECK_FALSE(fb.ok);
    CHECK(fb.error.find("silence") != std::string::npos);
    CHECK(s.rng.save().state == before.state);
    for (const auto& q : legal_actions(s))
        CHECK_FALSE((q.kind == ActionKind::CastSpell && (q.spell_id == kFireBolt || q.spell_id == kMagicMissile || q.spell_id == kHoldPerson)));
    bool thunder_listed = false;   // S only: still castable
    for (const auto& q : legal_actions(s)) thunder_listed = thunder_listed || (q.kind == ActionKind::CastSpell && q.spell_id == kThunder);
    CHECK(thunder_listed);
    // leaving ends the deafness; coming back completely inside starts it again
    geo->inside.erase({1, 2}); geo->whole.clear();
    auto out = step(s, move(2, {}, {1}));
    REQUIRE(out.ok);
    CHECK(of_kind(out.events, EventKind::ConditionRemoved).size() == 1);
    CHECK_FALSE(find(s, 2)->has(Condition::Deafened));
    geo->inside.insert({1, 2}); geo->whole = {{1, 2}};
    auto back = step(s, move(2, {1}));
    REQUIRE(back.ok);
    CHECK(find(s, 2)->has(Condition::Deafened));
    // the caster concentrates on something else: the zone ends (reason 1) and the deafness with it
    next_turn_of(s, 1);
    auto hold = step(s, cast(1, kHoldPerson, 3, 2));
    REQUIRE(hold.ok);
    const auto ended = of_kind(hold.events, EventKind::ZoneEnded);
    REQUIRE(ended.size() == 1); CHECK(ended[0].b == 1);
    CHECK_FALSE(find(s, 2)->has(Condition::Deafened));
    CHECK(s.zones.empty());
}

TEST_CASE("Stinking Cloud: completely within at the start of its turn: the action is lost, movement and bonus action remain") {
    auto geo = std::make_shared<Geo>();
    Combatant immune = dummy(3); immune.immunities = bit(DamageType::Poison);
    Combatant golem = dummy(4); golem.no_breath = true;
    auto s = start({caster(1), dummy(2), immune, golem, dummy(5)}, geo);
    geo->inside = {{1, 2}, {1, 3}, {1, 4}, {1, 5}};
    geo->whole = {{1, 2}, {1, 3}, {1, 4}};   // 5 is only partly inside
    REQUIRE(step(s, cast_zone(1, kCloud, 3)).ok);
    // one round: every other creature starts one turn
    std::vector<Event> all;
    for (int i = 0; i < 4; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
        const CombatantId now = current_actor(s);
        if (now == 2) {
            CHECK_FALSE(s.budget.action); CHECK(s.budget.action_lost);
            CHECK(s.budget.bonus_action); CHECK(s.budget.movement_ft > 0);
            CHECK_FALSE(step(s, {2, ActionKind::Dodge}).ok);
            CHECK(step(s, move(2)).ok);   // movement remains
        } else if (now != 1) {
            CHECK(s.budget.action); CHECK_FALSE(s.budget.action_lost);
        }
    }
    const auto ft = of_kind(all, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 3);   // 5 is only partly inside
    for (const auto& e : ft) {
        CHECK(e.b == int32_t(TriggerOn::ZoneStartTurn));
        if (e.target == 2) CHECK(e.c == 3);   // failed (DC 30)
        else { CHECK((e.target == 3 || e.target == 4)); CHECK(e.c == 2); }   // poison immune / doesn't breathe: automatic
    }
    int saves = 0;
    for (const auto& e : of_kind(all, EventKind::SavingThrowRolled)) saves += (e.actor == 3 || e.actor == 4) ? 1 : 0;
    CHECK(saves == 0);
    // 2 left the cloud: its next turn has its action back
    geo->whole.erase({1, 2}); geo->inside.erase({1, 2});
    next_turn_of(s, 2);
    CHECK(s.budget.action); CHECK_FALSE(s.budget.action_lost);
}

TEST_CASE("Wall of Fire: wall choices, appear save, end-of-turn damage in the wall or on the chosen side, upcast") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2), dummy(3), dummy(4)}, geo);
    // validation
    CHECK_FALSE(step(s, cast_zone(1, kWall, 4)).ok);                                  // the side is required
    ActionRequest bad = cast_zone(1, kWall, 4); bad.zone_side = 2;
    CHECK_FALSE(step(s, bad).ok);
    ActionRequest ring_end = cast_zone(1, kWall, 4); ring_end.zone_side = 1; ring_end.zone_ring = true; ring_end.has_zone_end = true;
    CHECK_FALSE(step(s, ring_end).ok);                                                // a ring has no end tile
    ActionRequest fog_side = cast_zone(1, kFog, 1); fog_side.zone_side = 1;
    CHECK_FALSE(step(s, fog_side).ok);                                                // no damaging side
    ActionRequest fog_ring = cast_zone(1, kFog, 1); fog_ring.zone_ring = true;
    CHECK_FALSE(step(s, fog_ring).ok);
    ActionRequest fog_end = cast_zone(1, kFog, 1); fog_end.has_zone_end = true;
    CHECK_FALSE(step(s, fog_end).ok);                                                 // not a line
    ActionRequest hold = cast(1, kHoldPerson, 2, 2); hold.zone_side = 1;
    CHECK_FALSE(step(s, hold).ok);                                                    // not a zone spell
    ActionRequest mv = move(1); mv.zone_ring = true;
    CHECK_FALSE(step(s, mv).ok);                                                      // CastSpell only
    CHECK(s.zones.empty());
    // legal forms: line / ring x side +1 / -1 at each slot level
    int forms = 0;
    for (const auto& q : legal_actions(s)) forms += (q.spell_id == kWall && q.spell_level == 4) ? 1 : 0;
    CHECK(forms == 4);
    // cast at slot 5 as a line, side -1: 2 stands in the wall when it appears (5d1 + 1d1 = 6, DC 30 fails)
    geo->inside = {{1, 2}};
    ActionRequest c = cast_zone(1, kWall, 5); c.has_zone_end = true; c.zone_x2 = 9; c.zone_y2 = 4; c.zone_side = -1;
    const int h2 = hp(s, 2);
    auto r = step(s, c);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(s.zones.size() == 1);
    const ActiveZone& z = s.zones[0];
    CHECK(z.has_end_tile); CHECK(z.end_x == 9); CHECK(z.end_y == 4); CHECK(z.side == -1); CHECK_FALSE(z.ring);
    CHECK(hp(s, 2) == h2 - 6);
    // end of turn: 2 in the wall and 3 on the damaging side take 6 with no save; 4 is neither
    geo->side = {{1, 3}};
    const int h3 = hp(s, 3), h4 = hp(s, 4);
    std::vector<Event> all = run_until(s, 1);
    auto e1 = step(s, {1, ActionKind::EndTurn});
    all.insert(all.end(), e1.events.begin(), e1.events.end());
    auto more = run_until(s, 1);
    all.insert(all.end(), more.begin(), more.end());
    CHECK(hp(s, 2) == h2 - 12);
    CHECK(hp(s, 3) == h3 - 6);
    CHECK(hp(s, 4) == h4);
    int end_hits = 0, end_saves = 0;
    for (const auto& e : of_kind(all, EventKind::FeatureTriggered))
        if (e.b == int32_t(TriggerOn::ZoneEndTurn)) { ++end_hits; CHECK(e.c == 1); }
    for (const auto& e : of_kind(all, EventKind::SavingThrowRolled)) end_saves += (e.actor == 2 || e.actor == 3) ? 1 : 0;
    CHECK(end_hits == 2); CHECK(end_saves == 0);
    // a ring instead: size = ring_size_ft
    auto s2 = start({caster(1), dummy(2)}, std::make_shared<Geo>());
    ActionRequest ring = cast_zone(1, kWall, 4); ring.zone_ring = true; ring.zone_side = 1;
    REQUIRE(step(s2, ring).ok);
    REQUIRE(s2.zones.size() == 1);
    CHECK(s2.zones[0].ring); CHECK(s2.zones[0].size_ft == 20); CHECK(s2.zones[0].side == 1); CHECK_FALSE(s2.zones[0].has_end_tile);
}

TEST_CASE("Fog Cloud and Darkness: flags only; Fog Cloud's radius grows per slot level") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    auto r = step(s, cast_zone(1, kFog, 2));
    REQUIRE(r.ok);
    CHECK(of_kind(r.events, EventKind::ZoneCreated)[0].c == kZoneHeavyObscured);
    CHECK(s.zones[0].size_ft == 40);
    next_turn_of(s, 1);
    auto d = step(s, cast_zone(1, kDark, 2));
    REQUIRE(d.ok);
    CHECK(of_kind(d.events, EventKind::ZoneCreated)[0].c == kZoneMagicalDarkness);
    REQUIRE(s.zones.size() == 1);   // the Fog Cloud ended with concentration
    CHECK(s.zones[0].size_ft == 15);
    CHECK(count(d.events, EventKind::FeatureTriggered) == 0);
}

TEST_CASE("Moonbeam: a shapechanger saves with disadvantage (one more d20)") {
    auto run = [](bool shape, int& natural, uint64_t& rng_after) {
        auto geo = std::make_shared<Geo>();
        Combatant t = dummy(2); t.shapechanger = shape;
        auto s = start({caster(1), t}, geo, 7);
        REQUIRE(step(s, cast_zone(1, kBeam, 2)).ok);
        end_turns_until(s, 2);
        auto in = step(s, move(2, {1}));
        REQUIRE(in.ok);
        const auto sv = of_kind(in.events, EventKind::SavingThrowRolled);
        REQUIRE(sv.size() == 1);
        natural = sv[0].a;
        rng_after = s.rng.save().state;
    };
    int n_norm = 0, n_shape = 0; uint64_t r_norm = 0, r_shape = 0;
    run(false, n_norm, r_norm);
    run(true, n_shape, r_shape);
    CHECK(n_shape <= n_norm);   // the same first d20, then the lower of two
    CHECK(r_shape != r_norm);
}

TEST_CASE("Spirit Guardians: an evil caster's aura deals necrotic damage") {
    auto hit = [](bool evil) {
        auto geo = std::make_shared<Geo>();
        Combatant c = caster(1); c.evil = evil;
        Combatant t = dummy(2); t.resistances = bit(DamageType::Necrotic);
        auto s = start({c, t}, geo);
        REQUIRE(step(s, cast_zone(1, kGuard, 3, false)).ok);
        end_turns_until(s, 2);
        const int before = hp(s, 2);
        REQUIRE(step(s, move(2, {1})).ok);
        return before - hp(s, 2);
    };
    CHECK(hit(false) == 3);   // radiant, 3d1
    CHECK(hit(true) == 1);    // necrotic, resisted
}

TEST_CASE("D7 feature aura: allies' save bonus, condition immunity, suppressed while the owner is down, ends with its death") {
    auto total = [](bool aura, bool owner_down, CombatantId target) {
        auto geo = std::make_shared<Geo>();
        Combatant pal = pc(1, 40); pal.abilities.score[std::size_t(Ability::Cha)] = 16; never_react(pal);
        Combatant ally = pc(2, 40); never_react(ally);
        Combatant foe = pc(4, 40); foe.team = 1; never_react(foe);
        auto s = begin_encounter({pal, ally, caster(3, 1), foe}, 5, nullptr, hooks(geo));
        if (aura) {
            std::vector<Event> ev;
            const uint32_t id = add_feature_aura(s, 1, kAura, &ev);
            REQUIRE(id == 1);
            REQUIRE(ev.size() == 1); CHECK(ev[0].kind == EventKind::ZoneCreated); CHECK(ev[0].target == 1);
            CHECK(s.zones[0].feature);
            geo->inside = {{1, 2}, {1, 4}};
        }
        if (owner_down) { mut(s, 1).hp = 0; add_condition(mut(s, 1), Condition::Unconscious); }
        end_turns_until(s, 3);
        auto r = step(s, cast(3, kHoldPerson, target, 2));
        REQUIRE_MESSAGE(r.ok, r.error);
        const auto sv = of_kind(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv.size() == 1);
        return sv[0].b;
    };
    CHECK(total(true, false, 2) == total(false, false, 2) + 3);   // Cha 16: +3
    CHECK(total(true, false, 4) == total(false, false, 4));       // a foe in the aura: allies only
    CHECK(total(true, true, 2) == total(false, true, 2));         // owner unconscious: suppressed
    // condition immunity, death
    auto geo = std::make_shared<Geo>();
    Combatant pal = pc(1, 40); never_react(pal);
    Combatant ally = pc(2, 40); never_react(ally);
    Combatant foe = pc(4, 40); foe.team = 1; never_react(foe);
    auto s = begin_encounter({pal, ally, caster(3, 1), foe}, 5, nullptr, hooks(geo));
    CHECK(add_feature_aura(s, 1, kFireBolt) == 0);   // not a zone
    CHECK(add_feature_aura(s, 9, kAura) == 0);       // no such owner
    REQUIRE(add_feature_aura(s, 1, kAura) == 1);
    geo->inside = {{1, 2}, {1, 4}};
    std::vector<Event> ev;
    CHECK_FALSE(apply_sourced_condition(s, 3, 2, Condition::Frightened, 0, ev));   // ally in the aura
    CHECK_FALSE(apply_sourced_condition(s, 3, 1, Condition::Frightened, 0, ev));   // the owner itself
    CHECK(apply_sourced_condition(s, 3, 4, Condition::Frightened, 0, ev));         // a foe
    mut(s, 1).dead = true;
    std::vector<Event> all = run_until(s, 3);
    for (int i = 0; i < 4; ++i) { auto x = step(s, {current_actor(s), ActionKind::EndTurn}); all.insert(all.end(), x.events.begin(), x.events.end()); }
    const auto ended = of_kind(all, EventKind::ZoneEnded);
    REQUIRE(ended.size() == 1); CHECK(ended[0].b == 2);
    CHECK(s.zones.empty());
}

TEST_CASE("zones_digest covers the PR 2 fields") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    ActionRequest c = cast_zone(1, kWall, 4); c.zone_side = 1;
    REQUIRE(step(s, c).ok);
    const uint64_t h = zones_digest(s);
    s.zones[0].side = -1;
    const uint64_t h_side = zones_digest(s);
    CHECK(h_side != h);
    s.zones[0].side = 1; s.budget.action_lost = true;
    CHECK(zones_digest(s) != h);
    s.budget.action_lost = false;
    CHECK(zones_digest(s) == h);
}

// ---- batch 3 ghast Stench (Lich rulings): creature-sourced aura, alive = works (unconscious too), ends at death ----
namespace {
int saves_of(const std::vector<Event>& ev, CombatantId who) {
    int n = 0;
    for (const auto& e : of_kind(ev, EventKind::SavingThrowRolled)) n += e.actor == who;
    return n;
}
EncounterState stench_start(const std::shared_ptr<Geo>& geo, int16_t dc) {
    Combatant ghast = dummy(1); ghast.hp = ghast.hp_max = 36;
    Combatant a = pc(2, 40); never_react(a);
    Combatant b = pc(3, 40); never_react(b);
    Combatant other = dummy(4);   // keeps the ghast's side in the fight while the ghast is down
    auto s = begin_encounter({ghast, a, b, other}, 5, nullptr, hooks(geo));
    std::vector<Event> ev;
    REQUIRE(add_feature_aura(s, 1, kStench, &ev) == 1);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == EventKind::ZoneCreated);
    CHECK(s.zones[0].feature);
    CHECK(s.zones[0].dc == 10);   // the SRD's fixed DC
    s.zones[0].dc = dc;           // 30 = always fails, 1 = always succeeds (Con 10)
    geo->inside = {{1, 2}};       // 2 within 5 ft, 3 outside
    return s;
}
}  // namespace

TEST_CASE("ghast Stench: start-of-turn save inside the radius only; poisoned until the start of its next turn") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 30);
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    CHECK(find(s, 2)->has(Condition::Poisoned));
    geo->inside.clear();          // leaves the radius: still poisoned until the start of its next turn
    ev = run_until(s, 3);
    CHECK(saves_of(ev, 3) == 0);
    CHECK_FALSE(find(s, 3)->has(Condition::Poisoned));
    CHECK(find(s, 2)->has(Condition::Poisoned));
    ev = run_until(s, 2);
    CHECK(saves_of(ev, 2) == 0);  // outside now: no save
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));   // ended at the start of its turn
    CHECK(s.aura_immunities.empty());   // failures give no immunity
}

TEST_CASE("ghast Stench: a success gives 24 h immunity to this ghast's Stench (per source)") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 1);
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    REQUIRE(s.aura_immunities.size() == 1);
    CHECK(s.aura_immunities[0].creature == 2);
    CHECK(s.aura_immunities[0].source == 1);
    CHECK(s.aura_immunities[0].spell_id == kStench);
    CHECK(s.aura_immunities[0].until_round == uint32_t(s.round) + 24u * 600u);
    s.zones[0].dc = 30;           // now it would fail, but the creature is immune
    for (int k = 0; k < 3; ++k) { ev = next_turn_of(s, 2); CHECK(saves_of(ev, 2) == 0); }
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    s.aura_immunities[0].until_round = uint32_t(s.round) + 1;   // 24 h later (the test advances the clock): saves again
    ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    CHECK(find(s, 2)->has(Condition::Poisoned));
}

TEST_CASE("ghast Stench: works while the ghast is unconscious, stops at its death") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 30);
    add_condition(mut(s, 1), Condition::Unconscious);   // incapacitated / unconscious but alive: still works
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    CHECK(find(s, 2)->has(Condition::Poisoned));
    mut(s, 1).dead = true;
    ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 0);   // dead: no save, even before the round-start removal
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    for (int k = 0; k < 3; ++k) ev = next_turn_of(s, 2);
    CHECK(s.zones.empty());
}

// Lich ruling (Stench expiry is per source): when the Stench poison ends it removes only its own instance; poison from
// another source stays. The instances are ActiveSpellEffects keyed by (spell_id, caster); end_effect clears the condition
// only when no other effect / sourced condition / untracked prior instance still holds it.
TEST_CASE("ghast Stench: its poison expiring leaves another source's poison (tracked effect)") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 30);
    const uint32_t kOther = spell_id_of("test.other#poison");
    add_condition(mut(s, 2), Condition::Poisoned);   // poisoned by another source first (no expiry)
    s.effects.push_back(ActiveSpellEffect{kOther, 4, 2, Condition::Poisoned, Ability::Con, 12, RepeatSave::None, false,
                                          Timed{}, 0, false});
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);                     // then by Stench
    REQUIRE(std::count_if(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.spell_id == kStench; }) == 1);
    geo->inside.clear();
    ev = next_turn_of(s, 2);                            // Stench's poison expires at the start of its next turn
    CHECK(std::none_of(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.spell_id == kStench; }));
    CHECK(find(s, 2)->has(Condition::Poisoned));     // still poisoned by the other source
    CHECK(of_kind(ev, EventKind::ConditionRemoved).empty());
}

TEST_CASE("ghast Stench: its poison expiring leaves an untracked prior poison (scenario / content condition)") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 30);
    add_condition(mut(s, 2), Condition::Poisoned);   // poisoned before combat (no effect record)
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    geo->inside.clear();
    ev = next_turn_of(s, 2);
    CHECK(std::none_of(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.spell_id == kStench; }));
    CHECK(find(s, 2)->has(Condition::Poisoned));
}

TEST_CASE("ghast Stench: its poison expiring leaves a sourced poison") {
    auto geo = std::make_shared<Geo>();
    auto s = stench_start(geo, 30);
    std::vector<Event> sev;
    REQUIRE(apply_sourced_condition(s, 4, 2, Condition::Poisoned, 0, sev));
    auto ev = next_turn_of(s, 2);
    CHECK(saves_of(ev, 2) == 1);
    geo->inside.clear();
    ev = next_turn_of(s, 2);
    CHECK(find(s, 2)->has(Condition::Poisoned));
}
