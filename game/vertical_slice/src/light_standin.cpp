#include "light_standin.hpp"

#include <algorithm>
#include <cstdlib>

namespace slice {

void fill_standin_light(ViewModel& vm, LightStandinState& st) {
    LightView& L = vm.light;
    L = {};
    L.present = true;
    L.frame = ++st.frame;
    LightChunkView ch;
    ch.has_fog = true;
    std::vector<eng::Vec2i> pcs;
    for (const ActorView& a : vm.actors)
        if (a.is_pc && !a.dead) pcs.push_back(a.tile);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            int best = 0;   // 0 dark, 1 dim, 2 bright
            int near = 1 << 20;
            for (eng::Vec2i p : pcs) {
                const int d = std::max(std::abs(p.x - x), std::abs(p.y - y)) * 5;
                near = std::min(near, d);
                best = std::max(best, d <= 20 ? 2 : d <= 40 ? 1 : 0);
            }
            const int i = y * 32 + x;
            ch.samples[size_t(i)] = uint8_t(best);
            if (near <= 40) {
                ch.seen_now[size_t(i / 64)] |= uint64_t(1) << (i % 64);
                st.explored[size_t(i / 64)] |= uint64_t(1) << (i % 64);
            }
        }
    ch.explored = st.explored;
    // Dirty rect = bounding box of changed samples (whole chunk on the first frame).
    int x0 = 32, y0 = 32, x1 = -1, y1 = -1;
    for (int i = 0; i < 32 * 32; ++i)
        if (!st.started || ch.samples[size_t(i)] != st.prev[size_t(i)]) {
            x0 = std::min(x0, i % 32); x1 = std::max(x1, i % 32);
            y0 = std::min(y0, i / 32); y1 = std::max(y1, i / 32);
        }
    if (!st.started) L.added.push_back({0, 0, 0});
    if (x1 >= 0) L.dirty.push_back({{0, 0, 0}, eng::RectI{x0, y0, x1 - x0 + 1, y1 - y0 + 1}});
    st.prev = ch.samples;
    st.started = true;
    for (const ActorView& a : vm.actors)
        if (a.is_pc && !a.dead) {
            LightSourceView s;
            s.id = (uint64_t(1) << 63) | a.id;   // "attached" tag bit
            s.attached = true;
            s.tile = a.tile;
            s.carrier = 1;
            s.carrier_id = a.id;
            s.bright_ft = 20;
            s.dim_ft = 20;
            s.preset = "srd51.core.light.torch";
            L.sources.push_back(s);
        }
    L.chunks.push_back(ch);
    L.fog = true;
}

} // namespace slice
