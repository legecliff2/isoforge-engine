// Tier-A monster gaps (Lich rulings appended to /workspace/tierA_gaps_proposal.md; SRD 5.1 wording only). Sim loader + combat.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "rules5e/checks.hpp"
#include "sim/driver.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    return m;
}
ContentLibrary lib_with(const std::vector<Json>& monsters) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
std::string load_error(const Json& m) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const auto e = lib.add(m, "monster");
    REQUIRE(e);
    return *e;
}
}  // namespace

TEST_CASE("Tier-A §25: passive_perception_reason srd_printed loads the printed passive as the override") {
    Json m = creature("t.printed");
    m["senses"]["passive_perception"] = 13;
    m["passive_perception_reason"] = "srd_printed";
    Json plain = creature("t.plain");
    auto lib = lib_with({m, plain});
    CHECK(lib.find("t.printed")->base.passive_perception_override == 13);
    CHECK(rules5e::passive_perception(lib.find("t.printed")->base) == 13);
    CHECK(lib.find("t.plain")->base.passive_perception_override == -1);   // no reason: derived, nothing stored
    Json bad = m; bad["passive_perception_reason"] = "typo";
    CHECK(load_error(bad).find("bad passive_perception_reason") != std::string::npos);
    bad = m; bad["$schema"] = "monster@4";
    CHECK(load_error(bad).find("passive_perception_reason needs a monster@5 file") != std::string::npos);
    bad = m; bad["senses"]["passive_perception"] = 61;
    CHECK(load_error(bad).find("bad senses.passive_perception") != std::string::npos);
}
