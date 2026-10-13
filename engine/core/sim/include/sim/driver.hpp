#pragma once
// Headless turn driver: actor policies choose targets / reactions; built-in tactics turn a
// target into commands (attack if in reach, else path toward it one tile at a time, else end
// turn). Policies are only consulted live; the command log alone replays the fight.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "sim/combat.hpp"

namespace eng::sim {

class IActorPolicy {
public:
    virtual ~IActorPolicy() = default;
    // Called once at the start of each of `self`'s turns. nullopt = no target (end turn).
    virtual std::optional<CombatantId> choose_target(const Combat& c, CombatantId self) = 0;
    // Sync adapter for reaction windows. Default: always take option 0.
    virtual ReactionDecision decide_reaction(const Combat&, const NeedsDecision& n) { return {n.window_seq, true, 0}; }
};

// Built-in: nearest conscious hostile (Chebyshev), ties by lowest id.
class NearestEnemyPolicy final : public IActorPolicy {
public:
    std::optional<CombatantId> choose_target(const Combat& c, CombatantId self) override;
};

using PolicyFor = std::function<IActorPolicy*(CombatantId)>;

struct RunResult {
    bool ended = false;
    uint32_t commands = 0;
    std::string error;
};

// Drives `c` until the encounter ends, an error occurs, or `max_commands` were submitted.
RunResult run_to_end(Combat& c, const PolicyFor& policy_for, uint32_t max_commands = 20000);

// One tactical command for the current actor toward `target` (exposed for tests).
Command next_command(const Combat& c, std::optional<CombatantId> target);

} // namespace eng::sim
