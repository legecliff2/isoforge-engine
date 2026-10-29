// B7: Shambling Mound Engulf. Two attacks in one multiattack, both hitting the same creature, apply the
// gelatinous-cube contain (restrained, blinded while that lasts, a Strength escape check, automatic damage at
// the start of the user's turns, one creature at a time). No save to avoid it. No new hash tag: damage_when 3
// folds under 0xAC, and the both-hits window is TurnBudget (replay re-runs the attacks).
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kEngulf = spell_id_of("test.mound#slams#engulf");

SpellDef engulf_def() {
    SpellDef en;
    en.id = kEngulf; en.content_id = "test.mound#slams#engulf"; en.level = 0; en.targets = 1;
    en.kind = SpellEffectKind::ApplyCondition; en.is_spell = false; en.magical = false; en.components = 0;
    en.applies_condition = true; en.condition = Condition::Restrained; en.escape_dc = 14;
    en.contain = 1; en.contain_dice = {2, 8, 4}; en.contain_damage_type = DamageType::Bludgeoning;
    en.condition_max_size = int8_t(Size::Medium); en.contain_one = true;
    en.has_while = true; en.while_condition = Condition::Blinded;
    return en;
}

EncounterHooks hooks() {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    REQUIRE(b->add(engulf_def()));
    h.spells = b;
    return h;
}

Combatant mound(int to_hit = 30) {
    auto c = monster(1);
    c.hp = c.hp_max = 136; c.size = Size::Large; c.abilities.score[0] = 18;
    c.attacks = {Attack{"slam", int8_t(to_hit), {2, 8, 4}, DamageType::Bludgeoning, 5, false, false}};
    c.multiattacks = {{"slams", {0, 0}}};
    c.multiattacks[0].engulf_both = kEngulf;
    return c;
}

Combatant victim(CombatantId id, Size sz = Size::Medium, int hp = 80) {
    auto c = pc(id, hp); c.size = sz; c.abilities.score[1] = 10;
    return c;
}

EncounterState start(std::vector<Combatant> cs, uint64_t seed) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}

ActionRequest atk(CombatantId t, uint8_t multi = 0) {
    ActionRequest r{1, ActionKind::Attack, t, 0};
    r.multiattack = multi;
    r.target_within_5ft = true;
    return r;
}

bool swing_hit(const StepResult& r) {
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) return (e.c & 1) != 0;
    return false;
}

const ActiveSpellEffect* held(const EncounterState& s, CombatantId t) {
    for (const auto& e : s.effects)
        if (e.target == t && e.spell_id == kEngulf && e.contain == 1) return &e;
    return nullptr;
}

// Both slams hit. Returns false if a natural 1 spoiled it (the caller tries another seed).
bool both_hit(EncounterState& s, CombatantId t) {
    auto a = step(s, atk(t, 1));
    if (!a.ok || !swing_hit(a)) return false;
    auto b = step(s, atk(t, 1));
    return b.ok && swing_hit(b);
}

}  // namespace

TEST_CASE("Engulf: both slams hit the same Medium creature: restrained, blinded, contained; damage on the mound's next turn") {
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = start({mound(), victim(2), victim(3, Size::Huge)}, seed);
        if (!both_hit(s, 2)) continue;
        const auto* e = held(s, 2);
        REQUIRE(e);
        CHECK(e->contain == 1);
        CHECK(e->escape_check);
        CHECK(e->dc == 14);
        CHECK(e->damage_when == 3);
        CHECK(e->damage.count == 2);
        CHECK(e->damage.sides == 8);
        CHECK(e->damage.bonus == 4);
        CHECK(e->has_also);
        CHECK(e->also == Condition::Blinded);
        CHECK(find(s, 2)->has(Condition::Restrained));
        CHECK(find(s, 2)->has(Condition::Blinded));
        CHECK_FALSE(find(s, 2)->has(Condition::Grappled));   // the escape check is the printed grapple DC
        CHECK(container_of(s, 2) == 1);
        CHECK(held(s, 3) == nullptr);
        CHECK(s.budget.engulf_spell == 0);
        CHECK_FALSE(s.budget.engulf_due);
        const int16_t hp = find(s, 2)->hp;
        CHECK(hp < 80);                                     // the slams landed
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 1);
        CHECK(find(s, 2)->hp < hp);                         // 2d8+4 at the start of the mound's turn
        CHECK(container_of(s, 2) == 1);
        return;
    }
    FAIL("no seed where both slams hit");
}

TEST_CASE("Engulf: a miss, or two different targets, does not engulf") {
    bool saw_miss = false;
    for (uint64_t seed = 1; seed < 80 && !saw_miss; ++seed) {
        auto s = start({mound(0), victim(2)}, seed);
        mut(s, 2).ac = 25;
        auto a = step(s, atk(2, 1));
        REQUIRE(a.ok);
        auto b = step(s, atk(2, 1));
        REQUIRE(b.ok);
        if (swing_hit(a) && swing_hit(b)) continue;
        saw_miss = true;
        CHECK(held(s, 2) == nullptr);
        CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
        CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
        CHECK(container_of(s, 2) == 0);
    }
    REQUIRE(saw_miss);

    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = start({mound(), victim(2), victim(3)}, seed);
        auto a = step(s, atk(2, 1));
        if (!a.ok || !swing_hit(a)) continue;
        auto b = step(s, atk(3, 1));
        if (!b.ok || !swing_hit(b)) continue;
        CHECK(held(s, 2) == nullptr);
        CHECK(held(s, 3) == nullptr);
        CHECK(container_of(s, 2) == 0);
        CHECK(container_of(s, 3) == 0);
        return;
    }
    FAIL("no seed where the two slams hit different creatures");
}

TEST_CASE("Engulf: a creature larger than Medium is not engulfed; the slam damage still lands") {
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s = start({mound(), victim(2, Size::Large)}, seed);
        if (!both_hit(s, 2)) continue;
        CHECK(held(s, 2) == nullptr);
        CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
        CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
        CHECK(find(s, 2)->hp < 80);
        return;
    }
    FAIL("no seed where both slams hit the Large creature");
}

TEST_CASE("Engulf: one creature at a time") {
    for (uint64_t seed = 1; seed < 60; ++seed) {
        auto s = start({mound(), victim(2), victim(3)}, seed);
        if (!both_hit(s, 2)) continue;
        REQUIRE(held(s, 2));
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 1);
        if (!both_hit(s, 3)) continue;
        CHECK(held(s, 3) == nullptr);
        CHECK(container_of(s, 3) == 0);
        CHECK(container_of(s, 2) == 1);
        return;
    }
    FAIL("could not engulf one creature and then swing at a second");
}

TEST_CASE("Engulf: immunity to restrained applies neither condition") {
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto immune = victim(2);
        immune.condition_immunities = bit(Condition::Restrained);
        auto s = start({mound(), immune}, seed);
        if (!both_hit(s, 2)) continue;
        CHECK(held(s, 2) == nullptr);
        CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
        CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
        CHECK(find(s, 2)->hp < 80);
        return;
    }
    FAIL("no seed where both slams hit the immune creature");
}

TEST_CASE("Engulf: a Strength check vs 14 escapes; the mound dying frees without prone") {
    bool escaped = false;
    for (uint64_t seed = 1; seed < 30 && !escaped; ++seed) {
        auto s = start({mound(), victim(2)}, seed);
        if (!both_hit(s, 2)) continue;
        REQUIRE(held(s, 2));
        mut(s, 2).abilities.score[0] = 30;
        end_turns_until(s, 2);
        ActionRequest esc{2, ActionKind::Attack}; esc.special = 4;
        REQUIRE(step(s, esc).ok);
        if (held(s, 2)) continue;
        CHECK(container_of(s, 2) == 0);
        CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
        CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
        escaped = true;
    }
    REQUIRE(escaped);
    bool freed = false;
    for (uint64_t seed = 1; seed < 40 && !freed; ++seed) {
        auto s = start({mound(), victim(2)}, seed);
        if (!both_hit(s, 2)) continue;
        // Confirm an escape was possible above by requiring this engulf, then kill the mound.
        mut(s, 1).hp = 0; mut(s, 1).dead = true;
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
        CHECK(container_of(s, 2) == 0);
        CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
        CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
        CHECK_FALSE(find(s, 2)->has(Condition::Prone));
        freed = true;
    }
    REQUIRE(freed);
}

TEST_CASE("Engulf: forfeiting the second slam does not carry the window, and replay matches") {
    bool forfeited = false;
    for (uint64_t seed = 1; seed < 40 && !forfeited; ++seed) {
        auto s = start({mound(), victim(2)}, seed);
        auto first = step(s, atk(2, 1));
        if (!first.ok || !swing_hit(first)) continue;
        CHECK(s.budget.engulf_spell == kEngulf);
        CHECK(s.budget.engulf_hits == 1);
        CHECK_FALSE(s.budget.engulf_due);
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 1);
        CHECK(s.budget.engulf_spell == 0);
        CHECK(held(s, 2) == nullptr);
        forfeited = true;
    }
    REQUIRE(forfeited);

    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto make = [&] { return start({mound(), victim(2)}, seed); };
        auto a = make();
        ActionLog log;
        auto r1 = step_logged(a, atk(2, 1), log);
        if (!r1.ok || !swing_hit(r1)) continue;
        auto r2 = step_logged(a, atk(2, 1), log);
        if (!r2.ok || !swing_hit(r2)) continue;
        REQUIRE(held(a, 2));
        REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
        while (current_actor(a) != 1) REQUIRE(step_logged(a, {current_actor(a), ActionKind::EndTurn}, log).ok);
        auto b = make();
        for (const auto& ls : log) {
            auto r = replay_step(b, ls);
            REQUIRE(r.ok);
            CHECK_FALSE(r.desync);
        }
        CHECK(find(a, 2)->hp == find(b, 2)->hp);
        CHECK(container_of(b, 2) == 1);
        CHECK(find(b, 2)->has(Condition::Blinded));
        CHECK(find(b, 2)->has(Condition::Restrained));
        return;
    }
    FAIL("no seed where both slams hit for the replay");
}
