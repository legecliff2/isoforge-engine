// Equipment AI (driver.cpp: equipment_command / light_command, CombatOptions::equipment only): draw, switch melee / ranged,
// thrown, pick up, light, douse with darkvision; determinism and a new pinned equipment-on AI replay golden. Equipment off
// never reaches any of it (existing goldens unchanged; see the off test below).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "sample_set.hpp"
#include "rules5e/equipment.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/events.hpp"
#include "sim/light_driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EquipOp;

namespace {

constexpr const char* kKitted = "test.ai.character.kitted";
constexpr const char* kGobKit = "test.ai.goblin_kit";          // scimitar held, shortbow carried
constexpr const char* kGobBare = "test.ai.goblin_bare";        // scimitar and shortbow carried, hands empty
constexpr const char* kGobArcher = "test.ai.goblin_archer";    // shortbow held (two hands), scimitar carried
constexpr const char* kGobThrower = "test.ai.goblin_thrower";  // javelin held, javelin + scimitar carried
constexpr const char* kGobBlind = "test.ai.goblin_nodark";     // no darkvision, scimitar + unlit srd torch held
constexpr const char* kGobTorch = "test.ai.goblin_torch";      // darkvision, scimitar + lit test torch held

using J = assets::Json;
J load(const char* rel) {
    std::ifstream f(std::filesystem::path(ENGINE_SOURCE_ROOT) / rel);
    return J::parse(f);
}

ContentLibrary& library() {
    static ContentLibrary l;
    static bool ready = false;
    if (ready) return l;
    ready = true;
    REQUIRE(l.load(sample_set()).empty());
    auto add_item = [&](const char* id, const rules5e::ItemDef& d) { if (!l.items()->find(id)) REQUIRE_FALSE(l.items()->add(id, d)); };
    rules5e::ItemDef torch;
    torch.kind = rules5e::ItemKind::Light;
    torch.light.states = {{20, 20}};
    torch.light.lit_by_default = true;
    add_item("test.ai.item.torch", torch);
    rules5e::ItemDef arrows;
    arrows.kind = rules5e::ItemKind::Ammunition;
    arrows.ammo_kind = rules5e::kAmmoArrow;
    add_item("test.ai.item.arrows", arrows);
    if (!l.items()->find("srd51.item.torch"))
        REQUIRE_FALSE(l.items()->add(J::parse(R"({"$schema": "item@2", "id": "srd51.item.torch", "name": "Torch", "category": "gear",
            "hands": 1, "light": {"bright_ft": 20, "dim_ft": 20, "shape": "radius", "duration_rounds": 600, "lit_by_default": false,
            "preset": "srd51.core.light.torch"}, "torch_attack": {"damage": [{"flat": 1, "type": "fire"}], "requires_lit": true}})")));
    const J gob = load("content/packs/srd51.core/monsters/goblin.json");
    auto item = [](const char* id, bool equipped) { return J{{"item", id}, {"equipped", equipped}}; };
    auto monster = [&](const char* id, J equipment, bool darkvision = true, bool javelin = false) {
        J g = gob;
        g["id"] = id;
        g["equipment"] = std::move(equipment);
        if (!darkvision) g["senses"] = J{{"passive_perception", 9}};
        if (javelin) {   // the javelin replaces the shortbow action (an action without an item would count as natural)
            J acts = J::array();
            for (const auto& x : g["actions"]) if (x.value("id", "") != "shortbow") acts.push_back(x);
            g["actions"] = acts;
            g["actions"].push_back(J{{"id", "javelin"}, {"name", "Javelin"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 4},
                                     {"range", J::array({30, 120})}, {"targets", 1}, {"properties", J::array({"thrown"})},
                                     {"damage", J::array({J{{"dice", "1d6+2"}, {"type", "piercing"}}})}});
        }
        REQUIRE_FALSE(l.add(g, "monster", "", 4));
    };
    monster(kGobKit, J::array({item("srd51.item.scimitar", true), item("srd51.item.shortbow", false)}));
    monster(kGobBare, J::array({item("srd51.item.scimitar", false), item("srd51.item.shortbow", false)}));
    monster(kGobArcher, J::array({item("srd51.item.shortbow", true), item("srd51.item.scimitar", false)}));
    monster(kGobThrower, J::array({item("srd51.item.javelin", true), item("srd51.item.javelin", false),
                                   item("srd51.item.scimitar", false)}), true, true);
    monster(kGobBlind, J::array({item("srd51.item.scimitar", true), item("srd51.item.torch", true)}), false);
    monster(kGobTorch, J::array({item("srd51.item.scimitar", true), item("test.ai.item.torch", true)}));
    J k = load("content/packs/srd51.core/characters/fighter_level_1.json");
    k["id"] = kKitted;
    k["equipment"] = J::array({item("srd51.item.chain_mail", true), item("srd51.item.longsword", true),
                               item("test.ai.item.torch", true), item("srd51.item.javelin", false), item("srd51.item.longbow", false),
                               J{{"item", "test.ai.item.arrows"}, {"count", 6}}});
    J acts = J::array();
    for (const auto& x : k["actions"]) if (x.value("id", "") == "longsword") acts.push_back(x);
    acts.push_back(J{{"id", "javelin"}, {"name", "Javelin"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 5},
                     {"range", J::array({30, 120})}, {"targets", 1}, {"properties", J::array({"thrown"})},
                     {"damage", J::array({J{{"dice", "1d6+3"}, {"type", "piercing"}}})}});
    acts.push_back(J{{"id", "longbow"}, {"name", "Longbow"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 4},
                     {"range", J::array({150, 600})}, {"targets", 1}, {"properties", J::array({"ammunition", "heavy", "two_handed"})},
                     {"damage", J::array({J{{"dice", "1d8+2"}, {"type", "piercing"}}})}});
    k["actions"] = acts;
    REQUIRE_FALSE(l.add(k, "character", "", 4));
    return l;
}

// Fighter (party) at (5,5), the monster `gap` tiles east.
world::Chunk arena(const char* monster, int gap, const char* pc = "srd51.character.fighter_level_1") {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {pc, monster};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + gap * int(i)), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

Command rules(rules5e::ActionRequest q) { Command c; c.request = q; return c; }
void until_turn(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i)
        REQUIRE(c.submit(rules({rules5e::current_actor(c.state()), ActionKind::EndTurn})).ok);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
uint32_t instance_of(const Combat& c, CombatantId who, const char* id) {
    const uint32_t d = c.item_defs()->find(id)->id;
    const auto* lo = c.loadout(who);
    for (const auto* h : {&lo->view.main, &lo->view.off}) if (h->instance && h->def == d) return h->instance;
    for (const auto& h : lo->view.carried) if (h.def == d) return h.instance;
    for (const auto& g : c.state().ground_items) if (g.item.def == d) return g.item.instance;
    return 0;
}
uint8_t attack_named(const Combat& c, CombatantId who, std::string_view id) {
    const auto* k = rules5e::find(c.state(), who);
    for (std::size_t i = 0; i < k->attacks.size(); ++i) if (k->attacks[i].id == id) return uint8_t(i);
    FAIL("no attack " << id);
    return 0;
}
bool is_equip(const Command& x, EquipOp op, uint32_t item) {
    return x.kind == CommandKind::Rules && x.request.kind == ActionKind::UseObject && x.request.equip_op == uint8_t(op) &&
           x.request.equip_item == item;
}
// The AI's command for the current actor; asserts it is one of Combat::legal_actions() when it is an equip op, then submits it.
Command ai_step(Combat& c, CombatantId target) {
    const Command x = next_command(c, target);
    if (x.request.kind == ActionKind::UseObject) {
        bool found = false;
        for (const auto& r : c.legal_actions())
            found = found || (r.kind == ActionKind::UseObject && r.equip_op == x.request.equip_op && r.equip_item == x.request.equip_item &&
                              r.equip_hand == x.request.equip_hand && r.equip_state == x.request.equip_state);
        CHECK(found);
    }
    const auto r = c.submit(x);
    REQUIRE_MESSAGE(r.ok, r.error);
    return x;
}

struct Setup {
    world::Chunk ch;
    ecs::Registry reg;
    std::optional<Combat> c;
    CombatantId pc = 0, mon = 0;
    Setup(const char* monster, int gap, uint64_t seed = 11) : ch(arena(monster, gap)) {
        std::string err;
        c = Combat::create(reg, ch, library(), seed, CombatOptions{true}, &err);
        REQUIRE_MESSAGE(c, err);
        pc = ch.spawns[0].stable_id;
        mon = ch.spawns[1].stable_id;
        REQUIRE(c->loadout(mon));
        until_turn(*c, mon);
    }
};

}  // namespace

TEST_CASE("equipment AI: empty hands draw the melee weapon (free interaction) with a hostile adjacent, then attack") {
    Setup s(kGobBare, 1);
    const uint32_t scim = instance_of(*s.c, s.mon, "srd51.item.scimitar");
    const Command a = ai_step(*s.c, s.pc);
    CHECK(is_equip(a, EquipOp::Draw, scim));
    CHECK(s.c->loadout(s.mon)->view.interaction_used);
    const Command b = ai_step(*s.c, s.pc);
    CHECK(b.request.kind == ActionKind::Attack);
    CHECK(b.request.attack_index == attack_named(*s.c, s.mon, "scimitar"));
}

TEST_CASE("equipment AI: switch - next to a hostile the archer draws its scimitar; far away the scimitar is dropped for the bow") {
    {
        // Rules hold a two-handed bow in the main hand with the off hand still free (equip_legal lists the draw): no drop is
        // needed, so the scimitar is drawn into the off hand rather than shooting with disadvantage.
        Setup s(kGobArcher, 1);
        const uint32_t scim = instance_of(*s.c, s.mon, "srd51.item.scimitar");
        CHECK(is_equip(ai_step(*s.c, s.pc), EquipOp::Draw, scim));
        const Command x = ai_step(*s.c, s.pc);
        CHECK(x.request.kind == ActionKind::Attack);
        CHECK(x.request.attack_index == attack_named(*s.c, s.mon, "scimitar"));
    }
    {
        Setup s(kGobKit, 10);   // 50 ft: no melee this turn (45 ft of movement needed, 30 ft speed); shortbow in range
        const uint32_t bow = instance_of(*s.c, s.mon, "srd51.item.shortbow");
        const uint32_t scim = instance_of(*s.c, s.mon, "srd51.item.scimitar");
        CHECK(is_equip(ai_step(*s.c, s.pc), EquipOp::Drop, scim));   // the bow needs both hands; stowing would spend the
                                                                       // interaction the draw needs, dropping is free
        CHECK(is_equip(ai_step(*s.c, s.pc), EquipOp::Draw, bow));
        const Command x = ai_step(*s.c, s.pc);
        CHECK(x.request.kind == ActionKind::Attack);
        CHECK(x.request.attack_index == attack_named(*s.c, s.mon, "shortbow"));
        CHECK(x.request.target == s.pc);
    }
}

TEST_CASE("equipment AI: out of melee reach but within thrown range, the held javelin is thrown (no draw of the scimitar)") {
    Setup s(kGobThrower, 8);   // 40 ft: 35 ft of movement to reach, javelin 30/120
    const Command x = ai_step(*s.c, s.pc);
    CHECK(x.request.kind == ActionKind::Attack);
    CHECK(x.request.attack_index == attack_named(*s.c, s.mon, "javelin"));
    CHECK(s.c->state().ground_items.size() == 1);   // it lands on the target's tile
}

TEST_CASE("equipment AI: a weapon on the ground within 5 ft is picked up when it beats what is carried") {
    Setup s(kGobKit, 1);
    const uint32_t scim = instance_of(*s.c, s.mon, "srd51.item.scimitar");
    rules5e::ActionRequest drop{s.mon, ActionKind::UseObject, 0};
    drop.equip_op = uint8_t(EquipOp::Drop);
    drop.equip_item = scim;
    REQUIRE(s.c->submit(rules(drop)).ok);
    REQUIRE(s.c->state().ground_items.size() == 1);
    // Hands empty; the shortbow (carried) would shoot with disadvantage next to the fighter: the scimitar on the ground wins.
    CHECK(is_equip(ai_step(*s.c, s.pc), EquipOp::PickUp, scim));
    CHECK(s.c->state().ground_items.empty());
    CHECK(ai_step(*s.c, s.pc).request.kind == ActionKind::Attack);
}

TEST_CASE("equipment AI: no darkvision, a dark tile (light driver frame) - the held torch is lit with the free interaction") {
    world::Chunk ch = arena(kGobBlind, 1);
    LightProviderInputs in;
    in.chunks = {&ch};   // default ambient: Dark
    LightDriver d(std::move(in));
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, library(), 11, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id, mon = ch.spawns[1].stable_id;
    const uint32_t torch = instance_of(*c, mon, "srd51.item.torch");
    until_turn(*c, mon);
    // Without a driver there is no light information: the AI leaves the torch alone and fights.
    CHECK_FALSE(light_command(*c, mon, pc));
    c->attach_light_driver(&d);
    REQUIRE(ai_light_at(*c, c->pos(mon)) == Light::Dark);
    const Command x = ai_step(*c, pc);
    CHECK(is_equip(x, EquipOp::SetLightState, torch));
    CHECK(x.request.equip_state == 0);
    const auto lights = c->equipment_lights();
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].lit);
    CHECK(ai_step(*c, pc).request.kind == ActionKind::Attack);
    c->attach_light_driver(nullptr);
}

TEST_CASE("equipment AI: darkvision and no ally needing the light - the lit torch is doused") {
    Setup s(kGobTorch, 1);
    const uint32_t torch = instance_of(*s.c, s.mon, "test.ai.item.torch");
    REQUIRE(s.c->equipment_lights().at(0).lit);
    const Command x = ai_step(*s.c, s.pc);
    CHECK(is_equip(x, EquipOp::SetLightState, torch));
    CHECK(x.request.equip_state == rules5e::kLightDouse);
    CHECK_FALSE(s.c->equipment_lights().at(0).lit);
    CHECK(ai_step(*s.c, s.pc).request.kind == ActionKind::Attack);   // nothing else to toggle: it fights
}

namespace {
struct Run { uint64_t hash = 0, log_hash = 0, events = 0; uint32_t commands = 0; int equip_ops = 0; bool ended = false; };
Run ai_run(const world::Chunk& ch, uint64_t seed, CombatOptions opt, std::vector<LogEntry>* log = nullptr) {
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, library(), seed, opt, &err);
    REQUIRE_MESSAGE(c, err);
    NearestEnemyPolicy p;
    const RunResult rr = run_to_end(*c, [&](CombatantId) { return &p; }, 400);
    REQUIRE_MESSAGE(rr.error.empty(), rr.error);
    Run out;
    out.hash = c->hash();
    out.log_hash = c->log_hash();
    out.events = event_stream_hash(c->events());
    out.commands = rr.commands;
    out.ended = rr.ended;
    for (const auto& e : c->log()) out.equip_ops += e.step.request.equip_op != 0;
    if (log) *log = c->log();
    return out;
}
world::Chunk duel() {
    world::Chunk ch = arena(kGobArcher, 6, kKitted);
    ch.spawns.push_back({kGobBare, Vec2i{12, 7}, world::spawn_stable_id(ch.coord, 2u), ""});
    ch.content_hash = world::compute_content_hash(ch);
    return ch;
}
}  // namespace

TEST_CASE("equipment AI: deterministic - the same seed twice gives the same log_hash, hash and events") {
    const world::Chunk ch = duel();
    const Run a = ai_run(ch, 2026, CombatOptions{true});
    const Run b = ai_run(ch, 2026, CombatOptions{true});
    CHECK(a.ended);
    CHECK(a.equip_ops > 0);
    CHECK(a.log_hash == b.log_hash);
    CHECK(a.hash == b.hash);
    CHECK(a.events == b.events);
    CHECK(a.commands == b.commands);
}

TEST_CASE("equipment AI replay golden (new, pinned): an AI-driven equipment-on fight replays from its log alone") {
    const world::Chunk ch = duel();
    std::vector<LogEntry> log;
    const Run a = ai_run(ch, 2026, CombatOptions{true}, &log);
    REQUIRE(a.ended);
    ecs::Registry rr;
    std::string err;
    auto b = replay(rr, ch, library(), 2026, CombatOptions{true}, log, &err);
    REQUIRE_MESSAGE(b, err);
    CHECK(b->hash() == a.hash);
    CHECK(b->log_hash() == a.log_hash);
    CHECK(event_stream_hash(b->events()) == a.events);
    MESSAGE("ai equipment golden hash=" << a.hash << " log_hash=" << a.log_hash << " events=" << a.events
            << " commands=" << a.commands << " equip_ops=" << a.equip_ops);
    CHECK(a.hash == 363147187033031642ull);
    CHECK(a.log_hash == 10762631187222608584ull);
    CHECK(a.events == 10391661412370034418ull);
}

TEST_CASE("equipment AI: equipment off never consults it - the AI fight equals the option-less combat") {
    const world::Chunk ch = duel();
    const Run off = ai_run(ch, 2026, CombatOptions{});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, library(), 2026);
    REQUIRE(c);
    NearestEnemyPolicy p;
    REQUIRE(run_to_end(*c, [&](CombatantId) { return &p; }, 400).error.empty());
    CHECK(off.equip_ops == 0);
    CHECK(c->hash() == off.hash);
    CHECK(c->log_hash() == off.log_hash);
    CHECK(event_stream_hash(c->events()) == off.events);
}
