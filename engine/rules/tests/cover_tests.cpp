// VERSION_BUMP_BATCH 7.2t: cover. Sim supplies EncounterHooks::cover (a pure function of positions); rules apply +2 / +5
// to attack AC and Dex saves, refuse targeting through total cover, and let an area skip creatures with total cover from
// its origin. No hook = Cover::None (goldens unchanged).
#include <doctest/doctest.h>

#include <map>
#include <tuple>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBreath = spell_id_of("test.dragon#fire_breath");
const uint32_t kSacred = spell_id_of("srd51.spell.sacred_flame");
const uint32_t kBall = spell_id_of("test.fireball");
const uint32_t kWisBlast = spell_id_of("test.wis_blast");

using CoverMap = std::map<std::tuple<CombatantId, CombatantId, bool>, Cover>;

SpellDef save_def(uint32_t id, const char* name, Ability ab, AreaShape area = AreaShape::None) {
    SpellDef d; d.id = id; d.content_id = name; d.level = 0; d.range_ft = 60; d.kind = SpellEffectKind::Save;
    d.save_ability = ab; d.fixed_dc = 30; d.on_save = OnSave::Half; d.dice = {2, 6, 0}; d.damage_type = DamageType::Fire;
    d.targets = 1; d.is_spell = false; d.magical = false; d.area = area; if (area != AreaShape::None) d.area_size_ft = 20;
    d.components = kComponentV;
    return d;
}

EncounterHooks hooks(std::shared_ptr<const CoverMap> m, std::vector<SpellDef> defs = {}) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    for (const auto& d : defs) REQUIRE(b->add(d));
    h.spells = b;
    if (m) h.cover = [m](const EncounterState&, const CoverQuery& q) {
        auto it = m->find({q.source, q.target, q.from_area});
        return it == m->end() ? Cover::None : it->second;
    };
    return h;
}

Combatant caster(CombatantId id, std::vector<uint32_t> known) {
    auto c = wizard(id);
    c.spells_known.insert(c.spells_known.end(), known.begin(), known.end());
    return c;
}

const Event* first(const std::vector<Event>& ev, EventKind k, CombatantId actor = 0) {
    for (const auto& e : ev) if (e.kind == k && (actor == 0 || e.actor == actor)) return &e;
    return nullptr;
}

std::vector<SpellDef> all_defs() {
    SpellDef sf = save_def(kSacred, "srd51.spell.sacred_flame", Ability::Dex); sf.ignores_cover = true;
    return {save_def(kBreath, "test.dragon#fire_breath", Ability::Dex), sf,
            save_def(kBall, "test.fireball", Ability::Dex, AreaShape::Sphere), save_def(kWisBlast, "test.wis_blast", Ability::Wis)};
}

// One request on a fresh encounter (seed fixed), with or without a cover map; the events of that step.
StepResult run(uint64_t seed, std::shared_ptr<const CoverMap> m, const ActionRequest& req, CombatantId who = 1) {
    auto s = begin_encounter({caster(1, {kBreath, kSacred, kBall, kWisBlast}), monster(2), monster(3)}, seed, nullptr,
                             hooks(m, all_defs()));
    end_turns_until(s, who);
    return step(s, req);
}

ActionRequest attack(CombatantId a, CombatantId t) { ActionRequest r{a, ActionKind::Attack, t}; r.target_within_5ft = true; return r; }

}  // namespace

TEST_CASE("cover: no hook means Cover::None (the same rolls and outcome as an empty answer)") {
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto a = run(seed, nullptr, attack(1, 2));
        auto b = run(seed, std::make_shared<CoverMap>(), attack(1, 2));
        REQUIRE(a.ok); REQUIRE(b.ok);
        REQUIRE(a.events.size() == b.events.size());
        for (std::size_t i = 0; i < a.events.size(); ++i) {
            CHECK(a.events[i].kind == b.events[i].kind); CHECK(a.events[i].a == b.events[i].a);
            CHECK(a.events[i].b == b.events[i].b); CHECK(a.events[i].c == b.events[i].c);
        }
    }
    CHECK(cover_bonus(Cover::None) == 0); CHECK(cover_bonus(Cover::Half) == 2);
    CHECK(cover_bonus(Cover::ThreeQuarters) == 5); CHECK(cover_bonus(Cover::Total) == 0);
}

TEST_CASE("cover: half +2 and three-quarters +5 to the target's AC for an attack roll") {
    for (Cover c : {Cover::Half, Cover::ThreeQuarters}) {
        auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, false}, c}});
        int differ = 0;
        for (uint64_t seed = 0; seed < 300; ++seed) {
            auto r = run(seed, m, attack(1, 2));
            REQUIRE(r.ok);
            const Event* e = first(r.events, EventKind::AttackResolved);
            REQUIRE(e);
            if (e->a == 20 || e->a == 1) continue;
            const bool hit = (e->c & 1) != 0;
            CHECK(hit == (e->b >= 15 + cover_bonus(c)));   // monster AC 15
            auto plain = run(seed, nullptr, attack(1, 2));
            differ += int(hit != ((first(plain.events, EventKind::AttackResolved)->c & 1) != 0));
        }
        CHECK(differ > 0);
    }
}

TEST_CASE("cover: Dex saves get +2 / +5 per target; other saves do not; Sacred Flame ignores it") {
    for (Cover c : {Cover::Half, Cover::ThreeQuarters}) {
        auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, false}, c}});
        for (uint64_t seed = 0; seed < 40; ++seed) {
            for (uint32_t sp : {kBreath, kWisBlast, kSacred}) {
                auto with = run(seed, m, cast(1, sp, 2));
                auto without = run(seed, nullptr, cast(1, sp, 2));
                REQUIRE(with.ok); REQUIRE(without.ok);
                const Event* a = first(with.events, EventKind::SavingThrowRolled);
                const Event* b = first(without.events, EventKind::SavingThrowRolled);
                REQUIRE(a); REQUIRE(b);
                CHECK(a->a == b->a);   // same natural roll
                const int want = sp == kBreath ? cover_bonus(c) : 0;
                CHECK(a->b == b->b + want);
            }
        }
    }
}

TEST_CASE("cover: total cover refuses an attack, a spell attack and a targeted save; nothing is spent") {
    auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, false}, Cover::Total}});
    auto s = begin_encounter({caster(1, {kBreath, kSacred, kBall, kWisBlast}), monster(2), monster(3)}, 3, nullptr, hooks(m, all_defs()));
    end_turns_until(s, 1);
    const auto slots = find(s, 1)->spell_slots;
    CHECK(step(s, attack(1, 2)).error == "target has total cover");
    CHECK(step(s, cast(1, kFireBolt, 2)).error == "target has total cover");
    CHECK(step(s, cast(1, kBreath, 2)).error == "target has total cover");
    CHECK(step(s, cast(1, kSacred, 2)).error == "target has total cover");   // ignores_cover never beats total cover
    CHECK(step(s, cast(1, kMagicMissile, 2, 1)).error == "target has total cover");
    CHECK(find(s, 1)->spell_slots == slots);
    CHECK(s.budget.action);
    CHECK(step(s, attack(1, 3)).ok);   // the other target is fine
}

TEST_CASE("cover: an area skips a creature with total cover from its origin; others keep their Dex bonus") {
    auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, true}, Cover::Total}, {{1, 3, true}, Cover::Half},
                                                 {{1, 2, false}, Cover::Total}});   // from_area decides, not the caster's line
    for (uint64_t seed = 0; seed < 30; ++seed) {
        ActionRequest req = cast(1, kBall, 0); req.targets = {2, 3};
        auto r = run(seed, m, req);
        REQUIRE(r.ok);
        CHECK(first(r.events, EventKind::SavingThrowRolled, 2) == nullptr);
        const Event* sv = first(r.events, EventKind::SavingThrowRolled, 3);
        REQUIRE(sv);
        ActionRequest only3 = req; only3.targets = {3};   // the skipped creature draws nothing: same rolls as casting on 3 alone
        auto plain = run(seed, nullptr, only3);
        const Event* p3 = first(plain.events, EventKind::SavingThrowRolled, 3);
        REQUIRE(p3);
        CHECK(sv->b == p3->b + 2);
    }
}

TEST_CASE("cover: a monster's attack is refused through total cover too") {
    auto m = std::make_shared<CoverMap>(CoverMap{{{2, 1, false}, Cover::Total}});
    auto s = begin_encounter({caster(1, {}), monster(2)}, 5, nullptr, hooks(m));
    end_turns_until(s, 2);
    CHECK(step(s, attack(2, 1)).error == "target has total cover");
}

TEST_CASE("cover: replay recomputes cover from the hook (no log field) and matches") {
    auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, true}, Cover::ThreeQuarters}, {{1, 3, true}, Cover::Half}});
    auto make = [&] {
        return begin_encounter({caster(1, {kBreath, kSacred, kBall, kWisBlast}), monster(2), monster(3)}, 17, nullptr, hooks(m, all_defs()));
    };
    auto a = make(), b = make();
    ActionLog log;
    end_turns_until(a, 1); end_turns_until(b, 1);
    ActionRequest ball = cast(1, kBall, 0); ball.targets = {2, 3};
    REQUIRE(step_logged(a, ball, log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    for (const auto& ls : log) { auto r = replay_step(b, ls); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    for (std::size_t i = 0; i < a.combatants.size(); ++i) {
        CHECK(a.combatants[i].hp == b.combatants[i].hp);
        CHECK(a.combatants[i].temp_hp == b.combatants[i].temp_hp);
    }
    CHECK(a.turn_index == b.turn_index);
}

// ---- EC review of #178: explicit origins, aimed areas, save actions, reactions ----
namespace {

const uint32_t kWeapon = spell_id_of("test.spiritual_weapon");
const uint32_t kCone = spell_id_of("test.dragon#cone_breath");

struct Recorder { std::vector<CoverQuery> q; };

// Total when the query's explicit origin is (9, 9) and the target is 2; Half for target 3 from any explicit origin.
EncounterHooks origin_hooks(std::shared_ptr<Recorder> rec) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    SpellDef w; w.id = kWeapon; w.content_id = "test.spiritual_weapon"; w.level = 2; w.range_ft = 60;
    w.casting_time = CastingTime::BonusAction; w.kind = SpellEffectKind::SummonWeapon; w.melee = true;
    w.dice = DiceExpr{1, 8, 0}; w.damage_type = DamageType::Force; w.add_spellcasting_modifier = true;
    w.duration = SpellDuration::Minutes; w.duration_amount = 1; w.components = kComponentV;
    REQUIRE(b->add(w));
    for (const auto& d : all_defs()) REQUIRE(b->add(d));
    SpellDef cone = save_def(kCone, "test.dragon#cone_breath", Ability::Dex, AreaShape::Cone);
    REQUIRE(b->add(cone));
    h.spells = b;
    h.cover = [rec](const EncounterState&, const CoverQuery& q) {
        rec->q.push_back(q);
        if (q.has_origin && q.origin_x == 9 && q.origin_y == 9 && q.target == 2) return Cover::Total;
        if (q.has_origin && q.target == 3) return Cover::Half;
        return Cover::None;
    };
    return h;
}

EncounterState origin_start(std::shared_ptr<Recorder> rec, uint64_t seed = 3) {
    auto c = caster(1, {kWeapon, kBreath, kSacred, kBall, kWisBlast, kCone});
    never_react(c);
    auto m2 = monster(2), m3 = monster(3);
    m2.hp = m2.hp_max = 200; m3.hp = m3.hp_max = 200; never_react(m2); never_react(m3);
    return begin_encounter({c, m2, m3}, seed, nullptr, origin_hooks(rec));
}

ActionRequest weapon_cast(CombatantId t, int32_t x, int32_t y) {
    ActionRequest r = cast(1, kWeapon, t, 2);
    r.as_bonus_action = true;
    r.summon_tiles = {ActionRequest::SummonTile{x, y}};
    return r;
}
ActionRequest weapon_attack(CombatantId t, int32_t x, int32_t y) {
    ActionRequest r{1, ActionKind::SummonWeaponAttack, t};
    r.spell_id = kWeapon; r.as_bonus_action = true;
    r.summon_tiles = {ActionRequest::SummonTile{x, y}};
    return r;
}
ActionRequest aimed_ball(int32_t x, int32_t y) {
    ActionRequest r = cast(1, kBall, 0); r.targets = {2, 3};
    r.has_area_origin = true; r.area_x = x; r.area_y = y;
    return r;
}

}  // namespace

TEST_CASE("cover: Spiritual Weapon measures from its tile; total cover refuses the cast and SummonWeaponAttack before spending") {
    auto rec = std::make_shared<Recorder>();
    auto s = origin_start(rec);
    end_turns_until(s, 1);
    const auto slots = find(s, 1)->spell_slots;
    CHECK(step(s, weapon_cast(2, 9, 9)).error == "target has total cover");   // from the spawn tile, not the caster
    CHECK(find(s, 1)->spell_slots == slots);
    CHECK(s.budget.bonus_action);
    CHECK(s.summon_weapons.empty());
    rec->q.clear();
    REQUIRE(step(s, weapon_cast(2, 2, 3)).ok);
    bool attack_from_tile = false;
    for (const auto& q : rec->q) attack_from_tile |= q.target == 2 && q.has_origin && q.origin_x == 2 && q.origin_y == 3;
    CHECK(attack_from_tile);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(step(s, weapon_attack(2, 9, 9)).error == "target has total cover");
    CHECK(s.budget.bonus_action);
    CHECK(s.summon_weapons[0].tile_x == 2); CHECK(s.summon_weapons[0].tile_y == 3);   // not moved
    rec->q.clear();
    auto r = step(s, weapon_attack(2, 4, 4));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::AttackResolved) == 1);
    bool after_move = false;
    for (const auto& q : rec->q) after_move |= q.has_origin && q.origin_x == 4 && q.origin_y == 4;
    CHECK(after_move);
    CHECK_FALSE(s.budget.bonus_action);
}

TEST_CASE("cover: an aimed area passes its origin; no origin = the caster's space; area_origin only on area spells") {
    auto rec = std::make_shared<Recorder>();
    auto s = origin_start(rec);
    end_turns_until(s, 1);
    ActionRequest bad = cast(1, kBreath, 2); bad.has_area_origin = true;
    CHECK(step(s, bad).error == "area_origin: area spells only");
    auto r = step(s, aimed_ball(9, 9));
    REQUIRE(r.ok);
    CHECK(first(r.events, EventKind::SavingThrowRolled, 2) == nullptr);   // total cover from (9, 9)
    CHECK(first(r.events, EventKind::SavingThrowRolled, 3) != nullptr);
    for (const auto& q : rec->q) if (q.from_area) { CHECK(q.has_origin); CHECK(q.origin_x == 9); CHECK(q.origin_y == 9); }
    auto s2 = origin_start(rec);
    end_turns_until(s2, 1);
    rec->q.clear();
    ActionRequest self = cast(1, kBall, 0); self.targets = {2, 3};
    auto r2 = step(s2, self);
    REQUIRE(r2.ok);
    CHECK(first(r2.events, EventKind::SavingThrowRolled, 2) != nullptr);
    for (const auto& q : rec->q) if (q.from_area) CHECK_FALSE(q.has_origin);
}

TEST_CASE("cover: replay of an aimed area recomputes the same cover from the logged origin") {
    auto rec = std::make_shared<Recorder>();
    auto a = origin_start(rec, 21), b = origin_start(rec, 21);
    ActionLog log;
    end_turns_until(a, 1); end_turns_until(b, 1);
    REQUIRE(step_logged(a, aimed_ball(9, 9), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    REQUIRE(log[0].request.has_area_origin);
    CHECK(log[0].request.area_x == 9);
    for (const auto& ls : log) { auto r = replay_step(b, ls); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    for (std::size_t i = 0; i < a.combatants.size(); ++i) CHECK(a.combatants[i].hp == b.combatants[i].hp);
    CHECK(find(b, 2)->hp == 200);   // skipped in the replay too
}

TEST_CASE("cover: a monster's area breath weapon (save action) gets the Dex bonus and skips total cover") {
    auto rec = std::make_shared<Recorder>();
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto s = origin_start(rec, seed);
        end_turns_until(s, 1);
        ActionRequest r = cast(1, kCone, 0); r.targets = {2, 3}; r.has_area_origin = true; r.area_x = 9; r.area_y = 9;
        auto out = step(s, r);
        REQUIRE(out.ok);
        CHECK(first(out.events, EventKind::ActionUsed) != nullptr);   // not a spell
        CHECK(first(out.events, EventKind::SavingThrowRolled, 2) == nullptr);
        const Event* sv = first(out.events, EventKind::SavingThrowRolled, 3);
        REQUIRE(sv);
        // same rolls without cover: a hookless run casting at 3 alone
        auto plain = begin_encounter({caster(1, {kCone}), monster(2), monster(3)}, seed, nullptr, hooks(nullptr, {save_def(kCone, "test.dragon#cone_breath", Ability::Dex, AreaShape::Cone)}));
        end_turns_until(plain, 1);
        ActionRequest only3 = cast(1, kCone, 0); only3.targets = {3};
        auto po = step(plain, only3);
        REQUIRE(po.ok);
        const Event* p3 = first(po.events, EventKind::SavingThrowRolled, 3);
        REQUIRE(p3);
        CHECK(sv->b == p3->b + 2);
    }
}

TEST_CASE("cover: opportunity and readied attacks into total cover are not offered (no reaction spent)") {
    auto m = std::make_shared<CoverMap>(CoverMap{{{1, 2, false}, Cover::Total}});
    EncounterHooks h = hooks(m);
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&, std::span<const ReactionOption>) {
        return ReactionDecision{0, true, 0}; };
    h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) { return e.kind == EventKind::Moved && e.actor == 2; };
    {   // opportunity attack
        auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
        mut(s, 2).hp = mut(s, 2).hp_max = 200;
        end_turns_until(s, 2);
        ActionRequest mv{2, ActionKind::Move}; mv.move_ft = 5; mv.leaves_reach = true;
        auto r = step(s, mv);
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);
        CHECK_FALSE(find(s, 1)->reaction_used);
    }
    {   // readied attack
        auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
        mut(s, 2).hp = mut(s, 2).hp_max = 200;
        end_turns_until(s, 1);
        ActionRequest ready{1, ActionKind::Ready, 2};
        ready.ready_kind = ActionKind::Attack; ready.trigger_id = 3; ready.target_within_5ft = true;
        REQUIRE(step(s, ready).ok);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 2);
        auto r = step(s, {2, ActionKind::Move, 0, 0, 5});
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);
        CHECK(find(s, 1)->has_readied);   // kept
        CHECK_FALSE(find(s, 1)->reaction_used);
    }
}
