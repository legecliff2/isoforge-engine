// R-G12 rules PR 3: Spiritual Weapon (D3, a non-creature token) and Pact of the Chain's attack trade (D6)
// (VERSION_BUMP_BATCH 7.2s, docs/proposals/SUMMONS_TRANSFORMS.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kWeapon = spell_id_of("test.spiritual_weapon");
const uint32_t kFamiliar = spell_id_of("test.find_familiar");
const uint32_t kConjure = spell_id_of("test.conjure_animals");

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef w; w.id = kWeapon; w.content_id = "test.spiritual_weapon"; w.level = 2; w.range_ft = 60;
    w.casting_time = CastingTime::BonusAction; w.kind = SpellEffectKind::SummonWeapon; w.melee = true;
    w.dice = DiceExpr{1, 8, 0}; w.damage_type = DamageType::Force; w.add_spellcasting_modifier = true; w.upcast_dice = 1;
    w.duration = SpellDuration::Minutes; w.duration_amount = 1;
    book->add(w);
    SpellDef f; f.id = kFamiliar; f.content_id = "test.find_familiar"; f.level = 1; f.range_ft = 10;
    f.kind = SpellEffectKind::Summon; f.summon = SummonSpec{1, SummonInitiative::Own, SummonControl::Telepathic, true};
    book->add(f);
    SpellDef c; c.id = kConjure; c.content_id = "test.conjure_animals"; c.level = 3; c.concentration = true; c.range_ft = 60;
    c.kind = SpellEffectKind::Summon; c.summon = SummonSpec{8, SummonInitiative::Group, SummonControl::Verbal, true};
    book->add(c);
    h.spells = book;
    h.build_summon = [](const EncounterState&, const Combatant&, const SpellDef&, uint8_t, uint8_t) -> std::optional<Combatant> {
        Combatant m = monster(0);
        m.hp = m.hp_max = int16_t(10);
        never_react(m);
        return m;
    };
    return h;
}
Combatant caster(CombatantId id) {
    Combatant c = wizard(id, 0);
    c.hp = c.hp_max = int16_t(60);
    c.spells_known = {kWeapon, kFamiliar, kConjure};
    c.pact_of_chain = true;
    never_react(c);
    return c;
}
Combatant foe(CombatantId id) { Combatant m = monster(id); m.hp = m.hp_max = int16_t(200); m.ac = 5; never_react(m); return m; }
EncounterState start(uint64_t seed = 3) { return begin_encounter({caster(1), foe(2), foe(3)}, seed, nullptr, hooks()); }
ActionRequest weapon(CombatantId a, CombatantId t, uint8_t lvl = 2) {
    ActionRequest r = cast(a, kWeapon, t, lvl);
    r.as_bonus_action = true;
    r.summon_tiles = {ActionRequest::SummonTile{2, 3}};   // the spawn tile (exactly one)
    return r;
}
ActionRequest weapon_attack(CombatantId a, CombatantId t) {
    ActionRequest r{a, ActionKind::SummonWeaponAttack, t};
    r.spell_id = kWeapon; r.as_bonus_action = true;
    return r;
}
std::vector<const Event*> all(const std::vector<Event>& ev, EventKind k) {
    std::vector<const Event*> out;
    for (const auto& e : ev) if (e.kind == k) out.push_back(&e);
    return out;
}
}  // namespace

TEST_CASE("spiritual weapon: the cast makes a token from the summon counter and attacks; not a combatant") {
    auto s = start();
    end_turns_until(s, 1);
    s.next_summon_id = 700;
    const std::size_t n = s.combatants.size(), order = s.order.size();
    auto r = step(s, weapon(1, 2));
    REQUIRE(r.ok);
    REQUIRE(r.logged);
    CHECK(r.logged->request.summon_ids == std::vector<CombatantId>{700});
    CHECK(s.next_summon_id == 701); CHECK(s.summons == 1);
    CHECK(s.combatants.size() == n); CHECK(s.order.size() == order);   // D3: no combatant, no turn
    REQUIRE(s.summon_weapons.size() == 1);
    const ActiveSummonWeapon& w = s.summon_weapons[0];
    CHECK(w.token_id == 700); CHECK(w.owner == 1); CHECK(w.spell_id == kWeapon); CHECK(w.slot_level == 2);
    CHECK(w.end_round == uint32_t(s.round) + 10u);
    const auto sm = all(r.events, EventKind::Summoned);
    REQUIRE(sm.size() == 1);
    CHECK(sm[0]->target == 700); CHECK(sm[0]->b == 0); CHECK(sm[0]->c == -1);
    const auto at = all(r.events, EventKind::AttackResolved);
    REQUIRE(at.size() == 1); CHECK(at[0]->actor == 1); CHECK(at[0]->target == 2);
    CHECK(sm[0] < at[0]);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);   // a bonus-action spell
    CHECK(has_summon_state(s));
    CHECK(find(s, 700) == nullptr);
}

TEST_CASE("spiritual weapon: cast without an attack; later bonus-action attacks; refusals") {
    auto s = start();
    end_turns_until(s, 1);
    CHECK(step(s, weapon_attack(1, 2)).error == "spiritual weapon: none active");
    auto r = step(s, weapon(1, 1));   // on the caster: no attack this turn
    REQUIRE(r.ok);
    CHECK(all(r.events, EventKind::AttackResolved).empty());
    CHECK(step(s, weapon_attack(1, 2)).error == "no bonus action available");
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    auto la = legal_actions(s);
    CHECK(std::count_if(la.begin(), la.end(), [](const ActionRequest& q) { return q.kind == ActionKind::SummonWeaponAttack; }) == 2);
    CHECK(step(s, weapon_attack(1, 1)).error == "bad target");
    auto a = step(s, weapon_attack(1, 3));
    REQUIRE(a.ok);
    const auto at = all(a.events, EventKind::AttackResolved);
    REQUIRE(at.size() == 1); CHECK(at[0]->target == 3);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(step(s, weapon_attack(1, 3)).error == "no bonus action available");
    ActionRequest bad{1, ActionKind::Dash}; bad.familiar_id = 4;
    CHECK(step(s, bad).error == "familiar_id: Attack only");
}

TEST_CASE("spiritual weapon: live ids refused, replay ids checked, replay determinism, 0xAF digest") {
    auto s = start();
    end_turns_until(s, 1);
    auto live = weapon(1, 2); live.summon_ids = {4};
    CHECK(step(s, live).error == "summon_ids: assigned by rules (leave empty)");
    auto make = [] { auto x = start(9); x.next_summon_id = 50; return x; };
    auto a = make(); auto b = make();
    end_turns_until(a, 1); end_turns_until(b, 1);
    const uint64_t d0 = summons_digest(a);
    ActionLog log;
    REQUIRE(step_logged(a, weapon(1, 2), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    CHECK(summons_digest(a) != d0);
    auto bad = log[0]; bad.request.summon_ids = {51};
    auto b2 = b;
    CHECK(replay_step(b2, bad).error == "summon_ids: do not match the summon counter");
    for (const auto& e : log) { auto r = replay_step(b, e); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    CHECK(summons_digest(a) == summons_digest(b));
    CHECK(find(a, 2)->hp == find(b, 2)->hp);
}

TEST_CASE("spiritual weapon: the minute ends at the owner's turn start; encounter end tears it down with summons by id") {
    auto s = start();
    end_turns_until(s, 1);
    REQUIRE(step(s, weapon(1, 1)).ok);
    s.summon_weapons[0].end_round = uint32_t(s.round) + 1u;
    (void)step(s, {1, ActionKind::EndTurn});
    std::vector<Event> seen;
    for (int i = 0; i < 6 && current_actor(s) != 1; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        seen.insert(seen.end(), r.events.begin(), r.events.end());
    }
    const auto se = all(seen, EventKind::SummonEnded);
    REQUIRE(se.size() == 1); CHECK(se[0]->b == 0); CHECK(se[0]->actor == 1); CHECK(se[0]->a == int32_t(kWeapon));
    CHECK(s.summon_weapons.empty());

    auto t = start();
    end_turns_until(t, 1);
    REQUIRE(step(t, weapon(1, 1)).ok);   // token 4
    ActionRequest fam = cast(1, kFamiliar, 1, 1); fam.summon_archetypes = {0};
    for (auto& e : t.order) e.total = 100;
    REQUIRE(step(t, {1, ActionKind::EndTurn}).ok);
    end_turns_until(t, 1);
    REQUIRE(step(t, fam).ok);   // creature 5
    for (CombatantId id : {2u, 3u}) { mut(t, id).hp = 0; mut(t, id).dead = true; }
    ActionRequest heal{1, ActionKind::Heal, 1}; heal.amount = 1;   // any step: the end check runs
    auto r = step(t, heal);
    REQUIRE(r.ok);
    CHECK(t.ended);
    const auto te = all(r.events, EventKind::SummonEnded);
    REQUIRE(te.size() == 2);
    CHECK(te[0]->target == 4); CHECK(te[1]->target == 5);
    for (const Event* e : te) CHECK(e->b == 5);
    REQUIRE(r.teardown_begin.has_value());
    CHECK(&r.events[*r.teardown_begin] == te[0]);
    CHECK(t.summon_weapons.empty());
}

TEST_CASE("pact of the chain: one attack traded for the familiar's reaction attack (D6)") {
    auto s = start();
    end_turns_until(s, 1);
    for (auto& e : s.order) e.total = 100;
    ActionRequest fam = cast(1, kFamiliar, 1, 1); fam.summon_archetypes = {0};
    REQUIRE(step(s, fam).ok);
    const CombatantId f = 4;
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    ActionRequest trade{1, ActionKind::Attack, 2, 0};
    trade.familiar_id = f;
    mut(s, 1).pact_of_chain = false;
    CHECK(step(s, trade).error == "familiar_attack: needs Pact of the Chain");
    mut(s, 1).pact_of_chain = true;
    ActionRequest other = trade; other.familiar_id = 2;
    CHECK(step(s, other).error == "familiar_attack: not your familiar");
    ActionRequest bad = trade; bad.attack_index = 9;
    CHECK(step(s, bad).error == "bad attack index");
    auto r = step(s, trade);
    REQUIRE(r.ok);
    const auto at = all(r.events, EventKind::AttackResolved);
    REQUIRE(at.size() == 1); CHECK(at[0]->actor == f); CHECK(at[0]->target == 2);
    CHECK(find(s, f)->reaction_used);
    CHECK_FALSE(s.budget.action);   // the Attack action was taken (its one attack traded)
    CHECK(step(s, trade).error == "familiar_attack: the familiar cannot react");
    ActionRequest cmd = trade; cmd.familiar_id = 0;
    CHECK(step(s, cmd).error == "no action available");
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    mut(s, 1).attacks_per_action = 2;   // Extra Attack: the trade is one of the two
    ActionRequest own{1, ActionKind::Attack, 2, 0};
    REQUIRE(step(s, own).ok);
    mut(s, f).reaction_used = false;
    REQUIRE(step(s, trade).ok);
    CHECK(s.budget.attacks_remaining == 0);
}

namespace {
// The PR 2 0xAF layout (no weapon rows), recomputed here: summon-only digests must not move with PR 3.
uint64_t pr2_digest(const EncounterState& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](uint64_t v, int bytes) { for (int i = 0; i < bytes; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 0x100000001B3ull; } };
    mix(s.next_summon_id, 4); mix(s.summons, 4);
    for (const auto& c : s.combatants) {
        if (c.summoner == 0) continue;
        mix(c.id, 4); mix(c.summoner, 4); mix(c.summon_source, 4); mix(c.summon_group, 4); mix(uint8_t(c.gone), 1); mix(c.commanded, 1);
    }
    mix(s.transforms, 4);
    return h;
}
}  // namespace

TEST_CASE("spiritual weapon: 0xAF adds weapon rows only while one exists (summon-only digest pinned to PR 2)") {
    auto s = start();
    end_turns_until(s, 1);
    for (auto& e : s.order) e.total = 100;
    ActionRequest fam = cast(1, kFamiliar, 1, 1); fam.summon_archetypes = {0};
    REQUIRE(step(s, fam).ok);
    CHECK(summons_digest(s) == pr2_digest(s));
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    REQUIRE(step(s, weapon(1, 1)).ok);
    CHECK(summons_digest(s) != pr2_digest(s));
    s.summon_weapons.clear();
    CHECK(summons_digest(s) == pr2_digest(s));
}

TEST_CASE("spiritual weapon: spawn and move tiles are stored, logged and replayed") {
    auto make = [] { return start(13); };
    auto a = make(); auto b = make();
    end_turns_until(a, 1); end_turns_until(b, 1);
    ActionLog log;
    auto c = weapon(1, 2); c.summon_tiles = {ActionRequest::SummonTile{4, 7}};
    REQUIRE(step_logged(a, c, log).ok);
    REQUIRE(a.summon_weapons.size() == 1);
    CHECK(a.summon_weapons[0].has_tile); CHECK(a.summon_weapons[0].tile_x == 4); CHECK(a.summon_weapons[0].tile_y == 7);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    end_turns_until(a, 1);
    auto m = weapon_attack(1, 3); m.summon_tiles = {ActionRequest::SummonTile{-3, 9}};
    auto bad = m; bad.summon_tiles = {ActionRequest::SummonTile{1, 1}, ActionRequest::SummonTile{2, 2}};
    CHECK(step(a, bad).error == "spiritual weapon: one tile");
    REQUIRE(step_logged(a, m, log).ok);
    CHECK(a.summon_weapons[0].tile_x == -3); CHECK(a.summon_weapons[0].tile_y == 9);
    CHECK(log.back().request.summon_tiles == m.summon_tiles);
    // replay b from the log; the turns between (other actors' EndTurn) were not logged in `a`, so mirror them
    REQUIRE(replay_step(b, log[0]).ok);
    REQUIRE(replay_step(b, log[1]).ok);
    end_turns_until(b, 1);
    auto r = replay_step(b, log[2]);
    REQUIRE(r.ok); CHECK_FALSE(r.desync);
    REQUIRE(b.summon_weapons.size() == 1);
    CHECK(b.summon_weapons[0].tile_x == -3); CHECK(b.summon_weapons[0].tile_y == 9);
    CHECK(summons_digest(a) == summons_digest(b));
    auto moved = a; moved.summon_weapons[0].tile_x = 0;
    CHECK(summons_digest(moved) != summons_digest(a));   // the tile is in 0xAF
}

TEST_CASE("pact of the chain: a familiar cannot attack on its own turn") {
    auto s = start();
    end_turns_until(s, 1);
    for (auto& e : s.order) e.total = 100;
    ActionRequest fam = cast(1, kFamiliar, 1, 1); fam.summon_archetypes = {0};
    REQUIRE(step(s, fam).ok);
    end_turns_until(s, 4);
    REQUIRE(current_actor(s) == 4);
    CHECK(step(s, {4, ActionKind::Attack, 2, 0}).error == "familiar_attack: a familiar cannot attack on its own turn");
    const auto la = legal_actions(s);
    CHECK(std::none_of(la.begin(), la.end(), [](const ActionRequest& q) { return q.kind == ActionKind::Attack; }));
    CHECK(step(s, {4, ActionKind::Dodge}).ok);
}

TEST_CASE("spiritual weapon: the cast needs exactly one spawn tile") {
    auto s = start();
    end_turns_until(s, 1);
    auto none = weapon(1, 2); none.summon_tiles.clear();
    CHECK(step(s, none).error == "spiritual weapon: one tile");
    auto two = weapon(1, 2); two.summon_tiles.push_back(ActionRequest::SummonTile{5, 5});
    CHECK(step(s, two).error == "spiritual weapon: one tile");
    CHECK(s.summon_weapons.empty());
    REQUIRE(step(s, weapon(1, 2)).ok);
    CHECK(s.summon_weapons[0].has_tile);
    CHECK(step(s, weapon_attack(1, 2)).error == "no bonus action available");   // the move list may be empty (no move)
}

TEST_CASE("pact of the chain: a familiar takes no opportunity attack and cannot ready an attack") {
    auto s = start();
    end_turns_until(s, 1);
    for (auto& e : s.order) e.total = 100;
    ActionRequest fam = cast(1, kFamiliar, 1, 1); fam.summon_archetypes = {0};
    REQUIRE(step(s, fam).ok);
    Combatant& f = mut(s, 4);
    f.reaction_policy.fill(ReactionPolicy::Auto);   // it would take any reaction offered
    end_turns_until(s, 4);
    REQUIRE(current_actor(s) == 4);
    ActionRequest ready{4, ActionKind::Ready, 2, 0}; ready.ready_kind = ActionKind::Attack;
    CHECK(step(s, ready).error == "familiar_attack: a familiar cannot ready an attack");
    REQUIRE(step(s, {4, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    ActionRequest mv{2, ActionKind::Move}; mv.move_ft = 5; mv.move_tiles = 1; mv.leaves_reach = true;
    auto r = step(s, mv);
    REQUIRE(r.ok);
    for (const auto& e : r.events)
        if (e.kind == EventKind::ReactionWindowOpened) CHECK(e.target != 4);   // no LeavingReach window for the familiar
    CHECK_FALSE(find(s, 4)->reaction_used);
    CHECK(count(r.events, EventKind::Moved) == 1);
}

TEST_CASE("spiritual weapon: SummonWeaponAttack with no tile = no move; the stored tile is kept") {
    auto s = start();
    end_turns_until(s, 1);
    REQUIRE(step(s, weapon(1, 1)).ok);   // spawn tile (2, 3)
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    auto m = weapon_attack(1, 2);
    REQUIRE(m.summon_tiles.empty());
    auto r = step(s, m);
    REQUIRE(r.ok);
    REQUIRE(s.summon_weapons.size() == 1);
    CHECK(s.summon_weapons[0].has_tile); CHECK(s.summon_weapons[0].tile_x == 2); CHECK(s.summon_weapons[0].tile_y == 3);
    const auto at = all(r.events, EventKind::AttackResolved);
    REQUIRE(at.size() == 1); CHECK(at[0]->actor == 1); CHECK(at[0]->target == 2);
    for (EventKind k : {EventKind::Moved, EventKind::Summoned, EventKind::SummonEnded}) CHECK(all(r.events, k).empty());
    REQUIRE(r.logged);
    CHECK(r.logged->request.summon_tiles.empty());   // nothing to fold under 0xBC: no move logged
}
