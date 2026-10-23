#include "debug_equip.hpp"

#include <algorithm>
#include <cctype>

#include "rules5e/equipment.hpp"

namespace slice {

namespace {
std::string short_name(const std::string& content_id) {
    std::string s = content_id.substr(content_id.rfind('.') + 1);
    for (char& c : s) c = c == '_' ? ' ' : char(std::toupper(static_cast<unsigned char>(c)));
    return s.empty() ? std::string("?") : s;
}
const char* where_name(Session::InventoryItem::Where w) {
    switch (w) {
    case Session::InventoryItem::MainHand: return "MAIN";
    case Session::InventoryItem::OffHand: return "OFF";
    case Session::InventoryItem::BothHands: return "BOTH";
    case Session::InventoryItem::Worn: return "WORN";
    default: return "PACK";
    }
}
bool held(const Session::InventoryItem& it) {
    return it.where == Session::InventoryItem::MainHand || it.where == Session::InventoryItem::OffHand ||
           it.where == Session::InventoryItem::BothHands;
}
std::vector<uint32_t> pcs(const Session& s) {
    std::vector<uint32_t> out;
    for (const ActorView& a : s.view().actors) if (a.is_pc && !a.dead) out.push_back(a.id);
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

uint32_t DebugEquipView::selected_pc(const Session& s) const {
    const std::vector<uint32_t> ids = pcs(s);
    if (ids.empty()) return 0;
    if (pc_ && std::find(ids.begin(), ids.end(), pc_) != ids.end()) return pc_;
    const uint32_t cur = s.current_actor();
    if (std::find(ids.begin(), ids.end(), cur) != ids.end()) return cur;
    return ids.front();
}

bool DebugEquipView::handle(Cmd cmd, Session& s) {
    if (cmd == Cmd::Toggle) { open_ = !open_; message_.clear(); return false; }
    if (!open_) return false;
    const uint32_t pc = selected_pc(s);
    const std::vector<Session::InventoryItem> inv = s.inventory(pc);
    switch (cmd) {
    case Cmd::Up: if (cursor_ > 0) --cursor_; return false;
    case Cmd::Down: if (cursor_ + 1 < inv.size()) ++cursor_; return false;
    case Cmd::NextPc: {
        const std::vector<uint32_t> ids = pcs(s);
        if (ids.empty()) return false;
        auto it = std::upper_bound(ids.begin(), ids.end(), pc);
        pc_ = it == ids.end() ? ids.front() : *it;
        cursor_ = 0;
        message_.clear();
        return false;
    }
    default: break;
    }
    cursor_ = inv.empty() ? 0 : std::min(cursor_, inv.size() - 1);
    if (inv.empty()) { message_ = "NO ITEMS"; return false; }
    const Session::InventoryItem& it = inv[cursor_];
    if (it.where == Session::InventoryItem::Worn) { message_ = "ARMOR: NOT IN COMBAT"; return false; }
    uint8_t op = 0, state = 0;
    if (cmd == Cmd::Equip) {
        if (held(it)) { message_ = "ALREADY HELD"; return false; }
        op = uint8_t(it.kind == EquipKind::Shield ? rules5e::EquipOp::DonShield : rules5e::EquipOp::Draw);
    } else if (cmd == Cmd::Unequip) {
        if (!held(it)) { message_ = "NOT HELD"; return false; }
        op = uint8_t(it.kind == EquipKind::Shield ? rules5e::EquipOp::DoffShield : rules5e::EquipOp::Stow);
    } else {   // CycleLight: unlit -> 0 -> 1 .. -> douse
        if (it.light_states == 0) { message_ = "NOT A LIGHT"; return false; }
        op = uint8_t(rules5e::EquipOp::SetLightState);
        state = !it.lit ? uint8_t(0) : uint8_t(it.state + 1) < it.light_states ? uint8_t(it.state + 1) : rules5e::kLightDouse;
    }
    const std::string name = short_name(it.content_id);
    if (s.current_actor() != pc) { message_ = "NOT ITS TURN"; return false; }
    const bool ok = s.submit_equip(pc, op, it.item, 0, state);
    if (!ok) message_ = "REFUSED: " + name;
    else if (cmd == Cmd::CycleLight)
        message_ = name + (state == rules5e::kLightDouse ? " DOUSED" : " LIGHT STATE " + std::to_string(state));
    else message_ = name + (cmd == Cmd::Equip ? " EQUIPPED" : " UNEQUIPPED");
    return ok;
}

std::vector<std::string> DebugEquipView::lines(const Session& s) const {
    std::vector<std::string> out;
    if (!open_) return out;
    const uint32_t pc = selected_pc(s);
    std::string name = "?";
    for (const ActorView& a : s.view().actors) if (a.id == pc) name = a.name;
    out.push_back("DEBUG EQUIP: " + name + " #" + std::to_string(pc) + (s.current_actor() == pc ? "  (TURN)" : ""));
    const std::vector<Session::InventoryItem> inv = s.inventory(pc);
    if (inv.empty()) out.push_back("  (NO LOADOUT)");
    const size_t cur = inv.empty() ? 0 : std::min(cursor_, inv.size() - 1);
    for (size_t i = 0; i < inv.size(); ++i) {
        const Session::InventoryItem& it = inv[i];
        std::string l = std::string(i == cur ? "> " : "  ") + where_name(it.where) + " " + short_name(it.content_id);
        if (it.count > 1) l += " X" + std::to_string(it.count);
        if (it.light_states > 0)
            l += it.lit ? " LIT S" + std::to_string(it.state) + " " + std::to_string(it.bright_ft) + "/" + std::to_string(it.dim_ft)
                        : std::string(" UNLIT");
        out.push_back(l);
    }
    out.push_back("UP/DOWN: SELECT  TAB: PC  E: EQUIP  U: UNEQUIP  L: LIGHT  F3: CLOSE");
    if (!message_.empty()) out.push_back(message_);
    return out;
}

} // namespace slice
