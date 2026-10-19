// R-G7: d20 reroll (Lucky), AttackRolled subtract window (Cutting Words), Great Weapon Fighting, Evasion.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/dice.hpp"

using namespace enc;

namespace {
const uint32_t kBlastHalf = spell_id_of("test.spell#blast_half");
const uint32_t kCrushHalf = spell_id_of("test.spell#crush_half");
const uint32_t kWords = 900, kInspiration = 901;

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kBlastHalf; d.content_id = "test.spell#blast_half"; d.level = 0; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 13; d.on_save = OnSave::Half;
    d.dice = DiceExpr{4, 6, 0}; d.damage_type = DamageType::Fire;
    book->add(d);
    SpellDef c = d; c.id = kCrushHalf; c.content_id = "test.spell#crush_half"; c.save_ability = Ability::Str;
    book->add(c);
    h.spells = book;
    return h;
}
Combatant fighter(CombatantId id, int to_hit, uint16_t props = 0, bool ranged = false) {
    auto c = pc(id, 200);
    Attack a{"weapon", int8_t(to_hit), {1, 12, 0}, DamageType::Slashing, uint8_t(ranged ? 80 : 5), ranged, false};
    a.properties = props;
    c.attacks = {a};
    return c;
}
Combatant dummy(CombatantId id, int ac) { auto m = monster(id); m.hp = m.hp_max = 5000; m.ac = int8_t(ac); return m; }
Combatant bard(CombatantId id, ReactionPolicy p) {
    auto b = monster(id); b.hp = b.hp_max = 500;
    Reaction r; r.id = kWords; r.window = ReactionTrigger::AttackRolled; r.kind = ReactionEffectKind::SubtractRoll;
    r.dice = DiceExpr{1, 6, 0}; r.pool = kInspiration; r.pool_cost = 1;
    b.reactions = {r};
    b.reaction_policy.fill(p);
    return b;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
struct Roll { int natural = 0, total = 0, dealt = 0; bool hit = false, crit = false, window = false, spent = false; };
Roll summarize(const StepResult& r) {
    Roll out;
    for (const auto& e : r.events) {
        if (e.kind == EventKind::AttackResolved) { out.natural = e.a; out.total = e.b; out.hit = e.c & 1; out.crit = (e.c & 2) != 0; }
        if (e.kind == EventKind::DamageDealt && e.target == 2) out.dealt += e.a;
        if (e.kind == EventKind::ReactionWindowOpened && e.a == int32_t(ReactionTrigger::AttackRolled)) out.window = true;
        if (e.kind == EventKind::ResourceSpent) out.spent = true;
    }
    return out;
}
ActionRequest swing(bool grip = false) {
    ActionRequest r{1, ActionKind::Attack, 2, 0, 0, false, true};
    r.two_handed_grip = grip;
    return r;
}
Roll attack_once(std::vector<Combatant> cs, uint64_t seed, bool grip = false) {
    auto s = start(std::move(cs), seed);
    const auto r = step(s, swing(grip));
    REQUIRE(r.ok);
    return summarize(r);
}
}  // namespace

TEST_CASE("enums and defaults") {
    CHECK(uint8_t(ReactionTrigger::AttackRolled) == 7);
    CHECK(kReactionTriggerCount == 8);
    CHECK(uint8_t(ReactionEffectKind::SubtractRoll) == 4);
    CHECK(uint8_t(ReactionOptionKind::SubtractRoll) == 8);
    Combatant c{};
    CHECK(c.reroll_ones == 0); CHECK_FALSE(c.great_weapon_fighting); CHECK_FALSE(c.evasion);
    Reaction r;
    CHECK(r.pool == 0); CHECK(r.pool_cost == 0);
    ActionRequest q{1, ActionKind::Attack};
    CHECK_FALSE(q.two_handed_grip);
}

TEST_CASE("reroll_natural_one: one more draw only on a natural 1, the die showing 1 is replaced") {
    rng::Rng r(7, 0);
    D20Roll x{12, 0, 15, RollMode::Normal};
    auto copy = r;
    CHECK_FALSE(reroll_natural_one(r, x));
    CHECK(r.d(20) == copy.d(20));   // no draw
    for (uint64_t seed = 0; seed < 40; ++seed) {
        rng::Rng a(seed, 0); auto peek = a; const int d = peek.d(20);
        D20Roll n{1, 0, 4, RollMode::Normal};
        REQUIRE(reroll_natural_one(a, n));
        CHECK(n.natural == d); CHECK(n.total == d + 3);
        rng::Rng b(seed, 0);
        D20Roll adv{1, 1, 6, RollMode::Advantage};
        reroll_natural_one(b, adv);
        CHECK(adv.natural == d); CHECK(adv.natural_other == 1);
        rng::Rng c(seed, 0);
        D20Roll dis{1, 15, 3, RollMode::Disadvantage};
        reroll_natural_one(c, dis);
        CHECK(dis.natural == std::min(d, 15)); CHECK(dis.natural_other == std::max(d, 15));
        CHECK(dis.total == 3 + dis.natural - 1);
    }
}

TEST_CASE("roll_dice_reroll_low matches a die-by-die reroll and roll_dice when nothing is low") {
    for (uint64_t seed = 0; seed < 50; ++seed) {
        rng::Rng a(seed, 0), b(seed, 0), c(seed, 0);
        const DiceExpr e{4, 6, 2};
        const int got = roll_dice_reroll_low(a, e, true, 2);
        int want = 2; bool low = false;
        for (int k = 0; k < 8; ++k) { int d = b.d(6); if (d <= 2) { low = true; d = b.d(6); } want += d; }
        CHECK(got == want);
        if (!low) CHECK(roll_dice(c, e, true) == got);
    }
}

TEST_CASE("Lucky on attack rolls: only a natural 1 changes, and it is rerolled") {
    int rerolled = 0;
    for (uint64_t seed = 0; seed < 300; ++seed) {
        auto plain = fighter(1, 5), lucky = fighter(1, 5);
        lucky.reroll_ones = 1;
        const Roll a = attack_once({plain, dummy(2, 12)}, seed), b = attack_once({lucky, dummy(2, 12)}, seed);
        if (a.natural != 1) { CHECK(b.natural == a.natural); CHECK(b.total == a.total); CHECK(b.hit == a.hit); }
        else { CHECK(b.total == b.natural + 5); if (b.natural != 1) ++rerolled; }
    }
    CHECK(rerolled > 0);
    auto checks_only = fighter(1, 5); checks_only.reroll_ones = 6;   // checks and saves, not attacks
    for (uint64_t seed = 0; seed < 100; ++seed) {
        const Roll a = attack_once({fighter(1, 5), dummy(2, 12)}, seed), b = attack_once({checks_only, dummy(2, 12)}, seed);
        CHECK(a.natural == b.natural);
    }
}

TEST_CASE("Lucky on ability checks and saving throws") {
    int checks = 0, saves = 0;
    for (uint64_t seed = 0; seed < 300; ++seed) {
        auto lucky = fighter(1, 5); lucky.reroll_ones = 2;
        auto s = start({fighter(1, 5), dummy(2, 12)}, seed);
        auto t = start({lucky, dummy(2, 12)}, seed);
        const auto a = encounter_check(s, 1, Skill::Athletics, 10);
        const auto b = encounter_check(t, 1, Skill::Athletics, 10);
        const Event* ea = nullptr; const Event* eb = nullptr;
        for (const auto& e : a.events) if (e.kind == EventKind::CheckRolled) ea = &e;
        for (const auto& e : b.events) if (e.kind == EventKind::CheckRolled) eb = &e;
        REQUIRE(ea); REQUIRE(eb);
        if (ea->a != 1) CHECK(eb->a == ea->a); else if (eb->a != 1) ++checks;
        // saves: the dummy saves against the wizard's Dex blast
        auto caster = wizard(1); caster.spells_known.push_back(kBlastHalf);
        auto target = dummy(2, 12); auto lucky_t = dummy(2, 12); lucky_t.reroll_ones = 4;
        auto u = start({caster, target}, seed);
        auto v = start({caster, lucky_t}, seed);
        const auto p = step(u, cast(1, kBlastHalf, 2));
        const auto q = step(v, cast(1, kBlastHalf, 2));
        REQUIRE(p.ok); REQUIRE(q.ok);
        const Event* sp = nullptr; const Event* sq = nullptr;
        for (const auto& e : p.events) if (e.kind == EventKind::SavingThrowRolled) sp = &e;
        for (const auto& e : q.events) if (e.kind == EventKind::SavingThrowRolled) sq = &e;
        REQUIRE(sp); REQUIRE(sq);
        if (sp->a != 1) CHECK(sq->a == sp->a); else if (sq->a != 1) ++saves;
    }
    CHECK(checks > 0); CHECK(saves > 0);
}

TEST_CASE("Evasion: a Dex save for half deals none on a success and half on a failure") {
    int succ = 0, fail_ = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto caster = wizard(1); caster.spells_known = {kBlastHalf, kCrushHalf};
        auto evader = dummy(2, 12); evader.evasion = true;
        auto u = start({caster, dummy(2, 12)}, seed);
        auto v = start({caster, evader}, seed);
        const auto p = step(u, cast(1, kBlastHalf, 2));
        const auto q = step(v, cast(1, kBlastHalf, 2));
        REQUIRE(p.ok); REQUIRE(q.ok);
        bool saved = false;
        for (const auto& e : q.events) if (e.kind == EventKind::SavingThrowRolled) saved = e.c & 1;
        const int plain = summarize(p).dealt, ev = summarize(q).dealt;
        if (saved) { CHECK(ev == 0); ++succ; }
        else { ++fail_; CHECK(ev == plain / 2); }   // plain failed too: same roll, full damage
        // a Str save is unaffected
        auto w = start({caster, evader}, seed);
        auto x = start({caster, dummy(2, 12)}, seed);
        CHECK(summarize(step(w, cast(1, kCrushHalf, 2))).dealt == summarize(step(x, cast(1, kCrushHalf, 2))).dealt);
    }
    CHECK(succ > 0); CHECK(fail_ > 0);
}

TEST_CASE("Great Weapon Fighting: two-handed melee only; versatile needs the two-handed grip") {
    int diff_2h = 0, diff_vers = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto gwf_2h = fighter(1, 40, kWeaponTwoHanded); gwf_2h.great_weapon_fighting = true;
        const Roll a = attack_once({fighter(1, 40, kWeaponTwoHanded), dummy(2, 10)}, seed);
        const Roll b = attack_once({gwf_2h, dummy(2, 10)}, seed);
        if (a.hit && !a.crit && a.dealt > 2) CHECK(b.dealt == a.dealt);
        if (a.dealt != b.dealt) { ++diff_2h; if (!a.crit) CHECK(a.dealt <= 2); }
        auto gwf_v = fighter(1, 40, kWeaponVersatile); gwf_v.great_weapon_fighting = true;
        const Roll c = attack_once({fighter(1, 40, kWeaponVersatile), dummy(2, 10)}, seed, true);
        CHECK(attack_once({gwf_v, dummy(2, 10)}, seed, false).dealt == c.dealt);   // one hand: no reroll
        if (attack_once({gwf_v, dummy(2, 10)}, seed, true).dealt != c.dealt) ++diff_vers;
        auto gwf_r = fighter(1, 40, kWeaponTwoHanded, true); gwf_r.great_weapon_fighting = true;   // ranged (heavy crossbow)
        CHECK(attack_once({gwf_r, dummy(2, 10)}, seed).dealt == attack_once({fighter(1, 40, kWeaponTwoHanded, true), dummy(2, 10)}, seed).dealt);
    }
    CHECK(diff_2h > 0); CHECK(diff_vers > 0);
}

TEST_CASE("Cutting Words: AttackRolled window subtracts the die, spends the pool, Auto only when it could flip a hit") {
    int taken = 0;
    for (uint64_t seed = 0; seed < 120; ++seed) {
        auto base_bard = bard(3, ReactionPolicy::Auto); base_bard.reactions.clear();
        auto s0 = start({fighter(1, 5), dummy(2, 15), base_bard}, seed);
        auto s1 = start({fighter(1, 5), dummy(2, 15), bard(3, ReactionPolicy::Auto)}, seed);
        register_pool(s0, 3, kInspiration, 5, 5);
        register_pool(s1, 3, kInspiration, 5, 5);
        const auto r0 = step(s0, swing());
        const auto r1 = step(s1, swing());
        REQUIRE(r0.ok); REQUIRE(r1.ok);
        const Roll a = summarize(r0), b = summarize(r1);
        CHECK_FALSE(a.window);
        CHECK(a.natural == b.natural);
        const bool could_flip = a.natural != 1 && a.natural != 20 && a.total >= 15 && a.total - 6 < 15;
        if (a.natural == 1 || a.natural == 20) CHECK_FALSE(b.window);
        else CHECK(b.window);
        if (could_flip) {
            ++taken;
            CHECK(b.spent);
            CHECK(a.total - b.total >= 1); CHECK(a.total - b.total <= 6);
            CHECK(b.hit == (b.total >= 15));
            CHECK(find_pool(s1, 3, kInspiration)->cur == 4);
            CHECK(find(s1, 3)->reaction_used);
        } else {
            CHECK(b.total == a.total); CHECK_FALSE(b.spent);
            CHECK(find_pool(s1, 3, kInspiration)->cur == 5);
        }
    }
    CHECK(taken > 0);
}

TEST_CASE("Cutting Words: not offered without the pool, to allies, or under Never; Ask pauses") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = start({fighter(1, 5), dummy(2, 15), bard(3, ReactionPolicy::Auto)}, seed);   // no pool registered
        CHECK_FALSE(summarize(step(s, swing())).window);
        auto ally = bard(3, ReactionPolicy::Auto); ally.team = 0;
        auto t = start({fighter(1, 5), dummy(2, 15), ally}, seed);
        register_pool(t, 3, kInspiration, 5, 5);
        CHECK_FALSE(summarize(step(t, swing())).window);
        auto u = start({fighter(1, 5), dummy(2, 15), bard(3, ReactionPolicy::Never)}, seed);
        register_pool(u, 3, kInspiration, 5, 5);
        CHECK_FALSE(summarize(step(u, swing())).window);
    }
    bool paused = false;
    for (uint64_t seed = 0; seed < 30 && !paused; ++seed) {
        auto s = start({fighter(1, 5), dummy(2, 15), bard(3, ReactionPolicy::Ask)}, seed);
        register_pool(s, 3, kInspiration, 5, 5);
        const auto r = step(s, swing());
        REQUIRE(r.ok);
        if (r.status != StepStatus::NeedsDecision) continue;
        paused = true;
        REQUIRE(r.needs);
        CHECK(r.needs->window.trigger == ReactionTrigger::AttackRolled);
        const auto done = step(s, decision_request(*r.needs, true, 0));
        REQUIRE(done.ok);
        CHECK(done.status == StepStatus::Done);
        CHECK(find_pool(s, 3, kInspiration)->cur == 4);
    }
    CHECK(paused);
}
