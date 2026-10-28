#include "aoe_preview.hpp"

#include <algorithm>
#include <string>

#include "rules5e/encounter.hpp"
#include "rules5e/spells.hpp"
#include "sim/combat.hpp"

namespace slice {

namespace {

AreaPreviewShape to_shape(rules5e::AreaShape s) {
    switch (s) {
    case rules5e::AreaShape::Cone: return AreaPreviewShape::Cone;
    case rules5e::AreaShape::Cube: return AreaPreviewShape::Cube;
    case rules5e::AreaShape::Cylinder: return AreaPreviewShape::Cylinder;
    case rules5e::AreaShape::Line: return AreaPreviewShape::Line;
    case rules5e::AreaShape::Sphere: return AreaPreviewShape::Sphere;
    case rules5e::AreaShape::None: break;
    }
    return AreaPreviewShape::None;
}

std::string short_label(const std::string& content_id) {
    const auto dot = content_id.rfind('.');
    std::string s = dot == std::string::npos ? content_id : content_id.substr(dot + 1);
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// The one place the tile set is produced. area_targets does not return tiles, and the geometry
// it uses is not a public helper. Do not reimplement it here.
void fill_area_tiles(AreaPreviewView& v, const eng::sim::Combat& combat, uint32_t caster,
                     const rules5e::SpellDef& def, eng::Vec2i aim) {
    // TODO(golem-area_preview): replace this stub with one call. area_targets itself will call
    // the same function, so the creature list below stays the hit set. No second copy of the
    // geometry, and no altitude fade (the overlay stays 2D):
    //
    //   v.tiles = combat.area_preview(caster, def.area, def.area_size_ft, def.area_width_ft,
    //                                 combat.pos(caster), aim);
    //   v.tiles_pending = false;
    //
    (void)combat;
    (void)caster;
    (void)def;
    (void)aim;
    v.tiles.clear();
    v.tiles_pending = true;
}

} // namespace

std::vector<uint32_t> area_spell_ids(const eng::sim::Combat& c, uint32_t actor) {
    std::vector<uint32_t> ids;
    const rules5e::Combatant* who = rules5e::find(c.state(), actor);
    if (!who) return ids;
    auto consider = [&](uint32_t id) {
        const rules5e::SpellDef* d = c.spell_def(id);
        if (!d || d->area == rules5e::AreaShape::None) return;
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
        ids.push_back(id);
    };
    for (uint32_t id : who->spells_known) consider(id);
    for (const rules5e::InnateSpell& inn : who->innate) consider(inn.spell_id);
    return ids;
}

AreaPreviewView area_probe(const eng::sim::Combat& c, uint32_t caster, uint32_t spell_id, eng::Vec2i aim,
                           uint32_t frame_ms) {
    AreaPreviewView v;
    v.pulse_ms = frame_ms;
    v.aim = aim;
    v.caster = caster;
    v.spell_id = spell_id;
    const rules5e::SpellDef* def = c.spell_def(spell_id);
    if (!def || def->area == rules5e::AreaShape::None) {
        v.note = "NO AREA SPELL";
        return v;
    }
    if (!rules5e::find(c.state(), caster)) {
        v.note = "NO ACTOR";
        return v;
    }
    v.present = true;
    v.shape = to_shape(def->area);
    v.spell_label = short_label(def->content_id);
    fill_area_tiles(v, c, caster, *def, aim);

    std::string err;
    const std::vector<rules5e::CombatantId> ids = c.area_targets(caster, *def, aim, &err);
    // "no creature in the area" is a valid empty template, not a rejected aim.
    if (err == "no creature in the area") err.clear();
    if (!err.empty()) {
        v.note = err;
        return v;
    }
    const rules5e::Combatant* caster_c = rules5e::find(c.state(), caster);
    for (rules5e::CombatantId id : ids) {
        if (std::find_if(v.hits.begin(), v.hits.end(), [&](const AreaHitView& h) { return h.id == id; }) != v.hits.end())
            continue;
        AreaHitView h;
        h.id = id;
        const rules5e::Combatant* hit = rules5e::find(c.state(), id);
        h.side = (caster_c && hit && hit->team == caster_c->team) ? AreaHitSide::Ally : AreaHitSide::Enemy;
        v.hits.push_back(h);
    }
    return v;
}

bool AreaTargetView::handle(Cmd cmd, const Session&) {
    if (cmd == Cmd::Toggle) {
        open_ = !open_;
        if (!open_) spell_index_ = 0;
        return false;
    }
    if (!open_ || cmd != Cmd::NextSpell) return false;
    ++spell_index_;
    return false;
}

void AreaTargetView::decorate(const Session& s, ViewModel& vm, eng::Vec2i cursor_tile, uint32_t frame_ms) const {
    vm.area = {};
    if (!open_) return;
    const uint32_t caster = s.current_actor();
    if (caster == 0 || !s.actor_is_pc(caster)) {
        vm.area.note = caster == 0 ? std::string("NO ACTOR") : std::string("NOT YOUR TURN");
        vm.area.pulse_ms = frame_ms;
        return;
    }
    const std::vector<uint32_t> spells = s.area_spell_ids(caster);
    if (spells.empty()) {
        vm.area.note = "NO AREA SPELL";
        vm.area.pulse_ms = frame_ms;
        return;
    }
    const uint32_t spell = spells[spell_index_ % spells.size()];
    vm.area = s.query_area(caster, spell, cursor_tile, frame_ms);
}

} // namespace slice
