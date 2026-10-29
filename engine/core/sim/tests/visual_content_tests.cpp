// Engine Core M0: optional visual on monster / character, identity migration to @6,
// presentation defaults, art-root existence, and Combat::hash / log_hash unchanged
// with or without the visual block.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "assets/pack_set.hpp"
#include "sim/combat.hpp"
#include "sim/presentation.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

assets::LoadResult migrate(const Json& doc) {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    return reg.load(doc);
}

world::Chunk arena(const std::vector<std::string>& archetypes) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < archetypes.size(); ++i)
        c.spawns.push_back({archetypes[i], Vec2i{10 + int(i), 10}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

struct Hashes { uint64_t hash = 0, log = 0; };
Hashes fight(const Json& fighter, const Json& goblin, uint64_t seed) {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(goblin, "monster"));
    REQUIRE_FALSE(lib.add(fighter, "character"));
    ecs::Registry reg;
    auto chunk = arena({fighter["id"], goblin["id"]});
    auto c = Combat::create(reg, chunk, lib, seed);
    REQUIRE(c);
    const CombatantId f = chunk.spawns[0].stable_id, g = chunk.spawns[1].stable_id;
    for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != f; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        auto r = c->submit(e);
        while (r.needs) r = c->decide({r.needs->window_seq, true, 0});
    }
    Command a;
    a.request = {f, ActionKind::Attack, g};
    a.request.attack_index = 0;
    auto r = c->submit(a);
    REQUIRE_MESSAGE(r.ok, r.error);
    while (r.needs) r = c->decide({r.needs->window_seq, true, 0});
    return {c->hash(), c->log_hash()};
}
} // namespace

TEST_CASE("monster@6 / character@6: identity migration leaves the body unchanged") {
    auto g5 = migrate(load_json("content/packs/srd51.core/monsters/goblin.json"));
    auto w5 = migrate(load_json("content/packs/srd51.core/monsters/wolf.json"));
    REQUIRE(g5.ok);
    REQUIRE(w5.ok);
    CHECK(g5.current.version == 6);
    CHECK(w5.data["$schema"] == "monster@6");
    Json tagged = w5.data;
    tagged["$schema"] = "monster@5";
    auto again = migrate(tagged);
    REQUIRE(again.ok);
    CHECK(again.data == w5.data);
    Json bare = w5.data;
    bare.erase("$schema");
    Json src = load_json("content/packs/srd51.core/monsters/wolf.json");
    src.erase("$schema");
    CHECK(bare == src);
    CHECK_FALSE(kSizeShortRuntime);
}

TEST_CASE("visual defaults: humanoid paperdoll, beast stand-in bin, huge unbinned") {
    ContentLibrary lib;
    auto gob = migrate(load_json("content/packs/srd51.core/monsters/goblin.json"));
    auto wolf = migrate(load_json("content/packs/srd51.core/monsters/wolf.json"));
    REQUIRE(gob.ok);
    REQUIRE(wolf.ok);
    REQUIRE_FALSE(lib.add(gob.data, "monster"));
    REQUIRE_FALSE(lib.add(wolf.data, "monster"));
    const Archetype* g = lib.find("srd51.monster.goblin");
    const Archetype* w = lib.find("srd51.monster.wolf");
    REQUIRE(g);
    REQUIRE(w);
    CHECK(g->visual.kind == VisualKind::Paperdoll);
    CHECK(g->visual.kit == "paperdoll");
    CHECK(g->visual.standin_bin.empty());
    CHECK_FALSE(g->visual.size_short);
    CHECK(w->visual.kind == VisualKind::Standin);
    CHECK(w->visual.standin_bin == "medium");
    CHECK(w->visual.kit == "standin.medium");

    Json huge = gob.data;
    huge["id"] = "test.monster.huge";
    huge["type"] = "dragon";
    huge["size"] = "huge";
    REQUIRE_FALSE(lib.add(huge, "monster"));
    const Archetype* h = lib.find("test.monster.huge");
    REQUIRE(h);
    CHECK(h->visual.kind == VisualKind::Unbinned);
    CHECK(h->visual.kit.empty());
    CHECK(h->visual.standin_bin.empty());

    Json authored = gob.data;
    authored["id"] = "test.monster.kit";
    authored["presentation"] = Json{{"sprite", "from.sprite"}};
    authored["visual"] = Json{{"kind", "standin"}, {"kit", "kit.test"}, {"standin_bin", "small"}, {"art_root", "art/standin"}, {"size_short", true}};
    const std::string pack = kRoot + "/content/packs/srd51.core";
    REQUIRE_FALSE(lib.add(authored, "monster", "", 0, pack));
    const Archetype* a = lib.find("test.monster.kit");
    REQUIRE(a);
    CHECK(a->visual.kind == VisualKind::Standin);
    CHECK(a->visual.standin_bin == "small");
    CHECK(a->visual.sprite == "from.sprite");
    CHECK(a->visual.kit == "kit.test");
    CHECK(a->visual.art_root == "art/standin");
    CHECK(a->visual.size_short);
    CHECK_FALSE(kSizeShortRuntime);

    Json missing = authored;
    missing["id"] = "test.monster.missing";
    missing["visual"]["art_root"] = "art/no-such-kit";
    auto err = lib.add(missing, "monster", "", 0, pack);
    REQUIRE(err);
    CHECK(err->find("does not exist") != std::string::npos);
    Json escape = authored;
    escape["id"] = "test.monster.escape";
    escape["visual"]["art_root"] = "art/../standin";
    err = lib.add(escape, "monster", "", 0, pack);
    REQUIRE(err);
    CHECK(err->find("..") != std::string::npos);
    Json bad = authored;
    bad["id"] = "test.monster.badkind";
    bad["visual"] = Json{{"kind", "dragon"}};
    err = lib.add(bad, "monster");
    REQUIRE(err);
    CHECK(err->find("visual.kind") != std::string::npos);
}

TEST_CASE("visual is inert: Combat::hash and log_hash match with and without it") {
    auto fighter = migrate(load_json("content/packs/sample.vertical_slice/characters/fighter_1.json"));
    auto goblin = migrate(load_json("content/packs/srd51.core/monsters/goblin.json"));
    REQUIRE(fighter.ok);
    REQUIRE(goblin.ok);
    Json vis_f = fighter.data;
    Json vis_g = goblin.data;
    vis_f["visual"] = Json{{"size_short", true}};
    vis_f["presentation"] = Json{{"sprite", "paperdoll"}};
    vis_g["visual"] = Json{{"kind", "paperdoll"}, {"art_root", "art/paperdoll"}, {"size_short", false}};
    vis_g["visual"]["kit"] = "paperdoll.alt";
    vis_g["presentation"] = Json{{"sprite", "ignored.when.kit.set"}};
    ContentLibrary shown;
    const std::string pack = kRoot + "/content/packs/srd51.core";
    REQUIRE_FALSE(shown.add(vis_g, "monster", "", 0, pack));
    REQUIRE_FALSE(shown.add(vis_f, "character", "", 0, pack));
    CHECK(shown.find(vis_g["id"])->visual.kit == "paperdoll.alt");
    CHECK(shown.find(vis_g["id"])->visual.sprite == "ignored.when.kit.set");
    CHECK(shown.find(vis_f["id"])->visual.size_short);
    CHECK(shown.find(vis_g["id"])->base.hp_max == goblin.data["hit_points"]["average"]);
    for (uint64_t seed : {1ull, 7ull, 42ull}) {
        const Hashes plain = fight(fighter.data, goblin.data, seed);
        const Hashes with = fight(vis_f, vis_g, seed);
        CHECK(plain.hash == with.hash);
        CHECK(plain.log == with.log);
    }
}

TEST_CASE("ContentLibrary::load reads the packed clip registry") {
    auto set = assets::PackSet::from_pack_dir(kRoot + "/content/packs/srd51.core");
    REQUIRE_MESSAGE(set.ok(), (set.error_strings().empty() ? std::string{} : set.error_strings().front()));
    ContentLibrary lib;
    auto errs = lib.load(set);
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? std::string{} : errs.front()));
    const assets::ClipRegistry* doll = lib.clip_kit("paperdoll");
    const assets::ClipRegistry* med = lib.clip_kit("standin.medium");
    REQUIRE(doll);
    REQUIRE(med);
    CHECK(doll->resolve("hard-land") == "prone_idle");
    CHECK(doll->resolve("dodge") == "guard");
    CHECK(med->resolve("climb") == "walk");
    CHECK(med->resolve("hide") == "idle");
    CHECK(lib.find("srd51.monster.goblin")->visual.kit == "paperdoll");
    CHECK(lib.find("srd51.monster.wolf")->visual.kit == "standin.medium");
    CHECK(lib.find("srd51.monster.wolf")->visual.standin_bin == "medium");
}
