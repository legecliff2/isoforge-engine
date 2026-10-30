// B11: Water Elemental Whelm. Each named creature (the ones in the elemental's space; rules has no
// tiles) makes one Strength save. Fail: the bludgeoning packet, and, when the target is within
// max_size and the cap has room, a restraining grapple (escape DC) plus the later crush.
// Success: no damage (on_save is None, not half) and a push out of the space.
// Unable to breathe, and an ally's Strength check to pull a creature out, are not this action.
// No new hash tag. water_elemental.json stays deferred.
#include <doctest/doctest.h>

#include <vector>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kWhelm = spell_id_of("test.water#whelm");

struct Seen {
    std::vector<ForcedMoveQuery> qs;
};

SpellDef whelm_def(int dc = 30) {
    SpellDef d;
    d.id = kWhelm; d.content_id = "test.water#whelm"; d.level = 0; d.targets = 8;
    d.kind = SpellEffectKind::Save; d.is_spell = false; d.magical = false; d.components = 0;
    d.save_ability = Ability::Str; d.fixed_dc = int8_t(dc); d.on_save = OnSave::None;
    d.dice = {0, 0, 13}; d.damage_type = DamageType::Bludgeoning;
    d.whelm = true; d.whelm_escape_dc = 14; d.whelm_max_size = int8_t(Size::Large);
    d.whelm_crush = {0, 0, 4}; d.whelm_crush_type = DamageType::Bludgeoning;
    d.usage_kind = 1; d.recharge_min = 4;
    return d;
}

EncounterHooks hooks_for(const SpellDef& def, Seen* seen) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    REQUIRE(b->add(def));
    h.spells = b;
    h.forced_move = [seen](const EncounterState&, const ForcedMoveQuery& q) {
        if (seen) seen->qs.push_back(q);
        ForcedMoveResult r;
        r.feet_moved = q.feet;
        return r;
    };
    return h;
}

Combatant elemental() {
    auto c = monster(1);
    c.size = Size::Large;
    c.hp = c.hp_max = 114; c.spells_known = {kWhelm};
    return c;
}

Combatant body(CombatantId id, Size size = Size::Medium, int hp = 80) {
    auto c = pc(id, hp);
    c.size = size;
    c.abilities.score[std::size_t(Ability::Str)] = 10;
    return c;
}

ActionRequest at(std::vector<CombatantId> ids) {
    ActionRequest r{1, ActionKind::CastSpell, ids.empty() ? CombatantId(0) : ids[0]};
    r.spell_id = kWhelm;
    r.targets = std::move(ids);
    return r;
}

EncounterState begin(std::vector<Combatant> cs, const SpellDef& def, Seen* seen, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks_for(def, seen));
    register_action_use(s, 1, kWhelm);
    end_turns_until(s, 1);
    return s;
}

int damage_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0;
    for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a;
    return n;
}

const SourcedCondition* hold(const EncounterState& s, CombatantId id) {
    for (const auto& e : s.sourced)
        if (e.condition == Condition::Grappled && e.target == id && e.source == 1) return &e;
    return nullptr;
}

int crush_rows(const EncounterState& s, CombatantId id) {
    int n = 0;
    for (const auto& e : s.effects)
        if (e.target == id && e.caster == 1 && e.damage_when == 3 && e.contain == 0 && e.spell_id == kWhelm) ++n;
    return n;
}

}  // namespace

TEST_CASE("Whelm: a failed Strength save deals the packet and grapples; a success takes nothing and is pushed") {
    Seen fail_seen;
    auto failed = begin({elemental(), body(2)}, whelm_def(30), &fail_seen);
    auto land = step(failed, at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 13);
    CHECK(fail_seen.qs.empty());   // a failure is not pushed
    const auto* g = hold(failed, 2);
    REQUIRE(g);
    CHECK(g->escape_dc == 14);
    CHECK(g->restrains);
    CHECK(g->via_attack == kWhelmGrapple);
    CHECK(g->owns_restrained);
    CHECK(find(failed, 2)->has(Condition::Grappled));
    CHECK(find(failed, 2)->has(Condition::Restrained));
    CHECK(crush_rows(failed, 2) == 1);
    CHECK(failed.effects[0].damage.bonus == 4);
    CHECK(failed.effects[0].magical == false);

    Seen ok_seen;
    auto saved = begin({elemental(), body(2)}, whelm_def(1), &ok_seen);
    auto out = step(saved, at({2}));
    REQUIRE(out.ok);
    CHECK(damage_to(out.events, 2) == 0);   // not half of 13
    CHECK_FALSE(find(saved, 2)->has(Condition::Grappled));
    CHECK_FALSE(find(saved, 2)->has(Condition::Restrained));
    CHECK(crush_rows(saved, 2) == 0);
    REQUIRE(ok_seen.qs.size() == 1);
    CHECK(ok_seen.qs[0].mode == 1);
    CHECK(ok_seen.qs[0].feet == kWhelmPushCapFt);
    CHECK(ok_seen.qs[0].clear_of_source);
    CHECK_FALSE(ok_seen.qs[0].prone_if_blocked);
    CHECK(ok_seen.qs[0].mover == 2);
    CHECK(ok_seen.qs[0].source == 1);
    CHECK(ok_seen.qs[0].dir_x == 0);
    CHECK(ok_seen.qs[0].dir_y == 0);
}

TEST_CASE("Whelm: Huge takes the damage and is not grappled; a Small user cannot grapple a Large target") {
    Seen seen;
    auto huge = begin({elemental(), body(2, Size::Huge)}, whelm_def(30), &seen);
    auto land = step(huge, at({2}));
    REQUIRE(land.ok);
    CHECK(damage_to(land.events, 2) == 13);
    CHECK(hold(huge, 2) == nullptr);
    CHECK_FALSE(find(huge, 2)->has(Condition::Restrained));
    CHECK(crush_rows(huge, 2) == 0);

    auto small = elemental();
    small.size = Size::Small;
    Seen mid;
    auto s = begin({small, body(2, Size::Large)}, whelm_def(30), &mid);
    auto miss = step(s, at({2}));
    REQUIRE(miss.ok);
    CHECK(damage_to(miss.events, 2) == 13);
    CHECK(hold(s, 2) == nullptr);
}

TEST_CASE("Whelm: one Large, or two Medium or smaller, in named-target order") {
    Seen seen;
    auto three = begin({elemental(), body(2), body(3), body(4)}, whelm_def(30), &seen);
    auto land = step(three, at({2, 3, 4}));
    REQUIRE(land.ok);
    CHECK(hold(three, 2) != nullptr);
    CHECK(hold(three, 3) != nullptr);
    CHECK(hold(three, 4) == nullptr);
    CHECK(damage_to(land.events, 4) == 13);
    CHECK(crush_rows(three, 2) == 1);
    CHECK(crush_rows(three, 4) == 0);

    Seen big;
    auto mixed = begin({elemental(), body(2, Size::Large), body(3)}, whelm_def(30), &big);
    REQUIRE(step(mixed, at({2, 3})).ok);
    CHECK(hold(mixed, 2) != nullptr);
    CHECK(hold(mixed, 3) == nullptr);

    Seen rev;
    auto other = begin({elemental(), body(2), body(3, Size::Large)}, whelm_def(30), &rev);
    REQUIRE(step(other, at({2, 3})).ok);
    CHECK(hold(other, 2) != nullptr);
    CHECK(hold(other, 3) == nullptr);
}

TEST_CASE("Whelm: the crush lands on the user's next turn, and escaping ends the grapple and the crush") {
    Seen seen;
    auto s = begin({elemental(), body(2)}, whelm_def(30), &seen);
    REQUIRE(step(s, at({2})).ok);
    const int16_t hp = find(s, 2)->hp;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(find(s, 2)->hp == int16_t(hp - 4));
    CHECK(hold(s, 2) != nullptr);

    Seen again;
    auto esc = begin({elemental(), body(2)}, whelm_def(30), &again);
    REQUIRE(step(esc, at({2})).ok);
    mut(esc, 2).abilities.score[std::size_t(Ability::Str)] = 36;
    mut(esc, 2).abilities.score[std::size_t(Ability::Dex)] = 36;
    end_turns_until(esc, 2);
    ActionRequest free{2, ActionKind::Attack};
    free.special = 4;
    REQUIRE(step(esc, free).ok);
    CHECK(hold(esc, 2) == nullptr);
    CHECK_FALSE(find(esc, 2)->has(Condition::Grappled));
    CHECK_FALSE(find(esc, 2)->has(Condition::Restrained));
    CHECK(crush_rows(esc, 2) == 0);
}

TEST_CASE("Whelm: a second failure on someone already held does not add a second crush") {
    Seen seen;
    auto s = begin({elemental(), body(2)}, whelm_def(30), &seen);
    REQUIRE(step(s, at({2})).ok);
    CHECK(crush_rows(s, 2) == 1);
    for (auto& u : s.action_uses) if (u.actor == 1 && u.spell_id == kWhelm) u.expended = false;
    s.budget.action = true;
    REQUIRE(step(s, at({2})).ok);
    CHECK(crush_rows(s, 2) == 1);
    CHECK(hold(s, 2) != nullptr);
}

TEST_CASE("Whelm: immunities, an ally, an empty space, and a bad target") {
    Seen plain;
    auto immune = begin({elemental(), body(2)}, whelm_def(30), &plain);
    mut(immune, 2).immunities = bit(DamageType::Bludgeoning);
    mut(immune, 2).condition_immunities = bit(Condition::Restrained);
    auto no = step(immune, at({2}));
    REQUIRE(no.ok);
    CHECK(damage_to(no.events, 2) == 0);
    CHECK(hold(immune, 2) != nullptr);
    CHECK_FALSE(find(immune, 2)->has(Condition::Restrained));
    CHECK_FALSE(hold(immune, 2)->owns_restrained);
    CHECK(crush_rows(immune, 2) == 1);

    Seen grab;
    auto nograp = begin({elemental(), body(2)}, whelm_def(30), &grab);
    mut(nograp, 2).condition_immunities = bit(Condition::Grappled);
    auto skip = step(nograp, at({2}));
    REQUIRE(skip.ok);
    CHECK(damage_to(skip.events, 2) == 13);
    CHECK(hold(nograp, 2) == nullptr);
    CHECK(crush_rows(nograp, 2) == 0);

    auto ally_c = body(3);
    ally_c.team = 1;
    Seen ally_seen;
    auto ally = begin({elemental(), body(2), ally_c}, whelm_def(30), &ally_seen);
    bool listed = false;
    for (const auto& q : legal_actions(ally))
        if (q.kind == ActionKind::CastSpell && q.spell_id == kWhelm && q.target == 3) listed = true;
    CHECK_FALSE(listed);
    auto hit = step(ally, at({3}));
    REQUIRE(hit.ok);
    CHECK(hold(ally, 3) != nullptr);

    Seen empty_seen;
    auto empty = begin({elemental(), body(2)}, whelm_def(30), &empty_seen);
    empty.hooks.spell_target_ok = [](const EncounterState&, const Combatant&, const SpellDef&, const Combatant&) { return false; };
    int offered = 0;
    for (const auto& q : legal_actions(empty))
        if (q.kind == ActionKind::CastSpell && q.spell_id == kWhelm) { CHECK(q.target == 0); ++offered; }
    CHECK(offered == 1);
    auto air = step(empty, at({}));
    REQUIRE(air.ok);
    CHECK(count(air.events, EventKind::SavingThrowRolled) == 0);
    CHECK(count(air.events, EventKind::DamageDealt) == 0);
    CHECK(empty_seen.qs.empty());
    CHECK(find_action_use(empty, 1, kWhelm)->expended);
    CHECK_FALSE(empty.budget.action);

    Seen self_seen;
    auto self_s = begin({elemental(), body(2)}, whelm_def(30), &self_seen);
    auto self = step(self_s, at({1}));
    CHECK_FALSE(self.ok);
    CHECK(self.error == std::string("bad target"));
    CHECK(self_s.budget.action);
    CHECK_FALSE(find_action_use(self_s, 1, kWhelm)->expended);
}
