#include "ecs/registry.hpp"

#include <atomic>

namespace eng::ecs {

namespace detail {
uint32_t next_component_type_index() noexcept {
    static std::atomic<uint32_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace detail

namespace {
auto id_lower(std::vector<std::pair<StableId, uint32_t>>& v, StableId id) {
    return std::lower_bound(v.begin(), v.end(), id,
                            [](const std::pair<StableId, uint32_t>& a, StableId b) { return a.first < b; });
}
} // namespace

Entity Registry::create(StableId id) {
    auto it = id_lower(by_id_, id);
    if (it != by_id_.end() && it->first == id)
        throw std::invalid_argument("ecs: duplicate StableId");
    uint32_t index;
    if (!free_.empty()) {
        index = free_.front();
        free_.pop_front();
    } else {
        index = uint32_t(slots_.size());
        slots_.emplace_back();
    }
    Slot& s = slots_[index];
    s.live = true;
    s.id = id;
    by_id_.insert(it, {id, index});
    return Entity{index, s.generation};
}

bool Registry::destroy(Entity e) {
    if (!alive(e)) return false;
    Slot& s = slots_[e.index];
    for (auto& st : storages_)
        if (st) st->erase(s.id);
    auto it = id_lower(by_id_, s.id);
    by_id_.erase(it);
    s.live = false;
    s.id = 0;
    ++s.generation;   // wraps after 2^32 reuses of one slot; accepted for v0
    free_.push_back(e.index);
    return true;
}

bool Registry::alive(Entity e) const noexcept {
    return e.index < slots_.size() && slots_[e.index].live && slots_[e.index].generation == e.generation;
}

std::optional<StableId> Registry::stable_id(Entity e) const noexcept {
    if (!alive(e)) return std::nullopt;
    return slots_[e.index].id;
}

std::optional<Entity> Registry::find(StableId id) const noexcept {
    auto it = std::lower_bound(by_id_.begin(), by_id_.end(), id,
                               [](const std::pair<StableId, uint32_t>& a, StableId b) { return a.first < b; });
    if (it == by_id_.end() || it->first != id) return std::nullopt;
    return Entity{it->second, slots_[it->second].generation};
}

std::vector<Entity> Registry::entities() const {
    std::vector<Entity> out;
    out.reserve(by_id_.size());
    for (auto& [id, idx] : by_id_) out.push_back(Entity{idx, slots_[idx].generation});
    return out;
}

} // namespace eng::ecs
