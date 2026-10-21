// docs/proposals/EQUIPMENT.md 9 (Engine Core): Combat's equipment side, active only with CombatOptions::equipment. Loadouts at
// setup, the rules hooks' apply_equip (draw, stow, drop, pick up, shield, hand over, the free object interaction), derived-stat
// recomputation, ground items as ECS entities, the Engine Core legal actions (PickUp, HandOver) and equipment lights.
#include <algorithm>

#include "rules5e/equipment.hpp"
#include "sim/combat.hpp"

namespace eng::sim {

using rules5e::ActionKind;
using rules5e::EquipOp;
using rules5e::HeldItem;
using rules5e::ItemKind;

void Combat::setup_equipment(const ContentLibrary& content) {
    items_ = content.items();
    if (!items_ || items_->empty()) return;
    for (std::size_t si = 0; si < created_count_ && si < slots_.size(); ++si) {
        const Slot& sl = slots_[si];
        std::vector<LoadoutSeed> seeds;
        for (const EquipmentEntry& e : sl.archetype->equipment)
            if (const rules5e::ItemDef* d = items_->find(e.id)) seeds.push_back({d->id, e.equipped});
        if (seeds.empty()) continue;
        build_loadout(state_, sl.id, seeds, *items_, sl.archetype->two_handed_grip);
    }
    // Light ids (LIGHTING 10.1): once, in item instance order; they never change (drop / pick up / hand over keep them).
    std::vector<uint32_t> lights;
    for (const auto& l : state_.loadouts) {
        for (const HeldItem* h : {&l.view.main, &l.view.off})
            if (h->instance)
                if (const auto* d = items_->find(h->def); d && d->kind == ItemKind::Light) lights.push_back(h->instance);
        for (const auto& h : l.view.carried)
            if (const auto* d = items_->find(h.def); d && d->kind == ItemKind::Light) lights.push_back(h.instance);
    }
    std::sort(lights.begin(), lights.end());
    for (std::size_t i = 0; i < lights.size(); ++i) item_lights_.push_back({lights[i], uint32_t(i + 1)});
    for (const auto& l : state_.loadouts) derive_seen_[l.owner] = derive_inputs(state_, l.owner);
}

const rules5e::Loadout* Combat::loadout(CombatantId id) const { return find_loadout(state_, id); }

bool Combat::within_5ft(CombatantId a, Vec2i tile) const {
    const Spot s = spot(a);
    return rect_distance(s.p, s.n, tile, 1) <= 1;
}

void Combat::refresh_interactions(rules5e::EncounterState& st) const {
    for (auto& l : st.loadouts) l.view.interaction_used = l.interaction_turn != 0 && l.interaction_turn == turns_started_;
}

namespace {

std::optional<HeldItem> take_hand(rules5e::LoadoutView& v, uint32_t instance) {
    for (HeldItem* h : {&v.main, &v.off})
        if (instance != 0 && h->instance == instance) { const HeldItem out = *h; *h = HeldItem{}; return out; }
    return std::nullopt;
}
std::optional<HeldItem> take_carried(rules5e::LoadoutView& v, uint32_t instance) {
    for (auto it = v.carried.begin(); it != v.carried.end(); ++it)
        if (instance != 0 && it->instance == instance) { const HeldItem out = *it; v.carried.erase(it); return out; }
    return std::nullopt;
}
// equip_hand 1 main, 2 off, 0 the first empty (main first). rules checked the hand is free.
void put_hand(rules5e::LoadoutView& v, const HeldItem& h, uint8_t hand) {
    if (hand == 2 || (hand == 0 && v.main.instance != 0)) v.off = h;
    else v.main = h;
}
void put_carried(rules5e::LoadoutView& v, const HeldItem& h) {
    auto it = std::lower_bound(v.carried.begin(), v.carried.end(), h.instance,
                               [](const HeldItem& x, uint32_t i) { return x.instance < i; });
    v.carried.insert(it, h);
}
HeldItem stowed(HeldItem h) {   // a lit light is covered when stowed and relit when drawn (EQUIPMENT.md 6.3)
    if (h.flags & rules5e::kHeldLit) h.flags = uint8_t((h.flags & ~rules5e::kHeldLit) | kHeldRelight);
    return h;
}
HeldItem drawn(HeldItem h) {
    if (h.flags & kHeldRelight) h.flags = uint8_t((h.flags & ~kHeldRelight) | rules5e::kHeldLit);
    return h;
}

} // namespace

// EncounterHooks::apply_equip: rules checked legality (equip_legal) and spent the action for Action-cost ops. Mutates only `st`
// (lookahead copies stay off the ECS; ground entities follow state_ after each completed step).
void Combat::apply_equip(rules5e::EncounterState& st, const rules5e::ActionRequest& req, uint8_t cost_kind) {
    rules5e::Loadout* l = find_loadout(st, req.actor);
    if (!l) return;
    rules5e::LoadoutView& v = l->view;
    CombatantId other = 0;
    switch (EquipOp(req.equip_op)) {
    case EquipOp::Draw:
    case EquipOp::DonShield:
        if (auto h = take_carried(v, req.equip_item)) put_hand(v, drawn(*h), req.equip_hand);
        break;
    case EquipOp::Stow:
    case EquipOp::DoffShield:
        if (auto h = take_hand(v, req.equip_item)) put_carried(v, stowed(*h));
        break;
    case EquipOp::Drop:
        if (auto h = take_hand(v, req.equip_item)) {
            const Vec2i at = pos(req.actor);
            rules5e::GroundItem g{*h, at.x, at.y};
            auto it = std::lower_bound(st.ground_items.begin(), st.ground_items.end(), h->instance,
                                       [](const rules5e::GroundItem& x, uint32_t i) { return x.item.instance < i; });
            st.ground_items.insert(it, g);
            st.ground_items_seen = true;
        }
        break;
    case EquipOp::PickUp:
        for (auto it = st.ground_items.begin(); it != st.ground_items.end(); ++it)
            if (it->item.instance == req.equip_item) {
                const HeldItem h = it->item;
                st.ground_items.erase(it);
                put_hand(v, h, req.equip_hand);
                break;
            }
        break;
    case EquipOp::HandOver:
        if (rules5e::Loadout* to = find_loadout(st, req.equip_to))
            if (auto h = take_hand(v, req.equip_item)) { put_hand(to->view, *h, 0); other = req.equip_to; }
        break;
    default: break;
    }
    if (cost_kind == uint8_t(rules5e::EquipCost::Interaction)) {   // the one free object interaction of this turn
        v.interaction_used = true;
        l->interaction_turn = turns_started_;
    }
    recompute_derived(st, req.actor, *items_);
    if (other) recompute_derived(st, other, *items_);
}

void Combat::recompute(rules5e::EncounterState& st, CombatantId id) {
    const auto* c = rules5e::find(st, id);
    if (!c || c->form != rules5e::FormKind::None) return;   // a form's statistics are its own block (R-G12)
    recompute_derived(st, id, *items_);
    ++recomputes_;
}

// After every completed step (live and replay): the interaction flag, the recompute triggers (EQUIPMENT.md 7.2: ability
// scores, proficiency / level, AC effects; equip changes recompute in apply_equip; conditions never), and ground entities.
void Combat::equipment_after_step() {
    if (!items_) return;
    refresh_interactions(state_);
    for (const auto& l : state_.loadouts) {
        const DeriveInputs in = derive_inputs(state_, l.owner);
        auto it = derive_seen_.find(l.owner);
        if (it != derive_seen_.end() && it->second == in) continue;
        derive_seen_[l.owner] = in;
        recompute(state_, l.owner);
    }
    sync_ground_entities();
}

void Combat::sync_ground_entities() {
    // Wanted: one entity per ground item (StableId kGroundItemIdBase + instance), at its tile.
    std::vector<uint32_t> have;
    for (const ecs::Entity e : reg_->entities())
        if (const GroundItemLink* g = reg_->get<GroundItemLink>(e)) have.push_back(g->instance);
    for (uint32_t inst : have) {
        bool keep = false;
        for (const auto& g : state_.ground_items) keep = keep || g.item.instance == inst;
        if (!keep)
            if (auto e = reg_->find(kGroundItemIdBase + inst)) reg_->destroy(*e);
    }
    for (const auto& g : state_.ground_items) {
        if (std::find(have.begin(), have.end(), g.item.instance) != have.end()) continue;
        const ecs::Entity e = reg_->create(kGroundItemIdBase + g.item.instance);
        reg_->emplace(e, TilePos{g.x, g.y});
        reg_->emplace(e, GroundItemLink{g.item.instance, g.item.def});
    }
}

std::vector<rules5e::ActionRequest> Combat::legal_actions() {
    install_hooks();
    std::vector<rules5e::ActionRequest> out = rules5e::legal_actions(state_);
    if (!options_.equipment || state_.loadouts.empty() || state_.ended || state_.order.empty()) return out;
    const CombatantId actor = rules5e::current_actor(state_);
    const rules5e::Loadout* l = find_loadout(state_, actor);
    if (!l) return out;
    auto push = [&](rules5e::ActionRequest r) {
        if (rules5e::equip_legal(state_, r).kind != rules5e::EquipCost::Refused) out.push_back(r);
    };
    for (const auto& g : state_.ground_items) {   // instance order
        if (!within_5ft(actor, {g.x, g.y})) continue;
        for (const uint8_t hand : {uint8_t(1), uint8_t(2)}) {
            rules5e::ActionRequest r{actor, ActionKind::UseObject, 0};
            r.equip_op = uint8_t(EquipOp::PickUp); r.equip_item = g.item.instance; r.equip_hand = hand; r.equip_in_reach = true;
            push(r);
        }
    }
    std::vector<uint32_t> held_ids;
    for (const HeldItem* h : {&l->view.main, &l->view.off}) if (h->instance) held_ids.push_back(h->instance);
    std::sort(held_ids.begin(), held_ids.end());
    const Spot as = spot(actor);
    for (const uint32_t inst : held_ids)
        for (const auto& k : state_.combatants) {
            if (k.id == actor || k.dead || !find_loadout(state_, k.id)) continue;
            const Spot ks = spot(k.id);
            if (rect_distance(as.p, as.n, ks.p, ks.n) > 1) continue;
            rules5e::ActionRequest r{actor, ActionKind::UseObject, 0};
            r.equip_op = uint8_t(EquipOp::HandOver); r.equip_item = inst; r.equip_to = k.id; r.equip_in_reach = true;
            push(r);
        }
    return out;
}

std::vector<EquipmentLight> Combat::equipment_lights() const {
    std::vector<EquipmentLight> out;
    for (const auto& [inst, lid] : item_lights_) {
        EquipmentLight e;
        e.light_id = lid;
        e.item = inst;
        bool found = false;
        for (const auto& l : state_.loadouts) {
            for (const HeldItem* h : {&l.view.main, &l.view.off})
                if (h->instance == inst) { found = true; e.def = h->def; e.lit = (h->flags & rules5e::kHeldLit) != 0; }
            for (const auto& h : l.view.carried)
                if (h.instance == inst) { found = true; e.def = h.def; e.lit = false; }
            if (found) { e.holder_id = l.owner; e.tile = pos(l.owner); break; }
        }
        if (!found)
            for (const auto& g : state_.ground_items)
                if (g.item.instance == inst) {
                    found = true; e.def = g.item.def; e.lit = (g.item.flags & rules5e::kHeldLit) != 0;
                    e.holder_id = 0; e.tile = {g.x, g.y};
                }
        out.push_back(e);
    }
    return out;
}

} // namespace eng::sim
