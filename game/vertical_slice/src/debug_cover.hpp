#pragma once
// Debug cover overlay (presentation only; no SDL, no renderer). Reads Combat::cover_between and
// copies the level onto ViewModel::cover for SliceView. Does not write sim state.
//
// Keys (main.cpp): F4 toggles. While open and the equip panel is closed, Tab cycles the target,
// N cycles the attacker, M toggles an aim-tile origin (the attacker's last committed point of
// origin, ActionRequest::has_area_origin). Arrow-key panning is left alone.
//
// Default pair: the current actor, else the first living id, and NearestEnemyPolicy's target
// (the same selection the turn driver uses). Cycling pins an explicit id until the overlay closes.
#include <cstdint>
#include <string>

#include "session.hpp"

namespace eng::sim { class Combat; }

namespace slice {

// Creature footprints, or a 1-tile aim origin. The level is Combat::cover_between's answer.
// present=false when either id is missing, dead, gone, or the same.
CoverDebugView cover_probe(const eng::sim::Combat& combat, uint32_t attacker, uint32_t target, bool aim,
                           eng::Vec2i aim_tile);

class DebugCoverView {
public:
    enum class Cmd : uint8_t { Toggle, NextTarget, NextAttacker, ToggleAim };

    bool handle(Cmd, const Session&);
    bool open() const { return open_; }
    const std::string& message() const { return note_; }
    // Fills vm.cover. Closed, or no measurable pair: present stays false.
    void decorate(const Session& s, ViewModel& vm) const;

private:
    bool open_ = false;
    bool aim_ = false;
    uint32_t attacker_ = 0;   // 0 = follow the current actor
    uint32_t target_ = 0;     // 0 = follow NearestEnemyPolicy
    std::string note_;
};

} // namespace slice
