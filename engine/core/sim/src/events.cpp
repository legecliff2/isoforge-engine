#include "sim/events.hpp"

#include <algorithm>

#include "base/canonical_hash.hpp"

namespace eng::sim {

uint32_t event_stream_version(std::span<const SimEvent> events) {
    // The lowest schema version that fits: the max over every construct the stream uses, from both kind
    // families, so one construct never masks a later, newer one.
    uint32_t v = 4;
    for (const auto& e : events) {
        switch (e.kind) {
            case SimEvent::Kind::Footprint: v = std::max<uint32_t>(v, 6); break;
            case SimEvent::Kind::SpellTarget: v = std::max<uint32_t>(v, 5); break;
            case SimEvent::Kind::TokenSpawned: case SimEvent::Kind::TokenRemoved: v = std::max<uint32_t>(v, 10); break;   // R-G12
            case SimEvent::Kind::ItemMoved: v = std::max<uint32_t>(v, 11); break;   // equipment (CombatOptions::equipment)
            case SimEvent::Kind::AltitudeChanged: v = std::max<uint32_t>(v, 12); break;   // altitude PR1
            case SimEvent::Kind::Rules:
                // rules5e kinds appended after v6 (39..44 and anything later): a stream carrying one is
                // newer than every stream that does not, and hashes at the current contract version (7).
                if (uint8_t(e.rules.kind) > uint8_t(kLastRulesEventKindV6)) v = std::max<uint32_t>(v, 7);
                // v8: a Moved carrying movement flags (forced / teleport / drag) in `c`; plain Moves keep c == 0
                if (e.rules.kind == rules5e::EventKind::Moved && e.rules.c != 0) v = std::max<uint32_t>(v, 8);
                // v9: R-G11 zone kinds (ZoneCreated / ZoneMoved / ZoneEnded); a zone trigger's FeatureTriggered (b = TriggerOn::Zone*)
                // only follows a ZoneCreated in the same stream
                if (uint8_t(e.rules.kind) > uint8_t(kLastRulesEventKindV8) ||
                    (e.rules.kind == rules5e::EventKind::FeatureTriggered && e.rules.b >= int32_t(rules5e::TriggerOn::ZoneAppear)))
                    v = std::max<uint32_t>(v, 9);
                // v10: R-G12 summon / transform kinds (Summoned .. Commanded, 48-52)
                if (uint8_t(e.rules.kind) > uint8_t(kLastRulesEventKindV9)) v = std::max<uint32_t>(v, 10);
                break;
            default: break;
        }
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
