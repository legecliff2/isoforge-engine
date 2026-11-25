// C-D dragon kit (contract v0.2). Fixture creatures only: no authored dragon JSON.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json melee(const char* id, const char* name, int reach = 5) {
    return {{"id", id}, {"name", name}, {"kind", "melee_weapon_attack"}, {"to_hit", 8}, {"reach", reach},
            {"targets", 1}, {"damage", Json::array({{{"dice", "1d6+4"}, {"type", "piercing"}}})}};
}

Json frightful(int dc, bool disadvantage, int rounds = 10) {
    Json a = {{"id", "frightful_presence"}, {"name", "Frightful Presence"}, {"kind", "save_effect"},
              {"origin", "self"}, {"within_ft", 120}, {"total_cover", "no_skip"}, {"chooses_targets", true},
              {"aware_only", true}, {"save", {{"ability", "wis"}, {"dc", dc}}},
              {"apply", Json::array({{{"condition", "frightened"},
                                       {"duration", {{"kind", "rounds"}, {"n", rounds}}},
                                       {"repeat_save", "end_of_turn"}}})},
              {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}},
              {"immune_on_end", true}};
    if (disadvantage) a["repeat_disadvantage_in_sight"] = true;
    return a;
}

Json wing(int within, int dc) {
    return {{"id", "wing_attack"}, {"name", "Wing Attack (Costs 2 Actions)"}, {"cost", 2},
            {"description", "The flight is not taken. Wing Attack resolves the save, damage and prone only; the dragon does not fly afterward. Deferred (legendary fly move, Q4)."},
            {"effect", {{"kind", "save_effect"}, {"id", "wing_attack"}, {"name", "Wing Attack"},
                        {"within_ft", within}, {"save", {{"ability", "dex"}, {"dc", dc}}},
                        {"damage", Json::array({{{"dice", "2d6+8"}, {"type", "bludgeoning"}}})},
                        {"on_success", "none"},
                        {"apply", Json::array({{{"condition", "prone"}}})}}}};
}

// A monster@7 stand-in. Not a stat block from the SRD.
Json dragon(const std::string& id, int fp_dc, bool disadvantage) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@7";
    m["id"] = id;
    m["name"] = "Dragon Kit";
    m["type"] = "dragon";
    m["size"] = "medium";
    m["hit_points"]["average"] = 200;
    m["abilities"]["wis"] = 10;
    m["actions"] = Json::array({melee("bite", "Bite"), melee("claw", "Claw"), melee("tail", "Tail", 10),
                                melee("chomp", "Chomp"), frightful(fp_dc, disadvantage)});
    m["actions"][3]["target_requires_any"] = Json::array({"grappled_by_self"});
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
                                {"effect", {{"kind", "multiattack"},
                                            {"options", Json::array({{{"id", "plain"},
                                                                      {"attacks", Json::array({"bite", "claw", "claw"})},
                                                                      {"lead_action", "frightful_presence"}}})}}}}});
    m["legendary_actions_per_round"] = 3;
    m["legendary_actions"] = Json::array({
        {{"id", "tail"}, {"name", "Tail Attack"}, {"description", "One tail."}, {"cost", 1},
         {"use", {{"attack", "tail"}}}},
        {{"id", "chomp"}, {"name", "Chomp"}, {"description", "A gated bite."}, {"cost", 1},
         {"use", {{"attack", "chomp"}}}},
        wing(10, fp_dc),
    });
    m.erase("behavior");
    return m;
}

Json victim(const std::string& id, Json speed = Json::parse(R"({"walk":30})")) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 200;
    m["abilities"]["wis"] = 10;
    m["abilities"]["dex"] = 10;
    m["speed"] = std::move(speed);
    m.erase("behavior");
    return m;
}

struct Spawn { std::string archetype; int x, y; std::string team; };
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls)
        c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        std::string ov = spawns[i].team.empty() ? "" : std::string("{\"team\":\"") + spawns[i].team + "\"}";
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y},
                            world::spawn_stable_id(c.coord, uint32_t(i)), ov});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

ContentLibrary base_lib(int fp_dc = 30, bool disadvantage = false) {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(dragon("t.dragon", fp_dc, disadvantage), "monster"));
    REQUIRE_FALSE(l.add(victim("t.victim"), "monster"));
    REQUIRE_FALSE(l.add(victim("t.flyer", Json::parse(R"({"walk":30,"fly":60})")), "monster"));
    return l;
}

SubmitResult go(Combat& c, Command cmd) {
    auto r = c.submit(cmd);
    while (r.needs) r = c.decide(rules5e::ReactionDecision{r.needs->window_seq, false, 0});
    return r;
}
Command end_turn(const Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    return e;
}
void to_actor(Combat& c, CombatantId id) {
    for (int i = 0; i < 48 && (rules5e::current_actor(c.state()) != id || c.state().legendary_window.x != 0) && !c.state().ended; ++i) {
        if (c.state().legendary_window.x != 0) {
            Command pass;
            pass.request = {c.state().legendary_window.candidate, ActionKind::LegendaryAction};
            REQUIRE(go(c, pass).ok);
        } else {
            REQUIRE(go(c, end_turn(c)).ok);
        }
    }
    REQUIRE(c.state().legendary_window.x == 0);
    REQUIRE(rules5e::current_actor(c.state()) == id);
}
void offer(Combat& c, CombatantId id) {
    for (int i = 0; i < 16 && c.state().legendary_window.candidate != id && !c.state().ended; ++i)
        REQUIRE(go(c, end_turn(c)).ok);
    REQUIRE(c.state().legendary_window.candidate == id);
}
int count_saves(const Combat& c, CombatantId who, std::size_t from = 0) {
    int n = 0;
    for (std::size_t i = from; i < c.events().size(); ++i) {
        const auto& e = c.events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == who) ++n;
    }
    return n;
}
bool has_kind(const Combat& c, EventKind k, std::size_t from = 0) {
    for (std::size_t i = from; i < c.events().size(); ++i)
        if (c.events()[i].kind == SimEvent::Kind::Rules && c.events()[i].rules.kind == k) return true;
    return false;
}
Command cast_fp(CombatantId dragon, std::vector<rules5e::CombatantId> exclude = {}) {
    Command cast;
    cast.request = {dragon, ActionKind::CastSpell};
    cast.request.spell_id = rules5e::spell_id_of("t.dragon#frightful_presence");
    cast.request.zone_excluded = std::move(exclude);
    return cast;
}
}  // namespace

TEST_CASE("C-D 1-2: within_ft is 3D with no sight gate; a wall does not drop eligibility") {
    auto lib = base_lib();
    std::vector<Vec2i> wall;
    for (int y = 0; y < 32; ++y) wall.push_back({6, y});
    // 0 dragon (4,4). 1 directly above, same tile, +120 ft. 2 at 120 ft south. 3 at 125 ft.
    // 4 behind a full wall, still within 120 ft. 5 blinded. 6 deafened.
    auto ch = arena({{"t.dragon", 4, 4, ""}, {"t.flyer", 4, 4, "foes"}, {"t.victim", 4, 28, "foes"},
                     {"t.victim", 4, 29, "foes"}, {"t.victim", 10, 4, "foes"}, {"t.victim", 5, 4, "foes"},
                     {"t.victim", 4, 5, "foes"}}, wall);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const auto ids = [&] {
        std::vector<rules5e::CombatantId> v;
        for (const auto& s : ch.spawns) v.push_back(s.stable_id);
        return v;
    }();
    REQUIRE(c->set_altitude(ids[1], 120, kAltForced));
    CombatTestAccess::state(*c).combatants;   // keep the access used
    for (auto& k : CombatTestAccess::state(*c).combatants) {
        if (k.id == ids[5]) k.conditions |= rules5e::bit(Condition::Blinded);
        if (k.id == ids[6]) k.conditions |= rules5e::bit(Condition::Deafened);
    }
    CHECK(c->distance_tiles(ids[0], ids[1]) * 5 == 120);
    CHECK(c->distance_tiles(ids[0], ids[2]) * 5 == 120);
    CHECK(c->distance_tiles(ids[0], ids[3]) * 5 == 125);
    CHECK_FALSE(c->line_of_sight(ids[0], ids[4]));
    auto derived = c->within_targets(ids[0], 120);
    CHECK(std::find(derived.begin(), derived.end(), ids[1]) != derived.end());
    CHECK(std::find(derived.begin(), derived.end(), ids[2]) != derived.end());
    CHECK(std::find(derived.begin(), derived.end(), ids[4]) != derived.end());
    CHECK(std::find(derived.begin(), derived.end(), ids[5]) != derived.end());
    CHECK(std::find(derived.begin(), derived.end(), ids[6]) != derived.end());
    CHECK(std::find(derived.begin(), derived.end(), ids[3]) == derived.end());
    to_actor(*c, ids[0]);
    const auto r = go(*c, cast_fp(ids[0]));
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(count_saves(*c, ids[1]) == 1);
    CHECK(count_saves(*c, ids[2]) == 1);
    CHECK(count_saves(*c, ids[4]) == 1);   // behind a wall, no Hide result: still eligible
    CHECK(count_saves(*c, ids[5]) == 1);
    CHECK(count_saves(*c, ids[6]) == 1);
    CHECK(count_saves(*c, ids[3]) == 0);
}

TEST_CASE("C-D 3-4: exclusion, an ineligible name, unconscious, hidden, and surprised") {
    auto lib = base_lib(1, false);
    auto ch = arena({{"t.dragon", 4, 4, ""}, {"t.victim", 5, 4, "foes"}, {"t.victim", 4, 5, ""},
                     {"t.victim", 6, 4, "foes"}, {"t.victim", 4, 6, "foes"}, {"t.victim", 7, 4, "foes"},
                     {"t.victim", 4, 29, "foes"}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    std::vector<rules5e::CombatantId> ids;
    for (const auto& s : ch.spawns) ids.push_back(s.stable_id);
    auto& st = CombatTestAccess::state(*c);
    for (auto& k : st.combatants) if (k.id == ids[3]) k.conditions |= rules5e::bit(Condition::Unconscious);
    to_actor(*c, ids[0]);
    st.hidden.push_back({ids[0], ids[4], 20});
    for (auto& k : st.combatants) if (k.id == ids[5]) k.surprised = true;
    // A name outside the derived set spends nothing.
    Command bad = cast_fp(ids[0], {ids[6]});
    const bool action_before = c->state().budget.action;
    auto refused = go(*c, bad);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.find("chosen creature is not eligible") != std::string::npos);
    CHECK(c->state().budget.action == action_before);
    CHECK(count_saves(*c, ids[1]) == 0);
    // A duplicate is refused the same way.
    Command dup = cast_fp(ids[0], {ids[1], ids[1]});
    auto duped = go(*c, dup);
    CHECK_FALSE(duped.ok);
    CHECK(duped.error.find("bad zone_excluded") != std::string::npos);
    CHECK(c->state().budget.action == action_before);
    // The ally is excluded. Unconscious and hidden-from are not eligible. Surprised still is.
    const std::size_t ev0 = c->events().size();
    auto ok = go(*c, cast_fp(ids[0], {ids[2]}));
    REQUIRE_MESSAGE(ok.ok, ok.error);
    CHECK(count_saves(*c, ids[2], ev0) == 0);
    CHECK(count_saves(*c, ids[3], ev0) == 0);
    CHECK(count_saves(*c, ids[4], ev0) == 0);
    CHECK(count_saves(*c, ids[5], ev0) == 1);
    CHECK(count_saves(*c, ids[1], ev0) == 1);
}
TEST_CASE("C-D 3: a logged exclusion list replays") {
    auto lib = base_lib(1, false);
    auto ch = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}, {"t.victim", 4, 5, ""}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, ally = ch.spawns[2].stable_id;
    to_actor(*c, d);
    auto ok = go(*c, cast_fp(d, {ally}));
    REQUIRE_MESSAGE(ok.ok, ok.error);
    CHECK(count_saves(*c, ally) == 0);
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, ch, lib, 3, c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
}

TEST_CASE("C-D 5: a success grants same-source immunity; a repeat and the timer do too") {
    auto lib = base_lib(1, false);
    auto ch = arena({{"t.dragon", 4, 4, ""}, {"t.dragon", 8, 4, ""}, {kFighter, 5, 4, ""}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    const auto a = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id, f = ch.spawns[2].stable_id;
    to_actor(*c, a);
    {
        auto first = go(*c, cast_fp(a));
        REQUIRE_MESSAGE(first.ok, first.error);
    }
    CHECK_FALSE(rules5e::find(c->state(), f)->has(Condition::Frightened));
    CHECK(count_saves(*c, f) == 1);
    if (rules5e::current_actor(c->state()) == a) REQUIRE(go(*c, end_turn(*c)).ok);
    to_actor(*c, a);
    const std::size_t ev1 = c->events().size();
    {
        auto second = go(*c, cast_fp(a));
        REQUIRE_MESSAGE(second.ok, second.error);   // immune to this dragon: no second save
    }
    CHECK(count_saves(*c, f, ev1) == 0);
    if (rules5e::current_actor(c->state()) == b) REQUIRE(go(*c, end_turn(*c)).ok);
    to_actor(*c, b);
    const std::size_t ev2 = c->events().size();
    REQUIRE(go(*c, cast_fp(b)).ok);   // a different source still applies
    CHECK(count_saves(*c, f, ev2) == 1);

    auto hard = base_lib(30, false);
    auto ch2 = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}});
    ecs::Registry reg3;
    auto c2 = Combat::create(reg3, ch2, hard, 6);
    REQUIRE(c2);
    const auto d = ch2.spawns[0].stable_id, v = ch2.spawns[1].stable_id;
    to_actor(*c2, d);
    REQUIRE(go(*c2, cast_fp(d)).ok);
    CHECK(rules5e::find(c2->state(), v)->has(Condition::Frightened));
    for (auto& e : CombatTestAccess::state(*c2).effects)
        if (e.target == v && e.condition == Condition::Frightened) e.dc = 1;
    to_actor(*c2, v);
    REQUIRE(go(*c2, end_turn(*c2)).ok);
    CHECK_FALSE(rules5e::find(c2->state(), v)->has(Condition::Frightened));
    CHECK_FALSE(c2->state().action_immunities.empty());

    auto timed = base_lib(30, false);
    auto ch3 = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}});
    ecs::Registry reg4;
    auto c3 = Combat::create(reg4, ch3, timed, 7);
    REQUIRE(c3);
    const auto d3 = ch3.spawns[0].stable_id, v3 = ch3.spawns[1].stable_id;
    to_actor(*c3, d3);
    REQUIRE(go(*c3, cast_fp(d3)).ok);
    for (auto& e : CombatTestAccess::state(*c3).effects)
        if (e.target == v3 && e.condition == Condition::Frightened) e.timed.rounds_left = 1;
    // The next round start expires the timer (immune_on_end). Pass any legendary window on the way.
    const int round0 = c3->state().round;
    for (int i = 0; i < 40 && c3->state().round == round0 && !c3->state().ended; ++i) {
        if (c3->state().legendary_window.x != 0) {
            Command pass;
            pass.request = {c3->state().legendary_window.candidate, ActionKind::LegendaryAction};
            REQUIRE(go(*c3, pass).ok);
        } else {
            REQUIRE(go(*c3, end_turn(*c3)).ok);
        }
    }
    CHECK_FALSE(rules5e::find(c3->state(), v3)->has(Condition::Frightened));
    CHECK_FALSE(c3->state().action_immunities.empty());
}

TEST_CASE("C-D 6: tarrasque repeat is at disadvantage in line of sight; a dragon's is not") {
    auto terra = base_lib(30, true);
    auto plain = base_lib(30, false);
    auto ch = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}});
    auto run = [&](ContentLibrary& lib, bool wall) {
        std::vector<Vec2i> walls;
        if (wall) for (int y = 0; y < 32; ++y) walls.push_back({6, y});
        // The fighter is east of the dragon; a wall to the west does not block this pair.
        // A wall between them does. Place the fighter west of a wall when asked.
        world::Chunk board = wall ? arena({{"t.dragon", 2, 5, ""}, {kFighter, 8, 5, ""}}, walls)
                                  : ch;
        ecs::Registry reg;
        auto c = Combat::create(reg, board, lib, 9);
        REQUIRE(c);
        const auto d = board.spawns[0].stable_id, v = board.spawns[1].stable_id;
        to_actor(*c, d);
        REQUIRE(go(*c, cast_fp(d)).ok);
        bool flag = false;
        for (const auto& e : c->state().effects)
            if (e.target == v && e.condition == Condition::Frightened) flag = e.repeat_los_disadvantage;
        to_actor(*c, v);
        auto saved = CombatTestAccess::state(*c).rng.save();
        rng::Rng pred = CombatTestAccess::state(*c).rng;
        const int first = pred.d(20);
        const int second = pred.d(20);
        const std::size_t ev0 = c->events().size();
        REQUIRE(go(*c, end_turn(*c)).ok);
        int natural = -1;
        for (std::size_t i = ev0; i < c->events().size(); ++i) {
            const auto& e = c->events()[i];
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == v)
                natural = e.rules.a;
        }
        return std::tuple<bool, int, int, int, rng::Pcg32::State>{flag, natural, first, second, saved};
    };
    auto [flag_t, nat_t, a_t, b_t, snap_t] = run(terra, false);
    auto [flag_w, nat_w, a_w, b_w, snap_w] = run(terra, true);
    CHECK(flag_t);
    CHECK(flag_w);
    CHECK(nat_t == std::min(a_t, b_t));   // line of sight: disadvantage
    CHECK(nat_w == a_w);                  // no line of sight: one die
    auto [flag_p, nat_p, a_p, b_p, snap_p] = run(plain, false);
    CHECK_FALSE(flag_p);
    CHECK(nat_p == a_p);                  // a dragon's FP repeats normally
    (void)snap_t; (void)snap_w; (void)snap_p; (void)b_w; (void)b_p;
}

TEST_CASE("C-D 7-8: Wisdom cover is nothing; Dexterity cover is +2/+5; total cover saves or skips") {
    auto lib = base_lib(15, false);
    auto saves_of = [&](world::Chunk ch, bool wing) {
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 21);
        REQUIRE(c);
        const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
        std::vector<int> totals;
        std::vector<int> naturals;
        if (wing) {
            offer(*c, d);
            Command w;
            w.request = {d, ActionKind::LegendaryAction};
            w.request.feature_id = 3;
            REQUIRE(go(*c, w).ok);
        } else {
            to_actor(*c, d);
            REQUIRE(go(*c, cast_fp(d)).ok);
        }
        for (const auto& e : c->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == v) {
                naturals.push_back(e.rules.a);
                totals.push_back(e.rules.b);
            }
        const auto cov = c->cover_of(rules5e::CoverQuery{d, v, false, 0, false, 0, 0});
        return std::tuple<std::vector<int>, std::vector<int>, rules5e::Cover>{naturals, totals, cov};
    };
    auto open = arena({{"t.dragon", 2, 5, ""}, {kFighter, 6, 5, ""}});
    auto half = arena({{"t.dragon", 2, 5, ""}, {kFighter, 6, 5, ""}, {"t.victim", 4, 5, "bystander"}});
    auto [n0, t0, c0] = saves_of(open, false);
    auto [n1, t1, c1] = saves_of(half, false);
    REQUIRE(c0 == rules5e::Cover::None);
    REQUIRE(c1 == rules5e::Cover::Half);
    REQUIRE(n0.size() == 1);
    REQUIRE(n1.size() == 1);
    // The two boards roll different dice. The bonus is total minus the natural, and Wisdom adds no cover.
    CHECK(t0[0] - n0[0] == t1[0] - n1[0]);
    std::vector<Vec2i> closed;
    for (int y = 0; y < 32; ++y) closed.push_back({4, y});
    auto blocked = arena({{"t.dragon", 2, 5, ""}, {kFighter, 6, 5, ""}}, closed);
    auto [nb, tb, cb] = saves_of(blocked, false);
    CHECK(cb == rules5e::Cover::Total);
    CHECK(nb.size() == 1);   // Q6a: a total-cover creature still saves

    // Wing Attack is 10 ft. Place the fighter two tiles away (10 ft) with a body between for half cover.
    auto wopen = arena({{"t.dragon", 2, 5, ""}, {kFighter, 4, 5, ""}});
    auto whalf = arena({{"t.dragon", 2, 5, ""}, {kFighter, 4, 6, ""}, {"t.victim", 3, 6, "bystander"}});
    auto [wn0, wt0, wc0] = saves_of(wopen, true);
    auto [wn1, wt1, wc1] = saves_of(whalf, true);
    REQUIRE(wn0.size() == 1);
    REQUIRE(wn1.size() == 1);
    CHECK(wc0 == rules5e::Cover::None);
    REQUIRE(wc1 == rules5e::Cover::Half);
    CHECK((wt1[0] - wn1[0]) - (wt0[0] - wn0[0]) == 2);   // Dexterity: half cover is +2
    auto wthree = arena({{"t.dragon", 3, 3, ""}, {kFighter, 5, 5, ""}}, {{4, 4}});
    auto [wn3, wt3, wc3] = saves_of(wthree, true);
    REQUIRE(wn3.size() == 1);
    REQUIRE(wc3 == rules5e::Cover::ThreeQuarters);
    CHECK((wt3[0] - wn3[0]) - (wt0[0] - wn0[0]) == 5);   // Dexterity: three-quarters cover is +5
    auto wblocked = arena({{"t.dragon", 2, 5, ""}, {kFighter, 4, 5, ""}}, {{3, 5}, {3, 4}, {3, 6}});
    auto [wnb, wtb, wcb] = saves_of(wblocked, true);
    if (wcb == rules5e::Cover::Total) CHECK(wnb.empty());   // Q6b: no save and no event
    (void)wtb;
}

TEST_CASE("C-D 8-9: Wing Attack damages and knocks prone, a flyer falls, the dragon does not move, cost 2, empty range") {
    auto lib = base_lib(30, false);
    auto ch = arena({{"t.dragon", 4, 4, ""}, {"t.flyer", 5, 4, "foes"}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 8);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
    REQUIRE(c->set_altitude(v, 10, kAltForced));   // 3D distance stays within the 10 ft radius
    const Vec2i before = c->pos(d);
    offer(*c, d);
    CHECK(rules5e::find(c->state(), d)->la_cur == 3);
    Command w;
    w.request = {d, ActionKind::LegendaryAction};
    w.request.feature_id = 3;
    const std::size_t ev0 = c->events().size();
    REQUIRE(go(*c, w).ok);
    CHECK(c->pos(d) == before);
    CHECK(rules5e::find(c->state(), v)->has(Condition::Prone));
    CHECK(rules5e::find(c->state(), v)->alt_ft == 0);
    bool fell = false, damaged = false, effect = false;
    for (std::size_t i = ev0; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind == SimEvent::Kind::AltitudeChanged && e.actor == v && e.rules.b == kAltFall) fell = true;
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::DamageDealt && e.rules.target == v) damaged = true;
    }
    for (const auto& e : c->state().effects) if (e.target == v) effect = true;
    CHECK(fell);
    CHECK(damaged);
    CHECK_FALSE(effect);   // untracked prone: no effect entry
    CHECK(rules5e::find(c->state(), d)->la_cur == 1);
    Command again = w;
    auto refused = go(*c, again);
    CHECK_FALSE(refused.ok);
    CHECK(rules5e::find(c->state(), d)->la_cur == 1);

    auto alone = arena({{"t.dragon", 4, 4, ""}, {"t.victim", 4, 29, "foes"}});   // 125 ft: outside the 10 ft radius
    ecs::Registry reg2;
    auto c2 = Combat::create(reg2, alone, lib, 8);
    REQUIRE(c2);
    const auto d2 = alone.spawns[0].stable_id;
    offer(*c2, d2);
    auto empty = go(*c2, w);
    CHECK_FALSE(empty.ok);
    CHECK(empty.error.find("no creature within range") != std::string::npos);
    CHECK(rules5e::find(c2->state(), d2)->la_cur == 3);
}

TEST_CASE("C-D 10-11: untimed prone compiles, other untimed conditions do not, lead_action arms and its errors fire") {
    auto lib = base_lib();
    const auto* arch = lib.find("t.dragon");
    REQUIRE(arch);
    const auto* fp = lib.spells()->find(rules5e::spell_id_of("t.dragon#frightful_presence"));
    REQUIRE(fp);
    const uint32_t fp_id = fp->id;
    CHECK(fp->within_ft == 120);
    CHECK(fp->total_cover_saves);
    CHECK(fp->chooses_targets);
    CHECK(fp->aware_only);
    CHECK_FALSE(fp->repeat_los_disadvantage);
    const auto* wing_def = lib.spells()->find(rules5e::spell_id_of("t.dragon#wing_attack"));
    REQUIRE(wing_def);
    CHECK(wing_def->condition == Condition::Prone);
    CHECK(wing_def->condition_untracked);
    CHECK(wing_def->within_ft == 10);
    CHECK_FALSE(wing_def->total_cover_saves);
    REQUIRE(arch->base.multiattacks.size() == 2);
    CHECK(arch->base.multiattacks[0].after_action == 0);
    CHECK(arch->base.multiattacks[1].after_action == fp->id);
    CHECK(arch->base.multiattacks[1].attacks == arch->base.multiattacks[0].attacks);

    Json bad = dragon("t.bad_prone", 11, false);
    bad["$schema"] = "monster@5";
    bad.erase("legendary_actions");
    bad.erase("legendary_actions_per_round");
    bad["traits"] = Json::array();
    bad["actions"] = Json::array({{{"id", "wing"}, {"name", "Wing"}, {"kind", "save_effect"},
                                   {"save", {{"ability", "dex"}, {"dc", 11}}},
                                   {"apply", Json::array({{{"condition", "poisoned"}}})}}});
    REQUIRE_FALSE(lib.add(bad, "monster"));
    CHECK_FALSE(lib.find("t.bad_prone")->unsupported_features.empty());

    Json leg = dragon("t.bad_leg", 11, false);
    leg["legendary_actions"] = Json::array({{{"id", "bad"}, {"name", "Bad"}, {"description", "no"},
                                             {"effect", {{"kind", "save_effect"}, {"save", {{"ability", "dex"}, {"dc", 11}}},
                                                         {"apply", Json::array({{{"condition", "poisoned"}}})}}}}});
    auto err = lib.add(leg, "monster");
    REQUIRE(err);
    CHECK(err->find("not usable") != std::string::npos);

    Json esc = dragon("t.esc", 11, false);
    esc.erase("legendary_actions");
    esc.erase("legendary_actions_per_round");
    esc["traits"] = Json::array();
    esc["actions"] = Json::array({{{"id", "breath"}, {"name", "Breath"}, {"kind", "save_effect"},
                                   {"save", {{"ability", "con"}, {"dc", 13}}},
                                   {"apply", Json::array({{{"condition", "restrained"}, {"repeat_save", "end_of_turn"}}})},
                                   {"escalates", true}}});
    REQUIRE_FALSE(lib.add(esc, "monster"));
    CHECK(lib.spells()->find(rules5e::spell_id_of("t.esc#breath"))->escalates);

    Json rider = dragon("t.rider", 11, false);
    rider.erase("legendary_actions");
    rider.erase("legendary_actions_per_round");
    rider["actions"] = Json::array({melee("bite", "Bite"),
                                    {{"id", "tentacles"}, {"name", "Tentacles"}, {"kind", "save_effect"}, {"rider_only", true},
                                     {"save", {{"ability", "con"}, {"dc", 13}}},
                                     {"apply", Json::array({{{"condition", "poisoned"},
                                                             {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                                             {"repeat_save", "end_of_turn"}}})}}});
    rider["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
                                    {"effect", {{"kind", "multiattack"},
                                                {"options", Json::array({{{"id", "pincers"},
                                                                          {"attacks", Json::array({"bite", "bite"})},
                                                                          {"free_rider", "tentacles"}}})}}}}});
    REQUIRE_FALSE(lib.add(rider, "monster"));
    CHECK(lib.find("t.rider")->save_actions[0].rider_only);

    auto miss = lib.add([&] {
        Json m = dragon("t.miss", 11, false);
        m["traits"][0]["effect"]["options"][0]["lead_action"] = "nope";
        return m;
    }(), "monster");
    REQUIRE(miss);
    CHECK(miss->find("lead_action names no save_effect of this creature") != std::string::npos);
    auto twice = lib.add([&] {
        Json m = dragon("t.twice", 11, false);
        m["traits"][0]["effect"]["options"].push_back(m["traits"][0]["effect"]["options"][0]);
        m["traits"][0]["effect"]["options"][1]["id"] = "other";
        return m;
    }(), "monster");
    REQUIRE(twice);
    CHECK(twice->find("lead_action: one option per action") != std::string::npos);
    auto mixed = lib.add([&] {
        Json m = dragon("t.mixed", 11, false);
        m["traits"][0]["effect"]["options"][0]["free_rider"] = "frightful_presence";
        return m;
    }(), "monster");
    REQUIRE(mixed);
    CHECK(mixed->find("lead_action takes no free_rider, count_dice or engulf_both") != std::string::npos);
    auto old = lib.add([&] {
        Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
        m["$schema"] = "monster@4";
        m["id"] = "t.old";
        m["actions"] = Json::array({melee("bite", "Bite"),
                                    {{"id", "presence"}, {"name", "Presence"}, {"kind", "save_effect"},
                                     {"save", {{"ability", "wis"}, {"dc", 11}}},
                                     {"damage", Json::array({{{"dice", "1d6"}, {"type", "psychic"}}})}}});
        m["traits"].push_back({{"id", "multiattack"}, {"name", "Multiattack"},
                               {"effect", {{"kind", "multiattack"},
                                           {"options", Json::array({{{"id", "plain"},
                                                                     {"attacks", Json::array({"bite", "bite"})},
                                                                     {"lead_action", "presence"}}})}}}});
        return m;
    }(), "monster");
    REQUIRE(old);
    CHECK_MESSAGE(old->find("lead_action needs a monster@5 file") != std::string::npos, *old);

    auto ch = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 12);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
    to_actor(*c, d);
    bool plain = false, lead = false, fp_listed = false;
    for (const auto& q : c->legal_actions()) {
        if (q.kind == ActionKind::Attack && q.multiattack == 1) plain = true;
        if (q.kind == ActionKind::Attack && q.multiattack == 2) lead = true;
        if (q.kind == ActionKind::CastSpell && q.spell_id == fp_id && q.target == 0) fp_listed = true;
    }
    CHECK(plain);
    CHECK_FALSE(lead);
    if (!fp_listed) {
        std::string seen;
        for (const auto& q : c->legal_actions())
            if (q.kind == ActionKind::CastSpell) seen += std::to_string(q.spell_id) + ":" + std::to_string(q.target) + " ";
        FAIL("FP not listed; casts " << seen);
    }
    Command direct;
    direct.request = {d, ActionKind::Attack, v, arch->base.multiattacks[1].attacks[0]};
    direct.request.multiattack = 2;
    auto no = go(*c, direct);
    CHECK_FALSE(no.ok);
    CHECK(no.error.find("bad multiattack") != std::string::npos);
    CHECK(c->state().budget.action);
    REQUIRE(go(*c, cast_fp(d)).ok);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.sequence == 2);
    CHECK(c->state().budget.attacks_remaining == 3);
    Command bite;
    bite.request = {d, ActionKind::Attack, v, arch->base.multiattacks[1].attacks[0]};
    REQUIRE(go(*c, bite).ok);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.attacks_remaining == 2);
}

TEST_CASE("C-D 12: a legendary tail is refused for total cover, the charmer, and a gate") {
    auto lib = base_lib();
    std::vector<Vec2i> wall;
    for (int y = 0; y < 32; ++y) wall.push_back({3, y});
    auto ch = arena({{"t.dragon", 2, 5, ""}, {kFighter, 4, 5, ""}, {kFighter, 2, 6, ""}, {kFighter, 2, 4, ""}}, wall);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 13);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, covered = ch.spawns[1].stable_id, near = ch.spawns[2].stable_id,
               gated = ch.spawns[3].stable_id;
    CHECK(c->cover_of(rules5e::CoverQuery{d, covered, false, 0, false, 0, 0}) == rules5e::Cover::Total);
    CombatTestAccess::state(*c).sourced.push_back({Condition::Charmed, d, near, 0});
    for (auto& k : CombatTestAccess::state(*c).combatants)
        if (k.id == d) k.conditions |= rules5e::bit(Condition::Charmed);
    offer(*c, d);
    const int la = rules5e::find(c->state(), d)->la_cur;
    auto listed = [&](rules5e::CombatantId t, uint32_t option) {
        for (const auto& q : c->legal_actions())
            if (q.kind == ActionKind::LegendaryAction && q.feature_id == option && q.target == t) return true;
        return false;
    };
    CHECK_FALSE(listed(covered, 1));
    CHECK_FALSE(listed(near, 1));
    CHECK_FALSE(listed(gated, 2));
    Command tail;
    tail.request = {d, ActionKind::LegendaryAction, covered};
    tail.request.feature_id = 1;
    auto cov = go(*c, tail);
    CHECK_FALSE(cov.ok);
    CHECK(cov.error.find("total cover") != std::string::npos);
    Command charm;
    charm.request = {d, ActionKind::LegendaryAction, near};
    charm.request.feature_id = 1;
    auto chm = go(*c, charm);
    CHECK_FALSE(chm.ok);
    CHECK(chm.error.find("charmer") != std::string::npos);
    Command gate;
    gate.request = {d, ActionKind::LegendaryAction, gated};
    gate.request.feature_id = 2;
    auto g = go(*c, gate);
    CHECK_FALSE(g.ok);
    CHECK(g.error.find("grappled") != std::string::npos);
    CHECK(rules5e::find(c->state(), d)->la_cur == la);
}

TEST_CASE("C-D 12a: Sanctuary on a legendary tail matches the own-turn ward save") {
    auto lib = base_lib();
    auto ch = arena({{"t.dragon", 4, 4, ""}, {kFighter, 5, 4, ""}, {kFighter, 4, 5, ""}});
    auto ward = [](Combat& c, rules5e::CombatantId who, int dc) {
        rules5e::ActiveSpellEffect e{};
        e.spell_id = rules5e::spell_id_of("srd51.spell.sanctuary");
        e.caster = who;
        e.target = who;
        e.condition = Condition::Warded;
        e.save_ability = rules5e::Ability::Wis;
        e.dc = int16_t(dc);
        CombatTestAccess::state(c).effects.push_back(e);
        for (auto& k : CombatTestAccess::state(c).combatants)
            if (k.id == who) k.conditions |= rules5e::bit(Condition::Warded);
    };
    auto natural_of = [](const Combat& c, rules5e::CombatantId dragon) {
        for (const auto& e : c.events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == dragon &&
                (e.rules.c & 4) != 0)
                return e.rules.a;
        return -1;
    };
    ecs::Registry reg_a;
    auto own = Combat::create(reg_a, ch, lib, 15);
    REQUIRE(own);
    const auto d = ch.spawns[0].stable_id, warded = ch.spawns[1].stable_id, alt = ch.spawns[2].stable_id;
    ward(*own, warded, 30);
    to_actor(*own, d);
    const uint8_t tail = 2;   // bite, claw, tail
    Command atk;
    atk.request = {d, ActionKind::Attack, warded, tail};
    REQUIRE(go(*own, atk).ok);
    const int own_nat = natural_of(*own, d);
    CHECK(own_nat >= 1);

    ecs::Registry reg_b;
    auto leg = Combat::create(reg_b, ch, lib, 15);
    REQUIRE(leg);
    ward(*leg, warded, 30);
    offer(*leg, d);
    const int la = rules5e::find(leg->state(), d)->la_cur;
    Command use;
    use.request = {d, ActionKind::LegendaryAction, warded};
    use.request.feature_id = 1;
    const std::size_t ev0 = leg->events().size();
    REQUIRE(go(*leg, use).ok);
    CHECK(natural_of(*leg, d) == own_nat);
    CHECK_FALSE(has_kind(*leg, EventKind::AttackResolved, ev0));
    CHECK(rules5e::find(leg->state(), d)->la_cur == la - 1);
    CHECK(has_kind(*leg, EventKind::LegendaryActionUsed, ev0));

    ecs::Registry reg_c;
    auto hit = Combat::create(reg_c, ch, lib, 16);
    REQUIRE(hit);
    ward(*hit, warded, 1);
    offer(*hit, d);
    const std::size_t ev1 = hit->events().size();
    REQUIRE(go(*hit, use).ok);
    CHECK(natural_of(*hit, d) >= 1);
    CHECK(has_kind(*hit, EventKind::AttackResolved, ev1));

    ecs::Registry reg_d;
    auto redirected = Combat::create(reg_d, ch, lib, 15);
    REQUIRE(redirected);
    ward(*redirected, warded, 30);
    offer(*redirected, d);
    Command alt_use = use;
    alt_use.request.ward_alternate = alt;
    const std::size_t ev2 = redirected->events().size();
    REQUIRE(go(*redirected, alt_use).ok);
    bool alt_hit = false;
    for (std::size_t i = ev2; i < redirected->events().size(); ++i) {
        const auto& e = redirected->events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.target == alt) alt_hit = true;
    }
    CHECK(alt_hit);
    // A poked ward is not in the log. Replay is the cast: cleric Sanctuary, then the logged alternate.
    {
        auto board = arena({{"t.dragon", 4, 4, ""}, {"srd51.character.cleric_level_1", 6, 4, ""}, {kFighter, 5, 4, ""},
                            {kFighter, 4, 5, ""}});
        ecs::Registry reg_s;
        auto live = Combat::create(reg_s, board, lib, 19);
        REQUIRE(live);
        const auto dragon = board.spawns[0].stable_id, cleric = board.spawns[1].stable_id,
                   ward_t = board.spawns[2].stable_id, other = board.spawns[3].stable_id;
        to_actor(*live, cleric);
        Command sanc;
        sanc.request = {cleric, ActionKind::CastSpell, ward_t};
        sanc.request.spell_id = rules5e::spell_id_of("srd51.spell.sanctuary");
        sanc.request.spell_level = 1;
        sanc.request.targets = {ward_t};
        auto cast = go(*live, sanc);
        REQUIRE_MESSAGE(cast.ok, cast.error);
        offer(*live, dragon);
        Command hit_alt;
        hit_alt.request = {dragon, ActionKind::LegendaryAction, ward_t};
        hit_alt.request.feature_id = 1;
        hit_alt.request.ward_alternate = other;
        auto used = go(*live, hit_alt);
        REQUIRE_MESSAGE(used.ok, used.error);
        ecs::Registry reg_r;
        std::string err;
        auto rp = replay(reg_r, board, lib, 19, live->log(), &err);
        REQUIRE_MESSAGE(rp, err);
        CHECK(rp->hash() == live->hash());
        CHECK(rp->log_hash() == live->log_hash());
    }

    ecs::Registry reg_e;
    auto bare = Combat::create(reg_e, ch, lib, 17);
    REQUIRE(bare);
    offer(*bare, d);
    const int la2 = rules5e::find(bare->state(), d)->la_cur;
    Command unw = use;
    unw.request.ward_alternate = alt;
    auto bad = go(*bare, unw);
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.find("not warded") != std::string::npos);
    CHECK(rules5e::find(bare->state(), d)->la_cur == la2);
    ward(*bare, warded, 30);
    Command far = use;
    far.request.ward_alternate = warded;   // the warded creature is not an alternate
    auto bad2 = go(*bare, far);
    CHECK_FALSE(bad2.ok);
    CHECK(bad2.error.find("not a legal alternate") != std::string::npos);
    CHECK(rules5e::find(bare->state(), d)->la_cur == la2);
}

TEST_CASE("C-D 13: pins, wyrmling digest, schema 14, 0xEC free, pool 42") {
    CHECK(sim::kSimEventSchemaVersion == 14);
    {
        std::ifstream g(kRoot + "/scenarios/wyrmling_breath/golden.txt");
        REQUIRE(g.good());
        std::string all((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(all.find("digest e3a911c69efcd4bc") != std::string::npos);
    }
    auto contains = [](const std::filesystem::path& p, const char* needle) {
        std::ifstream in(p);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return all.find(needle) != std::string::npos;
    };
    int pin_files = 0;
    for (const char* rel : {"engine/core/sim/tests/legendary_actions_tests.cpp"}) {
        CHECK(contains(std::filesystem::path(kRoot) / rel, "0x40c5e7a2531bae06"));
        CHECK(contains(std::filesystem::path(kRoot) / rel, "0x67bca9677135717d"));
        ++pin_files;
    }
    CHECK(pin_files == 1);
    for (const auto& ent : std::filesystem::recursive_directory_iterator(kRoot + "/engine")) {
        if (!ent.is_regular_file()) continue;
        const auto name = ent.path().filename().string();
        if (name == "legendary_actions_tests.cpp" || name == "dragon_kit_tests.cpp") continue;
        const auto ext = ent.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp" && ext != ".md") continue;
        CHECK_FALSE(contains(ent.path(), "0x40c5e7a2531bae06"));
        CHECK_FALSE(contains(ent.path(), "0x67bca9677135717d"));
    }
    CHECK_FALSE(contains(std::filesystem::path(kRoot) / "engine/core/sim/src/combat.cpp", "byte(0xEC)"));
    auto lib = base_lib();
    const auto* couatl = lib.find("srd51.monster.couatl");
    REQUIRE(couatl);
    const Archetype::FormFeature* pool = nullptr;
    for (const auto& [id, feat] : couatl->form_features) {
        (void)id;
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) pool = &feat;
    }
    REQUIRE(pool);
    CHECK(pool->forms.size() == 42);
}
