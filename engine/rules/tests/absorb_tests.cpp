// Lightning Absorption (CONTENT_CONVENTIONS §43): a listed type is not taken. The creature regains hit points
// equal to that component's amount (the number passed in, before immunity, resistance, and vulnerability).
// The regain is heal_hp / Healed. No new hash tag.
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
Combatant mound(CombatantId id) {
    auto c = monster(id);
    c.hp = c.hp_max = 136;
    c.absorb = bit(DamageType::Lightning);
    c.immunities = bit(DamageType::Lightning);   // the printed stat block; the trait still names a regain
    return c;
}
void arm(EncounterState& s, DamageType type, int16_t bonus) {
    auto& at = mut(s, 1).attacks[0];
    at.to_hit = 100;
    at.type = type;
    at.damage = {0, 0, bonus};
    at.extra_damage.clear();
}
void add_part(EncounterState& s, DamageType type, int16_t bonus) {
    DamagePart p;
    p.dice = {0, 0, bonus};
    p.type = type;
    mut(s, 1).attacks[0].extra_damage.push_back(p);
}
StepResult strike(EncounterState& s) {
    end_turns_until(s, 1);
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    return r;
}
const Event* healed(const std::vector<Event>& ev) {
    const Event* h = nullptr;
    for (const auto& e : ev)
        if (e.kind == EventKind::Healed && e.target == 2) h = &e;
    return h;
}
int dealt_to(const std::vector<Event>& ev) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == EventKind::DamageDealt && e.target == 2) n = e.a;
    return n;
}
}  // namespace

TEST_CASE("absorb: the source amount is kept, and immunity, resistance, and vulnerability do not change it") {
    Combatant c = mound(2);
    c.resistances = bit(DamageType::Lightning);
    c.vulnerabilities = bit(DamageType::Lightning);
    auto d = apply_damage(c, 13, DamageType::Lightning);
    CHECK(d.dealt == 0);
    CHECK(d.types_dealt == 0u);
    CHECK(d.absorbed_heal == 13);
    CHECK(d.hp_after == 136);
    CHECK(d.temp_hp_after == 0);
    CHECK(apply_damage(c, 0, DamageType::Lightning).absorbed_heal == 0);
    CHECK(apply_damage(c, -4, DamageType::Lightning).absorbed_heal == 0);

    Combatant plain = monster(3);
    plain.hp = plain.hp_max = 20;
    auto n = apply_damage(plain, 9, DamageType::Lightning);
    CHECK(n.dealt == 9);
    CHECK(n.absorbed_heal == 0);
    CHECK(n.types_dealt == bit(DamageType::Lightning));
}

TEST_CASE("absorb: a mixed hit keeps the other type and sums every absorbed component") {
    Combatant c = mound(2);
    c.resistances |= bit(DamageType::Fire);   // the printed cold/fire resistance; fire is not absorbed
    DamagePiece parts[] = {{10, DamageType::Fire}, {3, DamageType::Lightning}, {5, DamageType::Lightning}};
    auto d = apply_damage_parts(c, parts);
    CHECK(d.dealt == 5);   // fire 10 / 2
    CHECK(d.types_dealt == bit(DamageType::Fire));
    CHECK(d.absorbed_heal == 8);
    auto z = reduce_applied(c, d, 5);
    CHECK(z.dealt == 0);
    CHECK(z.types_dealt == 0u);
    CHECK(z.absorbed_heal == 8);   // a reduce_damage reaction does not eat the regain
}

TEST_CASE("absorb: lightning heals, does not spend temporary hit points, and does not break concentration") {
    auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
    mut(s, 2).hp = 100;
    mut(s, 2).temp_hp = 7;
    mut(s, 2).concentration_spell = 7100;
    arm(s, DamageType::Lightning, 13);
    auto hit = strike(s);
    CHECK(mut(s, 2).hp == 113);
    CHECK(mut(s, 2).temp_hp == 7);
    CHECK(mut(s, 2).concentration_spell == 7100);
    CHECK(dealt_to(hit.events) == 0);
    CHECK(count(hit.events, EventKind::ConcentrationEnded) == 0);
    const Event* h = healed(hit.events);
    REQUIRE(h);
    CHECK(h->a == 13);
    CHECK(h->b == 113);
    CHECK(h->c == 0);
}

TEST_CASE("absorb: slashing still harms, and a listed type at the maximum regains nothing") {
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        arm(s, DamageType::Slashing, 6);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 130);
        CHECK(healed(hit.events) == nullptr);
        CHECK(dealt_to(hit.events) == 6);
    }
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        arm(s, DamageType::Lightning, 6);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 136);
        const Event* h = healed(hit.events);
        REQUIRE(h);
        CHECK(h->a == 0);
    }
}

TEST_CASE("absorb: fire in the same hit is taken, then the lightning is regained if the creature lives") {
    auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
    mut(s, 2).hp = 40;
    mut(s, 2).temp_hp = 3;
    mut(s, 2).resistances |= bit(DamageType::Fire);
    arm(s, DamageType::Fire, 10);   // 5 after resistance; temp 3, then 2 off hit points
    add_part(s, DamageType::Lightning, 8);
    auto hit = strike(s);
    CHECK(dealt_to(hit.events) == 5);
    CHECK(mut(s, 2).temp_hp == 0);
    CHECK(mut(s, 2).hp == 46);   // 40 - 2 + 8
    const Event* h = healed(hit.events);
    REQUIRE(h);
    CHECK(h->a == 8);
}

TEST_CASE("absorb: other damage that kills is not undone by the regain") {
    auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
    mut(s, 2).hp = 4;
    arm(s, DamageType::Slashing, 10);
    add_part(s, DamageType::Lightning, 8);
    auto hit = strike(s);
    CHECK(mut(s, 2).dead);
    CHECK(mut(s, 2).hp == 0);
    CHECK(healed(hit.events) == nullptr);
}

TEST_CASE("absorb: immunity without the trait heals nothing; resistance and vulnerability do not scale the regain") {
    {
        auto s = begin_encounter({pc(1, 500), monster(2)}, 5);
        mut(s, 2).hp = mut(s, 2).hp_max = 40;
        mut(s, 2).immunities = bit(DamageType::Lightning);
        arm(s, DamageType::Lightning, 13);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 40);
        CHECK(healed(hit.events) == nullptr);
    }
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        mut(s, 2).hp = 100;
        mut(s, 2).resistances |= bit(DamageType::Lightning);
        arm(s, DamageType::Lightning, 10);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 110);   // not 105
        CHECK(healed(hit.events)->a == 10);
    }
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        mut(s, 2).hp = 100;
        mut(s, 2).vulnerabilities |= bit(DamageType::Lightning);
        arm(s, DamageType::Lightning, 10);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 110);   // not 120
        CHECK(healed(hit.events)->a == 10);
    }
}

TEST_CASE("absorb: NoHealing and no_regain_hp block the regain and the lightning is still not taken") {
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        mut(s, 2).hp = 100;
        ModifierSpec m;
        m.id = 9500;
        m.source = 9500;
        m.stat = ModStat::NoHealing;
        std::vector<Event> sink;
        REQUIRE(apply_modifier(s, 2, 2, m, sink));
        arm(s, DamageType::Lightning, 6);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 100);
        const Event* h = healed(hit.events);
        REQUIRE(h);
        CHECK(h->a == 0);
        CHECK(dealt_to(hit.events) == 0);
    }
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        mut(s, 2).hp = 100;
        mut(s, 2).no_regain_hp = true;
        arm(s, DamageType::Lightning, 6);
        auto hit = strike(s);
        CHECK(mut(s, 2).hp == 100);
        REQUIRE(healed(hit.events));
        CHECK(healed(hit.events)->a == 0);
    }
}

TEST_CASE("absorb: a creature at 0 hit points regains them, and the type does not suppress regeneration or freeze") {
    {
        auto s = begin_encounter({pc(1, 500), mound(2), monster(3)}, 5);
        make_dying(mut(s, 2));
        mut(s, 2).death.failures = 2;
        arm(s, DamageType::Lightning, 8);
        auto hit = strike(s);
        CHECK_FALSE(mut(s, 2).dead);
        CHECK(mut(s, 2).hp == 8);
        CHECK_FALSE(mut(s, 2).has(Condition::Unconscious));
        CHECK(mut(s, 2).has(Condition::Prone));   // commit_healing clears Unconscious only
        CHECK(mut(s, 2).death.failures == 0);
        const Event* h = healed(hit.events);
        REQUIRE(h);
        CHECK(h->a == 8);
        CHECK(h->c == 1);
        CHECK(mut(s, 2).concentration_spell == 0);
    }
    {
        auto s = begin_encounter({pc(1, 500), mound(2)}, 5);
        mut(s, 2).hp = 100;
        mut(s, 2).regeneration = {10, bit(DamageType::Lightning)};
        mut(s, 2).freeze = {bit(DamageType::Lightning), 20, spell_id_of("test.absorb#freeze")};
        arm(s, DamageType::Lightning, 6);
        strike(s);
        CHECK_FALSE(mut(s, 2).regen_suppressed);
        CHECK(mut(s, 2).hp == 106);
        CHECK(s.modifiers.empty());   // freeze keys off types_dealt; lightning was not taken
    }
}
