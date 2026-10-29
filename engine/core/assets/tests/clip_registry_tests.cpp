#include <doctest/doctest.h>

#include <algorithm>

#include "assets/clip_registry.hpp"

using namespace eng::assets;

namespace {
const std::string kKit = std::string(ENGINE_SOURCE_ROOT) + "/content/packs/srd51.core/art/clips.json";
} // namespace

TEST_CASE("clip registry: packed kit membership, including hard-land -> prone_idle") {
    ClipFile file;
    auto err = load_clip_file(std::filesystem::path(kKit), file);
    REQUIRE_MESSAGE(!err, err.value_or(std::string{}));
    const ClipRegistry* doll = nullptr;
    const ClipRegistry* stand = nullptr;
    for (const ClipRegistry& k : file.kits) {
        if (k.id == "paperdoll") doll = &k;
        if (k.id == "standin.medium") stand = &k;
    }
    REQUIRE(doll);
    REQUIRE(stand);
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
    bool saw_large = false;
    for (const ClipRegistry& k : file.kits) saw_large = saw_large || k.id == "standin.large";
    CHECK(saw_large);
    CHECK_FALSE(std::any_of(file.kits.begin(), file.kits.end(), [](const ClipRegistry& k) { return k.id == "standin.huge"; }));
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

TEST_CASE("clip registry: alias keys that are clips, duplicate kits, and huge bins are refused") {
    ClipFile file;
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"idle"}],"aliases":{"idle":"idle"}}]})"), file));
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"k","kind":"paperdoll","clips":[{"id":"a"}]},{"id":"k","kind":"paperdoll","clips":[{"id":"b"}]}]})"), file));
    CHECK(load_clip_file(Json::parse(R"({"kits":[{"id":"standin","kind":"standin","bins":["huge"],"clips":[{"id":"idle"}]}]})"), file));
    CHECK_FALSE(load_clip_file(Json::parse(R"({"kits":[{"id":"standin","kind":"standin","bins":["tiny","small"],"clips":[{"id":"idle"}],"fallback":"idle"}]})"), file));
    CHECK(file.kits.size() == 2);
    CHECK(file.kits[0].id == "standin.tiny");
    CHECK(file.kits[1].id == "standin.small");
}
