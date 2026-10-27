// Keen Senses (CONTENT_CONVENTIONS §29). Advantage on a Wisdom (Perception) check that relies on a listed sense.
// The printed passive is never increased. Hide takes the better of the sight passive and a keen hearing/smell passive.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include "encounter_util.hpp"

using namespace enc;

namespace {

using SightMap = std::map<std::pair<CombatantId, CombatantId>, SightFacts>;

std::function<SightFacts(const EncounterState&, const Combatant&, const Combatant&)> sight_hook(std::shared_ptr<SightMap> m) {
    return [m](const EncounterState&, const Combatant& v, const Combatant& t) {
        auto it = m->find({v.id, t.id});
        return it == m->end() ? SightFacts{} : it->second;
    };
}

bool same_events(const StepResult& a, const StepResult& b) {
    if (!a.ok || !b.ok || a.events.size() != b.events.size()) return false;
    for (std::size_t i = 0; i < a.events.size(); ++i)
        if (a.events[i].kind != b.events[i].kind || a.events[i].a != b.events[i].a || a.events[i].b != b.events[i].b ||
            a.events[i].c != b.events[i].c)
            return false;
    return true;
}

const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}

struct Perc {
    uint8_t keen = 0;
    uint8_t senses = 0;
    RollMode mode = RollMode::Normal;
    CombatantId target = 0;
    bool deaf = false;
    std::shared_ptr<SightMap> sight;
};

StepResult perc(uint64_t seed, Perc p) {
    auto s = begin_encounter({pc(1), monster(2)}, seed);
    mut(s, 1).keen_senses = p.keen;
    if (p.deaf) add_condition(mut(s, 1), Condition::Deafened);
    if (p.sight) s.hooks.sight_facts = sight_hook(p.sight);
    ActionRequest r;
    r.actor = 1;
    r.kind = ActionKind::AbilityCheck;
    r.skill = Skill::Perception;
    r.dc = 15;
    r.check_mode = p.mode;
    r.perception_senses = p.senses;
    r.target = p.target;
    return step(s, r);
}

SightFacts dim_unseen(bool audible = true) {
    SightFacts f;
    f.line_of_sight = false;
    f.subject.light = Light::Dim;
    f.audible = audible;
    return f;
}

SightFacts bright_unseen() {
    SightFacts f;
    f.line_of_sight = false;
    f.subject.light = Light::Bright;
    f.audible = true;
    return f;
}

SightFacts dim_seen() {
    SightFacts f;
    f.line_of_sight = true;
    f.subject.light = Light::Dim;
    f.audible = true;
    return f;
}

struct HideOut { int total = 0; bool hidden = false; };
HideOut hide_at(uint64_t seed, uint8_t keen, SightFacts facts, bool deaf = false) {
    auto a = pc(1);
    a.abilities.score[std::size_t(Ability::Dex)] = 14;   // Stealth +2, totals 3..22
    auto b = monster(2);
    b.keen_senses = keen;
    if (deaf) add_condition(b, Condition::Deafened);
    auto s = begin_encounter({a, b}, seed);
    end_turns_until(s, 1);
    auto m = std::make_shared<SightMap>();
    (*m)[{2, 1}] = facts;
    s.hooks.sight_facts = sight_hook(m);
    auto r = step(s, {1, ActionKind::Hide});
    REQUIRE(r.ok);
    const Event* hid = first(r.events, EventKind::Hid);
    REQUIRE(hid);
    return {hid->a, is_hidden_from(s, 1, 2)};
}

}  // namespace

TEST_CASE("keen senses: the named sense, one advantage, printed passive unchanged") {
    CHECK(keen_perception(0, kKeenSight, -5, true).passive_bonus == -5);
    CHECK(keen_perception(0, kKeenSight, -5, true).mode == RollMode::Disadvantage);
    CHECK(keen_perception(0, kKeenSight, 0, true).passive_bonus == 0);
    CHECK(keen_perception(0, kKeenSight, 0, true).mode == RollMode::Normal);

    CHECK(keen_perception(kKeenHearing, uint8_t(kKeenSight | kKeenHearing), -5, false).passive_bonus == 5);
    CHECK(keen_perception(kKeenHearing, kKeenSight, -5, false).passive_bonus == -5);   // hearing is not this check
    CHECK(keen_perception(kKeenSmell, uint8_t(kKeenSight | kKeenSmell), -5, false).passive_bonus == 5);
    CHECK(keen_perception(kKeenSmell, kKeenSmell, -5, false).passive_bonus == 5);      // dim light does not hit a smell check
    CHECK(keen_perception(kKeenSmell, kKeenSight, -5, true).mode == RollMode::Disadvantage);

    CHECK(keen_perception(kKeenSight, kKeenSight, -5, false).passive_bonus == -5);     // unseen: Keen Sight adds nothing
    CHECK(keen_perception(kKeenSight, kKeenSight, 0, false).passive_bonus == 0);
    CHECK(keen_perception(kKeenSight, kKeenSight, -5, true).passive_bonus == 0);       // sight works: advantage cancels -5
    CHECK(keen_perception(kKeenSight, kKeenSight, -5, true).mode == RollMode::Normal);
    CHECK(keen_perception(kKeenSight, kKeenSight, 0, true).passive_bonus == 5);

    const uint8_t both = uint8_t(kKeenHearing | kKeenSmell);
    CHECK(keen_perception(both, both, 0, false).passive_bonus == 5);                   // two senses, still +5
    CHECK(unseen_perception_bonus(0, -5, true) == -5);
    CHECK(unseen_perception_bonus(0, 0, true) == 0);
    CHECK(unseen_perception_bonus(kKeenHearing, -5, true) == 5);
    CHECK(unseen_perception_bonus(kKeenHearing, -5, false) == -5);
    CHECK(unseen_perception_bonus(kKeenSmell, -5, false) == 5);
    CHECK(unseen_perception_bonus(kKeenSight, -5, true) == -5);
    CHECK(unseen_perception_bonus(both, -5, true) == 5);
    CHECK(keen_surprise_bonus(0, false) == 0);
    CHECK(keen_surprise_bonus(kKeenHearing, false) == 5);
    CHECK(keen_surprise_bonus(kKeenHearing, true) == 0);
    CHECK(keen_surprise_bonus(kKeenSmell, true) == 5);
    CHECK(keen_surprise_bonus(kKeenSight, false) == 0);
    CHECK(keen_surprise_bonus(both, false) == 5);

    auto m = monster(2);
    m.keen_senses = both;
    CHECK(passive_perception(m) == 10);
    m.passive_perception_override = 13;
    CHECK(passive_perception(m) == 13);
    CHECK(passive_perception(m, 5) == 18);   // a caller bonus still applies; the mask did not
}

TEST_CASE("keen senses: Hide uses the better passive; Keen Sight does not help while unseen") {
    auto o = monster(2);
    const Sight dim = derive_sight(o, dim_unseen());
    CHECK_FALSE(dim.can_see);
    CHECK(dim.perceives);
    CHECK(passive_sight_penalty(dim) == -5);
    const Sight bright = derive_sight(o, bright_unseen());
    CHECK_FALSE(bright.can_see);
    CHECK(passive_sight_penalty(bright) == 0);
    const Sight quiet = derive_sight(o, dim_unseen(false));
    CHECK_FALSE(quiet.perceives);

    int between = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const auto plain = hide_at(seed, 0, dim_unseen());
        const int total = plain.total;
        CHECK(hide_at(seed, kKeenHearing, dim_unseen()).total == total);
        CHECK(hide_at(seed, kKeenSmell, dim_unseen()).total == total);
        CHECK(hide_at(seed, kKeenSight, dim_unseen()).total == total);
        CHECK(plain.hidden == (total >= 5));
        CHECK(hide_at(seed, kKeenHearing, dim_unseen()).hidden == (total >= 15));
        CHECK(hide_at(seed, kKeenSmell, dim_unseen()).hidden == (total >= 15));
        CHECK(hide_at(seed, kKeenSight, dim_unseen()).hidden == (total >= 5));
        CHECK(hide_at(seed, kKeenHearing, dim_unseen(false)).hidden == (total >= 5));
        CHECK(hide_at(seed, kKeenHearing, dim_unseen(), true).hidden == (total >= 5));
        CHECK(hide_at(seed, uint8_t(kKeenHearing | kKeenSmell), dim_unseen(), true).hidden == (total >= 15));
        CHECK(hide_at(seed, 0, bright_unseen()).hidden == (total >= 10));
        CHECK(hide_at(seed, kKeenHearing, bright_unseen()).hidden == (total >= 15));
        between += int(total >= 5 && total < 15);
    }
    CHECK(between > 0);
}

TEST_CASE("keen senses: surprise adds hearing or smell, not sight, and not the printed passive") {
    auto hider = pc(1);
    auto m = monster(2);
    CHECK(surprised_by_stealth({hider, m}, {{1, 12}}) == std::vector<CombatantId>{2});
    m.keen_senses = kKeenHearing;
    CHECK(surprised_by_stealth({hider, m}, {{1, 12}}).empty());
    m.keen_senses = kKeenSight;
    CHECK(surprised_by_stealth({hider, m}, {{1, 12}}) == std::vector<CombatantId>{2});
    m.keen_senses = kKeenHearing;
    add_condition(m, Condition::Deafened);
    CHECK(surprised_by_stealth({hider, m}, {{1, 12}}) == std::vector<CombatantId>{2});
    m.keen_senses = uint8_t(kKeenHearing | kKeenSmell);
    CHECK(surprised_by_stealth({hider, m}, {{1, 12}}).empty());
    m.passive_perception_override = 13;
    m.keen_senses = kKeenHearing;
    remove_condition(m, Condition::Deafened);
    CHECK(passive_perception(m) == 13);
    CHECK(surprised_by_stealth({hider, m}, {{1, 16}}).empty());          // compared passive 18
    CHECK(surprised_by_stealth({hider, m}, {{1, 18}}) == std::vector<CombatantId>{2});
    CHECK(passive_perception(m) == 13);
}

TEST_CASE("keen senses: an active Perception check names its sense") {
    Perc smell_adv;
    smell_adv.keen = kKeenSmell;
    smell_adv.senses = kKeenSmell;
    Perc forced;
    forced.mode = RollMode::Advantage;
    Perc smell_on_sight;
    smell_on_sight.keen = kKeenSmell;
    smell_on_sight.senses = kKeenSight;
    Perc sight_adv;
    sight_adv.keen = kKeenSight;
    sight_adv.senses = kKeenSight;
    Perc named_none;
    named_none.keen = uint8_t(kKeenSight | kKeenHearing | kKeenSmell);
    Perc cancel;
    cancel.keen = kKeenSmell;
    cancel.senses = kKeenSmell;
    cancel.mode = RollMode::Disadvantage;
    Perc deaf;
    deaf.keen = kKeenHearing;
    deaf.senses = kKeenHearing;
    deaf.deaf = true;

    auto dim = std::make_shared<SightMap>(SightMap{{{1, 2}, dim_seen()}});
    Perc sight_dim = sight_adv;
    sight_dim.target = 2;
    sight_dim.sight = dim;
    Perc plain_dim;
    plain_dim.senses = kKeenSight;
    plain_dim.target = 2;
    plain_dim.sight = dim;
    Perc dis;
    dis.mode = RollMode::Disadvantage;

    auto unseen = std::make_shared<SightMap>(SightMap{{{1, 2}, dim_unseen()}});
    Perc hear_unseen;
    hear_unseen.keen = kKeenHearing;
    hear_unseen.senses = uint8_t(kKeenHearing | kKeenSight);
    hear_unseen.target = 2;
    hear_unseen.sight = unseen;
    Perc sight_unseen;
    sight_unseen.keen = kKeenSight;
    sight_unseen.senses = uint8_t(kKeenHearing | kKeenSight);
    sight_unseen.target = 2;
    sight_unseen.sight = unseen;

    int smell_differs = 0, sight_differs = 0, dim_differs = 0, unseen_differs = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        auto adv = perc(seed, smell_adv);
        auto ref = perc(seed, forced);
        auto normal = perc(seed, {});
        CHECK(same_events(adv, ref));
        CHECK(same_events(perc(seed, smell_on_sight), normal));
        CHECK(same_events(perc(seed, sight_adv), ref));
        CHECK(same_events(perc(seed, named_none), normal));
        CHECK(same_events(perc(seed, cancel), normal));
        CHECK(same_events(perc(seed, deaf), normal));
        CHECK(perc(seed, deaf).ok);
        CHECK(same_events(perc(seed, sight_dim), normal));
        auto dim_dis = perc(seed, plain_dim);
        auto dis_ref = perc(seed, dis);
        CHECK(same_events(dim_dis, dis_ref));
        CHECK(same_events(perc(seed, hear_unseen), ref));
        CHECK(same_events(perc(seed, sight_unseen), dis_ref));
        smell_differs += int(!same_events(adv, normal));
        sight_differs += int(!same_events(perc(seed, sight_adv), normal));
        dim_differs += int(!same_events(dim_dis, normal));
        unseen_differs += int(!same_events(perc(seed, hear_unseen), dis_ref));
    }
    CHECK(smell_differs > 0);
    CHECK(sight_differs > 0);
    CHECK(dim_differs > 0);
    CHECK(unseen_differs > 0);

    auto s = begin_encounter({pc(1), monster(2)}, 1);
    ActionRequest bad;
    bad.actor = 1;
    bad.kind = ActionKind::Attack;
    bad.target = 2;
    bad.perception_senses = kKeenSmell;
    auto atk = step(s, bad);
    CHECK_FALSE(atk.ok);
    CHECK(std::string(atk.error).find("only valid on a Wisdom (Perception) check") != std::string::npos);
    bad.kind = ActionKind::AbilityCheck;
    bad.skill = Skill::Stealth;
    auto st = step(s, bad);
    CHECK_FALSE(st.ok);
    CHECK(std::string(st.error).find("only valid on a Wisdom (Perception) check") != std::string::npos);
    bad.skill = Skill::Perception;
    bad.perception_senses = 8;
    auto bits = step(s, bad);
    CHECK_FALSE(bits.ok);
    CHECK(std::string(bits.error).find("only sight, hearing, smell") != std::string::npos);
}

TEST_CASE("keen senses: Wild Shape copies the beast's mask and revert restores none") {
    auto c = pc(1);
    c.abilities.score[std::size_t(Ability::Wis)] = 16;
    c.keen_senses = 0;
    FormStats f;
    f.hp_max = 11;
    f.abilities.score = {12, 15, 12, 3, 12, 6};
    f.keen_senses = uint8_t(kKeenHearing | kKeenSmell);
    REQUIRE(apply_form(c, FormKind::WildShape, f));
    CHECK(c.keen_senses == uint8_t(kKeenHearing | kKeenSmell));
    CHECK(c.abilities[Ability::Wis] == 16);
    REQUIRE(revert_form(c));
    CHECK(c.keen_senses == 0);
    CHECK(c.abilities[Ability::Wis] == 16);
}
