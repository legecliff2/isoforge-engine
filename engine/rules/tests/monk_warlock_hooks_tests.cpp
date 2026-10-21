// VERSION_BUMP_BATCH 7.2zb: monk / warlock hooks (T&C #207 / #210). Costed bonus-action grants (Patient Defense, Step of the
// Wind), the AttackFromFeature predicate with a pick_one HitRider trigger and the forced_move / apply_modifier riders (Open
// Hand Technique), Reaction::attack_kinds (Deflect Missiles), SpellMod (Agonizing / Repelling Blast), and replay determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kKi = 900;
const uint32_t kFlurryId = spell_id_of("test.monk#bonus#flurry_of_blows");
const uint32_t kProne = spell_id_of("test.monk#save_condition.0");
const uint32_t kPush = spell_id_of("test.monk#forced_move.1");
const uint32_t kNoReact = spell_id_of("test.monk#apply_modifier.2");
const uint32_t kOpenHand = spell_id_of("test.monk#open_hand");
const uint32_t kEb = spell_id_of("test.warlock#eldritch_blast");
const uint32_t kOther = spell_id_of("test.warlock#other_blast");

Combatant monk(CombatantId id) {
    auto c = pc(id, 200);
    c.abilities.score[std::size_t(Ability::Dex)] = 16;
    c.attacks = {Attack{"unarmed", 40, {1, 6, 3}, DamageType::Bludgeoning, 5, false, true},
                 Attack{"dart", 40, {1, 4, 3}, DamageType::Piercing, 20, true, true}};
    BonusAttack b; b.id = kFlurryId; b.attack_index = 0; b.count = 2; b.requires_attacks = 1; b.pool = kKi; b.pool_cost = 1;
    c.bonus_attacks = {b};
    return c;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500; m.ac = 1;
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false},
                 Attack{"sling", 40, {1, 4, 0}, DamageType::Bludgeoning, 30, true, false}};
    m.reaction_policy.fill(ReactionPolicy::Never);
    return m;
}
struct Seen {
    std::vector<std::vector<ReactionOption>> rider_opts;
    std::vector<std::vector<ReactionOption>> damaged_opts;
    std::vector<ForcedMoveQuery> pushes;
    int pick = 0;
};
EncounterHooks hooks(const std::shared_ptr<Seen>& seen) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef pr; pr.id = kProne; pr.content_id = "test.monk#save_condition.0"; pr.level = 0; pr.kind = SpellEffectKind::Save;
    pr.save_ability = Ability::Dex; pr.fixed_dc = 30; pr.on_save = OnSave::None; pr.applies_condition = true;
    pr.condition = Condition::Prone; pr.condition_untracked = true; pr.is_spell = false; pr.magical = false; pr.targets = 1;
    book->add(pr);
    SpellDef pu; pu.id = kPush; pu.content_id = "test.monk#forced_move.1"; pu.level = 0; pu.kind = SpellEffectKind::Save;
    pu.save_ability = Ability::Str; pu.fixed_dc = 30; pu.on_save = OnSave::None; pu.is_spell = false; pu.magical = false; pu.targets = 1;
    SpellEffect fm; fm.kind = SpellEffectKind::ForcedMove; fm.affected_only = true; fm.forced.mode = 1; fm.forced.feet = 15;
    pu.extra_effects = {fm};
    book->add(pu);
    SpellDef nr; nr.id = kNoReact; nr.content_id = "test.monk#apply_modifier.2"; nr.level = 0; nr.kind = SpellEffectKind::ApplyModifier;
    nr.is_spell = false; nr.magical = false; nr.targets = 1;
    nr.modifier.id = spell_id_of("test.monk#apply_modifier.2#modifier"); nr.modifier.stat = ModStat::NoReactions;
    nr.modifier.applies_to = ModAppliesTo::Target; nr.modifier.expiry = 3;
    book->add(nr);
    for (uint32_t id : {kEb, kOther}) {
        SpellDef eb; eb.id = id; eb.content_id = id == kEb ? "test.warlock#eldritch_blast" : "test.warlock#other_blast";
        eb.level = 0; eb.range_ft = 120; eb.kind = SpellEffectKind::SpellAttack; eb.dice = DiceExpr{1, 10, 0};
        eb.damage_type = DamageType::Force; eb.fixed_attack = 30; eb.beams = 2; eb.targets = 1;
        book->add(eb);
    }
    h.spells = book;
    h.decide_reaction = [seen](const EncounterState&, const ReactionWindow& w, const Combatant&, std::span<const ReactionOption> opts) {
        if (w.trigger == ReactionTrigger::HitRider) {
            seen->rider_opts.emplace_back(opts.begin(), opts.end());
            return ReactionDecision{w.seq, true, uint8_t(seen->pick)};
        }
        if (w.trigger == ReactionTrigger::Damaged) seen->damaged_opts.emplace_back(opts.begin(), opts.end());
        return ReactionDecision{w.seq, false, 0};
    };
    h.forced_move = [seen](const EncounterState&, const ForcedMoveQuery& q) {
        seen->pushes.push_back(q);
        ForcedMoveResult r; r.feet_moved = q.feet; return r;
    };
    return h;
}
Trigger open_hand() {
    Trigger t; t.id = kOpenHand; t.on = TriggerOn::Hit; t.choice = 1; t.pick_one = true;
    Predicate p; p.kind = PredicateKind::AttackFromFeature; p.mask = kFlurryId; t.when = {p};
    RiderEffect a; a.kind = RiderKind::SaveCondition; a.spell_id = kProne;
    RiderEffect b; b.kind = RiderKind::Effect; b.spell_id = kPush;
    RiderEffect c; c.kind = RiderKind::Effect; c.spell_id = kNoReact;
    t.effects = {a, b, c};
    return t;
}
EncounterState start(std::vector<Combatant> cs, const std::shared_ptr<Seen>& seen, uint64_t seed = 3) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(seen));
    end_turns_until(s, 1);
    return s;
}
ActionRequest atk(uint8_t idx = 0) { return ActionRequest{1, ActionKind::Attack, 2, idx, 0, false, true}; }
ActionRequest flurry() { ActionRequest r{1, ActionKind::Attack, 2, 0, 0, true, true}; r.bonus_option = 1; return r; }
ActionRequest bonus(ActionKind k) { ActionRequest r{1, k}; r.as_bonus_action = true; return r; }
int n_of(const std::vector<Event>& ev, EventKind k) { return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.kind == k; })); }
bool hit_of(const StepResult& r) {
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) return (e.c & 1) != 0;
    return false;
}
bool no_reactions_on(const EncounterState& s, CombatantId id) {
    return std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& m) { return m.target == id && m.spec.stat == ModStat::NoReactions; });
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}

}  // namespace

TEST_CASE("7.2zb costed bonus-action grant: Patient Defense spends 1 ki, refused without it; the action Dodge stays free") {
    auto seen = std::make_shared<Seen>();
    auto c = monk(1);
    c.bonus_action_grants = bit(ActionKind::Dodge) | bit(ActionKind::Dash) | bit(ActionKind::Disengage);
    c.bonus_action_costs = {BonusActionCost{ActionKind::Dodge, kKi, 1}, BonusActionCost{ActionKind::Dash, kKi, 1},
                            BonusActionCost{ActionKind::Disengage, kKi, 1}};
    auto s = start({c, dummy(2)}, seen);
    register_pool(s, 1, kKi, 2, 1);
    auto r = step(s, bonus(ActionKind::Dodge));
    REQUIRE(r.ok);
    CHECK(find_pool(s, 1, kKi)->cur == 0);
    REQUIRE(n_of(r.events, EventKind::ResourceSpent) == 1);
    for (const auto& e : r.events) if (e.kind == EventKind::ResourceSpent) { CHECK(e.a == int32_t(kKi)); CHECK(e.b == 1); CHECK(e.c == 0); }
    CHECK(s.combatants[0].dodging);
    // next turn: no ki left: a bonus Dash / Disengage is refused, the action Dash is free
    step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1);
    CHECK_FALSE(step(s, bonus(ActionKind::Dash)).ok);
    CHECK_FALSE(step(s, bonus(ActionKind::Disengage)).ok);
    CHECK(s.budget.bonus_action);
    auto d = step(s, ActionRequest{1, ActionKind::Dash});
    REQUIRE(d.ok);
    CHECK(n_of(d.events, EventKind::ResourceSpent) == 0);
}

TEST_CASE("7.2zb costed grant: Step of the Wind Dash / Disengage; a free grant (no cost entry) spends nothing") {
    auto seen = std::make_shared<Seen>();
    auto c = monk(1);
    c.bonus_action_grants = bit(ActionKind::Dash) | bit(ActionKind::Disengage);
    c.bonus_action_costs = {BonusActionCost{ActionKind::Dash, kKi, 2}};
    auto s = start({c, dummy(2)}, seen);
    register_pool(s, 1, kKi, 5, 5);
    REQUIRE(step(s, bonus(ActionKind::Dash)).ok);
    CHECK(find_pool(s, 1, kKi)->cur == 3);
    CHECK_FALSE(step(s, bonus(ActionKind::Disengage)).ok);   // one bonus action
    CHECK(find_pool(s, 1, kKi)->cur == 3);
    step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1);
    auto r = step(s, bonus(ActionKind::Disengage));   // no cost entry: free (Cunning Action)
    REQUIRE(r.ok);
    CHECK(n_of(r.events, EventKind::ResourceSpent) == 0);
    CHECK(find_pool(s, 1, kKi)->cur == 3);
}

TEST_CASE("7.2zb AttackFromFeature: Open Hand fires only on Flurry of Blows hits, one option per effect") {
    auto seen = std::make_shared<Seen>();
    auto c = monk(1); c.triggers = {open_hand()};
    auto s = start({c, dummy(2)}, seen);
    register_pool(s, 1, kKi, 3, 3);
    auto a = step(s, atk());
    REQUIRE(a.ok);
    REQUIRE(hit_of(a));
    CHECK(seen->rider_opts.empty());   // the Attack action's hit is not from the feature
    seen->pick = 2;
    auto f = step(s, flurry());
    REQUIRE(f.ok);
    REQUIRE(hit_of(f));
    REQUIRE(seen->rider_opts.size() == 1);
    REQUIRE(seen->rider_opts[0].size() == 3);
    for (uint8_t i = 0; i < 3; ++i) { CHECK(seen->rider_opts[0][i].slot_level == i + 1); CHECK(seen->rider_opts[0][i].kind == ReactionOptionKind::Rider); }
    CHECK(no_reactions_on(s, 2));   // option 3: no reactions
    CHECK_FALSE(s.combatants[1].has(Condition::Prone));
    CHECK(seen->pushes.empty());
}

TEST_CASE("7.2zb Open Hand options: prone save, 15 ft push on a failed Str save") {
    for (int pick : {0, 1}) {
        auto seen = std::make_shared<Seen>();
        seen->pick = pick;
        auto c = monk(1); c.triggers = {open_hand()};
        auto s = start({c, dummy(2)}, seen);
        register_pool(s, 1, kKi, 3, 3);
        REQUIRE(step(s, atk()).ok);
        auto f = step(s, flurry());
        REQUIRE(f.ok);
        REQUIRE(hit_of(f));
        CHECK_FALSE(no_reactions_on(s, 2));
        if (pick == 0) { CHECK(s.combatants[1].has(Condition::Prone)); CHECK(seen->pushes.empty()); }
        else {
            CHECK_FALSE(s.combatants[1].has(Condition::Prone));
            REQUIRE(seen->pushes.size() == 1);
            CHECK(seen->pushes[0].mover == 2); CHECK(seen->pushes[0].feet == 15); CHECK(seen->pushes[0].mode == 1);
        }
    }
}

TEST_CASE("7.2zb Reaction::attack_kinds: Deflect Missiles is offered for a ranged weapon hit only") {
    for (uint8_t idx : {uint8_t(0), uint8_t(1)}) {
        auto seen = std::make_shared<Seen>();
        auto c = monk(1);
        Reaction dm; dm.id = 55; dm.window = ReactionTrigger::Damaged; dm.kind = ReactionEffectKind::ReduceDamage;
        dm.dice = DiceExpr{1, 10, 9}; dm.attack_kinds = kAttackKindRangedWeapon;
        c.reactions = {dm};
        auto m = dummy(2);
        auto s = begin_encounter({c, m}, 5, nullptr, hooks(seen));
        end_turns_until(s, 2);
        auto r = step(s, ActionRequest{2, ActionKind::Attack, 1, idx, 0, false, idx == 0});
        REQUIRE(r.ok);
        if (dealt_to(r.events, 1) == 0 && seen->damaged_opts.empty()) continue;
        bool offered = false;
        for (const auto& v : seen->damaged_opts) for (const auto& o : v) offered = offered || o.option_id == 55;
        CHECK(offered == (idx == 1));
    }
}

TEST_CASE("7.2zb SpellMod: +Cha per beam and a push per hit beam of that spell only") {
    for (uint32_t spell : {kEb, kOther}) {
        int with = 0, without = 0;
        std::size_t pushes = 0;
        for (int mod = 0; mod < 2; ++mod) {
            auto seen = std::make_shared<Seen>();
            auto w = wizard(1);
            w.abilities.score[std::size_t(Ability::Cha)] = 18;
            w.spells_known = {kEb, kOther};
            if (mod) w.spell_mods = {SpellMod{kEb, uint8_t(Ability::Cha), 10}};
            auto s = start({w, dummy(2)}, seen, 11);
            auto r = step(s, cast(1, spell, 2));
            REQUIRE(r.ok);
            (mod ? with : without) = dealt_to(r.events, 2);
            if (mod) pushes = seen->pushes.size();
            for (const auto& q : seen->pushes) { CHECK(q.feet == 10); CHECK(q.mode == 1); CHECK(q.mover == 2); }
        }
        if (spell == kEb) { CHECK(with == without + 8); CHECK(pushes == 2); }   // two beams hit (fixed_attack 30, AC 1)
        else { CHECK(with == without); CHECK(pushes == 0); }
    }
}

TEST_CASE("7.2zb replay: the same seed gives the same events (costs, pick_one, spell mods)") {
    auto run = [] {
        auto seen = std::make_shared<Seen>();
        seen->pick = 1;
        auto c = monk(1); c.triggers = {open_hand()};
        c.bonus_action_grants = bit(ActionKind::Dodge);
        c.bonus_action_costs = {BonusActionCost{ActionKind::Dodge, kKi, 1}};
        auto s = start({c, dummy(2)}, seen, 21);
        register_pool(s, 1, kKi, 4, 4);
        std::vector<Event> all;
        for (int t = 0; t < 3; ++t) {
            for (auto rq : {atk(), flurry(), flurry()}) { auto r = step(s, rq); all.insert(all.end(), r.events.begin(), r.events.end()); }
            step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1);
        }
        auto r = step(s, bonus(ActionKind::Dodge)); all.insert(all.end(), r.events.begin(), r.events.end());
        return std::make_pair(all, int(s.combatants[1].hp) * 1000 + int(find_pool(s, 1, kKi)->cur));
    };
    const auto a = run(), b = run();
    CHECK(a.second == b.second);
    REQUIRE(a.first.size() == b.first.size());
    for (std::size_t i = 0; i < a.first.size(); ++i) { CHECK(a.first[i].kind == b.first[i].kind); CHECK(a.first[i].a == b.first[i].a); }
}
