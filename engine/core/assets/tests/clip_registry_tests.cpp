#include <doctest/doctest.h>

#include "assets/clip_registry.hpp"

using namespace eng::assets;

namespace {
const std::string kKit = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/srd51.core/art/clips.json";

bool same_draw(const ClipRegistry& a, const ClipRegistry& b) {
    if (a.clips.size() != b.clips.size() || a.aliases != b.aliases || a.fallback != b.fallback) return false;
    for (size_t i = 0; i < a.clips.size(); ++i) {
        const Clip& x = a.clips[i];
        const Clip& y = b.clips[i];
        if (x.id != y.id || x.frames != y.frames || x.fps != y.fps || x.mode != y.mode) return false;
    }
    return true;
}
} // namespace

TEST_CASE("clip registry: packed kit membership, including hard-land -> prone_idle") {
    ClipFile file;
    auto err = load_clip_file(std::filesystem::path(kKit), file);
    REQUIRE_MESSAGE(!err, err.value_or(std::string{}));
    const ClipRegistry* doll = nullptr;
    const ClipRegistry* stand = nullptr;
    const ClipRegistry* large = nullptr;
    const ClipRegistry* huge = nullptr;
    for (const ClipRegistry& k : file.kits) {
        if (k.id == "paperdoll") doll = &k;
        if (k.id == "standin.medium") stand = &k;
        if (k.id == "standin.large") large = &k;
        if (k.id == "standin.huge") huge = &k;
    }
    REQUIRE(doll);
    REQUIRE(stand);
    REQUIRE(large);
    REQUIRE(huge);
    CHECK(doll->resolve("hard-land") == "prone_idle");
    CHECK(doll->resolve("hard_land") == "prone_idle");
    CHECK(doll->resolve("dodge") == "guard");
    CHECK(doll->resolve("knockback") == "react");
    CHECK(doll->resolve("swim_idle") == "idle");
    CHECK(doll->resolve("fly_idle") == "idle");
    CHECK(doll->resolve("swim_move") == "walk");
    CHECK(doll->resolve("fly_move") == "walk");
    CHECK(doll->resolve("climb") == "climb");
    CHECK(doll->resolve("hide") == "hide");
    CHECK(doll->resolve("sneak_idle") == "hide");
    CHECK_FALSE(doll->resolve("not_a_clip"));
    CHECK(stand->resolve("climb") == "walk");
    CHECK(stand->resolve("crawl") == "walk");
    CHECK(stand->resolve("hide") == "idle");
    CHECK(stand->resolve("melee") == "attack");
    CHECK(stand->resolve("hard_land") == "fall_impact");
    CHECK(stand->resolve("missing") == "idle");
    CHECK(stand->bin == "medium");
    CHECK(large->bin == "large");
    CHECK(huge->id == "standin.huge");
    CHECK(huge->bin == "huge");
    CHECK(huge->clips.size() == 9);
    CHECK(same_draw(*huge, *large));
}

TEST_CASE("clip registry: an alias target that is not a clip is refused") {
    ClipFile file;
    const char* bad = R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"idle"}],"aliases":{"hard_land":"prone_idle"}}]})";
    auto err = load_clip_file(Json::parse(bad), file);
    REQUIRE(err);
    CHECK(err->find("prone_idle") != std::string::npos);
    CHECK(err->find("not a clip") != std::string::npos);
    const char* ok = R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"idle"},{"id":"prone_idle"}],"aliases":{"hard-land":"prone_idle"}}]})";
    REQUIRE_FALSE(load_clip_file(Json::parse(ok), file));
    REQUIRE(file.kits.size() == 1);
    CHECK(file.kits[0].resolve("hard-land") == "prone_idle");
}

TEST_CASE("clip registry: alias keys that are clips, duplicate kits, and unknown bins are refused") {
    ClipFile file;
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"idle"}],"aliases":{"idle":"idle"}}]})"), file));
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"a"}]},{"id":"k","kind":"paperdoll","clips":[{"id":"b"}]}]})"), file));
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"standin","kind":"standin","bins":["gargantuan"],"clips":[{"id":"idle"}]}]})"), file));
    CHECK_FALSE(load_clip_file(Json::parse(R"({"kits":[{"id":"standin","kind":"standin","bins":["tiny","small","huge"],"clips":[{"id":"idle"}],"fallback":"idle"}]})"), file));
    CHECK(file.kits.size() == 3);
    CHECK(file.kits[0].id == "standin.tiny");
    CHECK(file.kits[1].id == "standin.small");
    CHECK(file.kits[2].id == "standin.huge");
    CHECK(file.kits[2].bin == "huge");
}
