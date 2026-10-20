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
inline constexpr uint32_t kSimEventSchemaVersion = 10;
// Last rules5e::EventKind surfaced under this schema version (rules appends; sim bumps).
inline constexpr rules5e::EventKind kLastRulesEventKind = rules5e::EventKind::Commanded;
// Last rules kind that existed at v9. A stream carrying a later kind (R-G12 kinds 48-52: Summoned .. Commanded) is a v10 stream.
inline constexpr rules5e::EventKind kLastRulesEventKindV9 = rules5e::EventKind::ZoneEnded;
// Last rules kind that existed at v8. A stream carrying a later kind (R-G11 ZoneCreated / ZoneMoved / ZoneEnded) is a v9 stream.
inline constexpr rules5e::EventKind kLastRulesEventKindV8 = rules5e::EventKind::ActionUsed;
// Last rules kind that existed at v6 and below. event_stream_version keys on this, not on kLastRulesEventKind:
// kinds 39..44 are locked at v7 but a stream carrying one is a v7 stream.
inline constexpr rules5e::EventKind kLastRulesEventKindV6 = rules5e::EventKind::SpellCheckRolled;
// v8: flag bits in a rules `Moved` event's `c` (0 for every ordinary move, and in every stream before v8). Defined once, in
// rules5e (which emits them for R-G9 pushes and teleports); these are the sim names for the same values.
inline constexpr int32_t kMovedForced = rules5e::kMovedForced;       // bit0: forced movement (shove push, R-G9 spell pushes)
inline constexpr int32_t kMovedTeleport = rules5e::kMovedTeleport;   // bit1: teleport (R-G9 Misty Step)
inline constexpr int32_t kMovedDrag = rules5e::kMovedDrag;           // bit2: dragged along by a grappler's own Move

struct SimEvent {
    // Append-only. Numeric values are part of the contract.
    enum class Kind : uint8_t {
        Rules = 0,       // `rules` holds a rules5e::Event (see docs/SIM_EVENTS.md for a/b/c per kind)
        TileMoved = 1,   // `actor` now stands on `tile` (the combat's grid frame: chunk-local
                         // for single-chunk combats, world tiles for multi-chunk ones)
        SpellTarget = 2, // v5. One per creature affected by a CastSpell, right after the request's
                         // rules events: `actor` = caster, `tile` = the target's tile, `rules.target`
                         // = target id, `rules.a` = spell id (other `rules` fields zero)
        Footprint = 3,   // v6. One per multi-tile creature (footprint side > 1), right after the opening
                         // rules events, in spawn order: `actor` = the creature, `tile` = its anchor
                         // (min-x/min-y tile, the frame of TileMoved), `rules.a` = footprint side in
                         // tiles (2 Large, 3 Huge, 4 Gargantuan). Its `TileMoved` tiles are anchors too.
    };
    Kind kind = Kind::Rules;
    rules5e::Event rules{};          // Kind::Rules only; zero otherwise
    rules5e::CombatantId actor = 0;  // TileMoved: mover; 0 otherwise
    Vec2i tile{};                    // TileMoved: destination; {0,0} otherwise
};

// Lowest schema version that can express the stream: 10 if it contains a rules kind past kLastRulesEventKindV9 (R-G12
// summons / transforms), 9 if it contains a rules kind past kLastRulesEventKindV8 (R-G11 zones),
// 8 if it contains a rules Moved with c != 0 (movement flags), 7 if
// it contains a rules kind past kLastRulesEventKindV6, 6 if it contains a v6 construct (Footprint), else
// 5 if it contains a v5 construct (SpellTarget), else 4. The digest records this, not
// kSimEventSchemaVersion, so streams without a CastSpell or a multi-tile creature keep their v4 / v5
// digests bit-identical (docs/SIM_EVENTS.md versioning policy).
uint32_t event_stream_version(std::span<const SimEvent> events);

// Canonical (StableHash/XXH3-64) digest of an event stream: stream version, count, then every field of
// every event in order. Equal streams <=> equal digests (up to hash collisions); golden material.
uint64_t event_stream_hash(std::span<const SimEvent> events);

} // namespace eng::sim
