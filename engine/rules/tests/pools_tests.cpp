// Row 13: pool-backed features (FeatureSpec: pool spend, cost kinds, grant_action, heal_pool) and Legendary Resistance.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBlast = spell_id_of("test.spell#lr_blast");
const uint32_t kRagePool = 700, kHandsPool = 701, kLrPool = 702;
const uint32_t kRage = 710, kSurge = 711, kHands = 712, kPlain = 713, kReact = 714, kBonusFree = 715;

SpellDef blast_def() {
    SpellDef d; d.id = kBlast; d.content_id = "test.spell#lr_blast"; d.level = 0; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.fixed_dc = 100; d.on_save = OnSave::None;
    d.dice = DiceExpr{1, 6, 0}; d.damage_type = DamageType::Fire;
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(blast_def());
    h.spells = book;
    h.feature_spec = [](const EncounterState&, const Combatant&, uint32_t f) -> std::optional<FeatureSpec> {
        FeatureSpec fs;
        if (f == kRage) { fs.pool = kRagePool; fs.cost = 2; return fs; }
        if (f == kSurge) { fs.cost = 4; fs.grant_actions = 1; return fs; }
        if (f == kHands) { fs.pool = kHandsPool; fs.heal_pool = true; return fs; }
        if (f == kReact) { fs.pool = kRagePool; fs.pool_cost = 2; fs.cost = 3; return fs; }
        if (f == kBonusFree) { fs.cost = 2; return fs; }
        return std::nullopt;
    };
    return h;
}
Combatant striker(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"dagger", 40, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    return c;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500; m.ac = 10;
    return m;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest use(CombatantId a, uint32_t f, bool bonus = false) {
    ActionRequest r{a, ActionKind::UseFeature};
    r.feature_id = f; r.as_bonus_action = bonus;
    return r;
}
ActionRequest stab(CombatantId a, CombatantId t) { return ActionRequest{a, ActionKind::Attack, t, 0, 0, false, true}; }
const Event* find_ev(const std::vector<Event>& ev, EventKind k, int nth = 0) {
    for (const auto& e : ev) if (e.kind == k && nth-- == 0) return &e;
    return nullptr;
}
}  // namespace

TEST_CASE("defaults") {
    TurnBudget b;
    CHECK(b.extra_actions == 0);
    Combatant c{};
    CHECK(c.legendary_pool == 0);
    FeatureSpec fs;
    CHECK(fs.pool == 0); CHECK(fs.cost == 0); CHECK_FALSE(fs.heal_pool);
}

TEST_CASE("a feature without a spec behaves as before") {
    auto s = start({striker(1), dummy(2)});
    auto r = step(s, use(1, kPlain));
    REQUIRE(r.ok);
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.bonus_action);
    CHECK(count(r.events, EventKind::ResourceSpent) == 0);
}

TEST_CASE("pool-backed bonus-action feature: spends, emits ResourceSpent, refuses when short") {
    auto s = start({striker(1), dummy(2)});
    register_pool(s, 1, kRagePool, 2, 2);
    auto r = step(s, use(1, kRage));
    REQUIRE(r.ok);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);
    const Event* e = find_ev(r.events, EventKind::ResourceSpent);
    REQUIRE(e);
    CHECK(e->actor == 1); CHECK(e->a == int32_t(kRagePool)); CHECK(e->b == 1); CHECK(e->c == 1);
    CHECK(find_pool(s, 1, kRagePool)->cur == 1);
    // next turn: spend the last one, then refuse
    end_turns_until(s, 2); end_turns_until(s, 1);
    REQUIRE(step(s, use(1, kRage)).ok);
    CHECK(find_pool(s, 1, kRagePool)->cur == 0);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto bad = step(s, use(1, kRage));
    CHECK_FALSE(bad.ok);
    CHECK(s.budget.bonus_action);
    CHECK(find_pool(s, 1, kRagePool)->cur == 0);
}

TEST_CASE("a pool the actor lacks refuses the feature without changing state") {
    auto s = start({striker(1), dummy(2)});
    auto r = step(s, use(1, kRage));
    CHECK_FALSE(r.ok);
    CHECK(s.budget.bonus_action);
}

TEST_CASE("cost kinds: reaction and free") {
    auto s = start({striker(1), dummy(2)});
    register_pool(s, 1, kRagePool, 5, 5);
    auto r = step(s, use(1, kReact));
    REQUIRE(r.ok);
    CHECK_FALSE(s.budget.reaction);
    CHECK(s.budget.action); CHECK(s.budget.bonus_action);
    CHECK(find_ev(r.events, EventKind::ResourceSpent)->b == 2);
    CHECK(find_pool(s, 1, kRagePool)->cur == 3);
    CHECK_FALSE(step(s, use(1, kReact)).ok);   // reaction already used
    CHECK(find_pool(s, 1, kRagePool)->cur == 3);
    auto f = step(s, use(1, kSurge));
    REQUIRE(f.ok);
    CHECK(s.budget.action);
}

TEST_CASE("grant_action: Action Surge gives one extra action this turn only") {
    auto s = start({striker(1), dummy(2)});
    auto first = step(s, stab(1, 2));
    REQUIRE(first.ok);
    CHECK_FALSE(s.budget.action);
    CHECK_FALSE(step(s, stab(1, 2)).ok);
    REQUIRE(step(s, use(1, kSurge)).ok);
    CHECK(s.budget.extra_actions == 1);
    bool has_attack = false;
    for (const auto& r : legal_actions(s)) has_attack = has_attack || r.kind == ActionKind::Attack;
    CHECK(has_attack);
    auto second = step(s, stab(1, 2));
    REQUIRE(second.ok);
    CHECK(s.budget.extra_actions == 0);
    CHECK_FALSE(step(s, stab(1, 2)).ok);
    // an unused surge does not carry over
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK(s.budget.extra_actions == 0);
    REQUIRE(step(s, use(1, kSurge)).ok);
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK(s.budget.extra_actions == 0);
}

TEST_CASE("heal_pool: Lay on Hands spends the amount and heals") {
    auto ally = striker(3);
    ally.hp = 10;
    auto s = start({striker(1), dummy(2), ally});
    register_pool(s, 1, kHandsPool, 25, 25);
    ActionRequest r = use(1, kHands);
    r.target = 3; r.amount = 7;
    auto out = step(s, r);
    REQUIRE(out.ok);
    CHECK(s.combatants[2].hp == 17);
    CHECK(find_pool(s, 1, kHandsPool)->cur == 18);
    const Event* sp = find_ev(out.events, EventKind::ResourceSpent);
    REQUIRE(sp);
    CHECK(sp->b == 7); CHECK(sp->c == 18);
    const Event* h = find_ev(out.events, EventKind::Healed);
    REQUIRE(h);
    CHECK(h->actor == 1); CHECK(h->target == 3); CHECK(h->a == 7);
    // too much, zero, and a missing target are refused
    end_turns_until(s, 2); end_turns_until(s, 1);
    ActionRequest big = use(1, kHands); big.target = 3; big.amount = 19;
    CHECK_FALSE(step(s, big).ok);
    ActionRequest zero = use(1, kHands); zero.target = 3;
    CHECK_FALSE(step(s, zero).ok);
    ActionRequest nobody = use(1, kHands); nobody.target = 99; nobody.amount = 1;
    CHECK_FALSE(step(s, nobody).ok);
    CHECK(find_pool(s, 1, kHandsPool)->cur == 18);
    ActionRequest self = use(1, kHands); self.amount = 4;
    REQUIRE(step(s, self).ok);
    CHECK(find_pool(s, 1, kHandsPool)->cur == 14);
}

TEST_CASE("Legendary Resistance: failed saves succeed until the pool runs dry") {
    auto caster = wizard(1);
    caster.spells_known.push_back(kBlast);
    auto boss = dummy(2);
    boss.legendary_pool = kLrPool;
    auto s = start({caster, boss});
    register_pool(s, 2, kLrPool, 1, 1);
    auto r = step(s, cast(1, kBlast, 2));
    REQUIRE(r.ok);
    const Event* spent = find_ev(r.events, EventKind::ResourceSpent);
    REQUIRE(spent);
    CHECK(spent->actor == 2); CHECK(spent->a == int32_t(kLrPool)); CHECK(spent->b == 1); CHECK(spent->c == 0);
    const Event* sv = find_ev(r.events, EventKind::SavingThrowRolled);
    REQUIRE(sv);
    CHECK((sv->c & 1) == 1);
    CHECK(s.combatants[1].hp == 500);
    CHECK(find_pool(s, 2, kLrPool)->cur == 0);
    // pool empty: the next failed save stands
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto r2 = step(s, cast(1, kBlast, 2));
    REQUIRE(r2.ok);
    CHECK(count(r2.events, EventKind::ResourceSpent) == 0);
    CHECK((find_ev(r2.events, EventKind::SavingThrowRolled)->c & 1) == 0);
    CHECK(s.combatants[1].hp < 500);
}

TEST_CASE("Legendary Resistance does nothing without a pool or when the save already passes") {
    auto caster = wizard(1);
    caster.spells_known.push_back(kBlast);
    auto boss = dummy(2);
    boss.legendary_pool = kLrPool;
    auto s = start({caster, boss});   // pool not registered
    auto r = step(s, cast(1, kBlast, 2));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ResourceSpent) == 0);
    CHECK(s.combatants[1].hp < 500);
}

TEST_CASE("pools digest tracks feature spends") {
    auto s = start({striker(1), dummy(2)});
    register_pool(s, 1, kRagePool, 3, 3);
    const uint64_t before = pools_digest(s);
    REQUIRE(step(s, use(1, kRage)).ok);
    CHECK(pools_digest(s) != before);
}
