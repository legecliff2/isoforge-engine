// Visible figure content (CONTENT_CONVENTIONS §52). No green_hag.json.
// 0xE7 folds while a figure or a discernment pair exists. Replay reproduces the hash.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json figure_action(int dc = 1) {
    return {{"id", "illusory_appearance"}, {"name", "Illusory Appearance"}, {"kind", "illusory_appearance"}, {"dc", dc}};
}
Json monster_doc(const std::string& id, std::vector<Json> actions, int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["type"] = "fey";
    m["hit_points"]["average"] = 40;
    m["actions"] = Json::array();
    for (auto& a : actions) m["actions"].push_back(std::move(a));
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) {
    return lib.add(m, "monster", "", version);
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 12 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
SubmitResult submit(Combat& c, Command cmd) {
    auto r = c.submit(cmd);
    while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
    return r;
}
}  // namespace

TEST_CASE("visible figure loader: monster@5 action, refused below monster@5 and on a character") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.hag", {figure_action(20)})));
    const auto* arch = lib.find("t.hag");
    REQUIRE(arch);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].illusory);
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.hag#illusory_appearance"));
    REQUIRE(d);
    CHECK(d->illusory_appearance);
    CHECK(d->fixed_dc == 20);
    CHECK_FALSE(d->is_spell);
    Json extra = figure_action();
    extra["usage"] = "1/day";
    const auto key = add(lib, monster_doc("t.bad", {extra}));
    REQUIRE(key);
    CHECK(key->find("illusory appearance: unknown key 'usage'") != std::string::npos);
    const auto old = add(lib, monster_doc("t.old", {figure_action()}, 4), 4);
    REQUIRE(old);
    CHECK(old->find("illusory appearance needs a monster@5 file") != std::string::npos);
    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.figure_pc";
    ch["actions"].push_back(figure_action());
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("illusory appearance: only valid on monsters") != std::string::npos);
    const auto range = add(lib, monster_doc("t.range", {figure_action(0)}));
    REQUIRE(range);
    CHECK(range->find("illusory appearance: dc must be 1..30") != std::string::npos);

    std::ifstream hag(kRoot + "/content/packs/srd51.core/monsters/green_hag.json");
    CHECK_FALSE(hag.good());
}

TEST_CASE("visible figure: 0xE7 tracks the figure and a discernment that outlives it") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.hag", {figure_action(1)})));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.hag", {2, 2}}, {"t.foe", {4, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ch.spawns[1].overrides_json = R"({"team":"foes"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const uint64_t before = c->hash();
    const CombatantId hag = ch.spawns[0].stable_id;
    const CombatantId foe_id = ch.spawns[1].stable_id;
    advance_to(*c, hag);
    Command cast;
    cast.request = {hag, ActionKind::CastSpell, hag};
    cast.request.spell_id = rules5e::spell_id_of("t.hag#illusory_appearance");
    auto r = submit(*c, cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    const uint64_t up = c->hash();
    CHECK(up != before);
    CHECK(rules5e::visible_figure(c->state(), foe_id, hag).active);
    Command again = cast;
    auto refused = submit(*c, again);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "illusory appearance: already disguised");
    CHECK(c->hash() == up);

    advance_to(*c, foe_id);
    Command look;
    look.request = {foe_id, ActionKind::InspectFigure, hag};
    auto saw = submit(*c, look);
    REQUIRE_MESSAGE(saw.ok, saw.error);
    const uint64_t known = c->hash();
    CHECK(known != up);
    CHECK(rules5e::visible_figure(c->state(), foe_id, hag).discerned);

    advance_to(*c, hag);
    Command end;
    end.request = cast.request;
    end.request.as_bonus_action = true;
    auto ended = submit(*c, end);
    REQUIRE_MESSAGE(ended.ok, ended.error);
    CHECK_FALSE(rules5e::visible_figure(c->state(), foe_id, hag).active);
    CHECK(rules5e::visible_figure(c->state(), foe_id, hag).discerned);
    const uint64_t memory = c->hash();
    CHECK(memory != before);
    CHECK(memory != up);

    auto restarted = submit(*c, cast);
    REQUIRE_MESSAGE(restarted.ok, restarted.error);
    CHECK(rules5e::visible_figure(c->state(), foe_id, hag).active);
    CHECK(rules5e::visible_figure(c->state(), foe_id, hag).discerned);
    CHECK(c->hash() != memory);

    std::string err;
    ecs::Registry reg2;
    auto replayed = replay(reg2, ch, lib, c->seed(), c->log(), &err);
    REQUIRE_MESSAGE(replayed, err);
    CHECK(replayed->hash() == c->hash());
    CHECK(rules5e::visible_figure(replayed->state(), foe_id, hag).discerned);
    CHECK(rules5e::visible_figure(replayed->state(), foe_id, hag).active);
}
