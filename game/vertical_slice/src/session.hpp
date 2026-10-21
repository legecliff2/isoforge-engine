#pragma once
// Game-side glue: loads a pack set (default sample.vertical_slice) through the world module (FileChunkProvider)
// and, optionally, runs Engine Core's headless sim fight (sim::Combat) one command at a time.
// Produces ViewModel snapshots; it never touches the renderer.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "view_model.hpp"

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

class Session {
public:
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
    // Non-fatal load warnings (e.g. two items styling one light preset differently).
    const std::vector<std::string>& warnings() const;

    ViewModel view() const;

private:
    Session();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Palette asset id -> placeholder tile kind ("tiles.dungeon.wall_01" -> Wall).
TileKind classify_tile(const std::string& asset_id);

} // namespace slice
