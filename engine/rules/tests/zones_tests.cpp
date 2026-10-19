// R-G11 rules PR 1: persistent zones and auras, per docs/proposals/ZONES_AURAS.md (§2, §3 contract). Sim's geometry is faked:
// `Geo::inside` answers hooks.zone_contains, and requests carry the membership lists sim would fill at submit.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kGuardians = spell_id_of("test.spell#spirit_guardians");
const uint32_t kGuardiansFixed = spell_id_of("test.spell#spirit_guardians_fixed");   // 3d1 radiant: exact damage
const uint32_t kMoonbeam = spell_id_of("test.spell#moonbeam");
const uint32_t kMoonbeamEnter = spell_id_of("test.spell#moonbeam_counts_move");     // counts_aura_move_as_enter
const uint32_t kSpikes = spell_id_of("test.spell#spike_growth");
const uint32_t kGrease = spell_id_of("test.spell#grease");
const uint32_t kWeb = spell_id_of("test.spell#web");
const uint32_t kWebEasy = spell_id_of("test.spell#web_easy");   // DC 1: escape always succeeds
const uint32_t kPush = spell_id_of("test.spell#push");          // ForcedMove 10 ft, no damage

struct Geo {
    std::set<std::pair<uint32_t, CombatantId>> inside;   // (zone, creature): in the zone now
    std::set<std::pair<uint32_t, CombatantId>> whole;    // completely within
    ForcedMoveResult push{};                             // the forced_move hook's answer (feet_moved = requested when 0)
    int calls = 0;
};

SpellEffect save_row_effect(Ability ab, DiceExpr dice, DamageType dt, OnSave on) {
    SpellEffect e; e.kind = SpellEffectKind::Save; e.save_ability = ab; e.dice = dice; e.damage_type = dt; e.on_save = on;
    return e;
}
ZoneTriggerRow row(ZoneTrigger on, SpellEffect e, uint8_t group = 1) {
    ZoneTriggerRow r; r.on = on; r.effect = e; r.group = group;
    return r;
}

SpellDef zone_spell(uint32_t id, const char* cid, uint8_t level, bool conc) {
    SpellDef d; d.id = id; d.content_id = cid; d.level = level; d.concentration = conc; d.kind = SpellEffectKind::Zone;
    d.range_ft = 120; d.fixed_dc = 30;
    return d;
}

EncounterHooks hooks(const std::shared_ptr<Geo>& geo) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    {   // Spirit Guardians: 15 ft aura on the caster; Enter / StartTurn share group 1; Wis half; +1d8 per slot above 3
        SpellDef d = zone_spell(kGuardians, "test.spell#spirit_guardians", 3, true);
        d.range_ft = 0; d.upcast_dice = 1;
        d.zone.shape = AreaShape::Sphere; d.zone.size_ft = 15; d.zone.anchor = ZoneAnchor::Creature; d.zone.flags = kZoneHalveSpeed;
        d.zone.chooses_excluded = true;
        const SpellEffect e = save_row_effect(Ability::Wis, {3, 8, 0}, DamageType::Radiant, OnSave::Half);
        d.zone.triggers = {row(ZoneTrigger::Enter, e), row(ZoneTrigger::StartTurn, e)};
        book->add(d);
        SpellDef f = d; f.id = kGuardiansFixed; f.content_id = "test.spell#spirit_guardians_fixed";
        const SpellEffect fe = save_row_effect(Ability::Wis, {3, 1, 0}, DamageType::Radiant, OnSave::Half);
        f.zone.triggers = {row(ZoneTrigger::Enter, fe), row(ZoneTrigger::StartTurn, fe)};
        book->add(f);
    }
    {   // Moonbeam: 5 ft cylinder at a point; an action moves it up to 60 ft on later turns
        SpellDef d = zone_spell(kMoonbeam, "test.spell#moonbeam", 2, true);
        d.upcast_dice = 1;
        d.zone.shape = AreaShape::Cylinder; d.zone.size_ft = 5; d.zone.height_ft = 40; d.zone.move_cost = 1; d.zone.move_ft = 60;
        const SpellEffect e = save_row_effect(Ability::Con, {2, 10, 0}, DamageType::Radiant, OnSave::Half);
        d.zone.triggers = {row(ZoneTrigger::Enter, e), row(ZoneTrigger::StartTurn, e)};
        book->add(d);
        SpellDef m = d; m.id = kMoonbeamEnter; m.content_id = "test.spell#moonbeam_counts_move";
        for (auto& r : m.zone.triggers) r.counts_aura_move_as_enter = true;
        book->add(m);
    }
    {   // Spike Growth: 2d4 piercing per 5 ft moved into / within, no save, no per-turn limit; difficult terrain
        SpellDef d = zone_spell(kSpikes, "test.spell#spike_growth", 2, true);
        d.zone.shape = AreaShape::Sphere; d.zone.size_ft = 20; d.zone.flags = kZoneDifficult;
        SpellEffect e; e.kind = SpellEffectKind::ApplyCondition; e.dice = {2, 4, 0}; e.damage_type = DamageType::Piercing;
        d.zone.triggers = {row(ZoneTrigger::MoveWithin, e, 0)};
        book->add(d);
    }
    {   // Grease: no concentration, 1 round here (timer); Appear / Enter / EndTurn: Dex save or prone
        SpellDef d = zone_spell(kGrease, "test.spell#grease", 1, false);
        d.expiry = 1; d.duration_rounds = 1;
        d.zone.shape = AreaShape::Cube; d.zone.size_ft = 10; d.zone.flags = kZoneDifficult;
        SpellEffect e = save_row_effect(Ability::Dex, {0, 0, 0}, DamageType::Force, OnSave::None);
        e.applies_condition = true; e.condition = Condition::Prone;
        d.zone.triggers = {row(ZoneTrigger::Appear, e, 0), row(ZoneTrigger::Enter, e, 0), row(ZoneTrigger::EndTurn, e, 0)};
        book->add(d);
    }
    {   // Web: starts its turn in it, or enters it during its own turn: Dex save or restrained while in the webs
        SpellDef d = zone_spell(kWeb, "test.spell#web", 2, true);
        d.zone.shape = AreaShape::Cube; d.zone.size_ft = 20; d.zone.flags = kZoneDifficult | kZoneLightObscured;
        SpellEffect e = save_row_effect(Ability::Dex, {0, 0, 0}, DamageType::Force, OnSave::None);
        e.applies_condition = true; e.condition = Condition::Restrained;
        ZoneTriggerRow st = row(ZoneTrigger::StartTurn, e, 0); st.while_in_zone = true;
        ZoneTriggerRow en = row(ZoneTrigger::Enter, e, 0); en.while_in_zone = true; en.own_turn_only = true;
        d.zone.triggers = {st, en};
        book->add(d);
        SpellDef easy = d; easy.id = kWebEasy; easy.content_id = "test.spell#web_easy"; easy.fixed_dc = 1;
        book->add(easy);
    }
    {
        SpellDef p; p.id = kPush; p.content_id = "test.spell#push"; p.level = 1; p.range_ft = 30;
        p.kind = SpellEffectKind::ForcedMove; p.forced.mode = 1; p.forced.feet = 10; p.targets = 3;
        book->add(p);
    }
    h.spells = book;
    h.zone_contains = [geo](const EncounterState&, uint32_t z, CombatantId c, bool completely) {
        return completely ? geo->whole.count({z, c}) > 0 : geo->inside.count({z, c}) > 0;
    };
    h.forced_move = [geo](const EncounterState&, const ForcedMoveQuery& q) {
        ++geo->calls;
        ForcedMoveResult r = geo->push;
        if (r.feet_moved == 0) r.feet_moved = q.feet;
        return r;
    };
    return h;
}

Combatant caster(CombatantId id) {
    auto w = wizard(id);
    w.spell_slots_max = {0, 4, 3, 3, 2, 0, 0, 0, 0, 0};
    w.spell_slots = w.spell_slots_max;
    w.spells_known = {kGuardians, kGuardiansFixed, kMoonbeam, kMoonbeamEnter, kSpikes, kGrease, kWeb, kWebEasy, kPush, kHoldPerson};
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
ActionRequest cast_zone(CombatantId a, uint32_t spell, uint8_t level, bool dest = true, int32_t x = 3, int32_t y = 4) {
    ActionRequest r{a, ActionKind::CastSpell, 0};
    r.spell_id = spell; r.spell_level = level;
    if (dest) { r.has_zone_dest = true; r.zone_x = x; r.zone_y = y; }
    return r;
}
ActionRequest move(CombatantId a, std::vector<uint32_t> entered = {}, std::vector<uint32_t> left = {},
                   std::vector<uint32_t> within = {}) {
    ActionRequest r{a, ActionKind::Move};
    r.move_ft = 5; r.move_tiles = 1;
    r.zones_entered = std::move(entered); r.zones_left = std::move(left); r.zones_within = std::move(within);
    return r;
}
std::vector<Event> of_kind(const std::vector<Event>& ev, EventKind k) {
    std::vector<Event> out;
    for (const auto& e : ev) if (e.kind == k) out.push_back(e);
    return out;
}
int index_of(const std::vector<Event>& ev, EventKind k, bool last = false) {
    int at = -1;
    for (int i = 0; i < int(ev.size()); ++i)
        if (ev[std::size_t(i)].kind == k) { at = i; if (!last) break; }
    return at;
}
int hp(const EncounterState& s, CombatantId id) { return find(s, id)->hp; }

}  // namespace

TEST_CASE("defaults: no zone, no zone state, appended enum values") {
    ZoneSpec z;
    CHECK(z.anchor == ZoneAnchor::Point); CHECK(z.flags == 0); CHECK(z.move_cost == 0); CHECK(z.triggers.empty());
    ZoneTriggerRow r;
    CHECK(r.on == ZoneTrigger::Enter); CHECK(r.group == 1); CHECK_FALSE(r.counts_aura_move_as_enter); CHECK_FALSE(r.while_in_zone);
    CHECK(uint8_t(SpellEffectKind::Zone) == 16);
    CHECK(uint8_t(EventKind::ZoneCreated) == 45); CHECK(uint8_t(EventKind::ZoneEnded) == 47);
    CHECK(uint8_t(TriggerOn::ZoneAppear) == 8); CHECK(uint8_t(TriggerOn::ZoneMoveWithin) == 12);
    ActionRequest q{1, ActionKind::Move};
    CHECK(q.zones_entered.empty()); CHECK(q.zone_move == 0); CHECK_FALSE(q.has_zone_dest);
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    CHECK(s.zones.empty()); CHECK(s.next_zone_id == 1);
    CHECK_FALSE(has_zone_state(s));
    // a Move with no zones: unchanged events (Moved only)
    auto r2 = step(s, move(1));
    REQUIRE(r2.ok);
    CHECK(r2.events.size() == 1);
}

TEST_CASE("Spirit Guardians: an aura on the caster; Enter and StartTurn share one hit per turn; excluded and owner unaffected") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2), dummy(3), dummy(4, 0)}, geo);
    ActionRequest c = cast_zone(1, kGuardiansFixed, 3, false);
    c.zone_excluded = {3};
    auto r = step(s, c);
    REQUIRE_MESSAGE(r.ok, r.error);
    const auto created = of_kind(r.events, EventKind::ZoneCreated);
    REQUIRE(created.size() == 1);
    CHECK(created[0].actor == 1); CHECK(created[0].target == 1);   // anchored on the caster
    CHECK(created[0].a == 1); CHECK(created[0].b == int32_t(kGuardiansFixed)); CHECK(created[0].c == kZoneHalveSpeed);
    CHECK(count(r.events, EventKind::FeatureTriggered) == 0);   // no Appear row
    REQUIRE(s.zones.size() == 1);
    CHECK(s.zones[0].excluded == std::vector<CombatantId>{3});
    CHECK(find(s, 1)->concentration_spell == kGuardiansFixed);
    CHECK(has_zone_state(s));
    // the dummy walks in on its turn: one hit (DC 30 fails: 3d1 = 3)
    end_turns_until(s, 2);
    const int before = hp(s, 2);
    auto in = step(s, move(2, {1}));
    REQUIRE(in.ok);
    const auto ft = of_kind(in.events, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 1);
    CHECK(ft[0].actor == 1); CHECK(ft[0].target == 2); CHECK(ft[0].a == int32_t(kGuardiansFixed));
    CHECK(ft[0].b == int32_t(TriggerOn::ZoneEnter)); CHECK(ft[0].c == 3);
    CHECK(index_of(in.events, EventKind::Moved) < index_of(in.events, EventKind::FeatureTriggered));
    CHECK(index_of(in.events, EventKind::FeatureTriggered) < index_of(in.events, EventKind::SavingThrowRolled));
    CHECK(hp(s, 2) == before - 3);
    // leaves and re-enters on the same turn: no second hit (D2)
    REQUIRE(step(s, move(2, {}, {1})).ok);
    auto again = step(s, move(2, {1}));
    REQUIRE(again.ok);
    CHECK(count(again.events, EventKind::FeatureTriggered) == 0);
    // its next turn starts inside: hit at the start of the turn, right after TurnStarted
    geo->inside = {{1, 2}, {1, 3}, {1, 1}};
    end_turns_until(s, 1);
    auto e = step(s, {1, ActionKind::EndTurn});   // the caster's own aura never hits it
    CHECK(count(e.events, EventKind::FeatureTriggered) == 0);
    std::vector<Event> all = e.events;
    for (int i = 0; i < 6 && current_actor(s) != 2; ++i) { auto x = step(s, {current_actor(s), ActionKind::EndTurn}); all.insert(all.end(), x.events.begin(), x.events.end()); }
    const auto starts = of_kind(all, EventKind::FeatureTriggered);
    REQUIRE(starts.size() == 1);   // 3 is excluded
    CHECK(starts[0].target == 2); CHECK(starts[0].b == int32_t(TriggerOn::ZoneStartTurn));
    // ... and entering again later that turn does not hit twice
    REQUIRE(current_actor(s) == 2);
    auto re = step(s, move(2, {1}));
    CHECK(count(re.events, EventKind::FeatureTriggered) == 0);
}

TEST_CASE("Spirit Guardians: upcast dice scale per slot level above 3") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kGuardiansFixed, 4, false)).ok);   // 3d1 + 1d1 = 4
    end_turns_until(s, 2);
    const int before = hp(s, 2);
    REQUIRE(step(s, move(2, {1})).ok);
    CHECK(hp(s, 2) == before - 4);
}

TEST_CASE("validation: zone fields belong to zone casts") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    CHECK_FALSE(step(s, cast_zone(1, kGuardians, 3, true)).ok);             // an aura has no destination
    ActionRequest ex = cast_zone(1, kMoonbeam, 2); ex.zone_excluded = {2};
    CHECK_FALSE(step(s, ex).ok);                                              // Moonbeam cannot exclude
    ActionRequest mv = move(1); mv.has_zone_dest = true;
    CHECK_FALSE(step(s, mv).ok);                                              // CastSpell only
    ActionRequest hold = cast(1, kHoldPerson, 2, 2); hold.has_zone_dest = true;
    CHECK_FALSE(step(s, hold).ok);                                            // not a zone spell
    ActionRequest zm = cast_zone(1, kMoonbeam, 2); zm.zone_move = 7;
    CHECK_FALSE(step(s, zm).ok);                                              // no such zone
    CHECK(s.zones.empty());
}

TEST_CASE("Moonbeam: a point zone; the move costs an action, not on the casting turn, up to 60 ft; landing on a creature is no Enter by default") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    auto r = step(s, cast_zone(1, kMoonbeam, 2, true, 3, 4));
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(s.zones.size() == 1);
    CHECK(s.zones[0].has_anchor_tile); CHECK(s.zones[0].anchor_x == 3); CHECK(s.zones[0].anchor_y == 4);
    CHECK(of_kind(r.events, EventKind::ZoneCreated)[0].target == 0);   // a point zone
    ActionRequest m = cast_zone(1, kMoonbeam, 2, true, 6, 6); m.zone_move = 1;
    CHECK_FALSE(step(s, m).ok);   // casting turn (and the action is spent anyway)
    // the caster's next turn: legal_actions offers the move
    end_turns_until(s, 2);
    end_turns_until(s, 1);
    bool offered = false;
    for (const auto& a : legal_actions(s)) offered = offered || (a.zone_move == 1 && a.spell_id == kMoonbeam);
    CHECK(offered);
    ActionRequest far = m; far.zone_move_ft = 65;
    CHECK_FALSE(step(s, far).ok);
    m.zone_move_ft = 30;
    m.zone_changes = {ZoneChange{2, 1, true}};
    const auto before = s.budget;
    auto mv = step(s, m);
    REQUIRE_MESSAGE(mv.ok, mv.error);
    REQUIRE(of_kind(mv.events, EventKind::ZoneMoved).size() == 1);
    CHECK(of_kind(mv.events, EventKind::ZoneMoved)[0].a == 1); CHECK(of_kind(mv.events, EventKind::ZoneMoved)[0].b == 30);
    CHECK(count(mv.events, EventKind::FeatureTriggered) == 0);   // D1: not an Enter
    CHECK(s.zones[0].anchor_x == 6); CHECK(s.zones[0].anchor_y == 6);
    CHECK(before.action); CHECK_FALSE(s.budget.action);           // D3: an action
    CHECK(find(s, 1)->spell_slots[2] == 2);                        // no slot
    CHECK_FALSE(step(s, m).ok);                                    // no action left
}

TEST_CASE("Moonbeam with counts_aura_move_as_enter: the creature it lands on is entered") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kMoonbeamEnter, 2)).ok);
    end_turns_until(s, 2);
    end_turns_until(s, 1);
    ActionRequest m = cast_zone(1, kMoonbeamEnter, 2, true, 1, 1); m.zone_move = 1; m.zone_changes = {ZoneChange{2, 1, true}};
    auto mv = step(s, m);
    REQUIRE(mv.ok);
    const auto ft = of_kind(mv.events, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 1);
    CHECK(ft[0].target == 2); CHECK(ft[0].b == int32_t(TriggerOn::ZoneEnter));
    CHECK(index_of(mv.events, EventKind::ZoneMoved) < index_of(mv.events, EventKind::FeatureTriggered));
}

TEST_CASE("Spike Growth: damage per 5 ft moved into or within, no save and no per-turn limit; leaving deals none") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kSpikes, 2)).ok);
    end_turns_until(s, 2);
    int before = hp(s, 2);
    auto a = step(s, move(2, {1}));
    REQUIRE(a.ok);
    auto ft = of_kind(a.events, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 1);
    CHECK(ft[0].b == int32_t(TriggerOn::ZoneMoveWithin)); CHECK(ft[0].c == 1);   // no save
    CHECK(count(a.events, EventKind::SavingThrowRolled) == 0);
    CHECK(hp(s, 2) < before);
    before = hp(s, 2);
    auto b = step(s, move(2, {}, {}, {1}));
    CHECK(count(b.events, EventKind::FeatureTriggered) == 1);
    CHECK(hp(s, 2) < before);
    before = hp(s, 2);
    auto c = step(s, move(2, {}, {1}));
    CHECK(count(c.events, EventKind::FeatureTriggered) == 0);
    CHECK(hp(s, 2) == before);
}

TEST_CASE("Spike Growth and forced movement: per 5 ft moved through, resolved right after the forced Moved") {
    auto geo = std::make_shared<Geo>();
    geo->push.zones_entered = {1};
    geo->push.zones_within = {1};   // 10 ft: entered, then one more tile inside
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kSpikes, 2)).ok);
    end_turns_until(s, 2);
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kPush, 2, 1));
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(geo->calls == 1);
    const auto ft = of_kind(r.events, EventKind::FeatureTriggered);
    CHECK(ft.size() == 2);
    CHECK(index_of(r.events, EventKind::Moved) < index_of(r.events, EventKind::FeatureTriggered));
    CHECK(index_of(r.events, EventKind::FeatureTriggered, true) < index_of(r.events, EventKind::SpellResolved));
}

TEST_CASE("Q7: a zone trigger of a forced move that opens an Ask window pauses; the re-run reuses the recorded result") {
    auto geo = std::make_shared<Geo>();
    geo->push.zones_entered = {1};
    auto h = hooks(geo);
    h.damaged_options = [](const EncounterState&, const ReactionWindow&, const Combatant&) {
        ReactionOption o{}; o.kind = ReactionOptionKind::ReduceDamage; o.half = true; o.option_id = 991;
        return std::vector<ReactionOption>{o};
    };
    auto asker = dummy(2);
    set_policy(asker, ReactionTrigger::Damaged, ReactionPolicy::Ask);
    auto s = begin_encounter({caster(1), asker}, 1, nullptr, h);
    end_turns_until(s, 1);
    REQUIRE(step(s, cast_zone(1, kSpikes, 2)).ok);
    end_turns_until(s, 2);
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kPush, 2, 1));
    REQUIRE(r.ok);
    REQUIRE(r.status == StepStatus::NeedsDecision);
    CHECK(geo->calls == 1);                       // asked once, in the run that paused
    REQUIRE(s.pending);
    REQUIRE(s.pending->forced_results.size() == 1);
    CHECK(s.pending->forced_results[0].first == 0);
    const int before = hp(s, 2);
    auto done = step(s, decision_request(*r.needs, true, 0));
    REQUIRE(done.ok);
    REQUIRE(done.status == StepStatus::Done);
    CHECK(geo->calls == 1);                       // the re-run reused the recorded result
    CHECK(count(done.events, EventKind::ReactionTaken) == 1);
    CHECK(hp(s, 2) < before);
    CHECK_FALSE(s.pending);
}

TEST_CASE("Grease: Appear, Enter and EndTurn knock prone on a failed Dex save; it ends on its timer (no concentration)") {
    auto geo = std::make_shared<Geo>();
    geo->inside = {{1, 2}};
    auto s = start({caster(1), dummy(2), dummy(3)}, geo);
    auto r = step(s, cast_zone(1, kGrease, 1));
    REQUIRE(r.ok);
    const auto ft = of_kind(r.events, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 1);
    CHECK(ft[0].target == 2); CHECK(ft[0].b == int32_t(TriggerOn::ZoneAppear)); CHECK(ft[0].c == 3);
    CHECK(find(s, 2)->has(Condition::Prone));
    CHECK(find(s, 1)->concentration_spell == 0);
    CHECK(index_of(r.events, EventKind::ZoneCreated) < index_of(r.events, EventKind::FeatureTriggered));
    CHECK(index_of(r.events, EventKind::ConditionAdded) < index_of(r.events, EventKind::SpellResolved));
    // 3 walks in on its turn and falls; at the end of its turn (still inside) it saves again (already prone: no new event)
    end_turns_until(s, 3);
    auto in = step(s, move(3, {1}));
    REQUIRE(in.ok);
    CHECK(find(s, 3)->has(Condition::Prone));
    geo->inside.insert({1, 3});
    auto end = step(s, {3, ActionKind::EndTurn});
    REQUIRE(end.ok);
    bool end_row = false;
    for (const auto& e : of_kind(end.events, EventKind::FeatureTriggered)) end_row = end_row || (e.target == 3 && e.b == int32_t(TriggerOn::ZoneEndTurn));
    CHECK(end_row);
    CHECK(index_of(end.events, EventKind::FeatureTriggered) < index_of(end.events, EventKind::TurnEnded));
    // timer: 1 round, so it ends when the next round starts
    std::vector<Event> all;
    for (int i = 0; i < 6 && !s.zones.empty(); ++i) { auto x = step(s, {current_actor(s), ActionKind::EndTurn}); all.insert(all.end(), x.events.begin(), x.events.end()); }
    CHECK(s.zones.empty());
    const auto ended = of_kind(all, EventKind::ZoneEnded);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].a == 1); CHECK(ended[0].b == 0);
    CHECK(has_zone_state(s));   // a zone has existed: next_zone_id moved
}

TEST_CASE("Web: a creature pushed in on another creature's turn is not caught (own_turn_only); a start-of-turn save can succeed") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2), dummy(3)}, geo);
    REQUIRE(step(s, cast_zone(1, kWebEasy, 2)).ok);
    // a creature shoved in on another creature's turn is not caught (own_turn_only)
    geo->push.zones_entered = {1};
    end_turns_until(s, 2);
    end_turns_until(s, 1);
    auto p = step(s, cast(1, kPush, 2, 1));
    REQUIRE(p.ok);
    CHECK(count(p.events, EventKind::FeatureTriggered) == 0);
    // its next turn starts in the webs: the DC 1 save succeeds (FeatureTriggered c = 0)
    geo->inside = {{1, 2}};
    std::vector<Event> all;
    for (int i = 0; i < 6 && current_actor(s) != 2; ++i) { auto x = step(s, {current_actor(s), ActionKind::EndTurn}); all.insert(all.end(), x.events.begin(), x.events.end()); }
    const auto ft = of_kind(all, EventKind::FeatureTriggered);
    REQUIRE(ft.size() == 1);
    CHECK(ft[0].b == int32_t(TriggerOn::ZoneStartTurn)); CHECK(ft[0].c == 0);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
    CHECK(s.effects.empty());
}

TEST_CASE("Web (DC 30): enter on its turn -> restrained; leaving ends it; concentration ending ends the zone before the effects") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kWeb, 2)).ok);
    end_turns_until(s, 2);
    auto in = step(s, move(2, {1}));
    REQUIRE(in.ok);
    CHECK(find(s, 2)->has(Condition::Restrained));
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].zone_id == 1); CHECK(s.effects[0].condition == Condition::Restrained);
    // already held: starting its next turn inside does not save again
    geo->inside = {{1, 2}};
    auto out = step(s, move(2, {}, {1}));   // leaving the webs ends the restraint
    REQUIRE(out.ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
    CHECK(count(out.events, EventKind::SpellEffectEnded) == 1);
    CHECK(count(out.events, EventKind::ConditionRemoved) == 1);
    auto back = step(s, move(2, {1}));   // own turn again: caught again
    REQUIRE(back.ok);
    CHECK(find(s, 2)->has(Condition::Restrained));
    // the next turn: still inside, held: no new save
    std::vector<Event> all;
    for (int i = 0; i < 6; ++i) {
        auto x = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), x.events.begin(), x.events.end());
        if (current_actor(s) == 2) break;
    }
    CHECK(count(all, EventKind::FeatureTriggered) == 0);
    // escape: an action, a plain Strength check (CheckRolled target = 0x100 | Str) vs DC 30: fails
    ActionRequest esc{2, ActionKind::Attack, 0}; esc.special = 4;
    auto e = step(s, esc);
    REQUIRE_MESSAGE(e.ok, e.error);
    const auto chk = of_kind(e.events, EventKind::CheckRolled);
    REQUIRE(chk.size() == 1);
    CHECK(chk[0].target == (0x100u | uint32_t(Ability::Str)));
    CHECK(find(s, 2)->has(Condition::Restrained));
    // concentration ends (the caster casts another concentration spell): ConcentrationEnded, ZoneEnded{1}, SpellEffectEnded
    end_turns_until(s, 1);
    auto hold = step(s, cast(1, kHoldPerson, 2, 2));
    REQUIRE_MESSAGE(hold.ok, hold.error);
    const int ce = index_of(hold.events, EventKind::ConcentrationEnded), ze = index_of(hold.events, EventKind::ZoneEnded),
              se = index_of(hold.events, EventKind::SpellEffectEnded);
    REQUIRE(ce >= 0); REQUIRE(ze >= 0); REQUIRE(se >= 0);
    CHECK(ce < ze); CHECK(ze < se);
    CHECK(hold.events[std::size_t(ze)].b == 1);
    CHECK(s.zones.empty());
}

TEST_CASE("Web escape succeeds against an easy DC") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kWebEasy, 2)).ok);
    // force the restraint through a failing save: place it directly via the hard web's mechanics is not possible at DC 1,
    // so give the zone a DC of 30 for the Enter and then lower it for the escape
    s.zones[0].dc = 30;
    end_turns_until(s, 2);
    REQUIRE(step(s, move(2, {1})).ok);
    REQUIRE(find(s, 2)->has(Condition::Restrained));
    s.effects[0].dc = 1;
    ActionRequest esc{2, ActionKind::Attack, 0}; esc.special = 4; esc.spell_id = kWebEasy;
    auto e = step(s, esc);
    REQUIRE_MESSAGE(e.ok, e.error);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
    CHECK(s.effects.empty());
    CHECK(s.zones.size() == 1);   // the web stays
    CHECK_FALSE(s.budget.action);
}

TEST_CASE("an aura owner's move reports others' changes: an aura moving onto a creature is not an Enter by default (D1)") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    REQUIRE(step(s, cast_zone(1, kGuardiansFixed, 3, false)).ok);
    ActionRequest m = move(1); m.zone_changes = {ZoneChange{2, 1, true}};
    auto r = step(s, m);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::FeatureTriggered) == 0);   // D1
}

TEST_CASE("zones_digest: covers every zone field and next_zone_id") {
    auto geo = std::make_shared<Geo>();
    auto s = start({caster(1), dummy(2)}, geo);
    const uint64_t empty = zones_digest(s);
    REQUIRE(step(s, cast_zone(1, kMoonbeam, 2, true, 3, 4)).ok);
    const uint64_t one = zones_digest(s);
    CHECK(one != empty);
    auto t = s; t.zones[0].anchor_x = 9; CHECK(zones_digest(t) != one);
    auto u = s; u.zones[0].fired.push_back(ZoneFired{2, 5, 1}); CHECK(zones_digest(u) != one);
    auto w = s; w.zones[0].excluded = {2}; CHECK(zones_digest(w) != one);
    auto x = s; x.next_zone_id = 7; CHECK(zones_digest(x) != one);
}
