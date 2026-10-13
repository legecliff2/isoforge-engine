#include "script_host/luau_policy.hpp"

namespace eng::script {

LuauActorPolicy::LuauActorPolicy(ScriptHost& host, ScriptId script, std::string fn)
    : host_(&host), script_(script), fn_(std::move(fn)) {}

rng::Pcg32& LuauActorPolicy::stream(const sim::Combat& c, sim::CombatantId self) {
    auto it = streams_.find(self);
    if (it == streams_.end()) {
        const int64_t keys[] = {0x6169 /* "ai" */, int64_t(self)};
        it = streams_.emplace(self, rng::Pcg32(rng::derive_seed(c.seed(), keys), 1)).first;
    }
    return it->second;
}

std::optional<sim::CombatantId> LuauActorPolicy::choose_target(const sim::Combat& c, sim::CombatantId self) {
    CallResult r = host_->call_entity_fn(script_, fn_, c.registry(), c.entity(self), stream(c, self));
    if (!r.ok()) {
        last_error_ = r.error.message;
        return std::nullopt;   // script failure: no target (ends turn), logged in last_error()
    }
    if (!r.target) return std::nullopt;
    const auto* link = c.registry().get<sim::CombatantLink>(*r.target);
    if (!link) return std::nullopt;
    return link->id;
}

} // namespace eng::script
