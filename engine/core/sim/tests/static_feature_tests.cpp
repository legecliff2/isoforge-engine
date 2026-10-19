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
    // open v1 kinds (not in the frozen 3 -> 4 list): live at any declared version; bad shapes are load errors
    REQUIRE_FALSE(lib.add(with("t.old", {{{"kind", "crit_range"}, {"min", 19}}}, 3), "character", "", 3));
    CHECK(lib.find("t.old")->base.crit_min == 19);
    CHECK(lib.add(with("t.bad1", {{{"kind", "attack_bonus"}, {"attacks", "thrown"}, {"amount", 2}}}), "character"));
    CHECK(lib.add(with("t.bad2", {{{"kind", "crit_range"}, {"min", 1}}}), "character"));
    CHECK(lib.add(with("t.bad3", {{{"kind", "ac_bonus"}}}), "character"));
}

TEST_CASE("static features: ac_bonus requires_armor sees character@4 {\"item\": ...} equipment and the legacy forms") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json armored_bonus = {{"kind", "ac_bonus"}, {"amount", 1}, {"requires_armor", true}};
    // The note must not name armor, so only equipment[] can make the character armored.
    auto make = [&](const std::string& id, Json equipment) {
        Json c = with(id, {armored_bonus});
        c["equipment"] = std::move(equipment);
        c["armor_class"]["note"] = "unarmored";
        return c;
    };
    // character@4 item form, namespaced id: wears chain mail, gets the bonus
    REQUIRE_FALSE(lib.add(make("t.item_armor", Json::array({{{"item", "srd51.item.chain_mail"}}, {{"item", "srd51.item.shield"}},
                                                            {{"item", "srd51.item.longsword"}}})), "character"));
    CHECK(lib.find("t.item_armor")->base.ac == 19);
    // the same character with no armor among its items: no bonus
    REQUIRE_FALSE(lib.add(make("t.item_bare", Json::array({{{"item", "srd51.item.shield"}}, {{"item", "srd51.item.longsword"}}})),
                          "character"));
    CHECK(lib.find("t.item_bare")->base.ac == 18);
    // legacy bare-string and {"id": ...} forms still count
    REQUIRE_FALSE(lib.add(make("t.str_armor", Json::array({"chain_mail", "shield", "longsword"})), "character"));
    CHECK(lib.find("t.str_armor")->base.ac == 19);
    REQUIRE_FALSE(lib.add(make("t.id_armor", Json::array({{{"id", "chain_mail"}}, {{"id", "longsword"}}})), "character"));
    CHECK(lib.find("t.id_armor")->base.ac == 19);
    // the "<armor>_armor" item naming holds in the item form too (srd51 leather armor)
    REQUIRE_FALSE(lib.add(make("t.item_leather", Json::array({{{"item", "srd51.item.leather_armor"}}})), "character"));
    CHECK(lib.find("t.item_leather")->base.ac == 19);
}

TEST_CASE("sim: equipment_entry_id reads the string, {\"id\"} and {\"item\"} forms") {
    CHECK(equipment_entry_id(Json("chain_mail")) == "chain_mail");
    CHECK(equipment_entry_id(Json{{"id", "chain_mail"}}) == "chain_mail");
    CHECK(equipment_entry_id(Json{{"item", "srd51.item.chain_mail"}}) == "srd51.item.chain_mail");
    CHECK(equipment_entry_id(Json{{"item", "srd51.item.shield"}, {"id", "x"}}) == "srd51.item.shield");
    CHECK(equipment_entry_id(Json{{"item", 3}}).empty());
    CHECK(equipment_entry_id(Json(5)).empty());
}
