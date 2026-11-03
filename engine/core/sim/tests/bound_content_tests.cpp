// Bound content (CONTENT_CONVENTIONS §50): monster@5 trait `bound` fills Combatant::bound.radius_ft.
// The amulet wearer is spawn overrides.bound_wearer, not an item. Within 60 feet, half of the damage
// the wearer takes (rounded up) is transferred. Farther, nothing is. 0xE5 folds only while a wearer is set.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <utility>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::DamageType;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kBound = {{"kind", "bound"}, {"radius_ft", 60}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects, int hp) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "bound_" + std::to_string(i++)}, {"name", "Bound"}, {"description", "test"},
                               {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }

Json swing_action() {
    Json dmg = Json::object();
    dmg["flat"] = 11;
    dmg["type"] = "slashing";
    Json act = Json::object();
    act["id"] = "hack";
    act["name"] = "hack";
    act["kind"] = "melee_weapon_attack";
    act["to_hit"] = 30;
    act["reach"] = 5;
    act["targets"] = 1;
    act["damage"] = Json::array({dmg});
    return act;
}
Json striker_monster() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.striker";
    m["actions"] = Json::array({swing_action()});
    return m;
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
int attack_index(const rules5e::Combatant& c, const char* id) {
    for (int i = 0; i < int(c.attacks.size()); ++i)
        if (c.attacks[std::size_t(i)].id == id) return i;
    return -1;
}
bool last_attack_hit(const Combat& c) {
    int flags = -1;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::AttackResolved) flags = e.rules.c;
    return flags >= 0 && (flags & 1) != 0;
}
int dealt_to(const Combat& c, CombatantId id) {
    int n = 0;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::DamageDealt && e.rules.target == id)
            n += e.rules.a;
    return n;
}
}  // namespace

TEST_CASE("bound loader: radius 60, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json same = {{"kind", "bound"}, {"radius_ft", 60}};
    REQUIRE_FALSE(add(lib, creature("t.guard", {kBound, same}, 80)));
    const auto* t = lib.find("t.guard");
    REQUIRE(t);
    CHECK(t->base.bound.radius_ft == 60);
    CHECK(t->base.bound.wearer == 0);

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}, 80), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_rad", {{"kind", "bound"}, {"radius_ft", 7}}).find("radius_ft must be a multiple of 5 in 5..120") != std::string::npos);
    CHECK(err("t.e_miss", {{"kind", "bound"}}).find("radius_ft must be a multiple of 5 in 5..120") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "bound"}, {"radius_ft", 60}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kBound, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kBound, {{"kind", "bound"}, {"radius_ft", 30}}}, 80));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one bound") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.bound_pc";
    ch["features"].push_back({{"id", "bound"}, {"name", "Bound"}, {"description", "test"}, {"effect", kBound}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);
}

TEST_CASE("bound: radius with no wearer is not in Combat::hash; a wearer is 0xE5") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kBound}, 80)));
    REQUIRE_FALSE(add(lib, creature("t.wearer", {}, 40)));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    auto plain = arena({{"t.striker", Vec2i{5, 5}}, {"t.wearer", Vec2i{6, 5}}, {"t.guard", Vec2i{6, 17}}});
    plain.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, plain, lib, 3);
    REQUIRE(c);
    const uint64_t bare = c->hash();
    rules5e::Combatant* g = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), plain.spawns[2].stable_id));
    REQUIRE(g->bound.radius_ft == 60);
    REQUIRE(g->bound.wearer == 0);
    g->bound.radius_ft = 0;
    CHECK(c->hash() == bare);
    g->bound.radius_ft = 60;

    auto linked_chunk = plain;
    linked_chunk.spawns[2].overrides_json =
        std::string("{\"bound_wearer\":") + std::to_string(plain.spawns[1].stable_id) + "}";
    ecs::Registry reg2;
    auto linked = Combat::create(reg2, linked_chunk, lib, 3);
    REQUIRE(linked);
    CHECK(rules5e::find(linked->state(), linked_chunk.spawns[2].stable_id)->bound.wearer == plain.spawns[1].stable_id);
    CHECK(linked->hash() != bare);
    auto* lg = const_cast<rules5e::Combatant*>(rules5e::find(linked->state(), linked_chunk.spawns[2].stable_id));
    lg->bound.wearer = 0;
    CHECK(linked->hash() == bare);
}

TEST_CASE("bound: within 60 feet transfers half rounded up; 65 feet does not; replay keeps the override") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kBound}, 80)));
    REQUIRE_FALSE(add(lib, creature("t.wearer", {}, 40)));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const int hack = attack_index(lib.find("t.striker")->base, "hack");
    REQUIRE(hack >= 0);

    auto open = [&](int guard_y, uint64_t seed, ecs::Registry& reg) {
        auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.wearer", Vec2i{6, 5}}, {"t.guard", Vec2i{6, guard_y}}});
        ch.spawns[0].overrides_json = R"({"team":"party"})";
        ch.spawns[2].overrides_json =
            std::string("{\"bound_wearer\":") + std::to_string(ch.spawns[1].stable_id) + "}";
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        return std::pair{std::move(ch), std::move(c)};
    };

    bool near = false;
    for (uint64_t seed = 1; seed < 40 && !near; ++seed) {
        ecs::Registry reg;
        auto [ch, c] = open(17, seed, reg);
        const CombatantId striker = ch.spawns[0].stable_id;
        const CombatantId wearer = ch.spawns[1].stable_id;
        const CombatantId guard = ch.spawns[2].stable_id;
        to_actor(*c, striker);
        Command cmd;
        cmd.request = {striker, ActionKind::Attack, wearer, uint8_t(hack)};
        REQUIRE(c->submit(cmd).ok);
        if (!last_attack_hit(*c)) continue;
        near = true;
        CHECK(dealt_to(*c, guard) == 6);
        CHECK(dealt_to(*c, wearer) == 5);
        CHECK(rules5e::find(c->state(), wearer)->hp == 35);
        CHECK(rules5e::find(c->state(), guard)->hp == 74);
        ecs::Registry again;
        auto rp = replay(again, ch, lib, seed, c->log());
        REQUIRE(rp);
        CHECK(rp->hash() == c->hash());
        CHECK(rules5e::find(rp->state(), guard)->bound.wearer == wearer);
        CHECK(rules5e::find(rp->state(), wearer)->hp == 35);
        CHECK(rules5e::find(rp->state(), guard)->hp == 74);
    }
    REQUIRE(near);

    bool far = false;
    for (uint64_t seed = 1; seed < 40 && !far; ++seed) {
        ecs::Registry reg;
        auto [ch, c] = open(18, seed, reg);
        const CombatantId striker = ch.spawns[0].stable_id;
        const CombatantId wearer = ch.spawns[1].stable_id;
        const CombatantId guard = ch.spawns[2].stable_id;
        to_actor(*c, striker);
        Command cmd;
        cmd.request = {striker, ActionKind::Attack, wearer, uint8_t(hack)};
        REQUIRE(c->submit(cmd).ok);
        if (!last_attack_hit(*c)) continue;
        far = true;
        CHECK(dealt_to(*c, guard) == 0);
        CHECK(dealt_to(*c, wearer) == 11);
        CHECK(rules5e::find(c->state(), wearer)->hp == 29);
        CHECK(rules5e::find(c->state(), guard)->hp == 80);
        CHECK(rules5e::find(c->state(), guard)->bound.wearer == wearer);
        (void)ch;
    }
    REQUIRE(far);
}

TEST_CASE("bound: fire resistance on the wearer is applied before the half is transferred") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kBound}, 80)));
    REQUIRE_FALSE(add(lib, creature("t.wearer", {}, 40)));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const int hack = attack_index(lib.find("t.striker")->base, "hack");
    REQUIRE(hack >= 0);
    bool saw = false;
    for (uint64_t seed = 1; seed < 40 && !saw; ++seed) {
        auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.wearer", Vec2i{6, 5}}, {"t.guard", Vec2i{6, 17}}});
        ch.spawns[0].overrides_json = R"({"team":"party"})";
        ch.spawns[2].overrides_json = std::string("{\"bound_wearer\":") + std::to_string(ch.spawns[1].stable_id) + "}";
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        const CombatantId striker = ch.spawns[0].stable_id;
        const CombatantId wearer = ch.spawns[1].stable_id;
        const CombatantId guard = ch.spawns[2].stable_id;
        auto* w = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), wearer));
        auto* striker_c = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), striker));
        w->resistances |= rules5e::bit(DamageType::Fire);
        striker_c->attacks[std::size_t(hack)].type = DamageType::Fire;
        to_actor(*c, striker);
        Command cmd;
        cmd.request = {striker, ActionKind::Attack, wearer, uint8_t(hack)};
        REQUIRE(c->submit(cmd).ok);
        if (!last_attack_hit(*c)) continue;
        saw = true;
        CHECK(dealt_to(*c, wearer) == 2);   // 11 fire, resistance halves down to 5, floor stays
        CHECK(dealt_to(*c, guard) == 3);
        CHECK(rules5e::find(c->state(), wearer)->hp == 38);
        CHECK(rules5e::find(c->state(), guard)->hp == 77);
    }
    REQUIRE(saw);
}

TEST_CASE("bound: overrides.bound_wearer is refused without the trait, on the guardian, or for an unknown id") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kBound}, 80)));
    REQUIRE_FALSE(add(lib, creature("t.wearer", {}, 40)));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.wearer", Vec2i{6, 5}}, {"t.guard", Vec2i{6, 17}}});
    ch.spawns[1].overrides_json = std::string("{\"bound_wearer\":") + std::to_string(ch.spawns[0].stable_id) + "}";
    ecs::Registry reg_trait;
    std::string err;
    CHECK_FALSE(Combat::create(reg_trait, ch, lib, 1, &err));
    CHECK(err.find("needs the bound trait") != std::string::npos);

    ch.spawns[1].overrides_json.clear();
    ch.spawns[2].overrides_json = std::string("{\"bound_wearer\":") + std::to_string(ch.spawns[2].stable_id) + "}";
    ecs::Registry reg_self;
    err.clear();
    CHECK_FALSE(Combat::create(reg_self, ch, lib, 1, &err));
    CHECK(err.find("cannot be the guardian") != std::string::npos);

    ch.spawns[2].overrides_json = R"({"bound_wearer":1})";
    ecs::Registry reg_missing;
    err.clear();
    CHECK_FALSE(Combat::create(reg_missing, ch, lib, 1, &err));
    CHECK(err.find("not a combatant") != std::string::npos);
}
