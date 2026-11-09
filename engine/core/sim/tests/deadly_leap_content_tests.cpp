// B8: Bulette Deadly Leap, content and grid. The jump step may end in another creature's space.
// The action names the creatures already standing there. The fixture stays synthetic; srd51.monster.bulette is the authored block.
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <string>
#include <vector>

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

Json leap_action(int dc) {
    return {{"id", "deadly_leap"}, {"name", "Deadly Leap"}, {"kind", "deadly_leap"}, {"jump_ft", 15},
            {"save", {{"ability", "str"}, {"dc", dc}}}, {"save_alt", "dex"}, {"push_ft", 5},
            {"damage", Json::array({{{"dice", "3d8+1"}, {"type", "bludgeoning"}},
                                    {{"dice", "3d8+1"}, {"type", "piercing"}}})}};
}

Json bulette_doc(const std::string& id, int dc) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["size"] = "large";
    m["hit_points"]["average"] = 120;
    m["speed"]["walk"] = 40;
    m["actions"] = Json::array({leap_action(dc)});
    m["traits"] = Json::array();
    m.erase("behavior");
    return m;
}

Json fighter_doc(const std::string& id) {
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["id"] = id;
    tank["hit_points"]["average"] = 200;
    tank["abilities"]["str"] = 10;
    tank["abilities"]["dex"] = 10;
    return tank;
}

ContentLibrary lib_of(const Json& bulette, const Json& foe) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(foe, "character"));
    const auto e = lib.add(bulette, "monster");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    return lib;
}

struct Spawn { std::string id; Vec2i at; };

struct Arena {
    ContentLibrary lib;
    world::Chunk ch;
    ecs::Registry reg;
    std::optional<Combat> c;
    CombatantId leaper = 0;
};

void fill(Arena& a, const Json& bulette, const Json& foe, const std::vector<Spawn>& spawns, uint64_t seed = 1) {
    a.lib = lib_of(bulette, foe);
    a.ch = world::Chunk{};   // value-init: TileCell has no default, so Arena's default init leaves flags indeterminate
    a.ch.coord = {5, 5, 0};
    a.ch.origin = world::Chunk::Origin::Handcrafted;
    a.ch.palette = {"", "floor"};
    for (auto& cell : a.ch.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        a.ch.spawns.push_back({spawns[i].id, spawns[i].at, world::spawn_stable_id(a.ch.coord, uint32_t(i)), ""});
    a.ch.content_hash = world::compute_content_hash(a.ch);
    std::string err;
    a.c = Combat::create(a.reg, a.ch, a.lib, seed, {}, &err);
    REQUIRE_MESSAGE(a.c, err);
    a.leaper = a.ch.spawns[0].stable_id;
    for (int i = 0; i < 8 && rules5e::current_actor(a.c->state()) != a.leaper; ++i) {
        Command end;
        end.request = {rules5e::current_actor(a.c->state()), ActionKind::EndTurn};
        REQUIRE(a.c->submit(end).ok);
        a.c->settle(nullptr);
    }
    REQUIRE(rules5e::current_actor(a.c->state()) == a.leaper);
}

void step_to(Combat& c, Vec2i to, uint16_t jump) {
    Command st;
    st.kind = CommandKind::Step;
    st.to = to;
    st.request.jump_ft = jump;
    auto res = c.submit(st);
    const auto* me = rules5e::find(c.state(), rules5e::current_actor(c.state()));
    const Vec2i at = c.pos(rules5e::current_actor(c.state()));
    const std::string why = res.error + " move=" + std::to_string(c.state().budget.movement_ft) +
                            " speed=" + std::to_string(me ? int(me->speed_ft) : -1) +
                            " at=" + std::to_string(at.x) + "," + std::to_string(at.y) +
                            " to=" + std::to_string(to.x) + "," + std::to_string(to.y);
    REQUIRE_MESSAGE(res.ok, why);
    c.settle(nullptr);
}

void step_east(Combat& c, CombatantId id, uint16_t jump) {
    const Vec2i p = c.pos(id);
    step_to(c, {p.x + 1, p.y}, jump);
}

uint32_t leap_id(const std::string& arch) { return rules5e::spell_id_of(arch + "#deadly_leap"); }

std::vector<int> damages_since(const Combat& c, rules5e::CombatantId id, std::size_t from) {
    std::vector<int> out;
    for (std::size_t i = from; i < c.events().size(); ++i) {
        const auto& ev = c.events()[i];
        if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == rules5e::EventKind::DamageDealt && ev.rules.target == id)
            out.push_back(ev.rules.a);
    }
    return out;
}

}  // namespace

TEST_CASE("deadly_leap compiles to one save with both damages, and is not a multiattack") {
    auto lib = lib_of(bulette_doc("t.bulette", 16), fighter_doc(kFighter));
    const auto* arch = lib.find("t.bulette");
    REQUIRE(arch);
    CHECK(arch->base.multiattacks.empty());
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].leap);
    CHECK(arch->save_actions[0].id == "deadly_leap");
    const auto* d = lib.spells()->find(leap_id("t.bulette"));
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::Save);
    CHECK(d->is_spell == false);
    CHECK(d->magical == false);
    CHECK(d->requires_jumped_ft == 15);
    CHECK(d->has_save_choice);
    CHECK(d->save_ability == rules5e::Ability::Str);
    CHECK(d->save_alt == rules5e::Ability::Dex);
    CHECK(d->fixed_dc == 16);
    CHECK(d->on_save == rules5e::OnSave::Half);
    CHECK(d->dice.count == 3);
    CHECK(d->dice.sides == 8);
    CHECK(d->dice.bonus == 1);
    CHECK(d->damage_type == rules5e::DamageType::Bludgeoning);
    CHECK(d->has_also_damage);
    CHECK(d->also_dice.count == 3);
    CHECK(d->also_damage_type == rules5e::DamageType::Piercing);
    CHECK(d->condition == Condition::Prone);
    CHECK(d->condition_untracked);
    CHECK(d->leap_push_ft == 5);
    CHECK(d->targets == 8);
    bool known = false;
    for (uint32_t id : arch->base.spells_known) known = known || id == d->id;
    CHECK(known);
}

TEST_CASE("deadly_leap: the loader refuses a dishonest action") {
    auto bad = [&](Json doc) {
        ContentLibrary one;
        REQUIRE(one.load(sample_set()).empty());
        return one.add(doc, "monster");
    };
    auto jump = bulette_doc("t.bad", 16);
    jump["actions"][0]["jump_ft"] = 12;
    auto e = bad(jump);
    REQUIRE(e);
    CHECK(e->find("jump_ft must be") != std::string::npos);

    auto one = bulette_doc("t.one", 16);
    one["actions"][0]["damage"] = Json::array({{{"dice", "3d8+1"}, {"type", "bludgeoning"}}});
    e = bad(one);
    REQUIRE(e);
    CHECK(e->find("damage needs two components") != std::string::npos);

    auto same = bulette_doc("t.same", 16);
    same["actions"][0]["save_alt"] = "str";
    e = bad(same);
    REQUIRE(e);
    CHECK(e->find("save_alt must be a different ability") != std::string::npos);

    auto old = bulette_doc("t.old", 16);
    old["$schema"] = "monster@4";
    e = bad(old);
    REQUIRE(e);
    CHECK(e->find("needs a monster@5 file") != std::string::npos);

    auto extra = bulette_doc("t.extra", 16);
    extra["actions"][0]["on_success"] = "half";
    e = bad(extra);
    REQUIRE(e);
    CHECK(e->find("unknown key") != std::string::npos);
}

TEST_CASE("Deadly Leap on the grid: jump gate, landing, fail, success, push, replay") {
    const auto foe = fighter_doc(kFighter);
    // A 5 ft jump that ends in the space is not 15 ft. The action is not spent.
    {
        Arena a;
        fill(a, bulette_doc("t.short", 30), foe, {{"t.short", {4, 5}}, {kFighter, {8, 5}}});
        step_east(*a.c, a.leaper, 0);
        step_east(*a.c, a.leaper, 0);
        step_east(*a.c, a.leaper, 5);
        CHECK(a.c->state().budget.jumped_ft == 5);
        CHECK(a.c->pos(a.ch.spawns[1].stable_id).x > a.c->pos(a.leaper).x);
        Command cast;
        cast.request = {a.leaper, ActionKind::CastSpell, a.ch.spawns[1].stable_id};
        cast.request.spell_id = leap_id("t.short");
        cast.request.targets = {a.ch.spawns[1].stable_id};
        auto res = a.c->submit(cast);
        CHECK_FALSE(res.ok);
        CHECK(res.error == "has not jumped far enough");
        CHECK(a.c->state().budget.action);
    }
    // Not standing in the space: refused before the jump gate.
    {
        Arena a;
        fill(a, bulette_doc("t.far", 30), foe, {{"t.far", {4, 5}}, {kFighter, {8, 5}}});
        Command cast;
        cast.request = {a.leaper, ActionKind::CastSpell, a.ch.spawns[1].stable_id};
        cast.request.spell_id = leap_id("t.far");
        cast.request.targets = {a.ch.spawns[1].stable_id};
        auto res = a.c->submit(cast);
        CHECK_FALSE(res.ok);
        CHECK(res.error == "target is not in the landing space");
        CHECK(a.c->state().budget.action);
    }

    auto land = [](Arena& a) {
        const Vec2i origin = a.c->pos(a.leaper);
        step_east(*a.c, a.leaper, 5);
        step_east(*a.c, a.leaper, 5);
        step_east(*a.c, a.leaper, 5);
        CHECK(a.c->state().budget.jumped_ft == 15);
        CHECK(a.c->pos(a.leaper) == Vec2i{origin.x + 3, origin.y});
    };

    std::vector<int> failed_packets;
    {
        Arena a;
        fill(a, bulette_doc("t.fail", 30), foe, {{"t.fail", {4, 5}}, {kFighter, {8, 5}}});
        const auto foe_id = a.ch.spawns[1].stable_id;
        land(a);
        const auto from = a.c->events().size();
        const int hp = rules5e::find(a.c->state(), foe_id)->hp;
        Command cast;
        cast.request = {a.leaper, ActionKind::CastSpell, foe_id};
        cast.request.spell_id = leap_id("t.fail");
        cast.request.targets = {foe_id};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        const auto* t = rules5e::find(a.c->state(), foe_id);
        CHECK(t->has(Condition::Prone));
        CHECK(a.c->pos(foe_id).x == a.c->pos(a.leaper).x + 1);   // a failure is not pushed
        failed_packets = damages_since(*a.c, foe_id, from);
        REQUIRE(failed_packets.size() == 2);
        CHECK(t->hp == hp - (failed_packets[0] + failed_packets[1]));
        CHECK_FALSE(a.c->state().budget.action);
    }
    {
        Arena a;
        fill(a, bulette_doc("t.ok", 1), foe, {{"t.ok", {4, 5}}, {kFighter, {8, 5}}}, 1);
        const auto foe_id = a.ch.spawns[1].stable_id;
        land(a);
        const Vec2i stood = a.c->pos(foe_id);
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {a.leaper, ActionKind::CastSpell, foe_id};
        cast.request.spell_id = leap_id("t.ok");
        cast.request.targets = {foe_id};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        const auto* t = rules5e::find(a.c->state(), foe_id);
        CHECK_FALSE(t->has(Condition::Prone));
        // Straight away from the Large centre: the Medium one tile east of the anchor goes +x, -y.
        CHECK(a.c->pos(foe_id) == Vec2i{stood.x + 1, stood.y - 1});
        const auto packets = damages_since(*a.c, foe_id, from);
        REQUIRE(packets.size() == 2);
        REQUIRE(failed_packets.size() == 2);
        CHECK(packets[0] == failed_packets[0] / 2);
        CHECK(packets[1] == failed_packets[1] / 2);
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, a.ch, a.lib, 1, a.c->log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->log_hash() == a.c->log_hash());
        CHECK(rep->hash() == a.c->hash());
        CHECK(rep->pos(foe_id) == Vec2i{stood.x + 1, stood.y - 1});
    }
}

TEST_CASE("Deadly Leap: no unoccupied space, two occupants, immunity, and the jump hash") {
    const auto foe = fighter_doc(kFighter);
    {
        auto immune = foe;
        immune["condition_immunities"] = Json::array({"prone"});
        immune["damage_immunities"] = Json::array({"bludgeoning"});
        Arena a;
        fill(a, bulette_doc("t.imm", 30), immune, {{"t.imm", {4, 5}}, {kFighter, {8, 5}}});
        const auto foe_id = a.ch.spawns[1].stable_id;
        step_east(*a.c, a.leaper, 5);
        step_east(*a.c, a.leaper, 5);
        step_east(*a.c, a.leaper, 5);
        const auto from = a.c->events().size();
        Command cast;
        cast.request = {a.leaper, ActionKind::CastSpell, foe_id};
        cast.request.spell_id = leap_id("t.imm");
        cast.request.targets = {foe_id};
        auto res = a.c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        a.c->settle(nullptr);
        CHECK_FALSE(rules5e::find(a.c->state(), foe_id)->has(Condition::Prone));
        const auto packets = damages_since(*a.c, foe_id, from);
        int positive = 0, sum = 0;
        for (int n : packets) { sum += n; positive += n > 0; }
        CHECK(positive == 1);   // bludgeoning is immune; piercing remains
        CHECK(sum > 0);
    }
    {
        Json blocker = foe;
        blocker["id"] = "t.block";
        ContentLibrary lib;
        REQUIRE(lib.load(sample_set()).empty());
        REQUIRE_FALSE(lib.add(foe, "character"));
        REQUIRE_FALSE(lib.add(blocker, "character"));
        auto doc = bulette_doc("t.blockleap", 1);
        REQUIRE_FALSE(lib.add(doc, "monster"));
        world::Chunk ch{};
        ch.coord = {5, 5, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        ch.spawns.push_back({"t.blockleap", {4, 5}, world::spawn_stable_id(ch.coord, 0), ""});
        ch.spawns.push_back({kFighter, {8, 5}, world::spawn_stable_id(ch.coord, 1), ""});
        ch.spawns.push_back({"t.block", {9, 4}, world::spawn_stable_id(ch.coord, 2), ""});
        ch.content_hash = world::compute_content_hash(ch);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 1);
        REQUIRE(c);
        const auto leap = ch.spawns[0].stable_id, foe_id = ch.spawns[1].stable_id;
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != leap; ++i) {
            Command end;
            end.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(end).ok);
            c->settle(nullptr);
        }
        const Vec2i stood = c->pos(foe_id);
        step_east(*c, leap, 5);
        step_east(*c, leap, 5);
        step_east(*c, leap, 5);
        Command cast;
        cast.request = {leap, ActionKind::CastSpell, foe_id};
        cast.request.spell_id = leap_id("t.blockleap");
        cast.request.targets = {foe_id};
        auto res = c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        c->settle(nullptr);
        CHECK(rules5e::find(c->state(), foe_id)->has(Condition::Prone));
        CHECK(c->pos(foe_id) == stood);
    }
    {
        Json second = foe;
        second["id"] = "t.second";
        ContentLibrary lib;
        REQUIRE(lib.load(sample_set()).empty());
        REQUIRE_FALSE(lib.add(foe, "character"));
        REQUIRE_FALSE(lib.add(second, "character"));
        REQUIRE_FALSE(lib.add(bulette_doc("t.two", 30), "monster"));
        world::Chunk ch{};
        ch.coord = {5, 5, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        ch.spawns.push_back({"t.two", {4, 5}, world::spawn_stable_id(ch.coord, 0), ""});
        ch.spawns.push_back({kFighter, {8, 5}, world::spawn_stable_id(ch.coord, 1), ""});
        ch.spawns.push_back({"t.second", {7, 5}, world::spawn_stable_id(ch.coord, 2), ""});
        ch.content_hash = world::compute_content_hash(ch);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        const auto leap = ch.spawns[0].stable_id, a_id = ch.spawns[1].stable_id, b_id = ch.spawns[2].stable_id;
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != leap; ++i) {
            Command end;
            end.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(end).ok);
            c->settle(nullptr);
        }
        const Vec2i stood_a = c->pos(a_id), stood_b = c->pos(b_id);
        step_east(*c, leap, 5);
        step_east(*c, leap, 5);
        step_east(*c, leap, 5);
        Command cast;
        cast.request = {leap, ActionKind::CastSpell, a_id};
        cast.request.spell_id = leap_id("t.two");
        cast.request.targets = {a_id, b_id};
        auto res = c->submit(cast);
        REQUIRE_MESSAGE(res.ok, res.error);
        c->settle(nullptr);
        CHECK(rules5e::find(c->state(), a_id)->has(Condition::Prone));
        CHECK(rules5e::find(c->state(), b_id)->has(Condition::Prone));
        CHECK(c->pos(a_id) == stood_a);
        CHECK(c->pos(b_id) == stood_b);
    }
    {
        Arena with;
        fill(with, bulette_doc("t.hash", 16), foe, {{"t.hash", {4, 5}}, {kFighter, {8, 5}}});
        step_east(*with.c, with.leaper, 5);
        const uint64_t jumped = with.c->log_hash();
        CHECK(with.c->log().back().step.request.jump_ft == 5);
        Arena plain;
        fill(plain, bulette_doc("t.hash2", 16), foe, {{"t.hash2", {4, 5}}, {kFighter, {8, 5}}});
        step_east(*plain.c, plain.leaper, 0);
        CHECK(plain.c->log_hash() != jumped);
        ecs::Registry reg2;
        std::string err;
        auto rep = replay(reg2, with.ch, with.lib, 1, with.c->log(), &err);
        REQUIRE_MESSAGE(rep, err);
        CHECK(rep->log_hash() == jumped);
    }
}
