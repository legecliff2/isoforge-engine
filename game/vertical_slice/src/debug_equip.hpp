#pragma once
// Debug equip / inventory view (debug only; game-side, no SDL, no renderer). Lists the selected PC's worn, held and
// carried items and drives equipment through Session::submit_equip, the same path as scripted commands:
//   equip   carried item -> Draw (any free hand) / DonShield
//   unequip held item    -> Stow / DoffShield
//   light   held light   -> EquipOp::SetLightState, cycling unlit -> state 0 (light) -> 1.. (e.g. hood) -> douse
// Keys (main.cpp): F3 toggles the panel; while open Up/Down select a row, Tab selects the next PC, E equips, U unequips,
// L cycles the light state (arrow-key panning is paused while the panel is open). The panel is pushed into
// ViewModel::debug_panel and drawn by SliceView. Equip ops only succeed on that PC's turn (rules), like any command.
#include <cstdint>
#include <string>
#include <vector>

#include "session.hpp"

namespace slice {

class DebugEquipView {
public:
    enum class Cmd : uint8_t { Toggle, Up, Down, NextPc, Equip, Unequip, CycleLight };

    // Applies one command (keyboard or script). Returns true when a sim command was submitted and accepted.
    bool handle(Cmd, Session&);
    bool open() const { return open_; }
    // The PC the panel shows: the chosen one, else the current actor if it is a PC, else the first PC (id order).
    uint32_t selected_pc(const Session&) const;
    // Index into Session::inventory(selected_pc) of the highlighted row (clamped).
    size_t cursor() const { return cursor_; }
    const std::string& message() const { return message_; }
    // Panel text (empty while closed): header, one row per item ('>' marks the cursor), key help, last message.
    std::vector<std::string> lines(const Session&) const;
    // Fills vm.debug_panel from lines().
    void decorate(const Session& s, ViewModel& vm) const { vm.debug_panel = lines(s); }

private:
    bool open_ = false;
    uint32_t pc_ = 0;
    size_t cursor_ = 0;
    std::string message_;
};

} // namespace slice
