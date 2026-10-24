// Batch 3 save_advantage (monster@5-local trait kind; field_shapes_proposal Gap 2, Lich-approved): the loader fills one static
// rules5e RollTrait{Save, mode, when, .., save_abilities, save_vs_conditions, save_vs_damage_types}; rules5e::save_trait_modes
// applies it against the SaveSource. SRD 5.1 Magic Resistance: "advantage on saving throws against spells and other magical
// effects" = {magical: true}. Static data: no hash tag.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/d20.hpp"
#include "rules5e/traits.hpp"
#include "sample_set.hpp"
#include "sim/content.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::Ability;
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "sa_" + std::to_string(i++)}, {"name", "Save Advantage"}, {"description", "test"}, {"effect", e}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }
}  // namespace

TEST_CASE("save_advantage loader: Dark Devotion, Magic Resistance, dedupe, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json dd = {{"kind", "save_advantage"}, {"save_vs_conditions", {"charmed", "frightened"}}};
    REQUIRE_FALSE(add(lib, creature("t.dd", {dd, dd})));   // a duplicate adds nothing
    const auto* a = lib.find("t.dd");
    REQUIRE(a);
    REQUIRE(a->base.roll_traits.size() == 1);
    const auto& t = a->base.roll_traits[0];
    CHECK(t.roll == rules5e::TraitRoll::Save);
    CHECK(t.mode == rules5e::RollMode::Advantage);
    CHECK(t.when == 0);
    CHECK(t.save_vs_conditions == (rules5e::bit(Condition::Charmed) | rules5e::bit(Condition::Frightened)));
    CHECK(t.save_abilities == 0);
    CHECK(t.save_vs_damage_types == 0);

    REQUIRE_FALSE(add(lib, creature("t.mr", {{{"kind", "save_advantage"}, {"magical", true}}, dd})));
    const auto* mr = lib.find("t.mr");
    REQUIRE(mr->base.roll_traits.size() == 2);   // distinct traits stack
    CHECK(mr->base.roll_traits[0].when == rules5e::kWhenSourceMagical);

    REQUIRE_FALSE(add(lib, creature("t.dw", {{{"kind", "save_advantage"}, {"vs_damage_types", {"poison"}}, {"abilities", {"con"}},
                                             {"mode", "disadvantage"}}})));
    const auto& w = lib.find("t.dw")->base.roll_traits.at(0);
    CHECK(w.mode == rules5e::RollMode::Disadvantage);
    CHECK(w.save_abilities == (1u << uint32_t(Ability::Con)));
    CHECK(w.save_vs_damage_types == (1u << uint32_t(rules5e::DamageType::Poison)));

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {e}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e1", {{"kind", "save_advantage"}, {"save_vs_conditions", Json::array()}}).find("bad save_vs_conditions") != std::string::npos);
    CHECK(err("t.e2", {{"kind", "save_advantage"}}).find("at least one") != std::string::npos);
    CHECK(err("t.e3", {{"kind", "save_advantage"}, {"magical", false}}).find("at least one") != std::string::npos);
    CHECK(err("t.e4", dd, 4).find("needs a monster@5 file") != std::string::npos);
    CHECK(err("t.e5", {{"kind", "save_advantage"}, {"magical", true}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e6", {{"kind", "save_advantage"}, {"magical", true}, {"mode", "normal"}}).find("mode must be") != std::string::npos);
}

TEST_CASE("save_advantage rules: magical vs nonmagical, condition gate, cancel with disadvantage") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.mr", {{{"kind", "save_advantage"}, {"magical", true}}})));
    REQUIRE_FALSE(add(lib, creature("t.dd", {{{"kind", "save_advantage"}, {"save_vs_conditions", {"charmed", "frightened"}}}})));
    REQUIRE_FALSE(add(lib, creature("t.both", {{{"kind", "save_advantage"}, {"magical", true}},
                                               {{"kind", "save_advantage"}, {"magical", true}, {"mode", "disadvantage"}}})));
    const rules5e::Combatant& mr = lib.find("t.mr")->base;
    const rules5e::Combatant& dd = lib.find("t.dd")->base;
    const rules5e::Combatant& both = lib.find("t.both")->base;

    const auto spell = rules5e::save_trait_modes(mr, Ability::Wis, rules5e::SaveSource{true, 0, 0});
    CHECK(spell.advantage);
    CHECK_FALSE(spell.disadvantage);
    const auto breath = rules5e::save_trait_modes(mr, Ability::Dex, rules5e::SaveSource{false, 0, 0});   // nonmagical
    CHECK_FALSE(breath.advantage);

    CHECK(rules5e::save_trait_modes(dd, Ability::Wis, rules5e::SaveSource{true, rules5e::bit(Condition::Frightened), 0}).advantage);
    CHECK_FALSE(rules5e::save_trait_modes(dd, Ability::Con, rules5e::SaveSource{true, rules5e::bit(Condition::Poisoned), 0}).advantage);
    CHECK_FALSE(rules5e::save_trait_modes(dd, Ability::Wis, rules5e::SaveSource{}).advantage);

    const auto c = rules5e::save_trait_modes(both, Ability::Wis, rules5e::SaveSource{true, 0, 0});
    CHECK(c.advantage);
    CHECK(c.disadvantage);   // both set: the roll is normal (SRD: "you don't roll more than one d20")
    CHECK(rules5e::combine_modes(c.advantage, c.disadvantage) == rules5e::RollMode::Normal);
}
