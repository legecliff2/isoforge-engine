#pragma once
// Consumer of the sim event stream (docs/SIM_EVENTS.md, schema v3). Pure: folds committed
// events into presentation facts (tiles, log lines). Rules it relies on:
//  - only Combat::events() is consumed; provisional events of a paused request
//    (StepStatus::NeedsDecision) are never fed in, so nothing here is "maybe";
//  - TileMoved is applied to its own actor whenever it arrives: a readied Move moves the
//    readier during another creature's turn, and since v3 TileMoved comes after all rules
//    events of the request rather than right after its Moved;
//  - unknown SimEvent::Kind and rules5e::EventKind values (e.g. spells 35-38 from #23) are skipped.
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "base/math.hpp"
#include "sim/events.hpp"

namespace slice {

struct FeedState {
    std::map<uint32_t, eng::Vec2i> tiles;   // last committed tile per combatant
    std::vector<std::string> log;           // newest last, capped
    uint32_t skipped_unknown = 0;
    uint32_t tile_moves = 0;
    size_t   max_log = 8;
};

using NameFn = std::function<std::string(uint32_t)>;

// True for rules kinds this viewer understands (values <= sim::kLastRulesEventKind).
bool known_rules_kind(rules5e::EventKind k);

// Applies `events` (a suffix of the committed stream) to `st`.
void apply_events(std::span<const eng::sim::SimEvent> events, FeedState& st, const NameFn& name);

} // namespace slice
