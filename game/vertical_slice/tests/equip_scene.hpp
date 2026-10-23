#pragma once
// Fixed equipment scene (tests + the offscreen snapshot tool): a 10 x 10 lit floor and three PCs. One faces SouthEast with
// a longsword, a shield, a dagger slung on its back and nothing else; one faces SouthWest with the same kit plus a lit
// lantern (30/30) instead of the shield; one carries a torch in its hooded / dim-only state. No RNG, no clock.
#include <cstdint>
#include <string>

#include "view_model.hpp"

namespace slice::equip_scene {

inline EquipVisual item(uint32_t inst, AttachSlot slot, EquipKind kind, const char* id) {
    EquipVisual e;
    e.item = inst; e.slot = slot; e.kind = kind; e.content_id = id;
    return e;
}

inline ViewModel build(bool equipped = true) {
    ViewModel vm;
    vm.sim_live = true;
    vm.round = 1;
    vm.turn = "FIGHTER";
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            TileView t;
            t.tile = {x, y};
            t.kind = TileKind::Floor;
            vm.tiles.push_back(t);
        }
    auto pc = [&](uint32_t id, const char* name, eng::Vec2i tile, Facing f) {
        ActorView a;
        a.id = id; a.name = name; a.tile = tile; a.hp = a.hp_max = 12; a.is_pc = true; a.facing = f;
        return a;
    };
    ActorView a = pc(1, "FIGHTER", {3, 4}, Facing::SouthEast), b = pc(2, "CLERIC", {5, 4}, Facing::SouthWest),
              c = pc(3, "ROGUE", {4, 6}, Facing::SouthEast);
    a.current = true;
    if (equipped) {
        a.equipment = {item(10, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.longsword"),
                       item(11, AttachSlot::OffHand, EquipKind::Shield, "srd51.item.shield"),
                       item(12, AttachSlot::Back, EquipKind::Weapon, "srd51.item.dagger")};
        EquipVisual lantern = item(22, AttachSlot::OffHand, EquipKind::Light, "srd51.item.lantern_hooded");
        lantern.lit = true; lantern.bright_ft = 30; lantern.dim_ft = 30;
        b.equipment = {item(20, AttachSlot::MainHand, EquipKind::Weapon, "srd51.item.mace"), lantern,
                       item(21, AttachSlot::Back, EquipKind::Shield, "srd51.item.shield")};
        EquipVisual torch = item(30, AttachSlot::MainHand, EquipKind::Light, "srd51.item.torch");
        torch.lit = true; torch.light_state = 1; torch.bright_ft = 0; torch.dim_ft = 5;
        c.equipment = {torch, item(31, AttachSlot::OffHand, EquipKind::Weapon, "srd51.item.shortbow")};
    }
    vm.actors = {a, b, c};
    return vm;
}

} // namespace slice::equip_scene
