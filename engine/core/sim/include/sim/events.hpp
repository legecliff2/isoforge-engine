#pragma once
// sim event stream: the ordered, deterministic record of everything that happened in a combat.
// Consumers (render/platform, UI, replays, tests) read Combat::events(). Contract and versioning
// policy: docs/SIM_EVENTS.md. Any change to the layout or meaning below must bump
// kSimEventSchemaVersion and update the locked layout test (sim_tests "event layout").
#include <cstdint>
#include <span>

#include "base/math.hpp"
#include "rules5e/encounter.hpp"

namespace eng::sim {

// Version of the event stream contract (SimEvent + rules5e::Event as surfaced by sim).
inline constexpr uint32_t kSimEventSchemaVersion = 1;

struct SimEvent {
    // Append-only. Numeric values are part of the contract.
    enum class Kind : uint8_t {
        Rules = 0,       // `rules` holds a rules5e::Event (see docs/SIM_EVENTS.md for a/b/c per kind)
        TileMoved = 1,   // `actor` now stands on `tile` (chunk-local tile coordinates)
    };
    Kind kind = Kind::Rules;
    rules5e::Event rules{};          // Kind::Rules only; zero otherwise
    rules5e::CombatantId actor = 0;  // TileMoved: mover; 0 otherwise
    Vec2i tile{};                    // TileMoved: destination; {0,0} otherwise
};

// Canonical (StableHash/XXH3-64) digest of an event stream: version, count, then every field of
// every event in order. Equal streams <=> equal digests (up to hash collisions); golden material.
uint64_t event_stream_hash(std::span<const SimEvent> events);

} // namespace eng::sim
