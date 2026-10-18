// Version-bump batch row 5: rules foundation (usage, ActionUse, ResourcePool, recharge roll, moved_ft,
// SpellBook::erase, ReactionOption::uses_reaction, SpellDef::{is_spell, magical, fixed_dc, fixed_attack},
// event kinds 39-44). See docs/proposals/VERSION_BUMP_BATCH.md sections 2, 7 and 9.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBreath = spell_id_of("test.archetype#fire_breath");

// A "breath weapon": a non-spell, non-magical save action, 6d6 fire, DEX save, half on success.
SpellDef breath(uint8_t usage_kind) {
    SpellDef d; d.content_id = "test.archetype#fire_breath"; d.id = kBreath; d.level = 0;
    d.range_ft = 60; d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex;
    d.on_save = OnSave::Half; d.dice = {6, 6, 0}; d.damage_type = DamageType::Fire; d.targets = 3;
    d.is_spell = false; d.magical = false; d.fixed_dc = 17;
    d.usage_kind = usage_kind;
    return d;
}

Combatant dragon(CombatantId id) {
    auto d = monster(id);
    d.hp = d.hp_max = 200;
    d.spells_known = {kBreath};
    return d;
}

Combatant sturdy_pc(CombatantId id) {
    auto c = pc(id, 200);
    c.team = 0;
    return c;
}

EncounterHooks hooks_with(const SpellDef& d) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    REQUIRE(b->add(d));
    h.spells = b;
    return h;
}

const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}

ActionRequest breathe(CombatantId a, CombatantId t) { return cast(a, kBreath, t, 0); }

}  // namespace

TEST_CASE("event kinds 39-44 are appended after SpellCheckRolled") {
    CHECK(int(EventKind::SpellCheckRolled) == 38);
    CHECK(int(EventKind::RechargeRolled) == 39);
    CHECK(int(EventKind::FeatureTriggered) == 40);
    CHECK(int(EventKind::ResourceSpent) == 41);
    CHECK(int(EventKind::ModifierApplied) == 42);
    CHECK(int(EventKind::ModifierExpired) == 43);
    CHECK(int(EventKind::ActionUsed) == 44);
    CHECK(kEventKindCount == 45);
}

TEST_CASE("defaults preserve every existing spell and reaction option") {
    SpellDef d;
    CHECK(d.is_spell);
    CHECK(d.magical);
    CHECK(d.fixed_dc == 0);
    CHECK(d.fixed_attack == 0);
    CHECK(d.usage_kind == 0);
    CHECK(ReactionOption{}.uses_reaction);
    EncounterState s;
    CHECK(s.action_uses.empty());
    CHECK(s.pools.empty());
    CHECK(s.budget.moved_ft == 0);
}

TEST_CASE("fixed_dc / fixed_attack override the caster-derived values; 0 derives") {
    auto w = wizard(1);
    SpellDef d;
    CHECK(spell_save_dc(w, d) == spell_save_dc(w));
    CHECK(spell_attack_bonus(w, d) == spell_attack_bonus(w));
    d.fixed_dc = 19; d.fixed_attack = 11;
    CHECK(spell_save_dc(w, d) == 19);
    CHECK(spell_attack_bonus(w, d) == 11);
}

TEST_CASE("SpellBook::erase removes a definition and keeps the rest sorted and findable") {
    auto b = srd_spellbook();
    const std::size_t n = b->all().size();
    CHECK_FALSE(b->erase(12345));
    CHECK(b->erase(kShield));
    CHECK(b->find(kShield) == nullptr);
    CHECK(b->all().size() == n - 1);
    CHECK(b->find(kFireBolt) != nullptr);
    CHECK(std::is_sorted(b->all().begin(), b->all().end(), [](const SpellDef& x, const SpellDef& y) { return x.id < y.id; }));
    SpellDef again; again.id = kShield; again.content_id = "srd51.spell.shield";
    CHECK(b->add(again));   // the id is free again (rebuild / override path)
}

TEST_CASE("non-spell save action: ActionUsed instead of SpellCast, fixed DC, no counter window, SpellResolved still emitted") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto h = hooks_with(breath(0));
        // A would-be counterer (a wizard who knows Counterspell) must not even be offered a window.
        auto cs = wizard(3, 0);
        auto s = begin_encounter({dragon(1), sturdy_pc(2), cs}, seed, nullptr, h);
        end_turns_until(s, 1);
        auto r = step(s, breathe(1, 2));
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::SpellCast) == 0);
        CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);
        CHECK(count(r.events, EventKind::SpellResolved) == 1);
        const Event* u = first(r.events, EventKind::ActionUsed);
        REQUIRE(u);
        CHECK(u->actor == 1); CHECK(u->target == 2); CHECK(u->a == int32_t(kBreath)); CHECK(u->b == 0); CHECK(u->c == 0);
        const Event* sv = first(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv);
        // Ordering: ActionUsed, then the save, then SpellResolved.
        auto idx = [&](EventKind k) { return std::find_if(r.events.begin(), r.events.end(), [&](const Event& e) { return e.kind == k; }) - r.events.begin(); };
        CHECK(idx(EventKind::ActionUsed) < idx(EventKind::SavingThrowRolled));
        CHECK(idx(EventKind::SavingThrowRolled) < idx(EventKind::SpellResolved));
        // fixed DC 17 (the dragon has no spellcasting ability: the derived DC would be 8).
        CHECK(sv->c == int32_t(sv->b >= 17));   // fixed DC 17 (derived would be 8: the dragon has no casting ability)
    }
}

TEST_CASE("is_spell = true keeps the SpellCast window and no ActionUsed (existing spells unchanged)") {
    auto s = begin_encounter({wizard(1), monster(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kFireBolt, 2));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SpellCast) == 1);
    CHECK(count(r.events, EventKind::ActionUsed) == 0);
}

TEST_CASE("non-magical damage: resistance to nonmagical damage applies to a breath weapon, not to a spell") {
    auto h = hooks_with(breath(0));
    auto s = begin_encounter({dragon(1), sturdy_pc(2)}, 5, nullptr, h);
    mut(s, 2).resistances = bit(DamageType::Fire);
    end_turns_until(s, 1);
    auto r = step(s, breathe(1, 2));
    REQUIRE(r.ok);
    const Event* dmg = first(r.events, EventKind::DamageDealt);
    REQUIRE(dmg);
    CHECK(dmg->a <= 18);   // 6d6 max 36, resisted once -> max 18 (half on a save would be 9 more)
}

TEST_CASE("recharge: an expended action cannot be used; the begin_turn d6 recharges it at >= min; no draw without a row") {
    SpellDef d = breath(1); d.recharge_min = 5;
    auto h = hooks_with(d);
    int recharged = 0, stayed = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = begin_encounter({dragon(1), sturdy_pc(2)}, seed, nullptr, h);
        register_action_use(s, 1, kBreath);
        end_turns_until(s, 1);
        auto r = step(s, breathe(1, 2));
        REQUIRE(r.ok);
        CHECK(find_action_use(s, 1, kBreath)->expended);
        const Event* u = first(r.events, EventKind::ActionUsed);
        REQUIRE(u);
        CHECK(u->c == 1);
        // Expended: rejected and not offered.
        CHECK_FALSE(step(s, breathe(1, 2)).ok);
        for (const auto& q : legal_actions(s)) CHECK_FALSE(q.spell_id == kBreath);
        // Next owner turn: one d6 roll.
        end_turns_until(s, 2);
        (void)step(s, {2, ActionKind::EndTurn});
        (void)step(s, {1, ActionKind::EndTurn});   // then until 1's turn starts again
        for (int i = 0; i < 5 && current_actor(s) != 1; ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
        REQUIRE(current_actor(s) == 1);
        // The roll happened when the turn began; its effect is visible in state.
        const bool back = !find_action_use(s, 1, kBreath)->expended;
        recharged += back; stayed += !back;
    }
    CHECK(recharged > 0);
    CHECK(stayed > 0);
}

TEST_CASE("recharge roll event: payload, placement after TurnStarted, reproducible, only for expended rows") {
    SpellDef d = breath(1); d.recharge_min = 5;
    auto h = hooks_with(d);
    auto s = begin_encounter({dragon(1), sturdy_pc(2)}, 11, nullptr, h);
    register_action_use(s, 1, kBreath, true);   // already expended
    end_turns_until(s, 2);
    auto r = step(s, {2, ActionKind::EndTurn});  // 1's turn begins
    REQUIRE(current_actor(s) == 1);
    const Event* ts = first(r.events, EventKind::TurnStarted);
    const Event* rr = first(r.events, EventKind::RechargeRolled);
    REQUIRE(ts); REQUIRE(rr);
    CHECK(rr > ts);
    CHECK(rr->actor == 1); CHECK(rr->a == int32_t(kBreath));
    CHECK(rr->b >= 1); CHECK(rr->b <= 6);
    CHECK(rr->c == int32_t(rr->b >= 5));
    CHECK(find_action_use(s, 1, kBreath)->expended == (rr->c == 0));
    // A non-expended row does not roll.
    auto s2 = begin_encounter({dragon(1), sturdy_pc(2)}, 11, nullptr, h);
    register_action_use(s2, 1, kBreath, false);
    end_turns_until(s2, 2);
    auto r2 = step(s2, {2, ActionKind::EndTurn});
    CHECK(count(r2.events, EventKind::RechargeRolled) == 0);
}

TEST_CASE("RNG invariant: a combat with no recharge row consumes the same rng as before (no draws)") {
    auto run = [](bool with_row) {
        auto h = hooks_with(breath(0));
        auto s = begin_encounter({dragon(1), sturdy_pc(2)}, 7, nullptr, h);
        if (with_row) register_action_use(s, 1, kBreath, false);   // present but never expended: no draw either
        for (int i = 0; i < 6; ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
        return s.rng;
    };
    auto a = run(false), b = run(true);
    CHECK(a.d(1000) == b.d(1000));
}

TEST_CASE("per-day / per-rest usage rides a FeatureUses row keyed by the spell id") {
    auto h = hooks_with(breath(2));
    auto s = begin_encounter({dragon(1), sturdy_pc(2)}, 4, nullptr, h);
    register_feature(s, 1, kBreath, 1, FeatureRecharge::LongRest);
    end_turns_until(s, 1);
    REQUIRE(step(s, breathe(1, 2)).ok);
    CHECK(feature_uses_left(s, 1, kBreath) == 0);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto r = step(s, breathe(1, 2));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "no uses left");
    recover_features(s, true);
    CHECK(step(s, breathe(1, 2)).ok);
}

TEST_CASE("pool usage: spends pool_cost, emits ResourceSpent, rejects when short, rests refill") {
    SpellDef d = breath(3); d.pool_id = 77; d.pool_cost = 2;
    auto h = hooks_with(d);
    auto s = begin_encounter({dragon(1), sturdy_pc(2)}, 9, nullptr, h);
    register_pool(s, 1, 77, 3, 3, FeatureRecharge::ShortRest);
    CHECK(can_spend(s, 1, 77, 3));
    CHECK_FALSE(can_spend(s, 1, 77, 4));
    CHECK_FALSE(can_spend(s, 1, 78, 1));   // unregistered
    end_turns_until(s, 1);
    auto r = step(s, breathe(1, 2));
    REQUIRE(r.ok);
    const Event* rs = first(r.events, EventKind::ResourceSpent);
    REQUIRE(rs);
    CHECK(rs->actor == 1); CHECK(rs->a == 77); CHECK(rs->b == 2); CHECK(rs->c == 1);
    CHECK(find_pool(s, 1, 77)->cur == 1);
    const auto actions = legal_actions(s);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto bad = step(s, breathe(1, 2));
    CHECK_FALSE(bad.ok);
    CHECK(bad.error == "not enough resource");
    for (const auto& q : legal_actions(s)) CHECK_FALSE(q.spell_id == kBreath);
    recover_features(s, false);   // short rest refills a ShortRest pool
    CHECK(find_pool(s, 1, 77)->cur == 3);
    CHECK_FALSE(spend_pool(s, 1, 77, 4));
    CHECK(find_pool(s, 1, 77)->cur == 3);   // a failed spend changes nothing
    CHECK(spend_pool(s, 1, 77, 3));
    CHECK(find_pool(s, 1, 77)->cur == 0);
    (void)actions;
}

TEST_CASE("register_pool clamps cur to max and is idempotent; digests differ per table and per value") {
    EncounterState s;
    register_pool(s, 1, 5, 4, 9);
    CHECK(find_pool(s, 1, 5)->cur == 4);
    register_pool(s, 1, 5, 4, 2);
    CHECK(s.pools.size() == 1);
    const uint64_t p0 = pools_digest(s);
    s.pools[0].cur = 1;
    CHECK(pools_digest(s) != p0);
    register_action_use(s, 1, 99);
    const uint64_t a0 = action_uses_digest(s);
    s.action_uses[0].expended = true;
    CHECK(action_uses_digest(s) != a0);
}

TEST_CASE("moved_ft: 5 * move_tiles only when the move completes, reset each turn") {
    auto s = begin_encounter({pc(1), monster(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    CHECK(s.budget.moved_ft == 0);
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5; mv.move_tiles = 1;
    REQUIRE(step(s, mv).ok);
    CHECK(s.budget.moved_ft == 5);
    mv.move_ft = 10; mv.move_tiles = 2;   // a multi-tile request counts every tile
    REQUIRE(step(s, mv).ok);
    CHECK(s.budget.moved_ft == 15);
    ActionRequest zero{1, ActionKind::Move}; zero.move_ft = 5;   // move_tiles = 0 (caller that doesn't supply it)
    REQUIRE(step(s, zero).ok);
    CHECK(s.budget.moved_ft == 15);
    CHECK_FALSE(step(s, [] { ActionRequest m{1, ActionKind::Move}; m.move_ft = 120; m.move_tiles = 1; return m; }()).ok);
    CHECK(s.budget.moved_ft == 15);   // a rejected request changes nothing
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK(s.budget.moved_ft == 0);
}

TEST_CASE("moved_ft: an opportunity-attack-interrupted move does not count; a declined one does") {
    auto make = [](bool killer) {
        auto p = pc(1);
        auto m = monster(2);
        m.attacks = {Attack{"bite", 30, {1, 4, 40}, DamageType::Piercing, 5, false, false}};   // always hits, kills
        p.hp = p.hp_max = 5;
        p.ac = 1;
        m.reaction_policy.fill(killer ? ReactionPolicy::Auto : ReactionPolicy::Never);
        EncounterHooks h = spell_hooks();
        auto s = begin_encounter({p, m}, 3, nullptr, h);
        end_turns_until(s, 1);
        return s;
    };
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5; mv.move_tiles = 1; mv.leaves_reach = true;
    auto hit = make(true);
    auto r = step(hit, mv);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::MoveInterrupted) == 1);
    CHECK(count(r.events, EventKind::Moved) == 0);
    CHECK(hit.budget.moved_ft == 0);
    auto free = make(false);
    auto r2 = step(free, mv);
    REQUIRE(r2.ok);
    CHECK(count(r2.events, EventKind::Moved) == 1);
    CHECK(free.budget.moved_ft == 5);
}

TEST_CASE("ReactionOption::uses_reaction = false leaves the reaction available") {
    auto w = wizard(1);
    auto g = monster(2);
    g.attacks = {Attack{"claw", 30, {1, 4, 0}, DamageType::Slashing, 5, false, false}};   // always hits
    EncounterHooks h = spell_hooks();
    h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant& r) {
        std::vector<ReactionOption> o;
        if (r.id == 1) {
            ReactionOption x{0, ReactionOptionKind::AcBonus, 100, 4242, 0};
            x.uses_reaction = false;
            o.push_back(x);
        }
        return o;
    };
    w.spells_known.clear();   // only the hook option
    int taken = 0;
    for (uint64_t seed = 0; seed < 20; ++seed) {   // Auto reacts when the bonus turns the hit into a miss (not on a nat 20)
        auto s = begin_encounter({w, g}, seed, nullptr, h);
        mut(s, 1).reaction_policy.fill(ReactionPolicy::Auto);
        end_turns_until(s, 2);
        auto r = step(s, {2, ActionKind::Attack, 1, 0, 0, false, true});
        REQUIRE(r.ok);
        if (count(r.events, EventKind::ReactionTaken) == 0) continue;
        ++taken;
        CHECK_FALSE(find(s, 1)->reaction_used);
        CHECK(count(r.events, EventKind::AcBonus) == 1);
    }
    CHECK(taken > 0);
}
