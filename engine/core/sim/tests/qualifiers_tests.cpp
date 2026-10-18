// Damage qualifiers from content (monster@3 / character@3): an action's `qualifiers`, the
// `damage_mitigation` effect (qualified resistances / immunities) and `magic_weapons`.
// Pre-@3 inertness is also pinned by compat_defenses_tests.cpp (digests unchanged).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "rules5e/damage.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

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

// A goblin stat block as a @3 monster `id` with the given trait effects and per-action qualifiers.
Json monster(const std::string& id, const std::vector<Json>& effects = {}, const Json& qualifiers = Json(), const char* schema = "monster@3") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["traits"] = Json::array();
    int i = 0;
    for (const Json& e : effects)
        m["traits"].push_back({{"id", "t" + std::to_string(i++)}, {"name", "T"}, {"description", "x"}, {"effect", e}});
    if (!qualifiers.is_null())
        for (auto& a : m["actions"]) a["qualifiers"] = qualifiers;
    return m;
}
Json mitigation(const char* which, const char* qual, std::vector<std::string> types) {
    return {{"kind", "damage_mitigation"}, {which, {{qual, Json(types)}}}};
}
const rules5e::Combatant& base(const ContentLibrary& lib, const char* id) {
    const Archetype* a = lib.find(id);
    REQUIRE(a);
    return a->base;
}
constexpr auto kSlash = rules5e::DamageType::Slashing;
constexpr auto kPierce = rules5e::DamageType::Piercing;
constexpr auto kFire = rules5e::DamageType::Fire;
using rules5e::kAttackAdamantine;
using rules5e::kAttackMagical;
using rules5e::kAttackSilvered;
} // namespace

TEST_CASE("qualifiers: action qualifiers load into Attack::qualifiers") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(monster("t.none"), "monster"));
    REQUIRE_FALSE(lib.add(monster("t.silver", {}, Json::array({"silvered"})), "monster"));
    REQUIRE_FALSE(lib.add(monster("t.all", {}, Json::array({"magical", "silvered", "adamantine"})), "monster"));
    for (const auto& at : base(lib, "t.none").attacks) CHECK(at.qualifiers == 0);
    for (const auto& at : base(lib, "t.silver").attacks) CHECK(at.qualifiers == kAttackSilvered);
    for (const auto& at : base(lib, "t.all").attacks) CHECK(at.qualifiers == (kAttackMagical | kAttackSilvered | kAttackAdamantine));
    auto bad = lib.add(monster("t.bad", {}, Json::array({"mithral"})), "monster");
    REQUIRE(bad);
    CHECK(bad->find("bad qualifier 'mithral'") != std::string::npos);
}

TEST_CASE("qualifiers: damage_mitigation fills the qualified masks, magic_weapons marks every attack") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(monster("t.ghost", {mitigation("resistances", "nonmagical", {"slashing", "piercing"})}), "monster"));
    REQUIRE_FALSE(lib.add(monster("t.were", {mitigation("immunities", "nonmagical_not_silvered", {"slashing"}),
                                             mitigation("resistances", "nonmagical_not_adamantine", {"fire"})}), "monster"));
    REQUIRE_FALSE(lib.add(monster("t.magic", {Json{{"kind", "magic_weapons"}}}), "monster"));
    const auto& g = base(lib, "t.ghost");
    CHECK(g.resist_qualified.nonmagical == (rules5e::bit(kSlash) | rules5e::bit(kPierce)));
    CHECK(g.resist_qualified.nonmagical_not_silvered == 0);
    CHECK(g.immune_qualified.nonmagical == 0);
    CHECK(g.resistances == 0);   // the plain masks are untouched
    const auto& w = base(lib, "t.were");
    CHECK(w.immune_qualified.nonmagical_not_silvered == rules5e::bit(kSlash));
    CHECK(w.resist_qualified.nonmagical_not_adamantine == rules5e::bit(kFire));
    for (const auto& at : base(lib, "t.magic").attacks) CHECK(at.qualifiers == kAttackMagical);
    CHECK(lib.find("t.ghost")->inert_features.empty());
    CHECK(lib.find("t.ghost")->unsupported_features.empty());
}

TEST_CASE("qualifiers: malformed damage_mitigation is rejected") {
    auto err = [](const Json& effect) {
        ContentLibrary lib;
        auto e = lib.add(monster("t.bad", {effect}), "monster");
        REQUIRE(e);
        return *e;
    };
    CHECK(err(Json{{"kind", "damage_mitigation"}}).find("needs resistances") != std::string::npos);
    CHECK(err(mitigation("resistances", "nonmagical_not_gold", {"slashing"})).find("unknown qualifier") != std::string::npos);
    CHECK(err(mitigation("resistances", "nonmagical", {"sparkles"})).find("bad damage type") != std::string::npos);
    CHECK(err(Json{{"kind", "damage_mitigation"}, {"immunities", Json::array({"slashing"})}}).find("must be an object") != std::string::npos);
    CHECK(err(Json{{"kind", "damage_mitigation"}, {"immunities", {{"nonmagical", "slashing"}}}}).find("must be an array") != std::string::npos);
}

TEST_CASE("qualifiers: loaded masks resist nonmagical hits and yield to silvered / adamantine / magical ones") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(monster("t.were", {mitigation("immunities", "nonmagical_not_silvered", {"slashing"}),
                                             mitigation("resistances", "nonmagical", {"piercing"})}), "monster"));
    REQUIRE_FALSE(lib.add(monster("t.ghost", {mitigation("resistances", "nonmagical_not_adamantine", {"slashing"})}), "monster"));
    const auto& w = base(lib, "t.were");
    // Immune to nonmagical, non-silvered slashing; resists nonmagical piercing.
    CHECK(rules5e::apply_damage(w, 10, kSlash, 0).dealt == 0);
    CHECK(rules5e::apply_damage(w, 10, kSlash, kAttackSilvered).dealt == 10);
    CHECK(rules5e::apply_damage(w, 10, kSlash, kAttackMagical).dealt == 10);
    CHECK(rules5e::apply_damage(w, 10, kPierce, 0).dealt == 5);
    CHECK(rules5e::apply_damage(w, 10, kPierce, kAttackSilvered).dealt == 5);   // silver does not beat plain "nonmagical"
    CHECK(rules5e::apply_damage(w, 10, kPierce, kAttackMagical).dealt == 10);
    CHECK(rules5e::apply_damage(w, 10, kFire, 0).dealt == 10);
    const auto& g = base(lib, "t.ghost");
    CHECK(rules5e::apply_damage(g, 10, kSlash, 0).dealt == 5);
    CHECK(rules5e::apply_damage(g, 10, kSlash, kAttackAdamantine).dealt == 10);
    CHECK(rules5e::apply_damage(g, 10, kSlash, kAttackSilvered).dealt == 5);   // silvered is not adamantine
}

TEST_CASE("qualifiers: in a sim fight a silvered or magical attacker hurts a werewolf, a plain one does not") {
    // attacker: always hits, exactly 10 slashing; werewolf: immune to nonmagical non-silvered slashing.
    auto attacker = [](const char* id, const Json& quals, const std::vector<Json>& effects = {}) {
        Json m = monster(id, effects, quals);
        m["hit_points"]["average"] = 40;
        for (auto& a : m["actions"]) {
            a["to_hit"] = 127;
            a["damage"] = Json::array({{{"dice", "1d1+9"}, {"type", "slashing"}}});
        }
        return m;
    };
    Json wolf = monster("t.wolf", {mitigation("immunities", "nonmagical_not_silvered", {"slashing"})});
    wolf["hit_points"]["average"] = 40;
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(wolf, "monster"));
    REQUIRE_FALSE(lib.add(attacker("t.plain", Json()), "monster"));
    REQUIRE_FALSE(lib.add(attacker("t.silver", Json::array({"silvered"})), "monster"));
    REQUIRE_FALSE(lib.add(attacker("t.magic", Json(), {Json{{"kind", "magic_weapons"}}}), "monster"));
    int hurt[3] = {0, 0, 0};
    const char* who[3] = {"t.plain", "t.silver", "t.magic"};
    for (int k = 0; k < 3; ++k) {
        world::Chunk ch{};
        ch.coord = {5, 5, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        ch.spawns.push_back({who[k], Vec2i{4, 4}, world::spawn_stable_id(ch.coord, 0), R"({"team":"a"})"});
        ch.spawns.push_back({"t.wolf", Vec2i{5, 4}, world::spawn_stable_id(ch.coord, 1), R"({"team":"b"})"});
        ch.content_hash = world::compute_content_hash(ch);
        const CombatantId A = ch.spawns[0].stable_id, W = ch.spawns[1].stable_id;
        for (uint64_t seed = 1; seed <= 12; ++seed) {   // a natural 1 misses: use several seeds
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != A; ++i) {
                Command e;
                e.request = {rules5e::current_actor(c->state()), rules5e::ActionKind::EndTurn};
                REQUIRE(c->submit(e).ok);
            }
            Command a;
            a.request = {A, rules5e::ActionKind::Attack, W};
            REQUIRE(c->submit(a).ok);
            const int lost = rules5e::find(c->state(), W)->hp_max - rules5e::find(c->state(), W)->hp;
            if (lost > 0) { CHECK(lost == 10); ++hurt[k]; }
        }
    }
    CHECK(hurt[0] == 0);    // plain: immune
    CHECK(hurt[1] > 0);     // silvered weapon
    CHECK(hurt[2] > 0);     // Magic Weapons
}

TEST_CASE("qualifiers: pre-@3 qualifiers and mitigation effects are inert (migration marker)") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    Json m = monster("t.old", {mitigation("immunities", "nonmagical", {"slashing"}), Json{{"kind", "magic_weapons"}}},
                     Json::array({"silvered"}), "monster@2");
    auto r = reg.load(m);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["inert_mechanics"] == Json::array({"attack_qualifiers"}));
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(r.data, "monster"));
    const Archetype* a = lib.find("t.old");
    CHECK(a->inert_features.size() == 2);
    CHECK(a->base.immune_qualified.nonmagical == 0);
    for (const auto& at : a->base.attacks) CHECK(at.qualifiers == 0);
    // No qualifiers authored: no marker, file byte-identical.
    auto r2 = reg.load(monster("t.old2", {}, Json(), "monster@2"));
    REQUIRE(r2.ok);
    CHECK_FALSE(r2.data.contains("inert_mechanics"));
}
