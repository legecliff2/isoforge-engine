// Altitude PR4 (SRD 5.1 Falling; docs/SRD_COMBAT_COVERAGE.md 11.8): falling damage (1d6 bludgeoning per 10 ft, max 20d6, lands
// prone), flyers that fall when knocked prone / to speed 0 / out of their fly speed, hover, the Fly spell hook
// (grant_fly / revoke_fly) and ForcedMoveResult::fell_ft.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
Combatant flyer(CombatantId id, int16_t alt, uint8_t fly = 60) {
    Combatant c = monster(id);
    c.team = 0;
    c.hp = c.hp_max = 500;   // survives 20d6
    c.fly_ft = fly;
    c.alt_ft = alt;
    never_react(c);
    return c;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 7) {
    auto s = begin_encounter(std::move(cs), seed);
    end_turns_until(s, 1);
    return s;
}
// The damage `dice` d6 draw from `r` (the same draws rules makes).
int expect_d6(rng::Rng& r, int dice) { int t = 0; for (int i = 0; i < dice; ++i) t += r.d(6); return t; }
int damage_of(const std::vector<Event>& ev, CombatantId id) {
    int t = 0;
    for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) t += e.a;
    return t;
}
}  // namespace

TEST_CASE("altitude PR4: fall damage is 1d6 per 10 ft (rules RNG), capped at 20d6; lands prone") {
    for (int ft : {10, 15, 20, 55, 200, 210, 300, 1000}) {
        auto s = start({flyer(1, 0), monster(2)});
        rng::Rng r = s.rng;
        const int dice = std::min(ft / 10, 20);
        const int want = expect_d6(r, dice);
        std::vector<Event> ev;
        fall(s, 1, ft, ev);
        const auto& c = *find(s, 1);
        CHECK(c.hp == 500 - want);
        CHECK(damage_of(ev, 1) == want);
        CHECK(c.has(Condition::Prone));
        CHECK(c.alt_ft == 0);
        CHECK(s.rng.save().state == r.save().state);   // exactly `dice` draws: 20 at most
        CHECK(s.landed.empty());                       // sim's own ledge drop: not recorded
    }
    auto s = start({flyer(1, 0), monster(2)});
    const auto r0 = s.rng.save();
    std::vector<Event> ev;
    fall(s, 1, 5, ev);   // under 10 ft: no damage, so not prone either
    CHECK(find(s, 1)->hp == 500);
    CHECK_FALSE(find(s, 1)->has(Condition::Prone));
    CHECK(s.rng.save().state == r0.state);
}

TEST_CASE("altitude PR4: a flyer knocked prone falls (damage, prone, at the ground, landed); hover does not") {
    auto s = start({flyer(1, 30), monster(2)});
    mut(s, 1).hover = false;
    add_condition(mut(s, 1), Condition::Prone);
    rng::Rng r = s.rng;
    auto res = step(s, ActionRequest{1, ActionKind::Dodge});   // any step settles the fall check
    INFO(res.error, " actor ", current_actor(s));
    REQUIRE(res.ok);
    CHECK(find(s, 1)->alt_ft == 0);
    REQUIRE(s.landed.size() == 1);
    CHECK(s.landed[0].id == 1);
    CHECK(s.landed[0].fell_ft == 30);
    CHECK(find(s, 1)->hp == 500 - expect_d6(r, 3));

    auto h = start({flyer(1, 30), monster(2)});
    mut(h, 1).hover = true;
    add_condition(mut(h, 1), Condition::Prone);
    REQUIRE(step(h, ActionRequest{1, ActionKind::Dodge}).ok);
    CHECK(find(h, 1)->alt_ft == 30);
    CHECK(h.landed.empty());
    CHECK(find(h, 1)->hp == 500);
}

TEST_CASE("altitude PR4: speed 0 (grappled / restrained) drops a flyer; a climber with no fly speed is not flying") {
    auto s = start({flyer(1, 20), monster(2)});
    add_condition(mut(s, 1), Condition::Restrained);
    REQUIRE(step(s, ActionRequest{1, ActionKind::Dodge}).ok);
    CHECK(find(s, 1)->alt_ft == 0);
    CHECK(find(s, 1)->has(Condition::Prone));

    auto c = start({flyer(1, 20, 0), monster(2)});
    mut(c, 1).climb_ft = 30;
    add_condition(mut(c, 1), Condition::Prone);
    REQUIRE(step(c, ActionRequest{1, ActionKind::Dodge}).ok);
    CHECK(find(c, 1)->alt_ft == 20);
}

TEST_CASE("altitude PR4: grant_fly / revoke_fly (fly speed = max(fly_ft, granted_fly_ft))") {
    auto s = start({flyer(1, 0, 0), monster(2)});
    CHECK(mode_speed(s, *find(s, 1), 1) == -1);
    CHECK_FALSE(grant_fly(s, 1, 0, 9));
    CHECK(grant_fly(s, 1, 60, 9));
    CHECK(find(s, 1)->granted_fly_ft == 60);
    CHECK(find(s, 1)->fly_speed() == 60);
    CHECK(mode_speed(s, *find(s, 1), 1) == 60);
    std::vector<Event> ev;
    CHECK_FALSE(revoke_fly(s, 1, 8, ev));   // another source's grant stays
    CHECK(find(s, 1)->granted_fly_ft == 60);
    CHECK(revoke_fly(s, 1, 9, ev));
    CHECK(find(s, 1)->granted_fly_ft == 0);
    CHECK(mode_speed(s, *find(s, 1), 1) == -1);
    CHECK(ev.empty());   // on the ground: nothing falls
    CHECK_FALSE(revoke_fly(s, 1, 0, ev));

    auto n = start({flyer(1, 0, 30), monster(2)});   // a natural fly speed: the larger one counts
    REQUIRE(grant_fly(n, 1, 60, 1));
    CHECK(find(n, 1)->fly_speed() == 60);
    REQUIRE(grant_fly(n, 1, 20, 1));
    CHECK(find(n, 1)->fly_speed() == 30);
}

TEST_CASE("altitude PR4: revoking a granted fly speed mid-air falls; held aloft by magic, prone does not") {
    auto s = start({flyer(1, 40, 0), monster(2)});
    REQUIRE(grant_fly(s, 1, 60, 5));
    add_condition(mut(s, 1), Condition::Prone);   // SRD: held aloft by magic (Fly) -> no fall
    REQUIRE(step(s, ActionRequest{1, ActionKind::Dodge}).ok);
    CHECK(find(s, 1)->alt_ft == 40);
    remove_condition(mut(s, 1), Condition::Prone);
    rng::Rng r = s.rng;
    std::vector<Event> ev;
    REQUIRE(revoke_fly(s, 1, 5, ev));
    CHECK(find(s, 1)->alt_ft == 0);
    CHECK(find(s, 1)->has(Condition::Prone));
    CHECK(damage_of(ev, 1) == expect_d6(r, 4));
    REQUIRE(s.landed.size() == 1);
    CHECK(s.landed[0].fell_ft == 40);

    auto h = start({flyer(1, 40, 0), monster(2)});   // hover: no fall
    mut(h, 1).hover = true;
    REQUIRE(grant_fly(h, 1, 60, 5));
    std::vector<Event> hv;
    REQUIRE(revoke_fly(h, 1, 5, hv));
    CHECK(find(h, 1)->alt_ft == 40);
}

TEST_CASE("altitude PR4: ForcedMoveResult::fell_ft deals falling damage after the Moved (lands prone)") {
    const uint32_t kWave = spell_id_of("test.spell#fall_wave");
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef tw; tw.id = kWave; tw.content_id = "test.spell#fall_wave"; tw.level = 1; tw.range_ft = 0;
    tw.kind = SpellEffectKind::Save; tw.save_ability = Ability::Con; tw.on_save = OnSave::Half; tw.dice = DiceExpr{1, 1, 0};
    tw.damage_type = DamageType::Thunder; tw.area = AreaShape::Cube; tw.area_size_ft = 15; tw.fixed_dc = 30;
    SpellEffect push; push.kind = SpellEffectKind::ForcedMove; push.affected_only = true; push.forced.mode = 1; push.forced.feet = 10;
    tw.extra_effects = {push};
    book->add(tw);
    h.spells = book;
    h.forced_move = [](const EncounterState&, const ForcedMoveQuery& q) {
        ForcedMoveResult r; r.feet_moved = q.feet; r.fell_ft = 30; return r;
    };
    auto w = wizard(1);
    w.spells_known = {kWave};
    never_react(w);
    auto t = flyer(2, 0, 0);
    t.team = 1;
    never_react(t);
    auto s = begin_encounter({w, t}, 3, nullptr, h);
    end_turns_until(s, 1);
    ActionRequest r{1, ActionKind::CastSpell, 2};
    r.spell_id = kWave; r.spell_level = 1; r.targets = {2};
    auto res = step(s, r);
    REQUIRE(res.ok);
    int moved_at = -1, fall_dmg_at = -1;
    for (int i = 0; i < int(res.events.size()); ++i) {
        const auto& e = res.events[std::size_t(i)];
        if (e.kind == EventKind::Moved && e.actor == 2 && moved_at < 0) moved_at = i;
        if (e.kind == EventKind::DamageDealt && e.target == 2 && moved_at >= 0) fall_dmg_at = i;
    }
    REQUIRE(moved_at >= 0);
    CHECK(fall_dmg_at > moved_at);
    CHECK(find(s, 2)->has(Condition::Prone));
    REQUIRE(s.landed.size() == 1);
    CHECK(s.landed[0].id == 2);
    CHECK(s.landed[0].fell_ft == 30);
}

TEST_CASE("altitude PR4: falls replay deterministically (step_logged / replay_step)") {
    auto s = start({flyer(1, 60), monster(2)});
    add_condition(mut(s, 1), Condition::Prone);
    const EncounterState s0 = s;
    ActionLog log;
    REQUIRE(step_logged(s, ActionRequest{1, ActionKind::Dodge}, log).ok);
    REQUIRE(step_logged(s, ActionRequest{1, ActionKind::EndTurn}, log).ok);
    EncounterState again = s0;
    for (const auto& e : log) REQUIRE(replay_step(again, e).ok);
    CHECK(find(again, 1)->hp == find(s, 1)->hp);
    CHECK(find(again, 1)->alt_ft == 0);
    CHECK(again.rng.save().state == s.rng.save().state);
    CHECK(find(s, 1)->hp < 500);
}
