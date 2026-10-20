#include "rules5e/initiative.hpp"

#include <algorithm>
#include "rules5e/abilities.hpp"

namespace rules5e {

std::vector<InitiativeEntry> roll_initiative(rng::Rng& r, std::span<const Combatant> cs) {
    struct Row { InitiativeEntry e; uint32_t tiebreak; };
    std::vector<Row> rows;
    rows.reserve(cs.size());
    for (const auto& c : cs) {
        int dex = modifier(c.abilities, Ability::Dex);
        int total = r.d(20) + dex + ((c.half_proficiency & 2) ? c.proficiency_bonus / 2 : 0);   // 7.2za Jack of All Trades
        rows.push_back({{c.id, int16_t(total), int8_t(dex)}, 0});
    }
    for (auto& row : rows) row.tiebreak = r.next_u32();
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.e.total != b.e.total) return a.e.total > b.e.total;
        if (a.e.dex_mod != b.e.dex_mod) return a.e.dex_mod > b.e.dex_mod;
        if (a.tiebreak != b.tiebreak) return a.tiebreak > b.tiebreak;
        return a.e.id < b.e.id;
    });
    std::vector<InitiativeEntry> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back(row.e);
    return out;
}

} // namespace rules5e
