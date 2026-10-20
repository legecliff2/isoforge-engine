// VERSION_BUMP_BATCH 7.2x: vision. Sim answers EncounterHooks::sight (pure, from the start-of-step state); rules apply
// unseen attackers / targets, the dim-light / light-obscurement Perception penalty, Hide (Stealth vs passive Perception,
// only while unseen; attacking reveals), the guessed tile against a hidden target, and the gaze's sight requirement.
// No hook = everyone sees everyone (goldens unchanged).
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <utility>
#include "spell_util.hpp"

using namespace enc;

namespace {

using SightMap = std::map<std::pair<CombatantId, CombatantId>, Sight>;   // (viewer, subject) -> answer; missing = Sight{}

std::function<Sight(const EncounterState&, const Combatant&, const Combatant&)> sight_hook(std::shared_ptr<SightMap> m) {
    return [m](const EncounterState&, const Combatant& v, const Combatant& t) {
        auto it = m->find({v.id, t.id});
        return it == m->end() ? Sight{} : it->second;
    };
}
Sight unseen() { Sight v; v.can_see = false; v.sense = Sense::None; return v; }

EncounterState fresh(uint64_t seed, std::shared_ptr<SightMap> m, CombatantId who = 1) {
    auto s = begin_encounter({pc(1, 200), monster(2)}, seed);
    if (m) s.hooks.sight = sight_hook(m);
    end_turns_until(s, who);
    return s;
}
ActionRequest attack(CombatantId a, CombatantId t) { ActionRequest r{a, ActionKind::Attack, t}; r.target_within_5ft = true; return r; }
const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}
bool same(const StepResult& a, const StepResult& b) {
    if (a.ok != b.ok || a.events.size() != b.events.size()) return false;
    for (std::size_t i = 0; i < a.events.size(); ++i)
        if (a.events[i].kind != b.events[i].kind || a.events[i].a != b.events[i].a || a.events[i].b != b.events[i].b ||
            a.events[i].c != b.events[i].c) return false;
    return true;
}
}  // namespace

TEST_CASE("vision: defaults; no hook means everyone sees everyone (same events as an all-default answer)") {
    Sight v;
    CHECK(v.can_see); CHECK(v.perceives); CHECK(v.sense == Sense::Sight);
    CHECK(v.light_at_subject == Light::Bright); CHECK(v.light_at_viewer == Light::Bright); CHECK(v.obscured == Obscured::None);
    ActionRequest r; CHECK_FALSE(r.has_guess);
    auto s0 = begin_encounter({pc(1), monster(2)}, 1);
    CHECK(sight(s0, 1, 2) == Sight{});
    CHECK_FALSE(has_hidden_state(s0));
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto a = fresh(seed, nullptr); auto b = fresh(seed, std::make_shared<SightMap>());
        CHECK(same(step(a, attack(1, 2)), step(b, attack(1, 2))));
    }
}

TEST_CASE("vision: an unseen attacker has advantage (as a hidden one), an unseen target gives disadvantage (as blinded)") {
    auto m = std::make_shared<SightMap>(SightMap{{{2, 1}, unseen()}});   // the monster can't see the pc
    auto n = std::make_shared<SightMap>(SightMap{{{1, 2}, unseen()}});   // the pc can't see the monster
    int differ = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto a = fresh(seed, m);
        auto ref = fresh(seed, nullptr); mut(ref, 1).stealth_total = 30;   // legacy hidden attacker: advantage
        auto ra = step(a, attack(1, 2)), rr = step(ref, attack(1, 2));
        CHECK(same(ra, rr));
        auto d = fresh(seed, n);
        auto bl = fresh(seed, nullptr); add_condition(mut(bl, 1), Condition::Blinded);   // blinded attacker: disadvantage
        CHECK(same(step(d, attack(1, 2)), step(bl, attack(1, 2))));
        auto plain = fresh(seed, nullptr);
        differ += int(!same(ra, step(plain, attack(1, 2))));
    }
    CHECK(differ > 0);
}

TEST_CASE("vision: dim light / light obscurement: sight-based Perception at disadvantage, passive -5") {
    Sight v; CHECK(passive_sight_penalty(v) == 0);
    v.light_at_subject = Light::Dim; CHECK(sight_perception_disadvantage(v)); CHECK(passive_sight_penalty(v) == -5);
    v.light_at_subject = Light::Bright; v.obscured = Obscured::Light; CHECK(passive_sight_penalty(v) == -5);
    v.sense = Sense::Blindsight; CHECK(passive_sight_penalty(v) == 0);   // not a sight-based check
}

TEST_CASE("vision: Hide only while unseen; Stealth vs passive Perception (with the dim-light -5); 0xC5 state") {
    auto m = std::make_shared<SightMap>();
    auto s = fresh(3, m);
    mut(s, 1).abilities.score[size_t(Ability::Dex)] = 30;   // Stealth >= 11
    REQUIRE(step(s, {1, ActionKind::Hide}).ok);   // seen: no hiding
    CHECK(s.hidden.empty()); CHECK(mut(s, 1).stealth_total == 0);
    end_turns_until(s, 2); end_turns_until(s, 1);
    (*m)[{2, 1}] = unseen();
    mut(s, 2).abilities.score[size_t(Ability::Wis)] = 1;   // passive 5
    const uint64_t before = hidden_digest(s);
    REQUIRE(step(s, {1, ActionKind::Hide}).ok);
    REQUIRE(s.hidden.size() == 1);
    CHECK(is_hidden_from(s, 1, 2)); CHECK(has_hidden_state(s)); CHECK(hidden_digest(s) != before);
    CHECK(mut(s, 1).stealth_total == 0);   // the hook path replaces the legacy flag

    auto t = fresh(3, m);   // a passive Perception just above the roll: hidden only without dim light
    mut(t, 1).abilities.score[size_t(Ability::Dex)] = 10;
    mut(t, 2).abilities.score[size_t(Ability::Wis)] = 30;   // passive 20; with dim light 15
    int dim_only = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto u = fresh(seed, m); mut(u, 1).abilities.score[size_t(Ability::Dex)] = 10; mut(u, 2).abilities.score[size_t(Ability::Wis)] = 30;
        auto r = step(u, {1, ActionKind::Hide}); REQUIRE(r.ok);
        const int total = first(r.events, EventKind::Hid)->a;
        CHECK(is_hidden_from(u, 1, 2) == (total >= 20));
        Sight dim = unseen(); dim.light_at_subject = Light::Dim;
        auto md = std::make_shared<SightMap>(SightMap{{{2, 1}, dim}});
        auto w = fresh(seed, md); mut(w, 1).abilities.score[size_t(Ability::Dex)] = 10; mut(w, 2).abilities.score[size_t(Ability::Wis)] = 30;
        REQUIRE(step(w, {1, ActionKind::Hide}).ok);
        CHECK(is_hidden_from(w, 1, 2) == (total >= 15));
        dim_only += int(is_hidden_from(w, 1, 2) && !is_hidden_from(u, 1, 2));
    }
    CHECK(dim_only > 0);
}

TEST_CASE("vision: attacking reveals; seen at a step's start ends hiding") {
    auto m = std::make_shared<SightMap>(SightMap{{{2, 1}, unseen()}});
    auto s = fresh(4, m);
    s.hidden.push_back({1, 2, 25});
    REQUIRE(step(s, attack(1, 2)).ok);
    CHECK(s.hidden.empty());

    auto t = fresh(4, m);
    t.hidden.push_back({1, 2, 25});
    REQUIRE(step(t, {1, ActionKind::Dodge}).ok);
    CHECK(is_hidden_from(t, 1, 2));   // still unseen
    m->erase({2, 1});                 // now it sees the pc
    REQUIRE(step(t, {1, ActionKind::EndTurn}).ok);
    CHECK_FALSE(is_hidden_from(t, 1, 2));
}

TEST_CASE("vision: an attack on a hidden target needs a guessed tile; a wrong guess misses") {
    auto m = std::make_shared<SightMap>(SightMap{{{1, 2}, unseen()}});
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = fresh(seed, m);
        s.hidden.push_back({2, 1, 25});
        auto no = step(s, attack(1, 2));
        CHECK_FALSE(no.ok);
        CHECK(no.error == "target is hidden: guess its tile (has_guess)");
        s.hooks.guess_correct = [](const EncounterState&, const Combatant&, const Combatant&, int32_t x, int32_t y) { return x == 3 && y == 4; };
        auto wrong = attack(1, 2); wrong.has_guess = true; wrong.guess_x = 9; wrong.guess_y = 9;
        auto w = step(s, wrong);
        REQUIRE(w.ok);
        const Event* e = first(w.events, EventKind::AttackResolved);
        REQUIRE(e); CHECK((e->c & 1) == 0);
        CHECK(first(w.events, EventKind::DamageDealt) == nullptr);
    }
    int hits = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = fresh(seed, m);
        s.hidden.push_back({2, 1, 25});
        s.hooks.guess_correct = [](const EncounterState&, const Combatant&, const Combatant&, int32_t x, int32_t y) { return x == 3 && y == 4; };
        auto right = attack(1, 2); right.has_guess = true; right.guess_x = 3; right.guess_y = 4;
        auto r = step(s, right);
        REQUIRE(r.ok);
        hits += int((first(r.events, EventKind::AttackResolved)->c & 1) != 0);
    }
    CHECK(hits > 0);
}

TEST_CASE("vision: a gaze needs the actor to see the gazer with sight; the hook replaces gaze_seen") {
    auto book = std::make_shared<SpellBook>(*spell_hooks().spells);
    const uint32_t kGaze = spell_id_of("test.gaze#vision");
    SpellDef b; b.id = kGaze; b.content_id = "test.gaze#vision"; b.is_spell = false; b.range_ft = 30;
    b.kind = SpellEffectKind::Save; b.save_ability = Ability::Con; b.fixed_dc = 30; b.applies_condition = true;
    b.condition = Condition::Restrained;
    REQUIRE(book->add(b));
    for (Sense sense : {Sense::Sight, Sense::Blindsight, Sense::Tremorsense}) {
        auto h = spell_hooks(); h.spells = book;
        auto v = pc(1, 200);
        auto g = monster(2); g.hp = g.hp_max = 200; g.gazes = {Combatant::Gaze{kGaze, 30}};
        auto s = begin_encounter({v, g}, 5, nullptr, h);
        end_turns_until(s, 2); end_turns_until(s, 1);
        if (!s.budget.gaze_pending) { end_turns_until(s, 2); end_turns_until(s, 1); }
        REQUIRE(s.budget.gaze_pending);
        auto m = std::make_shared<SightMap>(); Sight sv; sv.sense = sense; (*m)[{1, 2}] = sv;
        s.hooks.sight = sight_hook(m);
        ActionRequest r{1, ActionKind::GazeResponse};   // no gaze_seen: the hook decides
        REQUIRE(step(s, r).ok);
        CHECK(find(s, 1)->has(Condition::Restrained) == (sense == Sense::Sight));
    }
}
