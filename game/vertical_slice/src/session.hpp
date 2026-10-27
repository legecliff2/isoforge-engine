#pragma once
// Game-side glue: loads a pack set (default sample.vertical_slice) through the world module (FileChunkProvider)
// and, optionally, runs Engine Core's headless sim fight (sim::Combat) one command at a time.
// Produces ViewModel snapshots; it never touches the renderer.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "view_model.hpp"

namespace eng::sim { class ContentLibrary; struct EquipmentLight; struct LightSource; }

namespace slice {

// One light source carried by an entity (LIGHTING.md §3.4 shape, game-side). Interim input until
// sim-owned attached lights (lighting phase 3): the session turns each into a sim::AttachedLight
// (Carrier::Creature) so the sim computes the light; render only sees the published LightFrame.
struct CarriedLight {
    uint32_t    carrier_id = 0;        // combatant id; skipped while the carrier is dead or absent
    uint16_t    bright_ft = 20, dim_ft = 20;
    bool        magical = false;
    std::string preset = "srd51.core.light.torch";   // content key -> render-side glow style
};

// Ids of the interim attached sources (pc_torches, Options::carried_lights): far above any sim-allocated
// light id, so they never collide with equipment light ids (LIGHTING.md 10.1, answer 3).
inline constexpr uint32_t kInterimLightIdBase = 0x40000000u;

class Session {
public:
    // One light-capable item as the sim's equip state reports it (sim::Combat::equipment_lights()).
    struct EquippedLight {
        uint32_t light_id = 0, item = 0, holder_id = 0; eng::Vec2i tile{}; bool lit = false;
        uint8_t state = 0;                    // rules5e::light_state_index (item flags bits 4-6)
        uint16_t bright_ft = 0, dim_ft = 0;   // rules5e::light_radii(def, held): 0 / 0 while unlit or stowed
    };
    struct Options {
        // assets::PackSet::resolve(pack_roots, packs); depends (e.g. srd51.core) come in through
        // the set, never by being listed. The chunk comes from packs.front()'s chunks/0_0_0.json.
        std::vector<std::string> pack_roots;
        std::vector<std::string> packs;
        uint64_t    seed = 1;
        bool        with_sim = true;   // false: static chunk + spawns only
        // Interim scenario input until lighting phase 3 (attached lights / items from content): give each
        // PC a lit torch (sim::AttachedLight 20/20 ft, preset srd51.core.light.torch). The sim computes
        // all light; this only decides who carries a source. Off = ambient only (underground: Dark).
        bool        pc_torches = true;   // one producer of carried lights (see carried_lights)
        // Extra carried lights (scenario / tests), merged with the pc_torches producer. Same path as the
        // torches: sim::AttachedLight -> sim::LightFrame -> ViewModel::light.sources -> glows.
        std::vector<CarriedLight> carried_lights{};
        // Fog of war (LIGHTING.md §10): show this team's sim visibility masks. Unset = the player
        // team (the team of the first character combatant). fog = false shows everything.
        bool        fog = true;
        std::optional<uint8_t> fog_team = std::nullopt;
        // Equipment (EQUIPMENT.md; sim::CombatOptions::equipment): loadouts from character equipment[], and light-capable
        // items become an attached-light producer (held: follows the holder; dropped: on its ground tile).
        bool        equipment = false;
        // Called after content load, before the fight is created (tests / tools: e.g. code-built item defs).
        std::function<void(eng::sim::ContentLibrary&)> content_hook{};
    };
    // Config-file shape {"pack_roots": [...], "packs": [...]} (both optional) -> o. False + *error
    // on malformed input.
    static bool apply_pack_config(const std::string& json_text, Options& o, std::string* error);
    static std::unique_ptr<Session> open(const Options&, std::string* error);
    ~Session();

    // Advances the fight by one sim command (a step or an action, reactions settled by AI).
    // Returns false when there is nothing to do (static mode, ended, or error).
    bool step();
    bool ended() const;
    // A request is paused on a reaction decision (StepStatus::NeedsDecision); the next step()
    // answers it. Nothing from the paused request is in the view until it completes.
    bool paused() const;
    const std::string& error() const;
    // Scenario / test commands. submit_equip: one rules equip op (rules5e::EquipOp) by the current actor; false if
    // refused or not that actor's turn. `state` is ActionRequest::equip_state (EquipOp::SetLightState: a LightDef::states
    // index or rules5e::kLightDouse). end_turns_until: ends turns until `actor` acts.
    bool submit_equip(uint32_t actor, uint8_t op, uint32_t item, uint8_t hand = 0, uint8_t state = 0);
    bool end_turns_until(uint32_t actor);
    std::vector<EquippedLight> equipment_lights() const;
    // One item of a creature's loadout (debug equip view): held, worn or carried, read through sim::Combat::loadout.
    struct InventoryItem {
        enum Where : uint8_t { Carried, MainHand, OffHand, BothHands, Worn };
        uint32_t item = 0;          // instance (0 for worn armor: it has no instance)
        std::string content_id;
        Where where = Carried;
        EquipKind kind = EquipKind::Other;
        uint16_t count = 1;
        uint8_t light_states = 0;   // rules5e::LightDef::states size (0: not a light)
        bool lit = false;
        uint8_t state = 0;          // rules5e::light_state_index
        uint16_t bright_ft = 0, dim_ft = 0;
    };
    // Worn armor, main hand, off hand, then carried items (loadout order). Empty without a loadout.
    std::vector<InventoryItem> inventory(uint32_t actor) const;
    // Combatant whose turn it is (0: none / no fight).
    uint32_t current_actor() const;
    // Living combatants (not dead, not gone), ascending id. Empty without a fight.
    std::vector<uint32_t> living_ids() const;
    // NearestEnemyPolicy's target for `self` (the turn driver's selection). 0 when there is none.
    uint32_t suggested_target(uint32_t self) const;
    // Latest committed point-of-origin aim for `actor` (ActionRequest::has_area_origin). False when the log has none.
    bool last_aim_tile(uint32_t actor, eng::Vec2i* out) const;
    // Read-only Combat::cover_between. Creature pair: both footprints, those two ids excluded as bodies.
    // Aim: origin is `aim_tile` with side 1 (the attacker's body is still excluded). present=false when
    // there is no fight, either id is missing / dead / gone, or attacker == target.
    CoverDebugView query_cover(uint32_t attacker, uint32_t target, bool aim, eng::Vec2i aim_tile) const;
    // True when `actor` is a living or downed character archetype (a player creature). False for
    // monsters, a missing id, or no fight. Area preview uses this to stay player-side.
    bool actor_is_pc(uint32_t actor) const;
    // Area spells `actor` can cast (spells_known, then innate). Empty without a fight.
    std::vector<uint32_t> area_spell_ids(uint32_t actor) const;
    // Read-only Combat::area_targets for `spell_id`, plus the pending tile list. Does not cast.
    // frame_ms is stored on the view for the overlay pulse; the sim is not read for time.
    AreaPreviewView query_area(uint32_t caster, uint32_t spell_id, eng::Vec2i aim, uint32_t frame_ms) const;
    // Non-fatal load warnings (e.g. two items styling one light preset differently).
    const std::vector<std::string>& warnings() const;

    ViewModel view() const;

private:
    Session();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Equipment producer, one entry (exposed for tests): sized from the entry's state radii; held -> on the holder.
eng::sim::LightSource equipment_source(const eng::sim::EquipmentLight& e, eng::Vec2i holder_centre, std::string preset);

// Palette asset id -> placeholder tile kind ("tiles.dungeon.wall_01" -> Wall).
TileKind classify_tile(const std::string& asset_id);

} // namespace slice
