// R-G5, sim side: static feature kinds attack_bonus, crit_range and ac_bonus compile into the archetype's Combatant.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/content.hpp"

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
Json with(const std::string& id, std::vector<Json> effects, int declared_schema = 4) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");   // AC 18, chain mail + shield
    c["$schema"] = "character@" + std::to_string(declared_schema);
    c["id"] = id;
    for (std::size_t i = 0; i < effects.size(); ++i)
        c["features"].push_back({{"id", "s" + std::to_string(i)}, {"name", "S"}, {"description", "x"}, {"effect", effects[i]}});
    return c;
}
}  // namespace

TEST_CASE("static features: attack_bonus sums, crit_range lowest wins, ac_bonus armored / flat; defaults unchanged") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(with("t.plain", {}), "character"));
    const auto& p = lib.find("t.plain")->base;
    CHECK(p.attack_bonus_melee == 0);
    CHECK(p.attack_bonus_ranged == 0);
    CHECK(p.crit_min == 20);
    CHECK(p.ac == 18);
    REQUIRE_FALSE(lib.add(with("t.styled", {{{"kind", "attack_bonus"}, {"attacks", "ranged"}, {"amount", 2}},
                                            {{"kind", "attack_bonus"}, {"attacks", "ranged"}, {"amount", 1}},
                                            {{"kind", "attack_bonus"}, {"attacks", "melee"}, {"amount", 1}},
                                            {{"kind", "crit_range"}, {"min", 19}},
                                            {{"kind", "crit_range"}, {"min", 18}},
                                            {{"kind", "ac_bonus"}, {"amount", 1}, {"requires_armor", true}}}),
                          "character"));
    const auto& s = lib.find("t.styled")->base;
    CHECK(s.attack_bonus_ranged == 3);
    CHECK(s.attack_bonus_melee == 1);
    CHECK(s.crit_min == 18);
    CHECK(s.ac == 19);   // wears chain mail
    // not wearing armor: the armored bonus does nothing, a flat bonus still applies
    Json bare = with("t.bare", {{{"kind", "ac_bonus"}, {"amount", 1}, {"requires_armor", true}},
                                {{"kind", "ac_bonus"}, {"amount", 2}}});
    bare.erase("equipment");
    bare["armor_class"]["note"] = "unarmored";
    REQUIRE_FALSE(lib.add(bare, "character"));
    CHECK(lib.find("t.bare")->base.ac == 20);
    // an older file lists them inert; bad shapes are load errors
    REQUIRE_FALSE(lib.add(with("t.old", {{{"kind", "crit_range"}, {"min", 19}}}, 3), "character", "", 3));
    CHECK(lib.find("t.old")->base.crit_min == 20);
    CHECK(lib.add(with("t.bad1", {{{"kind", "attack_bonus"}, {"attacks", "thrown"}, {"amount", 2}}}), "character"));
    CHECK(lib.add(with("t.bad2", {{{"kind", "crit_range"}, {"min", 1}}}), "character"));
    CHECK(lib.add(with("t.bad3", {{{"kind", "ac_bonus"}}}), "character"));
}
