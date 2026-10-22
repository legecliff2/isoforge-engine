// docs/proposals/EQUIPMENT.md 9 (Engine Core): CombatOptions::equipment. Off: nothing changes (no loadout, hook, fold). On:
// loadouts from character@4 equipment[], apply_equip (draw / stow / drop / pick up / shield / hand over), the one free object
// interaction per turn, recompute triggers, ground items (entities, 0xD2), PickUp / HandOver legal actions, equipment lights.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "sample_set.hpp"
#include "rules5e/equipment.hpp"
#include "sim/combat.hpp"
#include "sim/events.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
    static std::shared_ptr<const rules5e::SpellBook>& spells(Combat& c) { return c.spells_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EquipOp;

namespace {

constexpr const char* kFighter = "srd51.character.fighter_level_1";
constexpr const char* kTorchbearer = "test.character.torchbearer";
constexpr const char* kThrower = "test.character.thrower";
constexpr const char* kArcher = "test.character.archer";
constexpr const char* kLanterns = "test.character.lanterns";
constexpr const char* kKitted = "test.character.kitted";
constexpr const char* kGoblinKit = "test.monster.goblin_kit";

assets::Json fighter_doc() {
    std::ifstream f(std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core/characters/fighter_level_1.json");
    return assets::Json::parse(f);
}

world::Chunk arena(const char* pc, int gap = 3) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {pc, "srd51.monster.goblin"};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + gap * int(i)), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

// The sample set, plus a test torch (item@2 light, built in code) and a fighter carrying it in the off hand.
ContentLibrary& library() {
    static ContentLibrary l;
    static bool ready = false;
    if (!ready) {
        ready = true;
        REQUIRE(l.load(sample_set()).empty());
        rules5e::ItemDef torch;
        torch.kind = rules5e::ItemKind::Light;
        torch.hands = 1;
        torch.light.states = {{20, 20}};
        torch.light.lit_by_default = true;
        REQUIRE_FALSE(l.items()->add("test.item.torch", torch));
        std::ifstream f(std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core/characters/fighter_level_1.json");
        assets::Json doc = assets::Json::parse(f);
        doc["id"] = kTorchbearer;
        doc["equipment"] = assets::Json::array({assets::Json{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                                assets::Json{{"item", "srd51.item.longsword"}, {"equipped", true}},
                                                assets::Json{{"item", "test.item.torch"}, {"equipped", true}},
                                                assets::Json{{"item", "srd51.item.shield"}}});
        REQUIRE_FALSE(l.add(doc, "character", "", 4));
        rules5e::ItemDef arrows;
        arrows.kind = rules5e::ItemKind::Ammunition;
        arrows.ammo_kind = rules5e::kAmmoArrow;
        REQUIRE_FALSE(l.items()->add("test.item.arrows", arrows));
        using J = assets::Json;
        J t = fighter_doc();
        t["id"] = kThrower;
        t["equipment"] = J::array({J{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                   J{{"item", "srd51.item.javelin"}, {"equipped", true}},
                                   J{{"item", "srd51.item.javelin"}}});
        t["actions"] = J::array({J{{"id", "javelin"}, {"name", "Javelin"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 5},
                                   {"range", J::array({30, 120})}, {"targets", 1}, {"properties", J::array({"thrown"})},
                                   {"damage", J::array({J{{"dice", "1d6+3"}, {"type", "piercing"}}})}}});
        REQUIRE_FALSE(l.add(t, "character", "", 4));
        J a = fighter_doc();
        a["id"] = kArcher;
        a["equipment"] = J::array({J{{"item", "srd51.item.longbow"}, {"equipped", true}},
                                   J{{"item", "test.item.arrows"}, {"count", 2}}});
        a["actions"] = J::array({J{{"id", "longbow"}, {"name", "Longbow"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 4},
                                   {"range", J::array({150, 600})}, {"targets", 1},
                                   {"properties", J::array({"ammunition", "heavy", "two_handed"})},
                                   {"damage", J::array({J{{"dice", "1d8+2"}, {"type", "piercing"}}})}}});
        REQUIRE_FALSE(l.add(a, "character", "", 4));
        // item@2 gear lights as in #250 (content/light-gear), fed straight to the table (item@2 loads via #246).
        // Stacked with #250 the real srd51 gear is already loaded from the pack: keep that copy (same light blocks).
        auto add_gear = [&](const J& d) { if (!l.items()->find(d["id"].get<std::string>())) REQUIRE_FALSE(l.items()->add(d)); };
        add_gear(J::parse(R"({"$schema": "item@2", "id": "srd51.item.torch", "name": "Torch", "category": "gear",
            "hands": 1, "light": {"bright_ft": 20, "dim_ft": 20, "shape": "radius", "duration_rounds": 600, "lit_by_default": false,
            "preset": "srd51.core.light.torch"}, "torch_attack": {"damage": [{"flat": 1, "type": "fire"}], "requires_lit": true}})"));
        add_gear(J::parse(R"({"$schema": "item@2", "id": "srd51.item.lantern_hooded", "name": "Lantern, hooded",
            "category": "gear", "hands": 1, "light": {"bright_ft": 30, "dim_ft": 30, "shape": "radius", "duration_rounds": 3600,
            "fuel": "srd51.item.oil_flask", "lit_by_default": false, "states": [{"id": "hooded", "bright_ft": 0, "dim_ft": 5,
            "toggle": "action"}], "preset": "srd51.core.light.lantern_hooded"}})"));
        add_gear(J::parse(R"({"$schema": "item@2", "id": "srd51.item.lantern_bullseye", "name": "Lantern, bullseye",
            "category": "gear", "hands": 1, "light": {"bright_ft": 60, "dim_ft": 60, "shape": "cone", "duration_rounds": 3600,
            "fuel": "srd51.item.oil_flask", "lit_by_default": false, "preset": "srd51.core.light.lantern_bullseye"}})"));
        J g = fighter_doc();
        g["id"] = kLanterns;
        g["equipment"] = J::array({J{{"item", "srd51.item.lantern_hooded"}, {"equipped", true}},
                                   J{{"item", "srd51.item.torch"}, {"equipped", true}},
                                   J{{"item", "srd51.item.lantern_bullseye"}}});
        REQUIRE_FALSE(l.add(g, "character", "", 4));
        J k = fighter_doc();
        k["id"] = kKitted;
        k["equipment"] = J::array({J{{"item", "srd51.item.chain_mail"}, {"equipped", true}},
                                   J{{"item", "srd51.item.longsword"}, {"equipped", true}},
                                   J{{"item", "test.item.torch"}, {"equipped", true}},
                                   J{{"item", "srd51.item.javelin"}}, J{{"item", "srd51.item.longbow"}},
                                   J{{"item", "test.item.arrows"}, {"count", 6}}});
        J acts = J::array();
        for (const auto& x : k["actions"]) if (x.value("id", "") == "longsword") acts.push_back(x);
        acts.push_back(t["actions"][0]);
        acts.push_back(a["actions"][0]);
        k["actions"] = acts;
        REQUIRE_FALSE(l.add(k, "character", "", 4));
        std::ifstream gf(std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core/monsters/goblin.json");
        J gob = J::parse(gf);
        gob["id"] = kGoblinKit;
        gob["equipment"] = J::array({J{{"item", "srd51.item.scimitar"}, {"equipped", true}}, J{{"item", "srd51.item.shortbow"}}});
        REQUIRE_FALSE(l.add(gob, "monster", "", 4));
    }
    return l;
}

Command rules(rules5e::ActionRequest q) { Command c; c.request = q; return c; }
rules5e::ActionRequest equip(CombatantId a, EquipOp op, uint32_t item, uint8_t hand = 0, CombatantId to = 0) {
    rules5e::ActionRequest r{a, ActionKind::UseObject, 0};
    r.equip_op = uint8_t(op); r.equip_item = item; r.equip_hand = hand; r.equip_to = to;
    return r;
}
// Ends turns until `who` acts.
void until_turn(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i)
        REQUIRE(c.submit(rules({rules5e::current_actor(c.state()), ActionKind::EndTurn})).ok);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
uint32_t item_of(const ItemDefTable& t, const rules5e::HeldItem& h, const char* id) {
    return t.find(id) && h.def == t.find(id)->id ? h.instance : 0;
}

}  // namespace

TEST_CASE("equipment off: no loadout, no hook, no fold; hashes equal the option-less combat") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry r0, r1;
    auto a = Combat::create(r0, ch, l, 7);
    auto b = Combat::create(r1, ch, l, 7, CombatOptions{});
    REQUIRE(a); REQUIRE(b);
    const CombatantId pc = ch.spawns[0].stable_id;
    CHECK(b->loadout(pc) == nullptr);
    CHECK(b->state().loadouts.empty());
    CHECK_FALSE(b->state().hooks.loadout_of);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(a->submit(rules({rules5e::current_actor(a->state()), ActionKind::EndTurn})).ok);
        REQUIRE(b->submit(rules({rules5e::current_actor(b->state()), ActionKind::EndTurn})).ok);
    }
    CHECK(a->hash() == b->hash());
    CHECK(a->log_hash() == b->log_hash());
    CHECK_FALSE(b->state().hooks.apply_equip);
    // An equip request is refused exactly as before the option existed.
    until_turn(*b, pc);
    const auto r = b->submit(rules(equip(pc, EquipOp::Stow, 1)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: equipment play is off");
    // Off legal actions == rules legal actions (no Engine Core entries).
    CHECK(b->legal_actions().size() == rules5e::legal_actions(b->state()).size());
}

TEST_CASE("equipment on: a combat without loaded items hashes as with the option off") {
    ContentLibrary& l = library();
    world::Chunk ch = arena("srd51.monster.goblin");
    ecs::Registry r0, r1;
    auto off = Combat::create(r0, ch, l, 3);
    auto on = Combat::create(r1, ch, l, 3, CombatOptions{true});
    REQUIRE(off); REQUIRE(on);
    CHECK(on->state().loadouts.empty());
    CHECK(on->hash() == off->hash());
}

TEST_CASE("equipment on: the fighter's loadout, setup AC / attacks equal the stat block, 0xD0 folds") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry r0, r1;
    auto off = Combat::create(r0, ch, l, 7);
    auto on = Combat::create(r1, ch, l, 7, CombatOptions{true});
    REQUIRE(off); REQUIRE(on);
    const CombatantId pc = ch.spawns[0].stable_id;
    const rules5e::Loadout* lo = on->loadout(pc);
    REQUIRE(lo);
    const ItemDefTable& t = *on->item_defs();
    CHECK(lo->view.armor_def == t.find("srd51.item.chain_mail")->id);
    CHECK(item_of(t, lo->view.main, "srd51.item.longsword") != 0);
    CHECK(item_of(t, lo->view.off, "srd51.item.shield") != 0);
    REQUIRE(lo->view.carried.size() == 1);
    CHECK(lo->view.carried[0].def == t.find("srd51.item.crossbow_light")->id);
    REQUIRE(lo->view.attacks.size() == 2);   // longsword, light crossbow
    CHECK(lo->view.attacks[0].def == t.find("srd51.item.longsword")->id);
    CHECK(lo->view.attacks[1].def == t.find("srd51.item.crossbow_light")->id);
    const auto* k_on = rules5e::find(on->state(), pc);
    const auto* k_off = rules5e::find(off->state(), pc);
    CHECK(k_on->ac == 18);
    CHECK(k_on->ac == k_off->ac);
    CHECK(k_on->attacks[0].to_hit == k_off->attacks[0].to_hit);
    CHECK(k_on->attacks[1].damage == k_off->attacks[1].damage);
    CHECK(on->hash() != off->hash());   // 0xD0
}

TEST_CASE("equipment on: shield, draw, stow, drop, pick up; the free interaction; ground entities and 0xD2; replay") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 7, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    const ItemDefTable& t = *c->item_defs();
    const uint32_t sword = c->loadout(pc)->view.main.instance, shield = c->loadout(pc)->view.off.instance;
    const uint32_t bow = c->loadout(pc)->view.carried[0].instance;
    until_turn(*c, pc);

    // Doff the shield: an action; AC 18 -> 16 (recompute on equip change).
    REQUIRE(c->submit(rules(equip(pc, EquipOp::DoffShield, shield))).ok);
    CHECK(rules5e::find(c->state(), pc)->ac == 16);
    CHECK_FALSE(c->state().budget.action);
    CHECK_FALSE(c->loadout(pc)->view.interaction_used);
    CHECK(c->state().hooks.loadout_of);
    // Stow the sword: the free object interaction.
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, sword))).ok);
    CHECK(c->loadout(pc)->view.main.instance == 0);
    CHECK(c->loadout(pc)->view.interaction_used);
    // A second interaction would cost the action, which is spent.
    auto r = c->submit(rules(equip(pc, EquipOp::Draw, bow, 1)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: no object interaction left");
    for (const auto& q : c->legal_actions()) CHECK(q.equip_op != uint8_t(EquipOp::Draw));
    // Next turn: the interaction is back. Draw the (two-handed) crossbow.
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    CHECK_FALSE(c->loadout(pc)->view.interaction_used);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Draw, bow, 1))).ok);
    CHECK(c->loadout(pc)->view.main.instance == bow);
    CHECK(c->state().budget.action);
    // Drop is free: the crossbow lands on the fighter's tile as a ground item entity (0xD2).
    const uint64_t before_drop = c->hash();
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, bow))).ok);
    CHECK(c->state().budget.action);
    REQUIRE(c->state().ground_items.size() == 1);
    CHECK(c->state().ground_items[0].item.instance == bow);
    CHECK(c->state().ground_items[0].x == c->pos(pc).x);
    auto ge = reg.find(kGroundItemIdBase + bow);
    REQUIRE(ge);
    CHECK(reg.get<GroundItemLink>(*ge)->def == t.find("srd51.item.crossbow_light")->id);
    CHECK(c->hash() != before_drop);
    // Picking it up: the interaction is spent, so it costs the action. Listed for both hands.
    int listed = 0;
    for (const auto& q : c->legal_actions())
        if (q.equip_op == uint8_t(EquipOp::PickUp)) { ++listed; CHECK(q.equip_item == bow); }
    CHECK(listed == 2);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::PickUp, bow, 1))).ok);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().ground_items.empty());
    CHECK_FALSE(reg.find(kGroundItemIdBase + bow));
    // Drop again; now neither interaction nor action is left: PickUp is refused and not listed.
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, bow))).ok);
    r = c->submit(rules(equip(pc, EquipOp::PickUp, bow, 1)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: no object interaction left");
    for (const auto& q : c->legal_actions()) CHECK(q.equip_op != uint8_t(EquipOp::PickUp));
    // Next turn: picked up with the interaction.
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    const uint64_t on_ground = c->hash();
    REQUIRE(c->submit(rules(equip(pc, EquipOp::PickUp, bow, 1))).ok);
    CHECK(c->state().budget.action);
    CHECK(c->state().ground_items.empty());
    CHECK(c->state().ground_items_seen);   // sticky: 0xD2 keeps folding (count 0)
    CHECK(c->loadout(pc)->view.main.instance == bow);
    CHECK(c->hash() != on_ground);
    // Don the shield into the off hand (an action; the crossbow occupies the main hand): AC back to 18.
    REQUIRE(c->submit(rules(equip(pc, EquipOp::DonShield, shield, 2))).ok);
    CHECK(rules5e::find(c->state(), pc)->ac == 18);

    // Replay with the same options reproduces both hashes; without them it desyncs (equip ops refused).
    ecs::Registry rr, rr2;
    std::string err;
    auto again = replay(rr, ch, l, 7, CombatOptions{true}, c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
    CHECK_FALSE(replay(rr2, ch, l, 7, c->log(), &err));
}

TEST_CASE("equipment on: 0xD1 folds the equip request into log_hash") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    const CombatantId pc = ch.spawns[0].stable_id;
    ecs::Registry r0, r1;
    auto a = Combat::create(r0, ch, l, 7, CombatOptions{true});
    auto b = Combat::create(r1, ch, l, 7, CombatOptions{true});
    until_turn(*a, pc);
    until_turn(*b, pc);
    const uint32_t sword = a->loadout(pc)->view.main.instance;
    REQUIRE(a->submit(rules(equip(pc, EquipOp::Stow, sword, 0))).ok);
    REQUIRE(b->submit(rules(equip(pc, EquipOp::Stow, sword, 1))).ok);   // same effect (stow ignores the hand), other request
    CHECK(a->hash() == b->hash());
    CHECK(a->log_hash() != b->log_hash());
}

TEST_CASE("equipment on: hand over to an adjacent ally with a loadout") {
    ContentLibrary& l = library();
    world::Chunk ch = arena(kFighter, 1);
    ch.spawns[1].archetype = kFighter;
    ch.spawns[1].overrides_json = R"({"team":"rivals"})";
    ch.content_hash = world::compute_content_hash(ch);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 11, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId a = ch.spawns[0].stable_id, b = ch.spawns[1].stable_id;
    until_turn(*c, a);
    const uint32_t sword = c->loadout(a)->view.main.instance;
    // b's hands are full (sword + shield): HandOver is not listed and is refused.
    for (const auto& q : c->legal_actions()) CHECK(q.equip_op != uint8_t(EquipOp::HandOver));
    CHECK(c->submit(rules(equip(a, EquipOp::HandOver, sword, 0, b))).error == "equip: the recipient has no free hand");
    // Give b a free hand (state edit, test only), then hand the sword over.
    auto& st = CombatTestAccess::state(*c);
    for (auto& lo : st.loadouts)
        if (lo.owner == b) { lo.view.carried.push_back(lo.view.main); lo.view.main = {}; }
    bool listed = false;
    for (const auto& q : c->legal_actions())
        listed = listed || (q.equip_op == uint8_t(EquipOp::HandOver) && q.equip_item == sword && q.equip_to == b);
    CHECK(listed);
    REQUIRE(c->submit(rules(equip(a, EquipOp::HandOver, sword, 0, b))).ok);
    CHECK(c->loadout(a)->view.main.instance == 0);
    CHECK(c->loadout(b)->view.main.instance == sword);
    CHECK(c->loadout(a)->view.interaction_used);
}

TEST_CASE("equipment on: recompute triggers (abilities, AC effects) but not conditions") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 7, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    auto& st = CombatTestAccess::state(*c);
    auto end_turn = [&] { REQUIRE(c->submit(rules({rules5e::current_actor(c->state()), ActionKind::EndTurn})).ok); };
    end_turn();
    const uint32_t n0 = c->recomputes();
    CHECK(n0 == 0);   // nothing changed yet
    // A condition: no recompute.
    for (auto& k : st.combatants) if (k.id == pc) k.conditions |= 1u << uint32_t(rules5e::Condition::Frightened);
    end_turn();
    CHECK(c->recomputes() == n0);
    // Str 16 -> 18: longsword +5 -> +6, damage +3 -> +4.
    const int hit0 = rules5e::find(c->state(), pc)->attacks[0].to_hit;
    const int dmg0 = rules5e::find(c->state(), pc)->attacks[0].damage.bonus;
    for (auto& k : st.combatants) if (k.id == pc) k.abilities.score[std::size_t(rules5e::Ability::Str)] = 18;
    end_turn();
    CHECK(c->recomputes() == n0 + 1);
    CHECK(rules5e::find(c->state(), pc)->attacks[0].to_hit == hit0 + 1);
    CHECK(rules5e::find(c->state(), pc)->attacks[0].damage.bonus == dmg0 + 1);
    // An AC effect starting: recompute (AC itself stays the base; the bonus is applied by rules at roll time).
    rules5e::ActiveModifier m;
    m.spec.stat = rules5e::ModStat::Ac;
    m.spec.bonus = 2;
    m.target = pc;
    st.modifiers.push_back(m);
    end_turn();
    CHECK(c->recomputes() == n0 + 2);
    CHECK(rules5e::find(c->state(), pc)->ac == 18);
    // ... and ending.
    st.modifiers.clear();
    end_turn();
    CHECK(c->recomputes() == n0 + 3);
    // Proficiency bonus change.
    for (auto& k : st.combatants) if (k.id == pc) k.proficiency_bonus = 3;
    end_turn();
    CHECK(c->recomputes() == n0 + 4);
    CHECK(rules5e::find(c->state(), pc)->attacks[0].to_hit == hit0 + 2);
}

TEST_CASE("equipment on: a dropped light keeps its light id (holder 0, still lit) and returns to its picker") {
    ContentLibrary& l = library();
    const auto ch = arena(kTorchbearer);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 5, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id;
    const uint32_t torch = c->loadout(pc)->view.off.instance;
    REQUIRE(torch != 0);
    auto lights = c->equipment_lights();
    REQUIRE(lights.size() == 1);
    const uint32_t lid = lights[0].light_id;
    CHECK(lights[0].item == torch);
    CHECK(lights[0].holder_id == pc);
    CHECK(lights[0].lit);
    until_turn(*c, pc);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, torch))).ok);
    lights = c->equipment_lights();
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].light_id == lid);
    CHECK(lights[0].holder_id == 0);
    CHECK(lights[0].tile == c->pos(pc));
    CHECK(lights[0].lit);   // a dropped torch stays lit (Q7)
    REQUIRE(c->submit(rules(equip(pc, EquipOp::PickUp, torch, 2))).ok);
    lights = c->equipment_lights();
    CHECK(lights[0].light_id == lid);
    CHECK(lights[0].holder_id == pc);
    CHECK(lights[0].lit);
    // Stowed: covered (unlit), same id; drawn: lit again.
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, torch))).ok);
    lights = c->equipment_lights();
    CHECK(lights[0].light_id == lid);
    CHECK_FALSE(lights[0].lit);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Draw, torch, 2))).ok);   // costs the action (interaction spent)
    CHECK(c->equipment_lights()[0].lit);
}

namespace {
rules5e::ActionRequest attack(CombatantId a, CombatantId t, uint8_t idx = 0) {
    rules5e::ActionRequest r{a, ActionKind::Attack, t};
    r.attack_index = idx;
    return r;
}
// Puts a ground item (test only) on `at` and returns its new instance id.
uint32_t place_ground(Combat& c, uint32_t def, Vec2i at) {
    auto& st = CombatTestAccess::state(c);
    const uint32_t inst = st.next_item_id++;
    st.ground_items.push_back({rules5e::HeldItem{inst, def, 1, 0}, at.x, at.y});
    st.ground_items_seen = true;
    return inst;
}
}  // namespace

TEST_CASE("equipment on: picking up a two-handed item needs two free hands (ground_item_def)") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 7, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    const uint32_t sword = c->loadout(pc)->view.main.instance, shield = c->loadout(pc)->view.off.instance;
    until_turn(*c, pc);
    const uint32_t bow = place_ground(*c, c->item_defs()->find("srd51.item.crossbow_light")->id, c->pos(pc));
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, sword))).ok);   // one hand free (the shield is in the other)
    auto r = c->submit(rules(equip(pc, EquipOp::PickUp, bow, 1)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: no free hand");
    for (const auto& q : c->legal_actions()) CHECK(q.equip_op != uint8_t(EquipOp::PickUp));
    REQUIRE(c->submit(rules(equip(pc, EquipOp::DoffShield, shield))).ok);   // both free now; interaction spent, action spent
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    bool listed = false;
    for (const auto& q : c->legal_actions()) listed = listed || (q.equip_op == uint8_t(EquipOp::PickUp) && q.equip_item == bow);
    CHECK(listed);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::PickUp, bow, 1))).ok);
    CHECK(c->loadout(pc)->view.main.instance == bow);
}

TEST_CASE("equipment on: an unknown ground item is refused") {
    ContentLibrary& l = library();
    const auto ch = arena(kFighter);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 7, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    until_turn(*c, pc);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, c->loadout(pc)->view.main.instance))).ok);
    auto r = c->submit(rules(equip(pc, EquipOp::PickUp, 4242, 1)));   // no such instance
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: unknown ground item");
    const uint32_t odd = place_ground(*c, 0xDEADu, c->pos(pc));       // a ground item whose def is not in the table
    r = c->submit(rules(equip(pc, EquipOp::PickUp, odd, 1)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "equip: unknown ground item");
}

TEST_CASE("equipment on: a thrown weapon lands on the target's tile; the next one is drawn from the pack") {
    ContentLibrary& l = library();
    const auto ch = arena(kThrower);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 13, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
    REQUIRE(c->loadout(pc)->view.attacks.size() == 1);
    CHECK(c->loadout(pc)->view.attacks[0].use == rules5e::WeaponUse::Thrown);
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == gob) { k.hp = 200; k.hp_max = 200; }
    const uint32_t first = c->loadout(pc)->view.main.instance, second = c->loadout(pc)->view.carried[0].instance;
    until_turn(*c, pc);
    const auto r1 = c->submit(rules(attack(pc, gob)));
    REQUIRE_MESSAGE(r1.ok, r1.error);
    REQUIRE(c->state().ground_items.size() == 1);
    CHECK(c->state().ground_items[0].item.instance == first);
    CHECK(c->state().ground_items[0].x == c->pos(gob).x);
    CHECK(c->loadout(pc)->view.main.instance == 0);
    CHECK(reg.find(kGroundItemIdBase + first));
    REQUIRE_FALSE(c->ended());
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    REQUIRE(c->submit(rules(attack(pc, gob))).ok);   // drawn as part of the attack (Q2)
    CHECK(c->state().ground_items.size() == 2);
    CHECK(c->loadout(pc)->view.carried.empty());
    REQUIRE_FALSE(c->ended());
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    const auto r3 = c->submit(rules(attack(pc, gob)));
    CHECK_FALSE(r3.ok);
    CHECK(r3.error == "attack: nothing to throw");
    (void)second;
}

TEST_CASE("equipment on: ammunition stacks run down for PCs; no stack or a monster = unlimited") {
    ContentLibrary& l = library();
    const auto ch = arena(kArcher, 4);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 17, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
    auto arrows = [&] { return int(c->loadout(pc)->view.carried.at(0).count); };
    REQUIRE(arrows() == 2);
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == gob) { k.hp = 200; k.hp_max = 200; }
    for (int shot = 0; shot < 2; ++shot) {
        until_turn(*c, pc);
        const auto r = c->submit(rules(attack(pc, gob)));
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(arrows() == 1 - shot);
        REQUIRE_FALSE(c->ended());
        REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    }
    until_turn(*c, pc);
    const auto r = c->submit(rules(attack(pc, gob)));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "attack: no ammunition");
    // The same archer as a monster never spends ammunition.
    ecs::Registry reg2;
    auto m = Combat::create(reg2, ch, l, 17, CombatOptions{true});
    for (auto& k : CombatTestAccess::state(*m).combatants) if (k.id == pc) k.is_pc = false;
    until_turn(*m, pc);
    REQUIRE(m->submit(rules(attack(pc, gob))).ok);
    CHECK(int(m->loadout(pc)->view.carried.at(0).count) == 2);
}

TEST_CASE("equipment on: on death everything held and carried drops to the creature's tile in the same step; lights keep ids") {
    ContentLibrary& l = library();
    const auto ch = arena(kTorchbearer, 1);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 23, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
    const uint32_t lid = c->equipment_lights().at(0).light_id;
    auto& st = CombatTestAccess::state(*c);
    for (auto& k : st.combatants)
        if (k.id == pc) { k.hp = 1; k.uses_death_saves = false; k.is_pc = false; k.ac = 0; }
    bool died = false;
    for (int round = 0; round < 12 && !died; ++round) {
        until_turn(*c, gob);
        REQUIRE(c->submit(rules(attack(gob, pc))).ok);
        died = rules5e::find(c->state(), pc)->dead;
        if (!died) REQUIRE(c->submit(rules({gob, ActionKind::EndTurn})).ok);
    }
    REQUIRE(died);
    const rules5e::Loadout* lo = c->loadout(pc);
    CHECK(lo->view.main.instance == 0);
    CHECK(lo->view.off.instance == 0);
    CHECK(lo->view.carried.empty());
    REQUIRE(c->state().ground_items.size() == 3);   // longsword, torch, shield
    for (const auto& g : c->state().ground_items) { CHECK(g.x == c->pos(pc).x); CHECK(g.y == c->pos(pc).y); }
    const auto lights = c->equipment_lights();
    CHECK(lights.at(0).light_id == lid);
    CHECK(lights.at(0).holder_id == 0);
    CHECK(lights.at(0).lit);
}

namespace {
std::vector<SimEvent> item_moves(const Combat& c) {
    std::vector<SimEvent> out;
    for (const auto& e : c.events()) if (e.kind == SimEvent::Kind::ItemMoved) out.push_back(e);
    return out;
}
}  // namespace

TEST_CASE("ItemMoved (v11): equip ops, thrown, death drop; never with equipment off; ammunition emits none") {
    ContentLibrary& l = library();
    {   // off: no ItemMoved, the stream version is unchanged
        const auto ch = arena(kFighter);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 7);
        for (int i = 0; i < 3; ++i) REQUIRE(c->submit(rules({rules5e::current_actor(c->state()), ActionKind::EndTurn})).ok);
        CHECK(item_moves(*c).empty());
        CHECK(event_stream_version(c->events()) < 11);
    }
    {   // equip ops
        const auto ch = arena(kFighter);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 7, CombatOptions{true});
        const CombatantId pc = ch.spawns[0].stable_id;
        const uint32_t sword = c->loadout(pc)->view.main.instance, shield = c->loadout(pc)->view.off.instance;
        until_turn(*c, pc);
        CHECK(item_moves(*c).empty());
        REQUIRE(c->submit(rules(equip(pc, EquipOp::DoffShield, shield))).ok);
        REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, sword))).ok);
        REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, sword))).error == "equip: the item is not in hand");
        REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
        until_turn(*c, pc);
        REQUIRE(c->submit(rules(equip(pc, EquipOp::Draw, sword, 1))).ok);
        REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, sword))).ok);
        REQUIRE(c->submit(rules(equip(pc, EquipOp::PickUp, sword, 1))).ok);   // costs the action
        const auto m = item_moves(*c);
        REQUIRE(m.size() == 5);
        const int reasons[] = {kItemDoffShield, kItemStow, kItemDraw, kItemDrop, kItemPickUp};
        const uint32_t items[] = {shield, sword, sword, sword, sword};
        const CombatantId holders[] = {pc, pc, pc, 0, pc};
        for (int i = 0; i < 5; ++i) {
            CHECK(m[i].rules.b == reasons[i]);
            CHECK(uint32_t(m[i].rules.a) == items[i]);
            CHECK(CombatantId(m[i].rules.c) == holders[i]);
            CHECK(m[i].actor == pc);
            CHECK(m[i].tile == c->pos(pc));
        }
        CHECK(c->events().back().kind == SimEvent::Kind::ItemMoved);   // after the step's rules events
        CHECK(event_stream_version(c->events()) == 11);
        ecs::Registry rr;
        std::string err;
        auto again = replay(rr, ch, l, 7, CombatOptions{true}, c->log(), &err);
        REQUIRE_MESSAGE(again, err);
        CHECK(again->hash() == c->hash());   // replay re-emits the same ItemMoved events
    }
    {   // thrown: lands on the target's tile
        const auto ch = arena(kThrower);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 13, CombatOptions{true});
        const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
        for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == gob) { k.hp = 200; k.hp_max = 200; }
        const uint32_t jav = c->loadout(pc)->view.main.instance;
        until_turn(*c, pc);
        REQUIRE(c->submit(rules(attack(pc, gob))).ok);
        const auto m = item_moves(*c);
        REQUIRE(m.size() == 1);
        CHECK(m[0].rules.b == kItemThrown);
        CHECK(uint32_t(m[0].rules.a) == jav);
        CHECK(m[0].rules.c == 0);
        CHECK(m[0].tile == c->pos(gob));
    }
    {   // ammunition: consumed, never on the ground, no ItemMoved
        const auto ch = arena(kArcher, 4);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 17, CombatOptions{true});
        const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
        for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == gob) { k.hp = 200; k.hp_max = 200; }
        until_turn(*c, pc);
        REQUIRE(c->submit(rules(attack(pc, gob))).ok);
        CHECK(c->loadout(pc)->view.carried.at(0).count == 1);
        CHECK(c->state().ground_items.empty());
        CHECK_FALSE(c->state().ground_items_seen);
        CHECK(item_moves(*c).empty());
    }
    {   // death drop: one ItemMoved per item, instance order, actor = the dead creature
        const auto ch = arena(kTorchbearer, 1);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 23, CombatOptions{true});
        const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
        for (auto& k : CombatTestAccess::state(*c).combatants)
            if (k.id == pc) { k.hp = 1; k.uses_death_saves = false; k.is_pc = false; k.ac = 0; }
        bool died = false;
        for (int round = 0; round < 12 && !died; ++round) {
            until_turn(*c, gob);
            REQUIRE(c->submit(rules(attack(gob, pc))).ok);
            died = rules5e::find(c->state(), pc)->dead;
            if (!died) REQUIRE(c->submit(rules({gob, ActionKind::EndTurn})).ok);
        }
        REQUIRE(died);
        const auto m = item_moves(*c);
        REQUIRE(m.size() == 3);
        for (std::size_t i = 0; i < m.size(); ++i) {
            CHECK(m[i].rules.b == kItemDeathDrop);
            CHECK(m[i].actor == pc);
            CHECK(m[i].rules.c == 0);
            CHECK(m[i].tile == c->pos(pc));
            if (i) CHECK(m[i - 1].rules.a < m[i].rules.a);
        }
    }
}

TEST_CASE("gear lights (item@2): srd51 torch / lanterns produce equipment lights with state and resolved radii") {
    ContentLibrary& l = library();
    const ItemDefTable& t = *l.items();
    const auto* lan = t.find("srd51.item.lantern_hooded");
    REQUIRE(lan);
    CHECK(lan->kind == rules5e::ItemKind::Light);
    REQUIRE(lan->light.states.size() == 2);
    CHECK(lan->light.states[0].bright_ft == 30);
    CHECK(lan->light.states[1].bright_ft == 0);
    CHECK(lan->light.states[1].dim_ft == 5);
    const auto* torch = t.find("srd51.item.torch");
    CHECK(torch->has_weapon);
    CHECK(torch->weapon_requires_lit);
    CHECK(torch->weapon.dice.bonus == 1);
    CHECK(t.find("srd51.item.lantern_bullseye")->light.shape == rules5e::LightShape::Cone);
    world::Chunk ch = arena(kLanterns);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 5, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id;
    auto lights = c->equipment_lights();
    REQUIRE(lights.size() == 3);   // instance order: hooded lantern, torch, bullseye (carried)
    CHECK(lights[0].def == lan->id);
    CHECK(lights[0].state == 0);
    CHECK_FALSE(lights[0].lit);   // lit_by_default false: unlit sheds nothing (rules5e::light_radii)
    CHECK(lights[0].bright_ft == 0);
    CHECK(lights[0].dim_ft == 0);
    CHECK_FALSE(lights[1].lit);   // srd torch: lit_by_default false
    CHECK(lights[1].bright_ft == 0);
    CHECK(lights[2].shape == rules5e::LightShape::Cone);
    CHECK(lights[2].holder_id == pc);
    // A non-default lit state (set directly here; SetLightState tests below) is reported with its radii and changes 0xD0.
    const uint64_t h0 = c->hash();
    for (auto& lo : CombatTestAccess::state(*c).loadouts)
        if (lo.owner == pc) lo.view.main.flags = rules5e::with_light_state(uint8_t(lo.view.main.flags | rules5e::kHeldLit), 1);
    lights = c->equipment_lights();
    CHECK(lights[0].state == 1);
    CHECK(lights[0].bright_ft == 0);
    CHECK(lights[0].dim_ft == 5);
    CHECK(c->hash() != h0);
}

TEST_CASE("ammunition recovery: half the expended ammunition returns at encounter end (no roll)") {
    ContentLibrary& l = library();
    const auto ch = arena(kKitted, 4);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 31, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
    const uint32_t bow = c->item_defs()->find("srd51.item.longbow")->id;
    const uint32_t arrows_def = c->item_defs()->find("test.item.arrows")->id;
    uint32_t bow_inst = 0, sword = c->loadout(pc)->view.main.instance, torch = c->loadout(pc)->view.off.instance;
    for (const auto& x : c->loadout(pc)->view.carried) if (x.def == bow) bow_inst = x.instance;
    for (auto& k : CombatTestAccess::state(*c).combatants) if (k.id == gob) { k.ac = 0; }
    until_turn(*c, pc);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Drop, torch))).ok);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Stow, sword))).ok);
    REQUIRE(c->submit(rules(equip(pc, EquipOp::Draw, bow_inst, 1))).ok);   // the action
    auto stack = [&] { for (const auto& x : c->loadout(pc)->view.carried) if (x.def == arrows_def) return int(x.count); return -1; };
    int shots = 0;
    while (!c->ended() && stack() > 0 && shots < 6) {
        REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
        if (c->ended()) break;
        until_turn(*c, pc);
        REQUIRE(c->submit(rules(attack(pc, gob, 2))).ok);
        ++shots;
    }
    REQUIRE(c->ended());
    REQUIRE(c->loadout(pc)->expended.size() == 1);
    CHECK(c->loadout(pc)->expended[0].count == shots);
    CHECK(stack() == 6 - shots + rules5e::recovered_ammo(shots));
}

namespace {
struct E2E { uint64_t hash = 0, log_hash = 0, events = 0; bool ended = false; std::size_t ground = 0; int arrows = -1; };
// A whole scripted combat with equipment on, no state edits (replayable from the log alone): the kitted fighter drops its lit
// torch, throws its javelin, stows its sword, picks the torch up and drops it again, draws its longbow and shoots; the
// equipped goblin stows its scimitar, draws its shortbow and shoots back; the goblin's death drops its items and ends the
// encounter (ammunition recovery).
E2E run_e2e(Combat& c, CombatantId pc, CombatantId gob) {
    const ItemDefTable& t = *c.item_defs();
    auto inst_of = [&](CombatantId who, const char* id) -> uint32_t {
        const auto* lo = c.loadout(who);
        const uint32_t d = t.find(id)->id;
        for (const auto* h : {&lo->view.main, &lo->view.off}) if (h->def == d && h->instance) return h->instance;
        for (const auto& h : lo->view.carried) if (h.def == d) return h.instance;
        for (const auto& g : c.state().ground_items) if (g.item.def == d) return g.item.instance;
        return 0;
    };
    const uint32_t torch = inst_of(pc, "test.item.torch"), sword = inst_of(pc, "srd51.item.longsword");
    const uint32_t bow = inst_of(pc, "srd51.item.longbow");
    const uint32_t scim = inst_of(gob, "srd51.item.scimitar"), sbow = inst_of(gob, "srd51.item.shortbow");
    uint8_t gob_bow_attack = 0;
    for (std::size_t i = 0; i < rules5e::find(c.state(), gob)->attacks.size(); ++i)
        if (rules5e::find(c.state(), gob)->attacks[i].id == "shortbow") gob_bow_attack = uint8_t(i);
    auto must = [&](const rules5e::ActionRequest& q) { const auto r = c.submit(rules(q)); REQUIRE_MESSAGE(r.ok, r.error); };
    int pc_turn = 0, gob_turn = 0;
    for (int step = 0; step < 60 && !c.ended(); ++step) {
        const CombatantId who = rules5e::current_actor(c.state());
        const auto* k = rules5e::find(c.state(), who);
        if (who == pc && k->hp > 0) {
            if (pc_turn == 0) {
                must(equip(pc, EquipOp::Drop, torch));
                must(equip(pc, EquipOp::Stow, sword));
                must(attack(pc, gob, 1));   // thrown javelin, drawn from the pack
            } else if (pc_turn == 1) {
                must(equip(pc, EquipOp::PickUp, torch, 2));
                must(equip(pc, EquipOp::Drop, torch));
                must(equip(pc, EquipOp::Draw, bow, 1));   // the action
            } else {
                must(attack(pc, gob, 2));
            }
            ++pc_turn;
        } else if (who == gob && k->hp > 0) {
            if (gob_turn == 0) {
                must(equip(gob, EquipOp::Stow, scim));
                must(equip(gob, EquipOp::Draw, sbow, 1));
            } else {
                must(attack(gob, pc, gob_bow_attack));
            }
            ++gob_turn;
        }
        if (!c.ended()) must({who, ActionKind::EndTurn});
    }
    E2E out;
    out.hash = c.hash();
    out.log_hash = c.log_hash();
    out.events = event_stream_hash(c.events());
    out.ended = c.ended();
    out.ground = c.state().ground_items.size();
    for (const auto& h : c.loadout(pc)->view.carried) if (h.def == t.find("test.item.arrows")->id) out.arrows = h.count;
    return out;
}
}  // namespace

TEST_CASE("equipment end-to-end: a scripted combat replays identically and matches pinned cross-platform hashes") {
    ContentLibrary& l = library();
    world::Chunk ch = arena(kKitted, 3);
    ch.spawns[1].archetype = kGoblinKit;
    ch.content_hash = world::compute_content_hash(ch);
    const CombatantId pc = ch.spawns[0].stable_id, gob = ch.spawns[1].stable_id;
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 2026, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    REQUIRE(c->loadout(gob));
    const E2E a = run_e2e(*c, pc, gob);
    REQUIRE(a.ended);
    CHECK(rules5e::find(c->state(), gob)->dead);
    CHECK(a.ground >= 4);   // the torch, the javelin, the goblin's scimitar and shortbow
    int deaths = 0, thrown = 0;
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::ItemMoved) { deaths += e.rules.b == kItemDeathDrop; thrown += e.rules.b == kItemThrown; }
    CHECK(deaths == 2);
    CHECK(thrown == 1);
    const auto lights = c->equipment_lights();
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].lit);
    CHECK(lights[0].holder_id == 0);
    CHECK_FALSE(c->loadout(pc)->expended.empty());
    CHECK(a.arrows == 6 - c->loadout(pc)->expended[0].count + rules5e::recovered_ammo(c->loadout(pc)->expended[0].count));

    ecs::Registry rr;
    auto b = replay(rr, ch, l, 2026, CombatOptions{true}, c->log(), &err);
    REQUIRE_MESSAGE(b, err);
    CHECK(b->hash() == a.hash);
    CHECK(b->log_hash() == a.log_hash);
    CHECK(event_stream_hash(b->events()) == a.events);
    CHECK(b->events().size() == c->events().size());

    MESSAGE("e2e hash=" << a.hash << " log_hash=" << a.log_hash << " events=" << a.events);
    // Pinned (new test, not a golden re-pin): any platform / compiler divergence fails here.
    CHECK(a.hash == 18080299159498595558ull);
    CHECK(a.log_hash == 13146101177623128839ull);
    CHECK(a.events == 12810335167693045063ull);
}

TEST_CASE("ItemDefTable: a duplicate add is an error result and leaves the first definition intact") {
    ItemDefTable t;
    const auto doc = assets::Json::parse(R"({"$schema": "item@2", "id": "test.item.lamp", "name": "Lamp", "category": "gear",
        "hands": 1, "light": {"bright_ft": 15, "dim_ft": 30, "states": [{"id": "dim", "bright_ft": 0, "dim_ft": 5}]}})");
    REQUIRE_FALSE(t.add(doc));
    const auto again = t.add(doc);
    REQUIRE(again);
    CHECK(*again == "test.item.lamp: duplicate item id");
    auto other = doc;
    other["light"]["states"] = assets::Json::array();   // a different second copy changes nothing either
    CHECK(t.add(other));
    rules5e::ItemDef code;
    CHECK(t.add(std::string("test.item.lamp"), code));
    CHECK(t.size() == 1);
    const auto* d = t.find("test.item.lamp");
    REQUIRE(d);
    REQUIRE(d->light.states.size() == 2);
    CHECK(d->light.states[0].bright_ft == 15);
    CHECK(d->light.states[1].dim_ft == 5);
    CHECK(d->weapon.id == "lamp");   // the string view still points at live storage
    // A later distinct add still works (no stale pending state from the refused ones).
    REQUIRE_FALSE(t.add(assets::Json::parse(R"({"$schema": "item@2", "id": "test.item.candle", "name": "Candle",
        "category": "gear", "light": {"bright_ft": 5, "dim_ft": 5}})")));
    CHECK(t.find("test.item.candle")->light.states.size() == 1);
}

namespace {
rules5e::ActionRequest light_req(CombatantId a, uint32_t item, uint8_t st) {
    rules5e::ActionRequest r = equip(a, EquipOp::SetLightState, item);
    r.equip_state = st;
    return r;
}
const EquipmentLight& light_of(const std::vector<EquipmentLight>& v, uint32_t inst) {
    for (const auto& e : v) if (e.item == inst) return e;
    FAIL("no light for item " << inst);
    return v.front();
}
std::vector<std::pair<uint32_t, uint8_t>> light_ops(Combat& c) {
    std::vector<std::pair<uint32_t, uint8_t>> out;
    for (const auto& r : c.legal_actions())
        if (r.equip_op == uint8_t(EquipOp::SetLightState)) out.emplace_back(r.equip_item, r.equip_state);
    return out;
}
} // namespace

TEST_CASE("SetLightState: light a torch, open / hood / douse a hooded lantern (30/30 -> 0/5), no ItemMoved event") {
    ContentLibrary& l = library();
    const auto ch = arena(kLanterns);
    ecs::Registry reg;
    std::string err;
    auto c = Combat::create(reg, ch, l, 5, CombatOptions{true}, &err);
    REQUIRE_MESSAGE(c, err);
    const CombatantId pc = ch.spawns[0].stable_id;
    const uint32_t lantern = c->loadout(pc)->view.main.instance, torch = c->loadout(pc)->view.off.instance;
    until_turn(*c, pc);
    const std::size_t events0 = c->events().size();
    // light the torch (free interaction)
    REQUIRE(c->submit(rules(light_req(pc, torch, 0))).ok);
    CHECK(c->loadout(pc)->view.interaction_used);
    CHECK((c->loadout(pc)->view.off.flags & rules5e::kHeldLit) != 0);
    CHECK(light_of(c->equipment_lights(), torch).lit);
    CHECK(light_of(c->equipment_lights(), torch).bright_ft == 20);
    CHECK(light_of(c->equipment_lights(), torch).dim_ft == 20);
    // second op this turn: Use an Object (the action)
    REQUIRE(c->submit(rules(light_req(pc, lantern, 0))).ok);
    CHECK_FALSE(c->state().budget.action);
    auto lan = light_of(c->equipment_lights(), lantern);
    CHECK(lan.lit); CHECK(lan.state == 0); CHECK(lan.bright_ft == 30); CHECK(lan.dim_ft == 30);
    for (std::size_t i = events0; i < c->events().size(); ++i) CHECK(c->events()[i].kind != SimEvent::Kind::ItemMoved);
    REQUIRE(c->submit(rules({pc, ActionKind::EndTurn})).ok);
    until_turn(*c, pc);
    // hood it
    REQUIRE(c->submit(rules(light_req(pc, lantern, 1))).ok);
    lan = light_of(c->equipment_lights(), lantern);
    CHECK(lan.lit); CHECK(lan.state == 1); CHECK(lan.bright_ft == 0); CHECK(lan.dim_ft == 5);
    CHECK(rules5e::light_state_index(c->loadout(pc)->view.main.flags) == 1);
    // douse it: unlit, state bits cleared, sheds nothing
    REQUIRE(c->submit(rules(light_req(pc, lantern, rules5e::kLightDouse))).ok);
    lan = light_of(c->equipment_lights(), lantern);
    CHECK_FALSE(lan.lit); CHECK(lan.state == 0); CHECK(lan.bright_ft == 0); CHECK(lan.dim_ft == 0);
    CHECK((c->loadout(pc)->view.main.flags & (rules5e::kHeldLit | rules5e::kHeldLightStateMask)) == 0);
    for (std::size_t i = events0; i < c->events().size(); ++i) CHECK(c->events()[i].kind != SimEvent::Kind::ItemMoved);
}

TEST_CASE("SetLightState: each refusal string (rules5e::equip_legal) via submit") {
    ContentLibrary& l = library();
    {
        const auto ch = arena(kLanterns);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 5, CombatOptions{true});
        REQUIRE(c);
        const CombatantId pc = ch.spawns[0].stable_id;
        const auto& v = c->loadout(pc)->view;
        const uint32_t lantern = v.main.instance, torch = v.off.instance;
        REQUIRE(v.carried.size() == 1);
        const uint32_t bullseye = v.carried[0].instance;
        until_turn(*c, pc);
        const uint64_t h0 = c->hash();
        CHECK(c->submit(rules(light_req(pc, bullseye, 0))).error == "equip: the item is not in hand");
        CHECK(c->submit(rules(light_req(pc, lantern, 2))).error == "equip: unknown light state");
        CHECK(c->submit(rules(light_req(pc, torch, 1))).error == "equip: unknown light state");
        CHECK(c->submit(rules(light_req(pc, torch, rules5e::kLightDouse))).error == "equip: the light is already in that state");
        REQUIRE(c->submit(rules(light_req(pc, lantern, 1))).ok);
        CHECK(c->submit(rules(light_req(pc, lantern, 1))).error == "equip: the light is already in that state");
        CHECK(c->hash() != h0);
    }
    {
        const auto ch = arena(kKitted);
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, l, 5, CombatOptions{true});
        REQUIRE(c);
        const CombatantId pc = ch.spawns[0].stable_id;
        until_turn(*c, pc);
        CHECK(c->submit(rules(light_req(pc, c->loadout(pc)->view.main.instance, 0))).error == "equip: not a light source");
    }
}

TEST_CASE("SetLightState: legal_actions lists each valid target state per held light, plus douse when lit") {
    ContentLibrary& l = library();
    const auto ch = arena(kLanterns);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 5, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    const uint32_t lantern = c->loadout(pc)->view.main.instance, torch = c->loadout(pc)->view.off.instance;
    REQUIRE(lantern < torch);
    until_turn(*c, pc);
    using P = std::vector<std::pair<uint32_t, uint8_t>>;
    CHECK(light_ops(*c) == P{{lantern, 0}, {lantern, 1}, {torch, 0}});   // carried bullseye: not in hand
    // every listed op is accepted, and they come after every other entry
    const auto all = c->legal_actions();
    bool seen = false;
    for (const auto& r : all) {
        if (r.equip_op == uint8_t(EquipOp::SetLightState)) seen = true;
        else CHECK_FALSE(seen);
    }
    REQUIRE(c->submit(rules(light_req(pc, lantern, 0))).ok);
    CHECK(light_ops(*c) == P{{lantern, 1}, {lantern, rules5e::kLightDouse}, {torch, 0}});
    // with equipment off nothing is listed
    ecs::Registry r2;
    auto off = Combat::create(r2, ch, l, 5);
    REQUIRE(off);
    CHECK(light_ops(*off).empty());
}

TEST_CASE("SetLightState hashing: state folds into 0xD0 only when non-default; 0xD1 carries equip_state only for op 10") {
    ContentLibrary& l = library();
    const auto ch = arena(kLanterns);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 5, CombatOptions{true});
    REQUIRE(c);
    const CombatantId pc = ch.spawns[0].stable_id;
    auto& main = [&]() -> rules5e::HeldItem& {
        for (auto& lo : CombatTestAccess::state(*c).loadouts) if (lo.owner == pc) return lo.view.main;
        FAIL("no loadout");
        return CombatTestAccess::state(*c).loadouts.front().view.main;
    }();
    const uint8_t f0 = main.flags;
    const uint64_t h0 = c->hash();
    main.flags = rules5e::with_light_state(f0, 0);                 // default state: same bits, same hash
    CHECK(c->hash() == h0);
    main.flags = uint8_t(f0 | rules5e::kHeldLit);                   // lit, state 0
    const uint64_t lit0 = c->hash();
    CHECK(lit0 != h0);
    main.flags = rules5e::with_light_state(uint8_t(f0 | rules5e::kHeldLit), 1);   // lit, hooded
    CHECK(c->hash() != lit0);
    main.flags = f0;
    CHECK(c->hash() == h0);
    // 0xD1: two replays differing only in equip_state
    auto run = [&](EquipOp op, uint8_t st, uint32_t item_of_main_or_off) {
        ecs::Registry r;
        auto k = Combat::create(r, ch, l, 5, CombatOptions{true});
        REQUIRE(k);
        until_turn(*k, pc);
        const auto& v = k->loadout(pc)->view;
        rules5e::ActionRequest q = equip(pc, op, item_of_main_or_off ? v.off.instance : v.main.instance);
        q.equip_state = st;
        REQUIRE(k->submit(rules(q)).ok);
        return std::pair{k->log_hash(), k->hash()};
    };
    CHECK(run(EquipOp::SetLightState, 0, 0).first != run(EquipOp::SetLightState, 1, 0).first);
    CHECK(run(EquipOp::Drop, 0, 1).first == run(EquipOp::Drop, 7, 1).first);   // not folded for other ops
    CHECK(run(EquipOp::Drop, 0, 1).second == run(EquipOp::Drop, 7, 1).second);
}
