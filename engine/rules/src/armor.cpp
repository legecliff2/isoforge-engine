#include "rules5e/armor.hpp"

#include <algorithm>

namespace rules5e {

int armor_dex_term(const ArmorPiece& a, int dex_mod) {
    if (!a.dex_bonus) return 0;
    return a.has_dex_cap ? std::min<int>(dex_mod, a.dex_cap) : dex_mod;
}

AcResult compute_ac(const AcInput& in) {
    AcResult r;
    const int dex = modifier(in.abilities, Ability::Dex);
    // Step 1: base, pick one, highest.
    int ac;
    if (in.has_armor) {
        const ArmorPiece& a = in.armor;
        ac = a.base_ac + armor_dex_term(a, dex) + a.magic_bonus;
        r.base = AcBaseKind::Armor;
        r.stealth_disadvantage = a.stealth_disadvantage;
        r.str_shortfall = a.str_min > 0 && int(in.abilities[Ability::Str]) < int(a.str_min);
        r.nonproficient_armor = !in.armor_proficient;
    } else {
        ac = in.unarmored_base + dex;
        r.base = AcBaseKind::Unarmored;
        auto take = [&](int v, AcBaseKind k) { if (v > ac) { ac = v; r.base = k; } };
        if (in.has_alt_ability) take(in.unarmored_base + dex + modifier(in.abilities, in.alt_ability), AcBaseKind::UnarmoredDefense);
        if (in.natural_base > 0) take(in.natural_base + (in.natural_dex_bonus ? dex : 0), AcBaseKind::Natural);
        for (const AcBaseCandidate& c : in.bases) {
            if (c.forbids_shield && in.has_shield) continue;
            switch (c.kind) {
            case AcBaseKind::Natural: take(c.fixed + (c.natural_dex ? dex : 0), c.kind); break;
            case AcBaseKind::UnarmoredDefense: take(c.fixed + dex + modifier(in.abilities, c.ability), c.kind); break;
            case AcBaseKind::FixedPlusDex: take(c.fixed + dex, c.kind); break;
            case AcBaseKind::Unarmored: take(10 + dex, c.kind); break;
            case AcBaseKind::Armor: break;   // armor comes from `armor`, never from a candidate
            }
        }
    }
    // Step 2: one shield.
    if (in.has_shield) { ac += in.shield.ac_bonus + in.shield.magic_bonus; r.shield_counted = true; }
    // Step 3: additive rows (legacy armored / flat bonuses first; then rows, same source => highest).
    if (in.has_armor) ac += in.armored_bonus;
    ac += in.flat_bonus;
    for (std::size_t i = 0; i < in.bonuses.size(); ++i) {
        const AcBonusRow& b = in.bonuses[i];
        if (b.requires_armor && !in.has_armor) continue;
        bool shadowed = false;   // a later or earlier row of the same source with a higher amount (first wins ties)
        for (std::size_t j = 0; j < in.bonuses.size() && !shadowed; ++j) {
            if (j == i || in.bonuses[j].source != b.source || (in.bonuses[j].requires_armor && !in.has_armor)) continue;
            shadowed = in.bonuses[j].amount > b.amount || (in.bonuses[j].amount == b.amount && j < i);
        }
        if (!shadowed) ac += b.amount;
    }
    // Step 4: documented adjustment. Step 5: floors, then clamp.
    ac += in.ac_adjust;
    if (in.floor > 0) ac = std::max<int>(ac, in.floor);
    r.ac = int16_t(std::clamp(ac, 0, 100));
    r.speed_ft = int16_t(std::max(0, int(in.base_speed_ft) - (r.str_shortfall ? 10 : 0)));
    return r;
}

} // namespace rules5e
