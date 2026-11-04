// B19 content: monster@5 action etherealness. No ghost.json.
// The action toggles Combatant::plane. 0xE6 folds only while some combatant is not Material.
#include <doctest/doctest.h>

#include <algorithm>
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
using rules5e::EventKind;
using rules5e::Plane;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json ethereal_action() {
    return {{"id", "etherealness"}, {"name", "Etherealness"}, {"kind", "etherealness"}};
}
Json visage() {
    return {{"id", "horrifying_visage"}, {"name", "Horrifying Visage"}, {"kind", "save_effect"},
            {"origin", "self"}, {"area", {{"shape", "sphere"}, {"size_ft", 60}}},
            {"save", {{"ability", "wis"}, {"dc", 30}}}, {"target_must_see_source", true},
            {"exclude_creature_types", Json::array({"undead"})},
            {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}}, {"immune_on_end", true},
            {"age_on_fail_by", {{"margin", 5}, {"dice", "1d4"}, {"years_per", 10}}},
            {"apply", Json::array({{{"condition", "frightened"},
                                    {"duration", {{"kind", "rounds"}, {"n", 10}}},
                                    {"repeat_save", "end_of_turn"}}})}};
}
Json monster_doc(const std::string& id, std::vector<Json> actions, int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["type"] = "undead";
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
int saves_for(const Combat& c, CombatantId who) {
    int n = 0;
    for (const auto& ev : c.events()) {
        if (ev.kind != SimEvent::Kind::Rules || ev.rules.kind != EventKind::SavingThrowRolled) continue;
        if (ev.rules.actor == who) ++n;
    }
    return n;
}
}  // namespace

TEST_CASE("etherealness loader: monster@5 action, refused below monster@5 and on a character") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.shade", {ethereal_action()})));
    const auto* arch = lib.find("t.shade");
    REQUIRE(arch);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].ethereal);
    CHECK_FALSE(arch->save_actions[0].area);
    CHECK(arch->save_actions[0].range_ft == 0);
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.shade#etherealness"));
    REQUIRE(d);
    CHECK(d->ethereal_toggle);
    CHECK_FALSE(d->is_spell);
    CHECK_FALSE(d->magical);
    CHECK(arch->base.plane == Plane::Material);

    Json extra = ethereal_action();
    extra["usage"] = {{"kind", "recharge"}, {"recharge", "5-6"}};
    const auto key = add(lib, monster_doc("t.key", {extra}));
    REQUIRE(key);
    CHECK(key->find("etherealness: unknown key 'usage'") != std::string::npos);
    const auto old = add(lib, monster_doc("t.old", {ethereal_action()}, 4), 4);
    REQUIRE(old);
    CHECK(old->find("etherealness needs a monster@5 file") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.ethereal_pc";
    ch["actions"].push_back(ethereal_action());
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("etherealness: only valid on monsters") != std::string::npos);

    std::ifstream ghost(kRoot + "/content/packs/srd51.core/monsters/ghost.json");
    CHECK_FALSE(ghost.good());
}

TEST_CASE("etherealness: toggle changes 0xE6 and returns to the all-Material hash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.shade", {ethereal_action()})));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.shade", {2, 2}}, {"t.foe", {4, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ch.spawns[1].overrides_json = R"({"team":"foes"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const uint64_t material = c->hash();
    const CombatantId shade = ch.spawns[0].stable_id;
    auto* g = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), shade));
    REQUIRE(g->plane == Plane::Material);
    g->plane = Plane::BorderEthereal;
    const uint64_t shifted = c->hash();
    CHECK(shifted != material);
    g->plane = Plane::Material;
    CHECK(c->hash() == material);

    advance_to(*c, shade);
    Command cast;
    cast.request = {shade, ActionKind::CastSpell, shade};
    cast.request.spell_id = rules5e::spell_id_of("t.shade#etherealness");
    auto r = c->submit(cast);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find(c->state(), shade)->plane == Plane::BorderEthereal);
    const uint64_t ethereal = c->hash();
    CHECK(ethereal != material);
    Command done;
    done.request = {shade, ActionKind::EndTurn};
    auto ended = c->submit(done);
    while (ended.needs) ended = c->decide({ended.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(ended.ok, ended.error);
    advance_to(*c, shade);
    r = c->submit(cast);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rules5e::find(c->state(), shade)->plane == Plane::Material);
    CHECK(c->hash() != ethereal);

    std::string err;
    ecs::Registry reg2;
    auto again = replay(reg2, ch, lib, c->seed(), c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    CHECK(rules5e::find(again->state(), shade)->plane == Plane::Material);
}

TEST_CASE("etherealness: a material attack is refused and an ethereal visage skips the material creature") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json hack = load_json("content/packs/srd51.core/monsters/goblin.json");
    hack["$schema"] = "monster@5";
    hack["id"] = "t.striker";
    hack["actions"][0]["to_hit"] = 30;
    REQUIRE_FALSE(add(lib, hack));
    REQUIRE_FALSE(add(lib, monster_doc("t.shade", {ethereal_action(), visage()})));
    auto ch = arena({{"t.striker", {2, 2}}, {"t.shade", {3, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"monsters"})";
    ch.spawns[1].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    const CombatantId striker = ch.spawns[0].stable_id;
    const CombatantId shade = ch.spawns[1].stable_id;
    auto* ghost = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), shade));
    ghost->plane = Plane::BorderEthereal;
    advance_to(*c, striker);
    Command atk;
    atk.request = {striker, ActionKind::Attack, shade, 0};
    auto refused = c->submit(atk);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "can't affect a creature on the other plane");
    CHECK(rules5e::find(c->state(), shade)->hp == 40);

    const auto* def = lib.spells()->find(rules5e::spell_id_of("t.shade#horrifying_visage"));
    REQUIRE(def);
    const auto inside = c->area_targets(shade, *def, c->pos(striker));
    CHECK(std::find(inside.begin(), inside.end(), striker) != inside.end());
    const rules5e::Sight seen = rules5e::sight(c->state(), striker, shade);
    CHECK(seen.can_see);
    CHECK_FALSE(rules5e::find(c->state(), shade)->has(rules5e::Condition::Invisible));

    advance_to(*c, shade);
    const int before = saves_for(*c, striker);
    Command cast;
    cast.aim = true;
    cast.to = c->pos(striker);
    cast.request = {shade, ActionKind::CastSpell};
    cast.request.spell_id = def->id;
    auto r = c->submit(cast);
    while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(saves_for(*c, striker) == before);
    CHECK_FALSE(rules5e::find(c->state(), striker)->has(rules5e::Condition::Frightened));
}
