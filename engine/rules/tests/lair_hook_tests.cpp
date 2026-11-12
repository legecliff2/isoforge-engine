// Lair hook (lair-hooks contract v0.1). Plumbing only: initiative count 20, lair loses ties,
// an engine convention, not SRD 5.1 text. No lair options, no events, no hash byte.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "spell_util.hpp"

using namespace enc;

namespace {

struct Snap {
    LairTurn turn;
    uint16_t round = 0;
    std::size_t effects = 0;
    int la_cur = -1;
    bool ended = false;
    bool window = false;
    bool pending = false;
    bool dodging = false;
    bool readied = false;
    int offers = 0;
};

void set_dex(Combatant& c, int score) {
    c.abilities.score[std::size_t(Ability::Dex)] = static_cast<int8_t>(score);
}

// int, not int16_t: MSVC (/WX) turns the pair converting constructor's int -> int16_t into C4244.
void set_order(EncounterState& s, std::initializer_list<std::pair<CombatantId, int>> rows) {
    s.order.clear();
    for (auto [id, total] : rows) {
        const Combatant* c = find(s, id);
        const int dex = c ? modifier(c->abilities, Ability::Dex) : 0;
        s.order.push_back(InitiativeEntry{id, static_cast<int16_t>(total), static_cast<int8_t>(dex)});
    }
    s.turn_index = 0;
}

EncounterHooks watch(std::vector<Snap>& out, int* offers = nullptr) {
    EncounterHooks h;
    h.lair_turn = [&out, offers](const EncounterState& s, const LairTurn& t) {
        Snap n;
        n.turn = t;
        n.round = s.round;
        n.effects = s.effects.size();
        n.ended = s.ended;
        n.window = s.legendary_window.x != 0;
        n.pending = s.pending.has_value();
        if (const Combatant* b = find(s, 2)) { n.la_cur = b->la_cur; n.dodging = b->dodging; }
        if (const Combatant* c = find(s, 3)) n.readied = c->has_readied;
        n.offers = offers ? *offers : 0;
        out.push_back(n);
    };
    return h;
}

bool same_event(const Event& a, const Event& b) {
    return a.kind == b.kind && a.actor == b.actor && a.target == b.target && a.a == b.a && a.b == b.b && a.c == b.c &&
           a.reaction_depth == b.reaction_depth;
}

bool same_events(const std::vector<Event>& a, const std::vector<Event>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same_event);
}

std::size_t find_kind(const std::vector<Event>& ev, EventKind k) {
    for (std::size_t i = 0; i < ev.size(); ++i) if (ev[i].kind == k) return i;
    return ev.size();
}

void end_ok(EncounterState& s) { REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok); }

std::vector<Combatant> low_trio() {
    Combatant a = pc(1), b = monster(2), c = monster(3);
    set_dex(a, 8); set_dex(b, 8); set_dex(c, 8);   // d20 - 1 is always below 20
    return {a, b, c};
}

void plant_round_effect(EncounterState& s) {
    ActiveSpellEffect e{};
    e.spell_id = 1;
    e.caster = 1;
    e.target = 1;
    e.no_condition = true;
    e.timed = Timed{Expiry::Rounds, int16_t{1}};   // runs out on the next round-start tick
    s.effects.push_back(e);
}

LegendaryOption attack_opt() {
    LegendaryOption o;
    o.cost = 1;
    o.use = LegendaryUse::Attack;
    return o;
}

ActionRequest la(CombatantId id, uint32_t option, CombatantId target) {
    ActionRequest r{id, ActionKind::LegendaryAction, target};
    r.feature_id = option;
    return r;
}

struct Fight {
    std::vector<Event> events;
    uint64_t summons = 0, actions = 0, features = 0;
    uint16_t round = 0;
    uint16_t turn = 0;
    std::vector<InitiativeEntry> order;
    std::vector<int> hp;
};

Fight play(bool noop) {
    EncounterHooks h;
    std::vector<Snap> seen;
    if (noop) h = watch(seen);   // records outside the rules state; the fight itself is unchanged
    std::vector<Event> open;
    auto s = begin_encounter({pc(1), monster(2), monster(3)}, 11, &open, h);
    Fight f;
    f.events = std::move(open);
    for (int i = 0; i < 8 && !s.ended; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        REQUIRE(r.ok);
        f.events.insert(f.events.end(), r.events.begin(), r.events.end());
    }
    f.summons = summons_digest(s);
    f.actions = action_uses_digest(s);
    f.features = feature_uses_digest(s);
    f.round = s.round;
    f.turn = s.turn_index;
    f.order = s.order;
    for (const auto& c : s.combatants) f.hp.push_back(c.hp);
    if (noop) CHECK(seen.size() >= 2);   // the hook ran; a no-op still leaves the fight unchanged
    return f;
}

}  // namespace

TEST_CASE("lair hook: no lair is bit-identical") {
    CHECK(kLairInitiative == 20);
    CHECK(int(EventKind::LegendaryActionsRegained) == 54);
    CHECK(kEventKindCount == 55);
    CHECK(int(ActionKind::LegendaryAction) == 27);   // next ActionKind stays 28
    const Fight empty = play(false);
    const Fight noop = play(true);
    CHECK(same_events(empty.events, noop.events));
    CHECK(empty.summons == noop.summons);
    CHECK(empty.actions == noop.actions);
    CHECK(empty.features == noop.features);
    CHECK(empty.round == noop.round);
    CHECK(empty.turn == noop.turn);
    CHECK(empty.hp == noop.hp);
    REQUIRE(empty.order.size() == noop.order.size());
    for (std::size_t i = 0; i < empty.order.size(); ++i) {
        CHECK(empty.order[i].id == noop.order[i].id);
        CHECK(empty.order[i].total == noop.order[i].total);
        CHECK(empty.order[i].dex_mod == noop.order[i].dex_mod);
    }
}

TEST_CASE("lair hook: mixed totals 23, 20, 19") {
    std::vector<Snap> seen;
    auto s = begin_encounter({pc(1), monster(2), monster(3)}, 1, nullptr, watch(seen));
    set_order(s, {{1, 23}, {2, 20}, {3, 19}});
    seen.clear();
    end_ok(s);   // 23 acts, then 20. The slot is still ahead.
    CHECK(seen.empty());
    auto r = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(r.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.slot == 2);
    CHECK(seen[0].turn.round == 1);
    CHECK(seen[0].round == 1);
    const std::size_t ended = find_kind(r.events, EventKind::TurnEnded);
    const std::size_t started = find_kind(r.events, EventKind::TurnStarted);
    CHECK(ended < seen[0].turn.event_index);
    CHECK(seen[0].turn.event_index == started);
    CHECK(r.events[ended].actor == 2);
    CHECK(r.events[started].actor == 3);
    CHECK_FALSE(seen[0].ended);
    CHECK_FALSE(seen[0].window);
    CHECK_FALSE(seen[0].pending);
}

TEST_CASE("lair hook: a creature at exactly 20 acts before the lair") {
    std::vector<Snap> seen;
    auto s = begin_encounter({pc(1), monster(2)}, 1, nullptr, watch(seen));
    set_order(s, {{1, 20}, {2, 20}});   // both tie the lair; the lair loses
    seen.clear();
    end_ok(s);
    CHECK(seen.empty());                // the first 20 has finished its turn
    const uint16_t round = s.round;
    auto r = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(r.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.slot == 2);
    CHECK(seen[0].turn.round == round);
    CHECK(seen[0].round == round);      // before ++round
    CHECK(s.round == uint16_t(round + 1));
    const std::size_t ended = find_kind(r.events, EventKind::TurnEnded);
    const std::size_t started = find_kind(r.events, EventKind::TurnStarted);
    CHECK(ended < seen[0].turn.event_index);
    CHECK(seen[0].turn.event_index == started);   // the lair is the last thing before the next round's TurnStarted
    CHECK(r.events[ended].actor == 2);
}

TEST_CASE("lair hook: every total is 20 or more, at the wrap before the tick") {
    Combatant a = pc(1), b = monster(2), c = monster(3);
    set_dex(a, 48); set_dex(b, 48); set_dex(c, 48);   // d20 + 19 is always >= 20
    std::vector<Snap> seen;
    auto s = begin_encounter({a, b, c}, 3, nullptr, watch(seen));
    CHECK(seen.empty());   // p == n: not inside begin_encounter
    CHECK(s.round == 1);
    plant_round_effect(s);
    StepResult fired{};
    for (int i = 0; i < 4 && seen.empty(); ++i) fired = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(fired.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.slot == 3);
    CHECK(seen[0].turn.round == 1);
    CHECK(seen[0].round == 1);
    CHECK(seen[0].effects == 1);          // the round-start tick has not run
    CHECK(s.round == 2);
    CHECK(s.effects.empty());
    const std::size_t tick = find_kind(fired.events, EventKind::SpellEffectEnded);
    const std::size_t started = find_kind(fired.events, EventKind::TurnStarted);
    CHECK(seen[0].turn.event_index == tick);
    CHECK(tick < started);
}

TEST_CASE("lair hook: every total is below 20") {
    std::vector<Snap> seen;
    std::vector<Event> open;
    auto s = begin_encounter(low_trio(), 5, &open, watch(seen));
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.round == 1);
    CHECK(seen[0].turn.slot == 0);
    CHECK(seen[0].round == 1);
    CHECK(seen[0].turn.event_index == 3);
    REQUIRE(seen[0].turn.event_index < open.size());
    CHECK(open[seen[0].turn.event_index].kind == EventKind::TurnStarted);
    CHECK(open[seen[0].turn.event_index - 1].kind == EventKind::InitiativeRolled);
    plant_round_effect(s);
    StepResult wrap{};
    for (int i = 0; i < 8 && s.round == 1; ++i) wrap = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(wrap.ok);
    REQUIRE(s.round == 2);
    REQUIRE(seen.size() == 2);
    CHECK(seen[1].turn.round == 2);
    CHECK(seen[1].turn.slot == 0);
    CHECK(seen[1].round == 2);
    CHECK(seen[1].effects == 0);          // after the tick
    const std::size_t tick = find_kind(wrap.events, EventKind::SpellEffectEnded);
    const std::size_t started = find_kind(wrap.events, EventKind::TurnStarted);
    CHECK(tick < seen[1].turn.event_index);
    CHECK(seen[1].turn.event_index == started);

    // The first creature is already dead: still one call, after InitiativeRolled, before the dead-skip.
    auto dead_party = [] {
        auto cs = low_trio();
        cs[0].dead = true;
        cs[0].hp = 0;
        return cs;
    };
    uint64_t seed = 0;
    for (; seed < 5000; ++seed) {
        auto cs = dead_party();
        rng::Rng r(seed, 0);
        auto order = roll_initiative(r, cs);
        if (!order.empty() && order[0].id == 1) break;
    }
    REQUIRE(seed < 5000);
    std::vector<Snap> dead_seen;
    std::vector<Event> dead_open;
    auto dead = begin_encounter(dead_party(), seed, &dead_open, watch(dead_seen));
    REQUIRE(dead_seen.size() == 1);
    CHECK(dead_seen[0].turn.round == 1);
    CHECK(dead_seen[0].turn.slot == 0);
    CHECK(dead_seen[0].turn.event_index == 3);
    CHECK(dead_open[2].kind == EventKind::InitiativeRolled);
    CHECK(dead_open[3].kind == EventKind::TurnStarted);
    CHECK(dead.order[0].id == 1);
    CHECK(find(dead, 1)->dead);
}

TEST_CASE("lair hook: a dead entry at the slot still fires once per round") {
    std::vector<Snap> seen;
    Combatant d = monster(4);
    auto s = begin_encounter({pc(1), pc(2), monster(3), d}, 2, nullptr, watch(seen));
    mut(s, 3).dead = true;
    mut(s, 3).hp = 0;
    set_order(s, {{1, 23}, {2, 20}, {3, 19}, {4, 10}});   // p == 2, and that entry is dead
    seen.clear();
    for (int i = 0; i < 20 && seen.size() < 5; ++i) end_ok(s);
    REQUIRE(seen.size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(seen[std::size_t(i)].turn.round == uint16_t(i + 1));
        CHECK(seen[std::size_t(i)].turn.slot == 2);
        CHECK_FALSE(seen[std::size_t(i)].ended);
    }
    CHECK(find(s, 3)->dead);
    CHECK_FALSE(s.ended);
}

TEST_CASE("lair hook: legendary actions resolve before the lair, and no window opens after it") {
    Combatant x = pc(1), boss = monster(2);
    x.hp = x.hp_max = 40;
    boss.hp = boss.hp_max = 40;
    never_react(x);
    never_react(boss);
    boss.la_max = boss.la_cur = 2;
    boss.legendary = {attack_opt()};
    boss.attacks[0].to_hit = 40;
    boss.attacks[0].damage = {1, 4, 0};
    std::vector<Snap> seen;
    auto s = begin_encounter({x, boss}, 1, nullptr, watch(seen));
    set_order(s, {{1, 21}, {2, 10}});
    seen.clear();
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK(seen.empty());
    CHECK(s.legendary_window.candidate == 2);
    REQUIRE(step(s, la(2, 1, 1)).ok);
    CHECK(seen.empty());
    CHECK(find(s, 2)->la_cur == 1);
    auto second = step(s, la(2, 1, 1));   // last point: the window closes and the turn advances
    REQUIRE(second.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.slot == 1);
    CHECK(seen[0].turn.round == 1);
    CHECK(seen[0].la_cur == 0);            // not refilled and not spent by the lair
    CHECK_FALSE(seen[0].window);
    CHECK_FALSE(seen[0].pending);
    CHECK(s.legendary_window.x == 0);
    CHECK(find(s, 2)->la_cur == 2);        // the owner's start_turn refills, after the hook
    int used = 0;
    for (std::size_t i = 0; i < second.events.size(); ++i) {
        if (second.events[i].kind != EventKind::LegendaryActionUsed) continue;
        CHECK(i < seen[0].turn.event_index);
        ++used;
    }
    CHECK(used == 1);
    const std::size_t regained = find_kind(second.events, EventKind::LegendaryActionsRegained);
    CHECK(seen[0].turn.event_index == regained);   // the refill is the owner's start_turn, after the hook
    CHECK(s.legendary_window.x == 0);
}

TEST_CASE("lair hook: a summon inserted at total 22 moves the slot") {
    const uint32_t kFam = spell_id_of("test.find_familiar");
    auto hooks = [&](std::vector<Snap>* log) {
        EncounterHooks h = spell_hooks();
        auto book = std::make_shared<SpellBook>(*h.spells);
        SpellDef f;
        f.id = kFam;
        f.content_id = "test.find_familiar";
        f.level = 1;
        f.range_ft = 10;
        f.kind = SpellEffectKind::Summon;
        f.summon = SummonSpec{1, SummonInitiative::Own, SummonControl::Telepathic, true};
        book->add(f);
        h.spells = book;
        h.build_summon = [](const EncounterState&, const Combatant&, const SpellDef&, uint8_t, uint8_t) {
            Combatant m = monster(0);
            m.abilities.score[std::size_t(Ability::Dex)] = static_cast<int8_t>(30);   // +10; total 22 is a 12 on the d20
            m.hp = m.hp_max = 11;
            return m;
        };
        if (log) h.lair_turn = [log](const EncounterState& s, const LairTurn& t) {
            Snap n;
            n.turn = t;
            n.round = s.round;
            log->push_back(n);
        };
        return h;
    };
    auto party = [&] {
        Combatant w = wizard(1), b = monster(2), c = monster(3);
        set_dex(w, 8); set_dex(b, 8); set_dex(c, 8);
        w.spells_known.push_back(kFam);
        w.hp = w.hp_max = 40;
        b.hp = b.hp_max = 40;
        c.hp = c.hp_max = 40;
        never_react(w); never_react(b); never_react(c);
        return std::vector<Combatant>{w, b, c};
    };
    uint64_t seed = 1;
    for (; seed < 4000; ++seed) {
        auto s = begin_encounter(party(), seed, nullptr, hooks(nullptr));
        end_turns_until(s, 1);
        ActionRequest cast = ::enc::cast(1, kFam, 1, 1);
        cast.summon_archetypes = {0};
        auto r = step(s, cast);
        if (!r.ok) continue;
        int16_t total = -1;
        for (const auto& e : s.order) if (e.id == 4) total = e.total;
        if (total == 22) break;
    }
    REQUIRE(seed < 4000);

    std::vector<Snap> seen;
    auto s = begin_encounter(party(), seed, nullptr, hooks(&seen));
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.slot == 0);
    CHECK(seen[0].turn.round == 1);
    end_turns_until(s, 1);
    const std::size_t before = seen.size();
    ActionRequest cast = ::enc::cast(1, kFam, 1, 1);
    cast.summon_archetypes = {0};
    auto summoned = step(s, cast);
    REQUIRE(summoned.ok);
    CHECK(s.order[0].id == 4);
    CHECK(s.order[0].total == 22);
    CHECK(seen.size() == before);   // the cast does not advance; this round's call already happened
    int above = 0;
    for (const auto& e : s.order) above += e.total >= kLairInitiative;
    CHECK(above == 1);
    const uint16_t at = s.round;
    for (int i = 0; i < 24 && s.round < at + 3; ++i) end_ok(s);
    auto count = [&](uint16_t round, uint8_t slot) {
        int n = 0;
        for (const auto& sn : seen) n += sn.turn.round == round && sn.turn.slot == slot;
        return n;
    };
    CHECK(count(at, 0) == 1);                 // the pre-summon slot, once
    CHECK(count(at, 1) == 0);
    CHECK(count(uint16_t(at + 1), 1) == 1);   // the next round's advance uses the new p
    CHECK(count(uint16_t(at + 2), 1) == 1);
}

TEST_CASE("lair hook: no call after EncounterEnded") {
    std::vector<Snap> seen;
    auto s = begin_encounter({pc(1), monster(2)}, 1, nullptr, watch(seen));
    set_order(s, {{1, 25}, {2, 10}});
    mut(s, 2).dead = true;
    mut(s, 2).hp = 0;
    seen.clear();
    auto r = step(s, {1, ActionKind::EndTurn});
    REQUIRE(r.ok);
    REQUIRE(s.ended);
    const std::size_t ended = find_kind(r.events, EventKind::EncounterEnded);
    REQUIRE(ended < r.events.size());
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.event_index < ended);
    CHECK_FALSE(seen[0].ended);
    CHECK_FALSE(seen[0].window);
    const std::size_t n = seen.size();
    CHECK_FALSE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(seen.size() == n);
}

TEST_CASE("lair hook: replay yields the same LairTurn sequence") {
    std::vector<LairTurn> live, again;
    auto hook = [](std::vector<LairTurn>& into) {
        EncounterHooks h;
        h.lair_turn = [&into](const EncounterState&, const LairTurn& t) { into.push_back(t); };
        return h;
    };
    auto party = [] { return std::vector<Combatant>{pc(1), monster(2), monster(3)}; };
    ActionLog log;
    auto s = begin_encounter(party(), 9, nullptr, hook(live));
    for (int i = 0; i < 8 && !s.ended; ++i) REQUIRE(step_logged(s, {current_actor(s), ActionKind::EndTurn}, log).ok);
    auto replay = begin_encounter(party(), 9, nullptr, hook(again));
    for (const auto& step : log) REQUIRE(replay_step(replay, step).ok);
    CHECK(live == again);
    CHECK(live.size() >= 2);
}

TEST_CASE("lair hook: a readied action is neither offered nor expired") {
    int offers = 0;
    std::vector<Snap> seen;
    EncounterHooks h = watch(seen, &offers);
    h.ready_trigger = [&offers](const EncounterState&, const ReadiedAction&, const Event&) {
        ++offers;
        return false;
    };
    auto s = begin_encounter({pc(1), monster(2), monster(3)}, 4, nullptr, h);
    set_order(s, {{1, 25}, {2, 18}, {3, 10}});
    seen.clear();
    end_ok(s);   // 25 ends, the lair fires, 18 starts
    end_ok(s);   // 18 ends, 10 starts
    REQUIRE(current_actor(s) == 3);
    ActionRequest ready{3, ActionKind::Ready, 1};
    ready.ready_kind = ActionKind::Attack;
    ready.trigger_id = 7;
    ready.target_within_5ft = true;
    REQUIRE(step(s, ready).ok);
    CHECK(find(s, 3)->has_readied);
    end_ok(s);   // 10 ends; the wrap is not the lair (p == 1)
    REQUIRE(current_actor(s) == 1);
    mut(s, 2).dodging = true;
    mut(s, 3).reaction_used = true;
    seen.clear();
    const int offers_before = offers;
    auto r = step(s, {1, ActionKind::EndTurn});
    REQUIRE(r.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].offers == offers_before);   // fire_readies runs after the step, not at the lair
    CHECK(seen[0].readied);
    CHECK(seen[0].dodging);                   // turn-scoped state is unchanged at the hook
    CHECK(find(s, 3)->reaction_used);
    CHECK(count(r.events, EventKind::ReadyExpired) == 0);
    CHECK(count(r.events, EventKind::ReadyTriggered) == 0);
    CHECK(find(s, 3)->has_readied);
    CHECK_FALSE(find(s, 2)->dodging);         // cleared by 2's start_turn, which is after the hook
    CHECK_FALSE(seen[0].window);
    CHECK_FALSE(seen[0].pending);
}

// Edge (b): every entry ahead of the slot is dead, so the cross shares this advance_turn with the wrap.
// The new round's tick (effect expiry) runs before the hook.
TEST_CASE("lair hook: a post-wrap slot cross fires after the round-start tick") {
    std::vector<Snap> seen;
    EncounterHooks h = watch(seen);
    h.end_condition = [](const EncounterState&) { return std::optional<EncounterOutcome>{}; };
    auto s = begin_encounter({pc(1), monster(2)}, 1, nullptr, h);
    set_order(s, {{1, 23}, {2, 19}});
    mut(s, 1).dead = true;
    mut(s, 1).hp = 0;
    s.turn_index = 1;   // B is acting; A, the only entry ahead of the slot, is dead
    plant_round_effect(s);
    seen.clear();
    const uint16_t round = s.round;
    auto r = step(s, {2, ActionKind::EndTurn});
    REQUIRE(r.ok);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].turn.round == uint16_t(round + 1));
    CHECK(seen[0].turn.slot == 1);
    CHECK(seen[0].round == uint16_t(round + 1));
    CHECK(seen[0].effects == 0);
    CHECK(s.effects.empty());
    const std::size_t tick = find_kind(r.events, EventKind::SpellEffectEnded);
    const std::size_t started = find_kind(r.events, EventKind::TurnStarted);
    CHECK(tick < seen[0].turn.event_index);
    CHECK(seen[0].turn.event_index == started);
    CHECK(r.events[started].actor == 2);
    end_ok(s);
    end_ok(s);
    REQUIRE(seen.size() == 3);
    CHECK(seen[1].turn.round == uint16_t(round + 2));
    CHECK(seen[1].turn.slot == 1);
    CHECK(seen[2].turn.round == uint16_t(round + 3));
    CHECK(seen[2].turn.slot == 1);
}

// Edge (a): every creature is already dead and every total is below 20. Round 1 fires in
// begin_encounter; the dead-skip advance wraps and fires round 2; then the encounter ends.
TEST_CASE("lair hook: every creature dead fires once per round, then the encounter ends") {
    std::vector<Snap> seen;
    auto party = [] {
        auto cs = low_trio();
        for (auto& c : cs) { c.dead = true; c.hp = 0; }
        return cs;
    };
    auto s = begin_encounter(party(), 4, nullptr, watch(seen));
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].turn.round == 1);
    CHECK(seen[0].turn.slot == 0);
    CHECK(seen[1].turn.round == 2);
    CHECK(seen[1].turn.slot == 0);
    CHECK(s.ended);
    CHECK(s.round == 2);
}
