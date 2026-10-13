// PackSet migration guard: content loaded through the ordered PackSet must be exactly what the
// pre-PackSet per-file loader produced (same archetypes => same fights => goldens unchanged).
// The pinned goldens themselves (vertical slice, events, compat fl1, cross-chunk) run through the
// PackSet path too, since ContentLibrary::load_pack is now a PackSet adapter.
#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <vector>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/driver.hpp"
#include "world/file_chunk_provider.hpp"

using namespace eng;
using namespace eng::sim;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

// The v0 loader, verbatim semantics: manifest contents .json -> registry -> add().
ContentLibrary legacy_load(const std::vector<std::string>& dirs) {
    ContentLibrary lib;
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    for (const std::string& dir : dirs) {
    auto man = reg.load_file(std::filesystem::path(dir) / "manifest.json");
    REQUIRE(man.ok);
    for (const auto& item : man.data["contents"]) {
        const std::string rel = item.get<std::string>();
        if (!rel.ends_with(".json")) continue;
        auto doc = reg.load_file(std::filesystem::path(dir) / rel);
        REQUIRE_MESSAGE(doc.ok, doc.error);
        if (doc.current.type != "monster" && doc.current.type != "character") continue;
        REQUIRE_FALSE(lib.add(doc.data, doc.current.type));
    }
    }
    return lib;
}

uint64_t fight(const ContentLibrary& lib, const std::string& dir, uint64_t seed) {
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, dir + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE(r.chunk);
    ecs::Registry reg;
    auto c = Combat::create(reg, *r.chunk, lib, seed);
    REQUIRE(c);
    NearestEnemyPolicy pol;
    REQUIRE(run_to_end(*c, [&](CombatantId) { return &pol; }).ended);
    return c->hash() ^ (c->log_hash() * 31) ^ (event_stream_hash(c->events()) * 17);
}
} // namespace

TEST_CASE("packset: sample (+ srd51.core) and fl1 (its own set) load identically to the v0 loader") {
    struct Case { std::string root, id, chunk_dir; std::vector<std::string> legacy_dirs; std::map<std::string, std::string> owner; };
    const std::vector<Case> cases = {
        {kRoot + "/content/packs", "sample.vertical_slice", kRoot + "/content/packs/sample.vertical_slice",
         {kRoot + "/content/packs/srd51.core", kRoot + "/content/packs/sample.vertical_slice"},
         {{"srd51.monster.goblin", "srd51.core"}, {"srd51.character.fighter_1", "sample.vertical_slice"}}},
        {kRoot + "/tests/compat/fl1", "sample.vertical_slice", kRoot + "/tests/compat/fl1/sample.vertical_slice",
         {kRoot + "/tests/compat/fl1/sample.vertical_slice"}, {}},
    };
    for (const auto& cs : cases) {
        CAPTURE(cs.root);
        const std::filesystem::path roots[] = {cs.root};
        const std::string req[] = {cs.id};
        const auto set = assets::PackSet::resolve(roots, req);
        REQUIRE(set.ok());
        ContentLibrary viaset;
        REQUIRE(viaset.load(set).empty());
        const ContentLibrary old = legacy_load(cs.legacy_dirs);
        REQUIRE(viaset.all().size() == old.all().size());
        for (const auto& [id, a] : old.all()) {
            const Archetype* b = viaset.find(id);
            REQUIRE(b);
            const auto o = cs.owner.find(id);
            CHECK(b->pack_id == (o == cs.owner.end() ? std::string("sample.vertical_slice") : o->second));
            CHECK(b->base.hp_max == a.base.hp_max);
            CHECK(b->base.ac == a.base.ac);
            CHECK(b->base.attacks.size() == a.base.attacks.size());
            for (std::size_t i = 0; i < a.base.attacks.size(); ++i) CHECK(b->base.attacks[i].damage == a.base.attacks[i].damage);
            CHECK(b->inert_features == a.inert_features);
            CHECK(b->ai == a.ai);
        }
        for (uint64_t seed : {1u, 2u, 3u, 2026u}) {
            CAPTURE(seed);
            CHECK(fight(viaset, cs.chunk_dir, seed) == fight(old, cs.chunk_dir, seed));
        }
    }
}
