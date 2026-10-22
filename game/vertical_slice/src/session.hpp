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
    struct EquippedLight { uint32_t light_id = 0, item = 0, holder_id = 0; eng::Vec2i tile{}; bool lit = false; };
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
    // refused or not that actor's turn. end_turns_until: ends turns until `actor` acts.
    bool submit_equip(uint32_t actor, uint8_t op, uint32_t item, uint8_t hand = 0);
    bool end_turns_until(uint32_t actor);
    std::vector<EquippedLight> equipment_lights() const;
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
