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
    // Sync adapter for reaction windows. HitRider (row 9c: the attacker is the reactor, Divine Smite style) is declined:
    // spending a resource on a rider is a deliberate choice. Damaged (row 11) takes a ReduceDamage option when there is
    // one, else the first (retaliate / riposte); AllyAttacked takes the first option (impose disadvantage). Every other
    // trigger (AttackRolled included: Cutting Words' subtract_roll) takes option 0.
    virtual ReactionDecision decide_reaction(const Combat&, const NeedsDecision& n) {
        if (n.trigger == rules5e::ReactionTrigger::HitRider) return {n.window_seq, false, 0};
        uint8_t pick = 0;
        if (n.trigger == rules5e::ReactionTrigger::Damaged)
            for (std::size_t i = 0; i < n.options.size(); ++i)
                if (n.options[i].kind == rules5e::ReactionOptionKind::ReduceDamage) { pick = uint8_t(i); break; }
        return {n.window_seq, true, pick};
    }
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

// ---- Spell follow-ups (AI side of Rules #222 / #228). Deterministic: integer scoring, ties to the lowest id. ----

// 7.2zd Sanctuary: the warded creature the request meets (its Attack target, or the first warded creature of another team
// among a no-area CastSpell's targets, in request order: the one rules save against), 0 = none.
CombatantId ward_target_of(const Combat& c, const rules5e::ActionRequest& q);
// 7.2zd Sanctuary: the creature to target instead after a failed ward save (ActionRequest::ward_alternate). Candidates come from
// rules5e::ward_alternate_targets (sorted by id, warded creatures excluded; a spell's own targets are skipped too); the pick
// is the built-in target scoring (NearestEnemyPolicy: conscious hostile, nearest Chebyshev, ties to the lowest id).
// 0 = none: the attack or spell is lost (the action stays spent).
CombatantId choose_ward_alternate(const Combat& c, const rules5e::ActionRequest& q);

// 7.2zd Preserve Life: the split of a split heal_pool feature (FeatureSpec::split_pool > 0) used by `user`. Recipients are
// `user` and its allies (not hostile, alive), lowest current hp first, ties to the lower id; each is filled up to its room
// (half its maximum minus its hp when split_cap_half) until the pool runs out. The validator (default
// rules5e::check_heal_split) is then asked; a rejected entry is adjusted (OverCap: its cap drops below the refused amount;
// OverPool: the pool budget drops below the running total) or dropped (OutOfRange, ExcludedType, BadTarget) and the split is
// rebuilt and re-checked. Every retry strictly shrinks the input, so it ends. Empty = nobody to heal.
using HealSplitValidator = std::function<rules5e::HealSplitCheck(const std::vector<rules5e::HealShare>&)>;
std::vector<rules5e::HealShare> plan_heal_split(const Combat& c, CombatantId user, const rules5e::FeatureSpec& spec,
                                                const HealSplitValidator& validate = {});

// 7.2zc Hex: does the spell have a marked `ability: "chosen"` row (ActionRequest::mark_ability required)?
bool spell_needs_mark_ability(const rules5e::SpellDef& def);
// 7.2zc: mark_ability (1 + Ability) for a cast at `target`: the target's weakest ability check (lowest ability score; the
// check bonus is the score's modifier), ties to the earlier ability (Str .. Cha). No such target: Str (1), the fixed default.
uint8_t choose_mark_ability(const Combat& c, CombatantId target);
// 7.2zc: the bonus-action move_mark (CastSpell, move_mark = true) for `self` when its concentration spell has a movable mark
// whose creature has dropped to 0 hit points and the bonus action is free. New mark: `preferred` when it is a conscious
// hostile other than the old mark, else the built-in target scoring. nullopt = nothing to offer.
std::optional<Command> move_mark_command(const Combat& c, CombatantId self, std::optional<CombatantId> preferred = std::nullopt);
// Fills the AI-chosen inputs of a request the AI (or a caller) built: mark_ability for a chosen-ability cast and
// ward_alternate for an Attack / CastSpell that meets a ward. Fields already set are left alone.
void complete_request(const Combat& c, rules5e::ActionRequest& q);

} // namespace eng::sim
