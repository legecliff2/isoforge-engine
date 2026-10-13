#pragma once
// sim::IActorPolicy backed by a Luau AI script (monster behavior.ai), API level 1.
// ctx.rng is a Pcg32 stream owned by the policy, seeded from derive_seed(encounter seed,
// "ai", combatant id), so decisions are a pure function of (seed, state, call sequence).
#include <map>
#include <string>

#include "rng/rng.hpp"
#include "script_host/script_host.hpp"
#include "sim/driver.hpp"

namespace eng::script {

class LuauActorPolicy final : public sim::IActorPolicy {
public:
    LuauActorPolicy(ScriptHost& host, ScriptId script, std::string fn = "choose_target");
    std::optional<sim::CombatantId> choose_target(const sim::Combat& c, sim::CombatantId self) override;
    const std::string& last_error() const noexcept { return last_error_; }

private:
    rng::Pcg32& stream(const sim::Combat& c, sim::CombatantId self);
    ScriptHost* host_;
    ScriptId script_;
    std::string fn_;
    std::map<sim::CombatantId, rng::Pcg32> streams_;
    std::string last_error_;
};

} // namespace eng::script
