// B7: Shambling Mound both-hits Engulf, content side. The option compiles to an ApplyCondition contain that is
// not a castable action. A running combat applies it when both slams hit. shambling_mound.json stays deferred.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json engulf_spec() {
    return {{"max_size", "medium"}, {"escape_dc", 14}, {"while", "blinded"},
            {"damage", {{"dice", "2d8+4"}, {"type", "bludgeoning"}}}};
}

Json mound_doc(const std::string& id, int to_hit) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["size"] = "large";
    m["hit_points"]["average"] = 136;
    Json slam = m["actions"][0];
    slam["id"] = "slam";
    slam["name"] = "Slam";
    slam["to_hit"] = to_hit;
    slam["reach"] = 10;
    slam["damage"] = Json::array({{{"dice", "2d8+4"}, {"type", "bludgeoning"}}});
    slam.erase("on_hit");
    m["actions"] = Json::array({slam});
    m["traits"] = Json::array({{{"id", "multiattack"}, {"name", "Multiattack"},
        {"description", "two slams; both hits engulf"},
        {"effect", {{"kind", "multiattack"}, {"options", Json::array({
            {{"id", "slams"}, {"attacks", Json::array({"slam", "slam"})}, {"engulf_both_hits", engulf_spec()}}
        })}}}}});
    return m;
}

ContentLibrary lib_of(const Json& doc) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 200;
    REQUIRE_FALSE(lib.add(tank, "character"));
    const auto e = lib.add(doc, "monster");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    return lib;
}

std::optional<std::string> add_only(const Json& doc) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    return lib.add(doc, "monster");
}
}

TEST_CASE("engulf_both_hits compiles to a contain ApplyCondition and is not an action") {
    auto lib = lib_of(mound_doc("t.mound", 40));
    const auto* arch = lib.find("t.mound");
    REQUIRE(arch);
    REQUIRE(arch->multiattack_decls.size() == 1);
    REQUIRE(arch->multiattack_decls[0].engulf_both_hits);
    CHECK(arch->multiattack_decls[0].engulf_both_hits->escape_dc == 14);
    CHECK(arch->multiattack_decls[0].engulf_both_hits->max_size == int8_t(rules5e::Size::Medium));
    CHECK(arch->multiattack_decls[0].engulf_both_hits->damage.count == 2);
    CHECK(arch->multiattack_decls[0].engulf_both_hits->damage.bonus == 4);
    CHECK(arch->multiattack_decls[0].engulf_both_hits->damage_type == rules5e::DamageType::Bludgeoning);
    REQUIRE(arch->base.multiattacks.size() == 1);
    const uint32_t sid = rules5e::spell_id_of("t.mound#slams#engulf");
    CHECK(arch->base.multiattacks[0].engulf_both == sid);
    CHECK(arch->base.multiattacks[0].free_rider == 0);
    const auto* d = lib.spells()->find(sid);
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::ApplyCondition);
    CHECK(d->condition == Condition::Restrained);
    CHECK(d->has_while);
    CHECK(d->while_condition == Condition::Blinded);
    CHECK(d->contain == 1);
    CHECK(d->contain_one);
    CHECK(d->escape_dc == 14);
    CHECK(d->condition_max_size == int8_t(rules5e::Size::Medium));
    CHECK_FALSE(d->is_spell);
    bool known = false;
    for (uint32_t id : arch->base.spells_known) known = known || id == sid;
    CHECK_FALSE(known);
    bool listed = false;
    for (const auto& sa : arch->save_actions) listed = listed || sa.spell_id == sid;
    CHECK_FALSE(listed);
}

TEST_CASE("engulf_both_hits: the loader refuses a dishonest option") {
    auto bad_while = mound_doc("t.bad", 7);
    bad_while["traits"][0]["effect"]["options"][0]["engulf_both_hits"]["while"] = "restrained";
    auto e = add_only(bad_while);
    REQUIRE(e);
    CHECK(e->find("while must be blinded") != std::string::npos);

    auto one = mound_doc("t.one", 7);
    one["traits"][0]["effect"]["options"][0]["attacks"] = Json::array({"slam"});
    e = add_only(one);
    REQUIRE(e);
    CHECK(e->find("at least two attacks") != std::string::npos);

    auto with_rider = mound_doc("t.rider", 7);
    with_rider["traits"][0]["effect"]["options"][0]["free_rider"] = "slam";
    e = add_only(with_rider);
    REQUIRE(e);
    CHECK(e->find("can't be combined with free_rider") != std::string::npos);

    auto old = mound_doc("t.old", 7);
    old["$schema"] = "monster@4";
    e = add_only(old);
    REQUIRE(e);
    CHECK(e->find("needs a monster@5 file") != std::string::npos);
}

TEST_CASE("engulf_both_hits: both slams in a combat engulf the fighter") {
    auto lib = lib_of(mound_doc("t.mound", 40));
    const uint32_t sid = rules5e::spell_id_of("t.mound#slams#engulf");
    for (uint64_t seed = 1; seed < 40; ++seed) {
        world::Chunk ch{};
        ch.coord = {5, 5, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        ch.spawns.push_back({"t.mound", {6, 5}, world::spawn_stable_id(ch.coord, 0), ""});
        ch.spawns.push_back({kFighter, {5, 5}, world::spawn_stable_id(ch.coord, 1), ""});
        ch.content_hash = world::compute_content_hash(ch);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        const rules5e::CombatantId mound = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id;
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != mound; ++i) {
            Command end;
            end.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(end).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == mound);
        bool offered = false;
        for (const auto& q : c->legal_actions())
            offered = offered || (q.kind == ActionKind::CastSpell && q.spell_id == sid);
        CHECK_FALSE(offered);
        bool both = true;
        for (int swing = 0; swing < 2; ++swing) {
            Command cmd;
            cmd.request = {mound, ActionKind::Attack, foe, 0};
            cmd.request.multiattack = 1;
            cmd.request.target_within_5ft = true;
            const auto before = c->events().size();
            auto res = c->submit(cmd);
            REQUIRE_MESSAGE(res.ok, res.error);
            bool hit = false;
            for (std::size_t i = before; i < c->events().size(); ++i) {
                const auto& ev = c->events()[i];
                if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == rules5e::EventKind::AttackResolved && (ev.rules.c & 1))
                    hit = true;
            }
            c->settle(nullptr);
            both = both && hit;
        }
        if (!both) continue;
        const auto* t = rules5e::find(c->state(), foe);
        REQUIRE(t);
        CHECK(t->has(Condition::Restrained));
        CHECK(t->has(Condition::Blinded));
        CHECK_FALSE(t->has(Condition::Grappled));
        CHECK(rules5e::container_of(c->state(), foe) == mound);
        bool contained = false;
        for (const auto& e : c->state().effects)
            if (e.target == foe && e.contain == 1 && e.spell_id == sid && e.has_also && e.also == Condition::Blinded &&
                e.escape_check && e.dc == 14 && e.damage_when == 3)
                contained = true;
        CHECK(contained);
        return;
    }
    FAIL("no seed where both slams hit");
}
