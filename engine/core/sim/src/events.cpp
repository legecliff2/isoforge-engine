#include "sim/events.hpp"

#include "base/canonical_hash.hpp"

namespace eng::sim {

uint32_t event_stream_version(std::span<const SimEvent> events) {
    uint32_t v = 4;
    for (const auto& e : events) {
        if (e.kind == SimEvent::Kind::Footprint) return 6;
        if (e.kind == SimEvent::Kind::SpellTarget) v = 5;
    }
    return v;
}

uint64_t event_stream_hash(std::span<const SimEvent> events) {
    CanonicalHasher h;
    h.u32(event_stream_version(events));
    h.u32(uint32_t(events.size()));
    for (const auto& e : events) {
        h.byte(uint8_t(e.kind));
        h.byte(uint8_t(e.rules.kind));
        h.u32(e.rules.actor);
        h.u32(e.rules.target);
        h.i32(e.rules.a);
        h.i32(e.rules.b);
        h.i32(e.rules.c);
        h.byte(e.rules.reaction_depth);
        h.u32(e.actor);
        h.i32(e.tile.x);
        h.i32(e.tile.y);
    }
    return h.value();
}

} // namespace eng::sim
