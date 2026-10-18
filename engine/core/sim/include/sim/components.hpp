#pragma once
// Sim-owned ECS components (moved from script_host, ARCHITECTURE.md §2.1 sim: "ECS <-> rules
// adapters"). rules5e::EncounterState is authoritative during combat; Combat mirrors hp and
// position back into these after every step so scripts/UI read consistent data.
#include <cstdint>

#include "rules5e/types.hpp"

namespace eng::sim {

struct Health  { int32_t hp = 0, hp_max = 0; };
struct Team    { uint8_t team = 0; };
struct TilePos { int32_t x = 0, y = 0; };            // chunk-local tile; for a multi-tile creature its anchor (min x, min y)
// Side of a multi-tile creature's square footprint (2..4). Attached only when > 1: a creature
// without it occupies exactly its TilePos tile (all pre-footprint content and scripts).
struct Footprint { uint8_t size = 1; };
struct CombatantLink { rules5e::CombatantId id = 0; };   // = spawn stable_id

} // namespace eng::sim
