#pragma once
// Minimal ECS registry (ARCHITECTURE.md §2.1 ecs, §4.5 ordering).
//
// Determinism rules applied here:
//  - No unordered containers; component storage is a vector sorted by StableId.
//  - each<Ts...>() visits entities in ascending StableId order, independent of creation,
//    destruction or component-insertion order.
//  - Freed slots are reused FIFO (oldest freed first) with a bumped generation, so slot
//    assignment is a pure function of the create/destroy call sequence.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "ecs/entity.hpp"

namespace eng::ecs {

namespace detail {
// Process-local dense type index. Only used for lookup, never for iteration order.
uint32_t next_component_type_index() noexcept;
template <class T> uint32_t component_type_index() noexcept {
    static const uint32_t idx = next_component_type_index();
    return idx;
}

struct IStorage {
    virtual ~IStorage() = default;
    virtual bool erase(StableId) = 0;
};
} // namespace detail

template <class T>
class ComponentStorage final : public detail::IStorage {
public:
    struct Item { StableId id; Entity entity; T value; };

    T& insert_or_assign(StableId id, Entity e, T value) {
        auto it = lower(id);
        if (it != items_.end() && it->id == id) { it->value = std::move(value); return it->value; }
        return items_.insert(it, Item{id, e, std::move(value)})->value;
    }
    T* find(StableId id) {
        auto it = lower(id);
        return (it != items_.end() && it->id == id) ? &it->value : nullptr;
    }
    bool erase(StableId id) override {
        auto it = lower(id);
        if (it == items_.end() || it->id != id) return false;
        items_.erase(it);
        return true;
    }
    std::size_t size() const noexcept { return items_.size(); }
    std::vector<Item>& items() noexcept { return items_; }   // sorted by id

private:
    typename std::vector<Item>::iterator lower(StableId id) {
        return std::lower_bound(items_.begin(), items_.end(), id,
                                [](const Item& a, StableId b) { return a.id < b; });
    }
    std::vector<Item> items_;
};

class Registry {
public:
    // Throws std::invalid_argument if `id` is already in use by a live entity.
    Entity create(StableId id);
    // Returns false for stale/null handles. Removes all components.
    bool destroy(Entity e);
    bool alive(Entity e) const noexcept;
    std::optional<StableId> stable_id(Entity e) const noexcept;
    std::optional<Entity> find(StableId id) const noexcept;
    std::size_t size() const noexcept { return by_id_.size(); }
    // Live entities in ascending StableId order.
    std::vector<Entity> entities() const;

    template <class T> T& emplace(Entity e, T value) {
        const StableId id = require(e);
        return storage<T>().insert_or_assign(id, e, std::move(value));
    }
    template <class T> T* get(Entity e) {
        auto id = stable_id(e);
        if (!id) return nullptr;
        auto* s = find_storage<T>();
        return s ? s->find(*id) : nullptr;
    }
    template <class T> bool has(Entity e) { return get<T>(e) != nullptr; }
    template <class T> bool remove(Entity e) {
        auto id = stable_id(e);
        auto* s = find_storage<T>();
        return id && s && s->erase(*id);
    }
    template <class T> std::size_t count() {
        auto* s = find_storage<T>();
        return s ? s->size() : 0;
    }

    // Calls f(Entity, T0&, Ts&...) for every entity having all components, in StableId order.
    // Do not add/remove T0 components or destroy entities inside f.
    template <class T0, class... Ts, class F> void each(F&& f) {
        auto* primary = find_storage<T0>();
        if (!primary) return;
        for (auto& item : primary->items()) {
            if constexpr (sizeof...(Ts) == 0) {
                f(item.entity, item.value);
            } else {
                std::tuple<Ts*...> rest{lookup<Ts>(item.id)...};
                if (((std::get<Ts*>(rest) != nullptr) && ...))
                    f(item.entity, item.value, *std::get<Ts*>(rest)...);
            }
        }
    }

private:
    struct Slot { uint32_t generation = 0; bool live = false; StableId id = 0; };

    StableId require(Entity e) const {
        auto id = stable_id(e);
        if (!id) throw std::invalid_argument("ecs: stale or null entity handle");
        return *id;
    }
    template <class T> ComponentStorage<T>* find_storage() {
        const uint32_t i = detail::component_type_index<T>();
        return i < storages_.size() ? static_cast<ComponentStorage<T>*>(storages_[i].get()) : nullptr;
    }
    template <class T> ComponentStorage<T>& storage() {
        const uint32_t i = detail::component_type_index<T>();
        if (i >= storages_.size()) storages_.resize(i + 1);
        if (!storages_[i]) storages_[i] = std::make_unique<ComponentStorage<T>>();
        return *static_cast<ComponentStorage<T>*>(storages_[i].get());
    }
    template <class T> T* lookup(StableId id) {
        auto* s = find_storage<T>();
        return s ? s->find(id) : nullptr;
    }

    std::vector<Slot> slots_;
    std::deque<uint32_t> free_;                                  // FIFO reuse
    std::vector<std::pair<StableId, uint32_t>> by_id_;           // sorted: StableId -> slot
    std::vector<std::unique_ptr<detail::IStorage>> storages_;    // by component type index
};

} // namespace eng::ecs
