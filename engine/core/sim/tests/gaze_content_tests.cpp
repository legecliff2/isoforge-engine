// Petrifying Gaze, sim side (rules #149, VERSION_BUMP_BATCH 7.2p hand-off): the loader's `gaze` feature kind (synthetic Save
// SpellDef + Combatant::Gaze), Combat::gaze_seen (line of sight, range_ft, not Blinded, gazer not Invisible) filled into
// GazeResponse at submit, the turn-start block, averting, the headless driver's answer on a surprised turn, replay, and the
// log_hash 0xBB / Combat::hash 0xAE folds (only when set).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json gaze_trait(int dc = 30, int range = 30, int margin = 5) {
    return {{"id", "petrifying_gaze"}, {"name", "Petrifying Gaze"}, {"description", "x"},
            {"effect", {{"kind", "gaze"}, {"range_ft", range}, {"save", {{"ability", "con"}, {"dc", dc}}},
                        {"condition", "restrained"}, {"repeat_save", "end_of_turn"}, {"escalates", true},
                        {"escalate_to", "petrified"}, {"escalate_margin", margin}}}};
}
// A goblin stat block standing in for the medusa (monster@4, Petrifying Gaze; DC 30 so the fighter always fails).
Json medusa(const std::string& id = "t.medusa", Json g = gaze_trait(), int declared = 4) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(declared);
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["traits"] = Json::array({g});
    return m;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(l.add(tank, "character"));
    const auto e = l.add(medusa(), "monster");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    return l;
}
world::Chunk arena(Vec2i gazer_at = {8, 5}, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight;
    c.spawns.push_back({kFighter, Vec2i{5, 5}, world::spawn_stable_id(c.coord, 0), ""});
    c.spawns.push_back({"t.medusa", gazer_at, world::spawn_stable_id(c.coord, 1), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
CombatantId fid(const world::Chunk& c) { return c.spawns[0].stable_id; }
CombatantId mid(const world::Chunk& c) { return c.spawns[1].stable_id; }
Command rules(CombatantId a, ActionKind k, bool avert = false) {
    Command x;
    x.request = {a, k};
    x.request.avert_gaze = avert;
    return x;
}
// Ends turns until `who` acts, answering a pending gaze by averting (no save, so nothing is petrified on the way; the
// averted eyes end at that creature's next turn start).
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        const CombatantId a = rules5e::current_actor(c.state());
        if (c.state().budget.gaze_pending) REQUIRE(c.submit(rules(a, ActionKind::GazeResponse, true)).ok);
        REQUIRE(c.submit(rules(a, ActionKind::EndTurn)).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
rules5e::Combatant* mut(rules5e::EncounterState& s, CombatantId id) {
    for (auto& k : s.combatants) if (k.id == id) return &k;
    return nullptr;
}
bool stoned(const Combat& c, CombatantId id) {
    const auto* k = rules5e::find(c.state(), id);
    return k->has(Condition::Restrained) || k->has(Condition::Petrified);
}
}  // namespace

TEST_CASE("gaze loader: compiles a Save SpellDef + Combatant::Gaze; bad shapes refuse; @3 stays inert") {
    auto l = lib();
    const Archetype* m = l.find("t.medusa");
    REQUIRE(m);
    REQUIRE(m->base.gazes.size() == 1);
    const auto gz = m->base.gazes[0];
    CHECK(gz.range_ft == 30);
    CHECK(gz.spell_id == rules5e::spell_id_of("t.medusa#petrifying_gaze"));
    CHECK(std::find(m->base.spells_known.begin(), m->base.spells_known.end(), gz.spell_id) == m->base.spells_known.end());
    const rules5e::SpellDef* d = l.spells()->find(gz.spell_id);
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::Save);
    CHECK_FALSE(d->is_spell);
    CHECK(d->save_ability == rules5e::Ability::Con);
    CHECK(d->fixed_dc == 30);
    CHECK(d->applies_condition);
    CHECK(d->condition == Condition::Restrained);
    CHECK(d->repeat_save == rules5e::RepeatSave::EndOfTurn);
    CHECK(d->escalates);
    CHECK(d->escalate_to == Condition::Petrified);
    CHECK(d->escalate_margin == 5);
    CHECK(l.find(kFighter)->base.gazes.empty());
    CHECK(l.find("srd51.monster.goblin")->base.gazes.empty());

    Json bad = gaze_trait();
    bad["effect"]["range_ft"] = 32;
    CHECK(l.add(medusa("t.bad1", bad), "monster").value_or("").find("bad range_ft") != std::string::npos);
    bad = gaze_trait();
    bad["effect"]["save"]["dc"] = 0;
    CHECK(l.add(medusa("t.bad2", bad), "monster").value_or("").find("bad save") != std::string::npos);
    bad = gaze_trait();
    bad["effect"]["escalate_to"] = "sleepy";
    CHECK(l.add(medusa("t.bad3", bad), "monster").value_or("").find("bad escalate_to") != std::string::npos);

    REQUIRE_FALSE(l.add(medusa("t.old", gaze_trait(), 3), "monster"));   // declared @3: listed inert, never compiled
    CHECK(l.find("t.old")->base.gazes.empty());
    CHECK(l.find("t.old")->inert_features == std::vector<std::string>{"petrifying_gaze (gaze)"});
}

TEST_CASE("gaze: a pending gaze blocks every other request until GazeResponse") {
    auto l = lib();
    const auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    to_actor(*c, mid(ch));
    REQUIRE_FALSE(c->state().budget.gaze_pending);   // the gazer itself has no other gazer to answer
    REQUIRE(c->submit(rules(mid(ch), ActionKind::EndTurn)).ok);
    REQUIRE(rules5e::current_actor(c->state()) == fid(ch));
    REQUIRE(c->state().budget.gaze_pending);
    const std::size_t logged = c->log().size();
    const std::string blocked = "gaze pending: answer with GazeResponse first";
    CHECK(c->submit(rules(fid(ch), ActionKind::Dodge)).error == blocked);
    CHECK(c->submit(rules(fid(ch), ActionKind::EndTurn)).error == blocked);
    Command st;
    st.kind = CommandKind::Step;
    st.to = {5, 6};
    CHECK(c->submit(st).error == blocked);
    CHECK(c->log().size() == logged);   // nothing logged by the refusals
    Command fake = rules(fid(ch), ActionKind::GazeResponse);
    fake.request.gaze_seen = {};   // the caller's list is replaced by the sim fact
    REQUIRE(c->submit(fake).ok);
    CHECK(c->log().back().step.request.gaze_seen == std::vector<CombatantId>{mid(ch)});
    CHECK_FALSE(c->state().budget.gaze_pending);
    CHECK(stoned(*c, fid(ch)));
    CHECK_FALSE(c->submit(rules(fid(ch), ActionKind::GazeResponse)).ok);   // answered once
}

TEST_CASE("gaze_seen: seen in range; not seen out of range, behind a wall, while Blinded, or the gazer Invisible") {
    auto l = lib();
    struct Case { const char* name; Vec2i at; std::vector<Vec2i> walls; Condition viewer_c; Condition gazer_c; bool seen; };
    const Condition none = Condition::Prone;   // a condition that does not touch sight (never applied below)
    const std::vector<Case> cases = {
        {"in range (30 ft)", {11, 5}, {}, none, none, true},
        {"out of range (35 ft)", {12, 5}, {}, none, none, false},
        {"no line of sight", {8, 5}, {{6, 4}, {6, 5}, {6, 6}}, none, none, false},
        {"blinded", {8, 5}, {}, Condition::Blinded, none, false},
        {"invisible gazer", {8, 5}, {}, none, Condition::Invisible, false},
    };
    for (const auto& k : cases) {
        CAPTURE(k.name);
        const auto ch = arena(k.at, k.walls);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 3);
        REQUIRE(c);
        to_actor(*c, mid(ch));
        auto& st = CombatTestAccess::state(*c);
        if (k.viewer_c != none) mut(st, fid(ch))->conditions |= rules5e::bit(k.viewer_c);
        if (k.gazer_c != none) mut(st, mid(ch))->conditions |= rules5e::bit(k.gazer_c);
        CHECK(c->gaze_seen(mid(ch)).empty());   // a gazer does not see itself
        REQUIRE(c->submit(rules(mid(ch), ActionKind::EndTurn)).ok);
        REQUIRE(c->state().budget.gaze_pending);   // pending regardless of sight: the answer carries it
        const auto seen = c->gaze_seen(fid(ch));
        CHECK(seen == (k.seen ? std::vector<CombatantId>{mid(ch)} : std::vector<CombatantId>{}));
        REQUIRE(c->submit(rules(fid(ch), ActionKind::GazeResponse)).ok);
        CHECK(c->log().back().step.request.gaze_seen == seen);
        CHECK(stoned(*c, fid(ch)) == k.seen);   // DC 30: a seen gazer always lands its gaze
    }
}

TEST_CASE("gaze: averting the eyes avoids the save; 0xBB / 0xAE fold only when set; gaze_seen is not folded") {
    auto l = lib();
    const auto ch = arena();
    ecs::Registry r1, r2;
    auto look = Combat::create(r1, ch, l, 3);
    auto avert = Combat::create(r2, ch, l, 3);
    REQUIRE(look); REQUIRE(avert);
    for (Combat* c : {&*look, &*avert}) {
        to_actor(*c, mid(ch));
        REQUIRE(c->submit(rules(mid(ch), ActionKind::EndTurn)).ok);
    }
    REQUIRE(look->submit(rules(fid(ch), ActionKind::GazeResponse)).ok);
    const auto ar = avert->submit(rules(fid(ch), ActionKind::GazeResponse, true));
    REQUIRE(ar.ok);
    CHECK(stoned(*look, fid(ch)));
    CHECK_FALSE(stoned(*avert, fid(ch)));
    CHECK(rules5e::find(avert->state(), fid(ch))->gaze_averted);
    CHECK_FALSE(rules5e::find(look->state(), fid(ch))->gaze_averted);
    CHECK(rules5e::has_gaze_state(avert->state()));
    CHECK_FALSE(rules5e::has_gaze_state(look->state()));   // nobody averted: 0xAE is not folded

    // Combat::hash: the averted id is folded (0xAE); clearing it changes the hash.
    const uint64_t h1 = avert->hash();
    mut(CombatTestAccess::state(*avert), fid(ch))->gaze_averted = false;
    CHECK(avert->hash() != h1);
    mut(CombatTestAccess::state(*avert), fid(ch))->gaze_averted = true;
    CHECK(avert->hash() == h1);
    // log_hash: the avert byte is folded (0xBB) only when set; gaze_seen never is.
    auto& lg = CombatTestAccess::log(*avert);
    const uint64_t l1 = avert->log_hash();
    lg.back().step.request.avert_gaze = false;
    const uint64_t l0 = avert->log_hash();
    CHECK(l0 != l1);
    lg.back().step.request.gaze_seen.clear();
    CHECK(avert->log_hash() == l0);
    lg.back().step.request.avert_gaze = true;
    lg.back().step.request.gaze_seen = {mid(ch)};
    CHECK(avert->log_hash() == l1);

    // The averted eyes end at the start of its next turn: 0xAE drops out again.
    REQUIRE(avert->submit(rules(fid(ch), ActionKind::EndTurn)).ok);
    to_actor(*avert, fid(ch));
    CHECK_FALSE(rules5e::find(avert->state(), fid(ch))->gaze_averted);
    CHECK_FALSE(rules5e::has_gaze_state(avert->state()));
}

TEST_CASE("gaze driver: a surprised creature answers GazeResponse (not averting), then EndTurn; run_to_end never stalls") {
    auto l = lib();
    const auto ch = arena();
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    REQUIRE(c->apply_surprise({fid(ch)}));
    for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != fid(ch); ++i)
        REQUIRE(c->submit(next_command(*c, std::nullopt)).ok);
    REQUIRE(rules5e::current_actor(c->state()) == fid(ch));
    REQUIRE(rules5e::find(c->state(), fid(ch))->surprised);
    REQUIRE(c->state().budget.gaze_pending);
    CHECK(c->submit(rules(fid(ch), ActionKind::GazeResponse, true)).error == "surprised: cannot avert the eyes");
    const Command g = next_command(*c, mid(ch));
    CHECK(g.request.kind == ActionKind::GazeResponse);
    CHECK_FALSE(g.request.avert_gaze);
    REQUIRE(c->submit(g).ok);
    CHECK(stoned(*c, fid(ch)));   // a surprised creature still saves
    const Command e = next_command(*c, mid(ch));
    CHECK(e.request.kind == ActionKind::EndTurn);
    REQUIRE(c->submit(e).ok);

    ecs::Registry reg2;
    auto d = Combat::create(reg2, ch, l, 4);
    REQUIRE(d);
    REQUIRE(d->apply_surprise({fid(ch)}));
    NearestEnemyPolicy pol;
    const auto rr = run_to_end(*d, [&](CombatantId) -> IActorPolicy* { return &pol; }, 120);
    CHECK((rr.ended || rr.error == "command limit reached"));
    bool answered = false;
    for (const auto& le : d->log()) answered = answered || le.step.request.kind == ActionKind::GazeResponse;
    CHECK(answered);
}

TEST_CASE("gaze: replay reproduces hash and log_hash (looking, averting, surprised)") {
    auto l = lib();
    const auto ch = arena();
    ecs::Registry reg, reg2;
    auto c = Combat::create(reg, ch, l, 11);
    REQUIRE(c);
    to_actor(*c, mid(ch));
    REQUIRE(c->submit(rules(mid(ch), ActionKind::EndTurn)).ok);
    REQUIRE(c->submit(rules(fid(ch), ActionKind::GazeResponse, true)).ok);
    REQUIRE(c->submit(rules(fid(ch), ActionKind::EndTurn)).ok);
    to_actor(*c, fid(ch));   // the next turn looks
    REQUIRE(c->submit(rules(fid(ch), ActionKind::GazeResponse)).ok);
    REQUIRE(c->submit(rules(fid(ch), ActionKind::EndTurn)).ok);
    std::string err;
    auto r = replay(reg2, ch, l, 11, c->log(), &err);
    REQUIRE_MESSAGE(r, err);
    CHECK(r->hash() == c->hash());
    CHECK(r->log_hash() == c->log_hash());
    CHECK(stoned(*r, fid(ch)) == stoned(*c, fid(ch)));
}
