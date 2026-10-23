#include "debug_cover.hpp"

#include <algorithm>

#include "rules5e/encounter.hpp"
#include "sim/combat.hpp"

namespace slice {

namespace {
static_assert(int(CoverLevel::None) == int(rules5e::Cover::None));
static_assert(int(CoverLevel::Half) == int(rules5e::Cover::Half));
static_assert(int(CoverLevel::ThreeQuarters) == int(rules5e::Cover::ThreeQuarters));
static_assert(int(CoverLevel::Total) == int(rules5e::Cover::Total));

CoverLevel to_level(rules5e::Cover c) {
    switch (c) {
    case rules5e::Cover::Half: return CoverLevel::Half;
    case rules5e::Cover::ThreeQuarters: return CoverLevel::ThreeQuarters;
    case rules5e::Cover::Total: return CoverLevel::Total;
    case rules5e::Cover::None: break;
    }
    return CoverLevel::None;
}

uint32_t default_attacker(const Session& s) {
    const std::vector<uint32_t> ids = s.living_ids();
    if (ids.empty()) return 0;
    const uint32_t cur = s.current_actor();
    if (std::find(ids.begin(), ids.end(), cur) != ids.end()) return cur;
    return ids.front();
}

uint32_t default_target(const Session& s, uint32_t attacker) {
    if (attacker == 0) return 0;
    if (const uint32_t suggested = s.suggested_target(attacker)) return suggested;
    for (uint32_t id : s.living_ids())
        if (id != attacker) return id;
    return 0;
}
} // namespace

CoverDebugView cover_probe(const eng::sim::Combat& c, uint32_t attacker, uint32_t target, bool aim,
                           eng::Vec2i aim_tile) {
    CoverDebugView v;
    const rules5e::Combatant* as = rules5e::find(c.state(), attacker);
    const rules5e::Combatant* ts = rules5e::find(c.state(), target);
    if (!as || !ts || as->dead || ts->dead || as->gone || ts->gone || attacker == target) return v;
    const eng::Vec2i tp = c.pos(target);
    int tn = c.footprint_size(target);
    if (tn < 1) tn = 1;
    eng::Vec2i op;
    int on = 1;
    if (aim) {
        op = aim_tile;
    } else {
        op = c.pos(attacker);
        on = c.footprint_size(attacker);
        if (on < 1) on = 1;
    }
    // except attacker and target: their own bodies are not cover, matching Combat::cover_of.
    const rules5e::Cover cov = c.cover_between(op, on, tp, tn, attacker, target);
    v.present = true;
    v.aim = aim;
    v.attacker = attacker;
    v.target = target;
    v.origin = op;
    v.origin_n = on;
    v.target_tile = tp;
    v.target_n = tn;
    v.level = to_level(cov);
    return v;
}

bool DebugCoverView::handle(Cmd cmd, const Session& s) {
    if (cmd == Cmd::Toggle) {
        open_ = !open_;
        if (!open_) {
            aim_ = false;
            attacker_ = 0;
            target_ = 0;
            note_.clear();
        }
        return false;
    }
    if (!open_) return false;
    if (cmd == Cmd::ToggleAim) {
        if (attacker_ == 0) attacker_ = default_attacker(s);
        if (aim_) {
            aim_ = false;
            note_.clear();
            return false;
        }
        eng::Vec2i tile;
        if (!s.last_aim_tile(attacker_, &tile)) {
            note_ = "NO AIM";
            return false;
        }
        aim_ = true;
        note_.clear();
        return false;
    }
    if (cmd != Cmd::NextTarget && cmd != Cmd::NextAttacker) return false;
    if (attacker_ == 0) attacker_ = default_attacker(s);
    if (target_ == 0 || target_ == attacker_) target_ = default_target(s, attacker_);
    const std::vector<uint32_t> ids = s.living_ids();
    if (ids.size() < 2 || attacker_ == 0) return false;
    note_.clear();
    const uint32_t pinned = cmd == Cmd::NextAttacker ? attacker_ : target_;
    const uint32_t skip = cmd == Cmd::NextAttacker ? target_ : attacker_;
    auto at = std::find(ids.begin(), ids.end(), pinned);
    const std::size_t start = at == ids.end() ? 0 : std::size_t(at - ids.begin());
    std::size_t i = start;
    bool moved = false;
    for (std::size_t k = 0; k < ids.size(); ++k) {
        i = (i + 1) % ids.size();
        if (i == start) break;                 // walked the whole list
        if (ids[i] != skip) { moved = true; break; }
    }
    // Two living creatures: the only other id is the one we would skip. Swap, then drop the
    // target pin so decorate picks the policy target of the new attacker (the previous one).
    if (!moved) i = (start + 1) % ids.size();
    if (cmd == Cmd::NextAttacker) attacker_ = ids[i];
    else target_ = ids[i];
    if (attacker_ == target_) target_ = 0;
    return false;
}

void DebugCoverView::decorate(const Session& s, ViewModel& vm) const {
    vm.cover = {};
    if (!open_) return;
    const std::vector<uint32_t> living = s.living_ids();
    auto alive = [&](uint32_t id) { return std::find(living.begin(), living.end(), id) != living.end(); };
    const uint32_t atk = alive(attacker_) ? attacker_ : default_attacker(s);
    uint32_t tgt = (alive(target_) && target_ != atk) ? target_ : default_target(s, atk);
    if (tgt == atk) tgt = 0;
    bool aim = false;
    eng::Vec2i tile{};
    if (aim_ && s.last_aim_tile(atk, &tile)) aim = true;
    if (atk != 0 && tgt != 0) vm.cover = s.query_cover(atk, tgt, aim, tile);
    if (!vm.cover.present) vm.cover.note = note_.empty() ? std::string("NO PAIR") : note_;
    else if (aim_ && !vm.cover.aim) vm.cover.note = "NO AIM";
    else if (!note_.empty()) vm.cover.note = note_;
}

} // namespace slice
