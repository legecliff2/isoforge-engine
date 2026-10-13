#pragma once
// Plain-data snapshot of what the vertical slice shows. The renderer (SliceView) only ever
// sees this: no sim/world/rules types, no game state lives on the render side.
#include <cstdint>
#include <string>
#include <vector>

#include "base/math.hpp"

namespace slice {

enum class TileKind : uint8_t { Floor, Wall, Rubble, Raised, Door, Water, Unknown };

struct TileView {
    eng::Vec2i tile;
    uint8_t    layer = 0;      // chunk layer: 0 ground, 1 detail, 2 walls, 3 roof
    uint8_t    elevation = 0;
    TileKind   kind = TileKind::Unknown;
};

struct ActorView {
    uint32_t    id = 0;        // stable id (sort key tie-break)
    std::string name;          // "FIGHTER", "GOBLIN"
    eng::Vec2i  tile;
    int         hp = 0, hp_max = 0;
    uint8_t     team = 0;
    bool        is_pc = false;
    bool        current = false;
    bool        down = false;  // 0 HP / unconscious
    bool        dead = false;
};

struct ViewModel {
    std::vector<TileView>    tiles;   // static: from the chunk
    std::vector<ActorView>   actors;
    bool        sim_live = false;     // false = static chunk view (no encounter)
    int         round = 0;
    std::string turn;                 // current actor name
    bool        ended = false;
    std::string status;               // e.g. "SPACE: STEP  A: AUTO"
    std::vector<std::string> log;     // newest last
};

} // namespace slice
