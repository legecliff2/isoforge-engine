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
    int ac;
    if (in.has_armor) {
        const ArmorPiece& a = in.armor;
        ac = a.base_ac + armor_dex_term(a, dex) + a.magic_bonus;
        r.stealth_disadvantage = a.stealth_disadvantage;
        r.str_shortfall = a.str_min > 0 && int(in.abilities[Ability::Str]) < int(a.str_min);
    } else {
        ac = in.unarmored_base + dex;
        if (in.has_alt_ability) ac = std::max(ac, in.unarmored_base + dex + modifier(in.abilities, in.alt_ability));
        if (in.natural_base > 0) ac = std::max(ac, in.natural_base + (in.natural_dex_bonus ? dex : 0));
    }
    if (in.has_shield) ac += in.shield.ac_bonus + in.shield.magic_bonus;
    if (in.has_armor) ac += in.armored_bonus;
    ac += in.flat_bonus;
    r.ac = int16_t(std::clamp(ac, 0, 100));
    r.speed_ft = int16_t(std::max(0, int(in.base_speed_ft) - (r.str_shortfall ? 10 : 0)));
    return r;
}

} // namespace rules5e
