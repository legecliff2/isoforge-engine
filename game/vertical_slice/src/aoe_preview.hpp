#pragma once
// Player area-spell preview (presentation only; no SDL, no renderer). Reads Combat::area_targets
// and copies creature ids onto ViewModel::area for the overlay in aoe_overlay.cpp. Does not write
// sim state, emit events, or draw. Monster telegraphs are out of scope: only a player character's
// turn is filled.
//
// Keys (main.cpp): F5 toggles. While open and the equip panel is closed, Q cycles the current
// actor's area spells (spells_known, then innate). The aim tile is the cursor, picked in the view
// and passed in. Nothing is submitted: aiming does not cast.
//
// Tile set: there is no pure membership helper on main. area_targets returns creature ids only,
// and its geometry is private. fill stays empty (tiles_pending) until Golem's
// Combat::area_preview(actor, shape, size/width, origin, aim). That swap is the body of
// fill_area_tiles in aoe_preview.cpp — one call, no second copy of the geometry. Altitude fade
// is out of scope; the overlay is 2D.
#include <cstdint>
#include <vector>

#include "session.hpp"

namespace eng::sim { class Combat; }

namespace slice {

// Creatures area_targets would catch, one each, plus an empty tile list (tiles_pending).
// present=false when `spell_id` is missing or has no area, or `caster` is missing.
// An illegal aim stays present with the rejection in `note` and no hits.
// `frame_ms` is copied onto pulse_ms; it is not read from the sim.
AreaPreviewView area_probe(const eng::sim::Combat& combat, uint32_t caster, uint32_t spell_id, eng::Vec2i aim,
                           uint32_t frame_ms);

// Area spells this creature can cast, spells_known then innate, first occurrence only.
std::vector<uint32_t> area_spell_ids(const eng::sim::Combat& combat, uint32_t actor);

class AreaTargetView {
public:
    enum class Cmd : uint8_t { Toggle, NextSpell };

    bool handle(Cmd, const Session&);
    bool open() const { return open_; }
    // Fills vm.area from the cursor tile. Closed: area is cleared.
    void decorate(const Session& s, ViewModel& vm, eng::Vec2i cursor_tile, uint32_t frame_ms) const;

private:
    bool open_ = false;
    std::size_t spell_index_ = 0;   // into area_spell_ids of the current actor
};

} // namespace slice
