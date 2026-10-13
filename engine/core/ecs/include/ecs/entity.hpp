#pragma once
// Entity handles (ARCHITECTURE.md §3.3 "generation-checked handles", §4.5 stable_id).
#include <compare>
#include <cstdint>

namespace eng::ecs {

// Runtime handle: slot index + generation. Slots are reused; the generation is bumped on
// destroy so stale handles fail alive()/get(). Handles are NOT persistent: save files, deltas
// and anything that affects ordering use StableId.
struct Entity {
    uint32_t index = 0xFFFF'FFFFu;
    uint32_t generation = 0;
    auto operator<=>(const Entity&) const = default;
};
inline constexpr Entity kNullEntity{};

// Persistent identity. For chunk spawns it is EntitySpawn::stable_id (widened); runtime
// spawns get ids from a caller-owned deterministic source. All iteration is in StableId order.
using StableId = uint64_t;

} // namespace eng::ecs
