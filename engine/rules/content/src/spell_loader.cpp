#include "rules5e_content/spell_loader.hpp"

#include <algorithm>
#include <fstream>
#include <map>

#include "rules5e/dice.hpp"

namespace rules5e_content {

using eng::assets::Json;
using namespace rules5e;

namespace {

template <class E>
bool lookup(const std::map<std::string, E>& m, const Json& v, E& out) {
    if (!v.is_string()) return false;
    auto it = m.find(v.get<std::string>());
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

const std::map<std::string, Ability> kAbility{{"str", Ability::Str}, {"dex", Ability::Dex}, {"con", Ability::Con},
                                              {"int", Ability::Int}, {"wis", Ability::Wis}, {"cha", Ability::Cha}};
const std::map<std::string, DamageType> kDamage{
    {"acid", DamageType::Acid}, {"bludgeoning", DamageType::Bludgeoning}, {"cold", DamageType::Cold},
    {"fire", DamageType::Fire}, {"force", DamageType::Force}, {"lightning", DamageType::Lightning},
    {"necrotic", DamageType::Necrotic}, {"piercing", DamageType::Piercing}, {"poison", DamageType::Poison},
    {"psychic", DamageType::Psychic}, {"radiant", DamageType::Radiant}, {"slashing", DamageType::Slashing},
    {"thunder", DamageType::Thunder}};
const std::map<std::string, Condition> kCondition{
    {"blinded", Condition::Blinded}, {"charmed", Condition::Charmed}, {"deafened", Condition::Deafened},
    {"frightened", Condition::Frightened}, {"grappled", Condition::Grappled}, {"incapacitated", Condition::Incapacitated},
    {"invisible", Condition::Invisible}, {"paralyzed", Condition::Paralyzed}, {"petrified", Condition::Petrified},
    {"poisoned", Condition::Poisoned}, {"prone", Condition::Prone}, {"restrained", Condition::Restrained},
    {"stunned", Condition::Stunned}, {"unconscious", Condition::Unconscious}, {"exhaustion", Condition::Exhaustion}};

SpellLoad err(std::string m) { return {std::nullopt, std::move(m)}; }

bool small_int(const Json& v, int lo, int hi, int& out) {
    if (!v.is_number_integer()) return false;
    auto x = v.get<int64_t>();
    if (x < lo || x > hi) return false;
    out = int(x);
    return true;
}

bool read_damage(const Json& d, SpellDef& s) {
    if (!d.is_object() || !d.contains("dice") || !d.contains("type") || !d["dice"].is_string()) return false;
    auto e = parse_dice(d["dice"].get<std::string>());
    if (!e) return false;
    s.dice = *e;
    return lookup(kDamage, d["type"], s.damage_type);
}

} // namespace

SpellLoad spell_from_json(const Json& j) {
    if (!j.is_object() || j.value("$schema", "") != "spell@1") return err("not a spell@1 document");
    SpellDef s;
    if (!j.contains("id") || !j["id"].is_string()) return err("id missing");
    s.content_id = j["id"].get<std::string>();
    s.id = spell_id_of(s.content_id);
    int lvl = 0;
    if (!j.contains("level") || !small_int(j["level"], 0, 9, lvl)) return err("level must be 0..9");
    s.level = uint8_t(lvl);

    const Json ct = j.value("casting_time", Json::object());
    const std::string ck = ct.value("kind", "");
    if (ck == "action") s.casting_time = CastingTime::Action;
    else if (ck == "bonus_action") s.casting_time = CastingTime::BonusAction;
    else if (ck == "reaction") {
        s.casting_time = CastingTime::Reaction;
        const std::string tr = ct.value("trigger", "");
        if (tr == "being_hit") s.reaction_trigger = ReactionTrigger::BeingHit;
        else if (tr == "spell_cast") s.reaction_trigger = ReactionTrigger::SpellCast;
        else return err("casting_time.trigger must be being_hit or spell_cast");
    } else return err("casting_time.kind invalid");

    const Json rg = j.value("range", Json::object());
    const std::string rk = rg.value("kind", "");
    int ft = 0;
    if (rk == "self") s.range_ft = 0;
    else if (rk == "touch") s.range_ft = 5;
    else if (rk == "ranged" && rg.contains("feet") && small_int(rg["feet"], 0, 65535, ft)) s.range_ft = uint16_t(ft);
    else return err("range invalid");

    const Json c = j.value("components", Json::object());
    if (c.value("v", false)) s.components |= kComponentV;
    if (c.value("s", false)) s.components |= kComponentS;
    if (c.contains("m")) s.components |= kComponentM;
    if (s.components == 0) return err("components: at least one of v, s, m");

    const Json du = j.value("duration", Json::object());
    s.concentration = du.value("concentration", false);

    int n = 1;
    if (j.contains("targets") && !small_int(j["targets"], 1, 255, n)) return err("targets invalid");
    s.targets = uint8_t(n);

    if (!j.contains("effect") || !j["effect"].is_object()) return err("effect missing");
    const Json& e = j["effect"];
    const std::string kind = e.value("kind", "");
    if (kind == "spell_attack") {
        s.kind = SpellEffectKind::SpellAttack;
        s.melee = e.value("attack", "") == "melee";
        if (!e.contains("damage") || !read_damage(e["damage"], s)) return err("effect.damage invalid");
    } else if (kind == "save") {
        s.kind = SpellEffectKind::Save;
        if (!e.contains("ability") || !lookup(kAbility, e["ability"], s.save_ability)) return err("effect.ability invalid");
        if (e.contains("damage") && !read_damage(e["damage"], s)) return err("effect.damage invalid");
        s.on_save = e.value("on_success", "none") == "half" ? OnSave::Half : OnSave::None;
        if (e.contains("condition")) {
            if (!lookup(kCondition, e["condition"], s.condition)) return err("effect.condition invalid");
            s.applies_condition = true;
        }
        if (e.value("repeat_save", "") == "end_of_turn") s.repeat_save = RepeatSave::EndOfTurn;
        if (!e.contains("damage") && !s.applies_condition) return err("save effect needs damage or condition");
    } else if (kind == "heal") {
        s.kind = SpellEffectKind::Heal;
        auto d = e.contains("dice") && e["dice"].is_string() ? parse_dice(e["dice"].get<std::string>()) : std::nullopt;
        if (!d) return err("effect.dice invalid");
        s.dice = *d;
        s.add_spellcasting_modifier = e.value("add_spellcasting_modifier", false);
    } else if (kind == "auto_hit") {
        s.kind = SpellEffectKind::AutoHit;
        int darts = 0;
        if (!e.contains("darts") || !small_int(e["darts"], 1, 255, darts)) return err("effect.darts invalid");
        s.darts = uint8_t(darts);
        if (!e.contains("damage") || !read_damage(e["damage"], s)) return err("effect.damage invalid");
    } else if (kind == "ac_bonus") {
        s.kind = SpellEffectKind::AcBonus;
        int b = 0;
        if (!e.contains("bonus") || !small_int(e["bonus"], 1, 20, b)) return err("effect.bonus invalid");
        s.ac_bonus = int8_t(b);
        if (e.contains("blocks_spell")) {
            if (!e["blocks_spell"].is_string()) return err("effect.blocks_spell invalid");
            s.blocks_spell = spell_id_of(e["blocks_spell"].get<std::string>());
        }
    } else if (kind == "counter") {
        s.kind = SpellEffectKind::Counter;
        int b = 10;
        if (e.contains("check_dc_base") && !small_int(e["check_dc_base"], 1, 99, b)) return err("effect.check_dc_base invalid");
        s.counter_dc_base = uint8_t(b);
    } else return err("effect.kind invalid");

    const bool reaction_kind = s.kind == SpellEffectKind::AcBonus || s.kind == SpellEffectKind::Counter;
    if (reaction_kind != (s.casting_time == CastingTime::Reaction)) return err("ac_bonus/counter effects are reactions (and only they)");
    if (reaction_kind && s.reaction_trigger != (s.kind == SpellEffectKind::AcBonus ? ReactionTrigger::BeingHit : ReactionTrigger::SpellCast))
        return err("casting_time.trigger doesn't match the effect");

    const Json sc = j.value("scaling", Json::object());
    if (sc.contains("cantrip")) {
        if (s.level != 0) return err("scaling.cantrip is only for cantrips");
        const Json lv = sc["cantrip"].value("levels", Json::array());
        if (!lv.is_array() || lv.empty() || lv.size() > 3) return err("scaling.cantrip.levels invalid");
        s.cantrip_tiers = {0, 0, 0};
        for (std::size_t i = 0; i < lv.size(); ++i) {
            int t = 0;
            if (!small_int(lv[i], 2, 20, t)) return err("scaling.cantrip.levels invalid");
            s.cantrip_tiers[i] = uint8_t(t);
        }
        s.cantrip_scales = true;
    }
    if (sc.contains("upcast")) {
        if (s.level == 0) return err("scaling.upcast is only for leveled spells");
        const Json& u = sc["upcast"];
        int v = 0;
        if (u.contains("dice")) { if (!small_int(u["dice"], 1, 255, v)) return err("scaling.upcast.dice invalid"); s.upcast_dice = uint8_t(v); }
        if (u.contains("darts")) { if (!small_int(u["darts"], 1, 255, v)) return err("scaling.upcast.darts invalid"); s.upcast_darts = uint8_t(v); }
        if (u.contains("targets")) { if (!small_int(u["targets"], 1, 255, v)) return err("scaling.upcast.targets invalid"); s.upcast_targets = uint8_t(v); }
    }
    return {std::move(s), {}};
}

SpellBookLoad load_spellbook(const std::filesystem::path& dir) {
    SpellBookLoad out;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    if (ec) { out.errors.push_back(dir.string() + ": " + ec.message()); return out; }
    std::sort(files.begin(), files.end());
    auto book = std::make_shared<SpellBook>();
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        Json j = Json::parse(in, nullptr, false);
        if (j.is_discarded()) { out.errors.push_back(f.string() + ": invalid JSON"); continue; }
        if (!j.is_object() || j.value("$schema", "") != "spell@1") continue;
        auto r = spell_from_json(j);
        if (!r.def) { out.errors.push_back(f.string() + ": " + r.error); continue; }
        if (!book->add(std::move(*r.def))) out.errors.push_back(f.string() + ": duplicate spell id (hash collision)");
    }
    if (out.errors.empty()) out.book = std::move(book);
    return out;
}

} // namespace rules5e_content
