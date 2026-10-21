#include "sim/light_driver.hpp"

#include <algorithm>
#include <utility>

#include "sim/combat.hpp"
#include "world/chunk_streamer.hpp"

namespace eng::sim {

namespace {
world::ChunkCoord add(world::ChunkCoord c, world::ChunkCoord off) { return {c.x + off.x, c.y + off.y, c.level}; }
} // namespace

std::vector<Viewer> combat_viewers(const Combat& combat) {
    std::vector<Viewer> out;
    for (const rules5e::Combatant& cb : combat.state().combatants) {
        if (cb.dead || cb.gone) continue;
        Viewer v;
        v.id = cb.id;
        v.team = cb.team;
        v.tile = carrier_centre(combat.pos(cb.id), combat.footprint_size(cb.id));
        v.senses = senses_of(cb);
        v.blinded = cb.has(rules5e::Condition::Blinded);
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const Viewer& a, const Viewer& b) { return a.id < b.id; });
    return out;
}

LightDriver::LightDriver(LightProviderInputs in) : in_(std::move(in)) {}

LightDriver::~LightDriver() {
    if (pinned_ && in_.streamer) in_.streamer->unpin(in_.pin_owner);
}

std::shared_ptr<const LightFrame> LightDriver::latest() const {
    std::lock_guard<std::mutex> lk(mu_);
    return latest_;
}

std::shared_ptr<const LightFrame> LightDriver::step(const Combat& combat) {
    const world::ChunkCoord off = combat.frame_offset();
    const std::vector<world::ChunkCoord>& frame = combat.frame_chunks();
    const int16_t level = frame.empty() ? int16_t(0) : frame.front().level;
    const Vec2i tile_off{off.x * world::kChunkSize, off.y * world::kChunkSize};
    const std::vector<Viewer> viewers = combat_viewers(combat);

    // Streaming: pin the combat's chunks while it runs (halos follow from the streamer), foci = creatures then camera.
    if (in_.streamer) {
        if (!combat.ended() && !pinned_) {
            std::vector<world::ChunkCoord> w;
            for (const auto& c : frame) w.push_back(add(c, off));
            in_.streamer->pin(in_.pin_owner, w);
            pinned_ = true;
        } else if (combat.ended() && pinned_) {
            in_.streamer->unpin(in_.pin_owner);
            pinned_ = false;
        }
        std::vector<world::Focus> foci;
        for (const Viewer& v : viewers) foci.push_back(world::Focus{{v.tile.x + tile_off.x, v.tile.y + tile_off.y}, level, 1});
        if (in_.camera_tile) foci.push_back(world::Focus{*in_.camera_tile, level, 1});
        in_.streamer->set_foci(foci);
    }

    // Halo gate (§7): only Complete halos are evaluated; Pending blocks, Failed is an error state.
    std::vector<world::ChunkCoord> ready, blocked;
    split_by_halo(frame, [&](world::ChunkCoord c) {
        return in_.streamer ? in_.streamer->halo_state(add(c, off)) : world::HaloState::Complete;
    }, ready, blocked);
    pending_ = false;
    failed_.clear();
    for (const auto& c : blocked) {
        const world::HaloState hs = in_.streamer->halo_state(add(c, off));
        if (hs == world::HaloState::Failed) failed_.push_back(add(c, off));
        else pending_ = true;
    }

    auto chunk_at = [&](world::ChunkCoord wc) -> const world::Chunk* {
        if (in_.streamer) return in_.streamer->get(wc);
        for (const world::Chunk* ch : in_.chunks)
            if (ch && ch->coord == wc) return ch;
        return nullptr;
    };

    LightWorld w;
    w.grid = &combat.grid();
    // Static chunk@2 lights of every resident chunk in the ready chunks' 3x3 halos (kMaxLightFt < kChunkSize).
    std::vector<world::ChunkCoord> halo;
    for (const auto& c : ready)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) halo.push_back(world::ChunkCoord{c.x + dx, c.y + dy, c.level});
    std::sort(halo.begin(), halo.end());
    halo.erase(std::unique(halo.begin(), halo.end()), halo.end());
    for (const auto& fc : halo) {
        const world::ChunkCoord wc = add(fc, off);
        const world::Chunk* ch = chunk_at(wc);
        if (!ch) continue;   // known absent (no sources) or outside a non-streamed world
        for (std::size_t i = 0; i < ch->lights.size(); ++i) {
            const world::Chunk::Light& cl = ch->lights[i];
            StaticLight sl;
            sl.tile = cl.tile;
            sl.bright_ft = cl.bright_ft;
            sl.dim_ft = cl.dim_ft;
            sl.flags = cl.flags;
            sl.stable_id = cl.stable_id;
            LightSource s = static_source(wc, uint32_t(i), sl, cl.preset);   // id in world coords; tile into the frame
            s.tile = Vec2i{s.tile.x - tile_off.x, s.tile.y - tile_off.y};
            w.sources.push_back(std::move(s));
        }
    }
    // Ambient: a chunk@2 ambient authored identically by every ready chunk wins; otherwise the default.
    w.ambient = in_.ambient;
    {
        std::optional<world::Chunk::Ambient> common;
        bool same = !ready.empty();
        for (const auto& c : ready) {
            const world::Chunk* ch = chunk_at(add(c, off));
            if (!ch || !ch->ambient) { same = false; break; }
            if (common && *common != *ch->ambient) { same = false; break; }
            common = ch->ambient;
        }
        if (same && common) w.ambient = Ambient{AmbientKind(common->kind), Light(common->level)};
    }
    if (in_.attached_sources) in_.attached_sources(combat, w.sources);
    w.normalise();
    cache_.begin_step(std::move(w));
    vis_.begin_step(cache_, viewers, ready);

    LightFrameInputs fi;
    fi.chunks = ready;
    fi.blocked = blocked;
    fi.visibility = &vis_;
    std::shared_ptr<const LightFrame> prev = latest();
    auto f = publish_light_frame(cache_, fi, ++step_, prev.get());
    {
        std::lock_guard<std::mutex> lk(mu_);
        latest_ = f;
    }
    return f;
}

} // namespace eng::sim
