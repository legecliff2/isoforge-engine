// Freeze content (CONTENT_CONVENTIONS §42): monster@5 trait `freeze` fills Combatant::freeze.
// The rules path is engine/rules/tests/freeze_tests.cpp. A loaded cold hit applies speed_sub 20 until the end
// of the creature's next turn. No SIM_EVENTS tag (the Speed row is the existing 0xA2). by / speed_sub are static.
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
const Json kFreeze = {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 20}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = 114;
    m["speed"] = {{"walk", 30}, {"swim", 90}};
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "freeze_" + std::to_string(i++)}, {"name", "Freeze"}, {"description", "test"},
                               {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }

Json swing_action(const char* id, const char* type) {
    Json dmg = Json::object();
    dmg["flat"] = 6;
    dmg["type"] = type;
    Json act = Json::object();
    act["id"] = id;
    act["name"] = id;
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
    m["actions"] = Json::array({swing_action("hack", "slashing"), swing_action("chill", "cold")});
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
rules5e::Combatant* poke(Combat& c, CombatantId id) {
    return const_cast<rules5e::Combatant*>(rules5e::find(c.state(), id));
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
void swing(Combat& c, CombatantId who, CombatantId target, int index) {
    to_actor(c, who);
    Command cmd;
    cmd.request = {who, ActionKind::Attack, target, uint8_t(index)};
    REQUIRE(c.submit(cmd).ok);
    REQUIRE(last_attack_hit(c));
}
const rules5e::ActiveModifier* freeze_row(const rules5e::EncounterState& s, CombatantId id, uint32_t mod) {
    for (const auto& m : s.modifiers)
        if (m.target == id && m.spec.id == mod) return &m;
    return nullptr;
}
}  // namespace

TEST_CASE("freeze loader: cold and 20 feet, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json swapped = {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 20}};
    REQUIRE_FALSE(add(lib, creature("t.freeze", {kFreeze, swapped})));
    const auto* t = lib.find("t.freeze");
    REQUIRE(t);
    CHECK(t->base.freeze.by == rules5e::bit(DamageType::Cold));
    CHECK(t->base.freeze.speed_sub == 20);
    CHECK(t->base.freeze.modifier_id == rules5e::spell_id_of("t.freeze#freeze_0#freeze"));
    CHECK(t->base.speed_ft == 30);
    CHECK(t->base.swim_ft == 90);

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_sub0", {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 0}}).find("speed_sub must be an integer 1..255") != std::string::npos);
    CHECK(err("t.e_subbig", {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 256}}).find("speed_sub must be an integer 1..255") != std::string::npos);
    CHECK(err("t.e_submiss", {{"kind", "freeze"}, {"by", {"cold"}}}).find("speed_sub must be an integer 1..255") != std::string::npos);
    CHECK(err("t.e_empty", {{"kind", "freeze"}, {"by", Json::array()}, {"speed_sub", 20}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_duptype", {{"kind", "freeze"}, {"by", {"cold", "cold"}}, {"speed_sub", 20}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_badtype", {{"kind", "freeze"}, {"by", {"holy"}}, {"speed_sub", 20}}).find("bad by") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 20}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kFreeze, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kFreeze, {{"kind", "freeze"}, {"by", {"cold"}}, {"speed_sub", 10}}}));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one freeze") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.freeze_pc";
    ch["features"].push_back({{"id", "freeze"}, {"name", "Freeze"}, {"description", "test"}, {"effect", kFreeze}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);

    const auto* gob = lib.find("srd51.monster.goblin");
    REQUIRE(gob);
    CHECK(gob->base.freeze.by == 0u);
    CHECK(gob->base.freeze.speed_sub == 0);
}

TEST_CASE("freeze: static config is not in Combat::hash") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.freeze", {kFreeze})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.freeze", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId elem = ch.spawns[1].stable_id;
    const uint64_t h0 = c->hash();
    const uint64_t l0 = c->log_hash();
    rules5e::Combatant* t = poke(*c, elem);
    REQUIRE(t->freeze.by == rules5e::bit(DamageType::Cold));
    REQUIRE(t->freeze.speed_sub == 20);
    t->freeze.by = 0;
    t->freeze.speed_sub = 0;
    t->freeze.modifier_id = 0;
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);
}

TEST_CASE("freeze: a loaded cold hit slows the next turn, and a replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.freeze", {kFreeze})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const auto* striker_arch = lib.find("t.striker");
    REQUIRE(striker_arch);
    const int hack = attack_index(striker_arch->base, "hack");
    const int chill = attack_index(striker_arch->base, "chill");
    REQUIRE(hack >= 0);
    REQUIRE(chill >= 0);
    constexpr uint64_t kSeed = 11;
    const uint32_t mod = rules5e::spell_id_of("t.freeze#freeze_0#freeze");
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.freeze", Vec2i{6, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId striker = ch.spawns[0].stable_id;
    const CombatantId elem = ch.spawns[1].stable_id;

    if (rules5e::current_actor(c->state()) == elem) end_turn(*c);
    swing(*c, striker, elem, hack);
    CHECK(poke(*c, elem)->hp == 108);
    CHECK(freeze_row(c->state(), elem, mod) == nullptr);
    to_actor(*c, elem);
    CHECK(c->state().budget.movement_ft == 30);

    end_turn(*c);
    swing(*c, striker, elem, chill);
    CHECK(poke(*c, elem)->hp == 102);
    const auto* m = freeze_row(c->state(), elem, mod);
    REQUIRE(m);
    CHECK(m->spec.speed_sub == 20);
    CHECK(m->timed.kind == rules5e::Expiry::EndOfTargetNextTurn);
    CHECK(m->timed.rounds_left == 1);
    CHECK(rules5e::modified_speed(c->state(), *rules5e::find(c->state(), elem)) == 10);
    CHECK(rules5e::mode_speed(c->state(), *rules5e::find(c->state(), elem), 2) == 70);
    to_actor(*c, elem);
    CHECK(c->state().budget.movement_ft == 10);
    end_turn(*c);
    CHECK(freeze_row(c->state(), elem, mod) == nullptr);
    to_actor(*c, elem);
    CHECK(c->state().budget.movement_ft == 30);

    ecs::Registry again;
    auto rp = replay(again, ch, lib, kSeed, c->log());
    REQUIRE(rp);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
    CHECK(rules5e::find(rp->state(), elem)->hp == 102);
    CHECK(rp->state().budget.movement_ft == 30);
}

struct Fight { world::Chunk ch; ecs::Registry reg; std::optional<Combat> c; CombatantId striker; CombatantId elem; };

void open_fight(Fight& f, ContentLibrary& lib, uint64_t seed) {
    f.ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.freeze", Vec2i{6, 5}}});
    f.ch.spawns[0].overrides_json = R"({"team":"party"})";
    f.c = Combat::create(f.reg, f.ch, lib, seed);
    REQUIRE(f.c);
    f.striker = f.ch.spawns[0].stable_id;
    f.elem = f.ch.spawns[1].stable_id;
    if (rules5e::current_actor(f.c->state()) == f.elem) end_turn(*f.c);
}

TEST_CASE("freeze: a second cold hit does not stack, immunity deals none, and a kill applies nothing") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.freeze", {kFreeze})));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const int chill = attack_index(lib.find("t.striker")->base, "chill");
    REQUIRE(chill >= 0);
    const uint32_t mod = rules5e::spell_id_of("t.freeze#freeze_0#freeze");
    {
        Fight f;
        open_fight(f, lib, 3);
        poke(*f.c, f.striker)->attacks_per_action = 2;
        swing(*f.c, f.striker, f.elem, chill);
        swing(*f.c, f.striker, f.elem, chill);
        int rows = 0;
        for (const auto& m : f.c->state().modifiers)
            if (m.target == f.elem && m.spec.stat == rules5e::ModStat::Speed) ++rows;
        CHECK(rows == 1);
        CHECK(rules5e::modified_speed(f.c->state(), *rules5e::find(f.c->state(), f.elem)) == 10);
    }
    {
        Fight f;
        open_fight(f, lib, 4);
        poke(*f.c, f.elem)->immunities |= rules5e::bit(DamageType::Cold);
        const int hp = poke(*f.c, f.elem)->hp;
        swing(*f.c, f.striker, f.elem, chill);
        CHECK(poke(*f.c, f.elem)->hp == hp);
        CHECK(freeze_row(f.c->state(), f.elem, mod) == nullptr);
    }
    {
        Fight f;
        open_fight(f, lib, 5);
        poke(*f.c, f.elem)->hp = 1;
        swing(*f.c, f.striker, f.elem, chill);
        CHECK(poke(*f.c, f.elem)->dead);
        CHECK(freeze_row(f.c->state(), f.elem, mod) == nullptr);
    }
}
