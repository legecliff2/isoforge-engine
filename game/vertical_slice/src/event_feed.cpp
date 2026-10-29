#include "event_feed.hpp"

namespace slice {

using rules5e::EventKind;
using eng::sim::SimEvent;

bool known_rules_kind(EventKind k) {
    return static_cast<uint32_t>(k) <= static_cast<uint32_t>(eng::sim::kLastRulesEventKind);
}

namespace {
void push(FeedState& st, std::string s) {
    st.log.push_back(std::move(s));
    while (st.log.size() > st.max_log) st.log.erase(st.log.begin());
}
} // namespace

void note_aim_facing(FeedState& st, uint32_t actor, eng::Vec2i from, eng::Vec2i to) {
    if (auto f = facing_from_delta(to.x - from.x, to.y - from.y)) st.facing[actor] = *f;
}

void apply_events(std::span<const SimEvent> events, FeedState& st, const NameFn& name) {
    for (const SimEvent& e : events) {
        if (e.kind == SimEvent::Kind::TileMoved) {
            auto prev = st.tiles.find(e.actor);
            if (prev != st.tiles.end()) note_aim_facing(st, e.actor, prev->second, e.tile);
            st.tiles[e.actor] = e.tile;   // any actor, any turn (readied moves), anywhere in the block
            ++st.tile_moves;
            continue;
        }
        if (e.kind != SimEvent::Kind::Rules) { ++st.skipped_unknown; continue; }   // newer sim kind
        const rules5e::Event& r = e.rules;
        if (!known_rules_kind(r.kind)) { ++st.skipped_unknown; continue; }       // newer rules kind
        switch (r.kind) {
        case EventKind::TurnStarted: push(st, "ROUND " + std::to_string(r.a) + ": " + name(r.actor)); break;
        case EventKind::AttackResolved:
            push(st, name(r.actor) + ((r.c & 2) ? " CRITS " : (r.c & 1) ? " HITS " : " MISSES ") + name(r.target) +
                         " (" + std::to_string(r.b) + ")");
            break;
        case EventKind::DamageDealt: push(st, name(r.target) + " TAKES " + std::to_string(r.a) + " HP: " + std::to_string(r.b)); break;
        case EventKind::Moved:
            if (r.reaction_depth > 0) push(st, name(r.actor) + " MOVES (READIED)");
            break;
        case EventKind::ReadyTriggered: push(st, name(r.actor) + " READIED ACTION!"); break;
        case EventKind::CombatantDowned: push(st, name(r.actor) + " IS DOWN!"); break;
        case EventKind::CombatantDied: push(st, name(r.actor) + " DIES!"); break;
        case EventKind::ReactionTaken: push(st, name(r.actor) + " REACTS"); break;
        case EventKind::Healed: push(st, name(r.target) + " HEALS " + std::to_string(r.a)); break;
        case EventKind::EncounterEnded: push(st, "ENCOUNTER ENDED"); break;
        // v4 spellcasting kinds (35-38). Spell names aren't resolvable here (a = hashed spell id),
        // so the log stays generic. Known kinds: never counted as unknown.
        case EventKind::SavingThrowRolled:
            push(st, name(r.actor) + ((r.c & 1) ? " SAVES (" : " FAILS SAVE (") + std::to_string(r.b) + ")");
            break;
        case EventKind::ConcentrationEnded: push(st, name(r.actor) + " LOSES CONCENTRATION"); break;
        case EventKind::SpellEffectEnded: push(st, "SPELL ENDS ON " + name(r.actor)); break;
        case EventKind::SpellCheckRolled:
            push(st, name(r.actor) + ((r.c & 1) ? " COUNTERS (" : " FAILS COUNTER (") + std::to_string(r.b) + ")");
            break;
        default: break;   // known, not shown
        }
    }
}

} // namespace slice
