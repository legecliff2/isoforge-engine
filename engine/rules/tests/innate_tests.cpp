// Row 8: innate spellcasting (Combatant::innate, ActionRequest::innate) and printed DC / attack overrides.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

// A monster with no slots that knows nothing: everything it casts is innate.
Combatant mage_monster(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 200;
    m.abilities.score[std::size_t(Ability::Int)] = 10;
    m.abilities.score[std::size_t(Ability::Cha)] = 18;   // +4
    m.spellcasting_ability = Ability::Int;               // slot ability (+0)
    m.proficiency_bonus = 3;
    m.caster_level = 9;
    return m;
}
InnateSpell at_will(uint32_t id) { InnateSpell i; i.spell_id = id; return i; }
InnateSpell per_day(uint32_t id, uint8_t n) { InnateSpell i; i.spell_id = id; i.uses_max = n; i.uses_left = n; return i; }

ActionRequest icast(CombatantId a, uint32_t spell, CombatantId t, uint8_t level) {
    auto r = cast(a, spell, t, level);
    r.innate = true;
    return r;
}
const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
Combatant target(CombatantId id) { auto c = pc(id, 400); return c; }

}  // namespace

TEST_CASE("defaults: no innate list, no overrides, request flag false (old behaviour)") {
    Combatant c{};
    CHECK(c.innate.empty());
    CHECK(c.spell_dc_override == 0);
    CHECK(c.spell_attack_override == 0);
    CHECK_FALSE(ActionRequest{}.innate);
    auto w = wizard(1);
    CHECK(spell_save_dc(w) == 14);
    CHECK(spell_attack_bonus(w) == 6);
}

TEST_CASE("DC / attack overrides feed spell_save_dc / spell_attack_bonus; fixed_dc on the def still wins") {
    auto m = mage_monster(1);
    m.spellcasting_ability = Ability::Int;
    CHECK(spell_save_dc(m) == 11);         // 8 + 3 + 0
    m.spell_dc_override = 17; m.spell_attack_override = 9;
    CHECK(spell_save_dc(m) == 17);
    CHECK(spell_attack_bonus(m) == 9);
    SpellDef d;
    CHECK(spell_save_dc(m, d) == 17);
    d.fixed_dc = 21; d.fixed_attack = 12;
    CHECK(spell_save_dc(m, d) == 21);
    CHECK(spell_attack_bonus(m, d) == 12);
}

TEST_CASE("innate at-will cast: no slot, own level, no spell_known needed, SpellCast window as for any spell") {
    auto m = mage_monster(1);
    m.innate = {at_will(kFireBolt)};
    auto s = begin_encounter({m, target(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    for (int turn = 0; turn < 3; ++turn) {   // at will: repeatable every turn
        auto r = step(s, icast(1, kFireBolt, 2, 0));
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(count(r.events, EventKind::SpellCast) == 1);
        CHECK(count(r.events, EventKind::SpellResolved) == 1);
        CHECK(count(r.events, EventKind::ActionUsed) == 0);   // it is a spell
        end_turns_until(s, 2); end_turns_until(s, 1);
    }
}

TEST_CASE("innate: not in the list, upcast, no uses left and unknown book entries are rejected") {
    auto m = mage_monster(1);
    m.innate = {per_day(kMagicMissile, 1)};
    m.spells_known = {kFireBolt};
    auto s = begin_encounter({m, target(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto r = step(s, icast(1, kFireBolt, 2, 0));   // known, but not innate
    CHECK_FALSE(r.ok); CHECK(r.error == "not an innate spell");
    r = step(s, icast(1, kMagicMissile, 2, 2));   // slot level above the spell's own
    CHECK_FALSE(r.ok); CHECK(r.error == "innate spells cannot be upcast");
    r = step(s, icast(1, kMagicMissile, 2, 0));    // below the spell's own level (it is level 1)
    CHECK_FALSE(r.ok); CHECK(r.error == "innate spells cannot be upcast");
    REQUIRE(step(s, icast(1, kMagicMissile, 2, 1)).ok);
    end_turns_until(s, 2); end_turns_until(s, 1);
    r = step(s, icast(1, kMagicMissile, 2, 1));
    CHECK_FALSE(r.ok); CHECK(r.error == "innate spell has no uses left");
    // innate flag on an id outside the book
    r = step(s, icast(1, 0xDEADBEEF, 2, 1));
    CHECK_FALSE(r.ok);
    // a slot cast of a non-innate spell is unaffected
    auto k = step(s, cast(1, kFireBolt, 2, 0));
    CHECK(k.ok);
}

TEST_CASE("per-day: counts uses, legal_actions offers while uses remain, no slot spent") {
    auto m = mage_monster(1);
    m.innate = {per_day(kHoldPerson, 2)};
    m.spell_slots_max = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; m.spell_slots = m.spell_slots_max;
    auto s = begin_encounter({m, target(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    auto offered = [&] {
        int n = 0;
        for (const auto& q : legal_actions(s)) if (q.kind == ActionKind::CastSpell && q.spell_id == kHoldPerson) {
            CHECK(q.innate); CHECK(q.spell_level == 2); ++n;
        }
        return n;
    };
    CHECK(offered() == 1);
    REQUIRE(step(s, icast(1, kHoldPerson, 2, 2)).ok);
    CHECK(find(s, 1)->innate[0].uses_left == 1);
    end_turns_until(s, 2); end_turns_until(s, 1);
    mut(s, 2).conditions = 0; s.effects.clear(); mut(s, 1).concentration_spell = 0;
    CHECK(offered() == 1);
    REQUIRE(step(s, icast(1, kHoldPerson, 2, 2)).ok);
    CHECK(find(s, 1)->innate[0].uses_left == 0);
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK(offered() == 0);
    // Slots were never involved.
    for (int l = 1; l <= 9; ++l) CHECK(find(s, 1)->spell_slots[std::size_t(l)] == 0);
}

TEST_CASE("components: skipped for innate unless as_listed") {
    for (bool listed : {false, true}) {
        auto m = mage_monster(1);
        auto is = at_will(kHoldPerson); is.as_listed = listed;
        m.innate = {is};
        EncounterHooks h = spell_hooks();
        h.can_provide_components = [](const EncounterState&, const Combatant&, const SpellDef&) { return false; };
        auto s = begin_encounter({m, target(2)}, 3, nullptr, h);
        end_turns_until(s, 1);
        bool offered = false;
        for (const auto& q : legal_actions(s)) offered |= (q.spell_id == kHoldPerson && q.innate);
        CHECK(offered == !listed);
        auto r = step(s, icast(1, kHoldPerson, 2, 2));
        if (listed) { CHECK_FALSE(r.ok); CHECK(r.error == "components unavailable"); }
        else CHECK(r.ok);
    }
}

TEST_CASE("innate ability: the block's own ability sets the DC; the override beats it; slot casting keeps the slot ability") {
    // Fixed save DC comes from caster: Cha +4 innate vs Int +0 slot. DC = 8 + 3 + mod.
    auto run = [&](bool use_innate_ability, int dc_override) {
        std::vector<int> dcs;
        for (uint64_t seed = 0; seed < 200; ++seed) {
            auto m = mage_monster(1);
            auto is = at_will(kHoldPerson); is.has_ability = use_innate_ability; is.ability = Ability::Cha;
            m.innate = {is};
            m.spell_dc_override = int16_t(dc_override);
            auto t = target(2); t.hp = t.hp_max = 400;
            auto s = begin_encounter({m, t}, seed, nullptr, spell_hooks());
            end_turns_until(s, 1);
            auto r = step(s, icast(1, kHoldPerson, 2, 2));
            REQUIRE(r.ok);
            const Event* sv = first(r.events, EventKind::SavingThrowRolled);
            REQUIRE(sv);
            // success iff total >= DC: infer the DC bound from every sample (max failing total, min passing total).
            dcs.push_back(int(sv->c) ? sv->b : -sv->b - 1);
            CHECK(find(s, 1)->spellcasting_ability == Ability::Int);   // restored after the cast
        }
        int lo = 1000, hi = -1000;   // hi = highest failing total, lo = lowest passing total
        for (int v : dcs) { if (v >= 0) lo = std::min(lo, v); else hi = std::max(hi, -v - 1); }
        return std::pair{hi, lo};   // DC in (hi, lo]
    };
    auto [h0, l0] = run(false, 0);   // DC 11 (8+3+0)
    CHECK(h0 < 11); CHECK(l0 >= 11);
    auto [h1, l1] = run(true, 0);    // DC 15 (8+3+4)
    CHECK(h1 < 15); CHECK(l1 >= 15);
    CHECK(h1 >= 11);                 // some totals between 11 and 14 failed, which DC 11 would have passed
    auto [h2, l2] = run(true, 19);   // override wins: DC 19
    CHECK(h2 < 19); CHECK(l2 >= 19);
}

TEST_CASE("innate recharge: one use, expended on cast, d6 at the owner's turn start, only when expended") {
    SpellDef d;   // use Hold Person as the carrier
    auto m = mage_monster(1);
    InnateSpell is; is.spell_id = kHoldPerson; is.recharge_min = 6; is.uses_left = 1;
    m.innate = {is};
    int back = 0, stayed = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto s = begin_encounter({m, target(2)}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        REQUIRE(step(s, icast(1, kHoldPerson, 2, 2)).ok);
        CHECK(find(s, 1)->innate[0].uses_left == 0);
        CHECK_FALSE(step(s, icast(1, kHoldPerson, 2, 2)).ok);
        end_turns_until(s, 2);
        auto r = step(s, {2, ActionKind::EndTurn});   // 1's turn starts: one RechargeRolled
        const Event* rr = first(r.events, EventKind::RechargeRolled);
        REQUIRE(rr);
        CHECK(rr->actor == 1); CHECK(rr->a == int32_t(kHoldPerson)); CHECK(rr->c == int32_t(rr->b >= 6));
        CHECK(find(s, 1)->innate[0].uses_left == (rr->c ? 1 : 0));
        (rr->c ? back : stayed)++;
        if (rr->c) {   // a ready spell does not roll next time
            end_turns_until(s, 2);
            auto r2 = step(s, {2, ActionKind::EndTurn});
            CHECK(count(r2.events, EventKind::RechargeRolled) == 0);
        }
    }
    CHECK(back > 0); CHECK(stayed > 0);
    (void)d;
}

TEST_CASE("RNG invariant: a combatant with only at-will or ready innate spells draws nothing at turn start") {
    auto run = [](bool innate) {
        auto m = mage_monster(1);
        if (innate) m.innate = {at_will(kFireBolt), per_day(kMagicMissile, 3)};
        auto s = begin_encounter({m, target(2)}, 7, nullptr, spell_hooks());
        for (int i = 0; i < 8; ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
        return s.rng;
    };
    auto a = run(false), b = run(true);
    CHECK(a.d(1000) == b.d(1000));
}

TEST_CASE("innate digest: covers uses, ability, overrides; absent state is detected as empty") {
    auto m = mage_monster(1);
    EncounterState s;
    s.combatants = {m, target(2)};
    CHECK_FALSE(has_innate_state(s));
    s.combatants[0].innate = {per_day(kFireBolt, 2)};
    CHECK(has_innate_state(s));
    const uint64_t d0 = innate_digest(s);
    s.combatants[0].innate[0].uses_left = 1;
    const uint64_t d1 = innate_digest(s);
    CHECK(d1 != d0);
    s.combatants[0].innate[0].has_ability = true;
    CHECK(innate_digest(s) != d1);
    s.combatants[0].innate.clear();
    s.combatants[0].spell_dc_override = 15;
    CHECK(has_innate_state(s));
    CHECK(innate_digest(s) != d0);
}

TEST_CASE("slot casting is unchanged for a creature that also has innate spells") {
    auto w = wizard(1);
    w.innate = {at_will(kFireBolt)};
    auto s = begin_encounter({w, monster(2)}, 3, nullptr, spell_hooks());
    end_turns_until(s, 1);
    const auto slots = find(s, 1)->spell_slots;
    REQUIRE(step(s, cast(1, kMagicMissile, 2, 1)).ok);
    CHECK(find(s, 1)->spell_slots[1] == slots[1] - 1);
}
