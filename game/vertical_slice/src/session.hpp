#pragma once
// Game-side glue: loads sample.vertical_slice through the world module (FileChunkProvider)
// and, optionally, runs Engine Core's headless sim fight (sim::Combat) one command at a time.
// Produces ViewModel snapshots; it never touches the renderer.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "view_model.hpp"

namespace slice {

class Session {
public:
    struct Options {
        std::string pack_dir;
        uint64_t    seed = 1;
        bool        with_sim = true;   // false: static chunk + spawns only
    };
    static std::unique_ptr<Session> open(const Options&, std::string* error);
    ~Session();

    // Advances the fight by one sim command (a step or an action, reactions settled by AI).
    // Returns false when there is nothing to do (static mode, ended, or error).
    bool step();
    bool ended() const;
    const std::string& error() const;

    ViewModel view() const;

private:
    Session();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Palette asset id -> placeholder tile kind ("tiles.dungeon.wall_01" -> Wall).
TileKind classify_tile(const std::string& asset_id);

} // namespace slice
