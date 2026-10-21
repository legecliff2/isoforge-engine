// docs/proposals/EQUIPMENT.md 9 (Engine Core): CombatOptions::equipment. Off: nothing changes (no loadout, hook, fold). On:
// loadouts from character@4 equipment[], apply_equip (draw / stow / drop / pick up / shield / hand over), the one free object
// interaction per turn, recompute triggers, ground items (entities, 0xD2), PickUp / HandOver legal actions, equipment lights.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "sample_set.hpp"
#include "rules5e/equipment.hpp"
#include "sim/combat.hpp"
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
        torch.light.bright_ft = 20;
        torch.light.dim_ft = 20;
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
