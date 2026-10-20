#include "rules5e/font_of_magic.hpp"

namespace rules5e {

namespace {
const ResourcePool* pool_of(const Combatant& c, const std::vector<ResourcePool>& pools) {
    if (c.sorcery_pool == 0) return nullptr;
    for (const auto& p : pools) if (p.actor == c.id && p.pool_id == c.sorcery_pool) return &p;
    return nullptr;
}
}  // namespace

FlexibleOutcome check_flexible_casting(const Combatant& c, const std::vector<ResourcePool>& pools, uint8_t flexible,
                                       uint8_t level) {
    FlexibleOutcome o;
    const ResourcePool* p = pool_of(c, pools);
    if (!p) { o.result = FlexibleResult::NoPool; return o; }
    o.points_after = p->cur;
    if (flexible == uint8_t(FlexibleDir::SlotToPoints)) {
        if (level < 1 || level > 9) { o.result = FlexibleResult::BadLevel; return o; }
        if (c.spell_slots[level] == 0) { o.result = FlexibleResult::NoSlot; return o; }
        if (int(p->cur) + int(level) > int(p->max)) { o.result = FlexibleResult::OverMax; return o; }
        o.points_delta = int16_t(level);
    } else if (flexible == uint8_t(FlexibleDir::PointsToSlot)) {
        const int cost = flexible_casting_cost(level);
        if (cost == 0) { o.result = FlexibleResult::BadLevel; return o; }
        if (int(p->cur) < cost || c.spell_slots[level] == 255) { o.result = FlexibleResult::NotEnoughPoints; return o; }
        o.points_delta = int16_t(-cost);
    } else {
        o.result = FlexibleResult::BadDirection;
        return o;
    }
    o.result = FlexibleResult::Ok;
    o.points_after = uint16_t(int(p->cur) + o.points_delta);
    return o;
}

FlexibleOutcome flexible_casting(Combatant& c, std::vector<ResourcePool>& pools, uint8_t flexible, uint8_t level) {
    FlexibleOutcome o = check_flexible_casting(c, pools, flexible, level);
    if (o.result != FlexibleResult::Ok) return o;
    for (auto& p : pools)
        if (p.actor == c.id && p.pool_id == c.sorcery_pool) p.cur = o.points_after;
    if (flexible == uint8_t(FlexibleDir::SlotToPoints)) c.spell_slots[level] = uint8_t(c.spell_slots[level] - 1);
    else c.spell_slots[level] = uint8_t(c.spell_slots[level] + 1);
    return o;
}

}  // namespace rules5e
