// item@2 / monster@5 / character@5 (docs/proposals/EQUIPMENT.md section 7, #245): the 4 -> 5 steps are identity,
// so an old-version file and a new-version file with the same body build the same archetypes and every
// encounter hash (Combat::hash / log_hash) is unchanged.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/combat.hpp"
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

// Loads a raw document through the engine registry, as PackSet does.
assets::LoadResult load(const Json& doc) {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    return reg.load(doc);
}

// The same body with the given $schema version.
Json with_version(Json doc, const std::string& type, uint32_t v) {
    doc["$schema"] = type + "@" + std::to_string(v);
    return doc;
}

struct Hashes { uint64_t hash = 0, log = 0, chunk = 0; };
// A short fixed fight: everyone ends turns, then the fighter attacks the goblin.
Hashes fight(const assets::LoadResult& fighter, const assets::LoadResult& goblin, uint64_t seed) {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(goblin.data, "monster", "", goblin.declared.version));
    REQUIRE_FALSE(lib.add(fighter.data, "character", "", fighter.declared.version));
    ecs::Registry reg;
    auto chunk = arena({fighter.data["id"], goblin.data["id"]});
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
    return {c->hash(), c->log_hash(), chunk.content_hash};
}
} // namespace

TEST_CASE("equip schema bump: @4 and @5 files with the same body load and hash identically") {
    // Frozen @3 / @4 copies (tests/data/schema_bump_v4), migrated to the body an @4 file of today carries.
    auto g3 = load(load_json("engine/core/sim/tests/data/schema_bump_v4/monsters/goblin.json"));
    auto f3 = load(load_json("engine/core/sim/tests/data/schema_bump_v4/characters/fighter_level_1.json"));
    REQUIRE_MESSAGE(g3.ok, g3.error);
    REQUIRE_MESSAGE(f3.ok, f3.error);
    CHECK(g3.current.version == assets::kMonsterSchemaVersion);
    CHECK(f3.current.version == assets::kCharacterSchemaVersion);
    CHECK(assets::kMonsterSchemaVersion == 6u);
    CHECK(assets::kCharacterSchemaVersion == 6u);

    auto g4 = load(with_version(g3.data, "monster", 4));
    auto g5 = load(with_version(g3.data, "monster", 5));
    auto f4 = load(with_version(f3.data, "character", 4));
    auto f5 = load(with_version(f3.data, "character", 5));
    for (const auto* r : {&g4, &g5, &f4, &f5}) REQUIRE_MESSAGE(r->ok, r->error);
    CHECK(g4.declared.version == 4u);
    CHECK(g5.declared.version == 5u);
    CHECK(g4.data == g5.data);
    CHECK(f4.data == f5.data);
    CHECK(g4.data == g3.data);   // 4 -> 5 adds nothing on top of the 3 -> 4 result
    CHECK(f4.data == f3.data);

    for (uint64_t seed : {1ull, 7ull, 42ull}) {
        const Hashes old_v = fight(f4, g4, seed);
        const Hashes new_v = fight(f5, g5, seed);
        const Hashes from3 = fight(f3, g3, seed);
        CHECK(old_v.hash == new_v.hash);
        CHECK(old_v.log == new_v.log);
        CHECK(old_v.chunk == new_v.chunk);
        CHECK(from3.chunk == old_v.chunk);
        CHECK(from3.hash != 0u);
    }
}

TEST_CASE("equip schema bump: frozen @4 files load and hash the same as their @5 re-tag") {
    const Json m4 = load_json("engine/core/sim/tests/data/schema_bump_v4/monsters/animated_armor.json");
    const Json c4 = load_json("engine/core/sim/tests/data/schema_bump_v4/characters/barbarian_level_1.json");
    REQUIRE(m4["$schema"] == "monster@4");
    REQUIRE(c4["$schema"] == "character@4");
    auto mo = load(m4), mn = load(with_version(m4, "monster", 5));
    auto co = load(c4), cn = load(with_version(c4, "character", 5));
    for (const auto* r : {&mo, &mn, &co, &cn}) REQUIRE_MESSAGE(r->ok, r->error);
    CHECK(mo.data == mn.data);
    CHECK(co.data == cn.data);
    Json body = mo.data, src = m4;
    body.erase("$schema");
    src.erase("$schema");
    CHECK(body == src);   // identity: the @4 body is untouched
    for (uint64_t seed : {3ull, 11ull}) {
        const Hashes o = fight(co, mo, seed);
        const Hashes n = fight(cn, mn, seed);
        CHECK(o.hash == n.hash);
        CHECK(o.log == n.log);
        CHECK(o.chunk == n.chunk);
    }
}
