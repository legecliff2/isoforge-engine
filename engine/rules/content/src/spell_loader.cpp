#include "rules5e_content/spell_loader.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>

#include "assets/content_migrations.hpp"
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

const std::map<std::string, CreatureType> kCreatureType{
    {"aberration", CreatureType::Aberration}, {"beast", CreatureType::Beast}, {"celestial", CreatureType::Celestial},
    {"construct", CreatureType::Construct}, {"dragon", CreatureType::Dragon}, {"elemental", CreatureType::Elemental},
    {"fey", CreatureType::Fey}, {"fiend", CreatureType::Fiend}, {"giant", CreatureType::Giant},
    {"humanoid", CreatureType::Humanoid}, {"monstrosity", CreatureType::Monstrosity}, {"ooze", CreatureType::Ooze},
    {"plant", CreatureType::Plant}, {"undead", CreatureType::Undead}};

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
    // Mirrors schemas/spell/v1.schema.json plus tools/validate check_spell (same rules, same wording).
    if (!j.is_object() || j.value("$schema", "") != "spell@1") return err("not a spell@1 document");
    SpellDef s;
    std::vector<std::pair<std::string, std::string>> refs;
    if (!j.contains("id") || !j["id"].is_string()) return err("id missing");
    s.content_id = j["id"].get<std::string>();
    if (s.content_id.find(".spell.") == std::string::npos) return err("id must be '<ns>.spell.<name>'");
    s.id = spell_id_of(s.content_id);
    int lvl = 0;
    if (!j.contains("level") || !small_int(j["level"], 0, 9, lvl)) return err("level must be 0..9");
    s.level = uint8_t(lvl);
    s.ritual = j.value("ritual", false);

    const Json ct = j.value("casting_time", Json::object());
    const std::string ck = ct.value("kind", "");
    const bool has_trigger = ct.contains("trigger");
    if (ck == "action") s.casting_time = CastingTime::Action;
    else if (ck == "bonus_action") s.casting_time = CastingTime::BonusAction;
    else if (ck == "reaction") s.casting_time = CastingTime::Reaction;
    else if (ck == "minute" || ck == "hour") {
        s.casting_time = ck == "minute" ? CastingTime::Minute : CastingTime::Hour;
        int a = 0;
        if (!ct.contains("amount") || !small_int(ct["amount"], 1, 255, a)) return err("casting_time.amount required for minute/hour");
        s.casting_amount = uint8_t(a);
    } else return err("casting_time.kind invalid");
    if ((s.casting_time == CastingTime::Reaction) != has_trigger) return err("casting_time.trigger is required for (and only for) reactions");
    if (has_trigger) {
        const Json& tr = ct["trigger"];
        const std::string tk = tr.value("kind", "");
        if (tk == "being_hit") s.reaction_trigger = ReactionTrigger::BeingHit;
        else if (tk == "spell_cast") s.reaction_trigger = ReactionTrigger::SpellCast;
        else return err("casting_time.trigger.kind must be being_hit or spell_cast");
        if (tr.contains("or_targeted_by")) {
            if (tk != "being_hit") return err("casting_time.trigger.or_targeted_by is only for being_hit");
            for (const Json& r : tr["or_targeted_by"]) {
                if (!r.is_string() || r.get<std::string>().find(".spell.") == std::string::npos)
                    return err("casting_time.trigger.or_targeted_by entries must be spell ids");
                s.or_targeted_by.push_back(spell_id_of(r.get<std::string>()));
                refs.emplace_back("casting_time.trigger.or_targeted_by", r.get<std::string>());
            }
        }
        s.requires_sight = tr.value("requires_sight", false);
    }

    // range: self = 0 ft, touch = 5 ft (adjacent; sim checks reach), ranged = feet (>= 5).
    const Json rg = j.value("range", Json::object());
    const std::string rk = rg.value("kind", "");
    int ft = 0;
    if (rk == "self" && !rg.contains("feet")) s.range_ft = 0;
    else if (rk == "touch" && !rg.contains("feet")) s.range_ft = 5;
    else if (rk == "ranged" && rg.contains("feet") && small_int(rg["feet"], 5, 65535, ft) && ft % 5 == 0) s.range_ft = uint16_t(ft);
    else return err("range invalid (ranged needs feet >= 5, a multiple of 5)");

    if (j.contains("area")) {
        const Json& a = j["area"];
        static const std::map<std::string, AreaShape> kShape{{"cone", AreaShape::Cone}, {"cube", AreaShape::Cube},
            {"cylinder", AreaShape::Cylinder}, {"line", AreaShape::Line}, {"sphere", AreaShape::Sphere}};
        if (!lookup(kShape, a.value("shape", Json()), s.area)) return err("area.shape invalid");
        int v = 0;
        if (!a.contains("size_ft") || !small_int(a["size_ft"], 5, 65535, v)) return err("area.size_ft invalid");
        s.area_size_ft = uint16_t(v);
        if ((s.area == AreaShape::Line) != a.contains("width_ft")) return err("area.width_ft is required for (and only for) lines");
        if ((s.area == AreaShape::Cylinder) != a.contains("height_ft")) return err("area.height_ft is required for (and only for) cylinders");
        if (a.contains("width_ft")) { if (!small_int(a["width_ft"], 5, 65535, v)) return err("area.width_ft invalid"); s.area_width_ft = uint16_t(v); }
        if (a.contains("height_ft")) { if (!small_int(a["height_ft"], 5, 65535, v)) return err("area.height_ft invalid"); s.area_height_ft = uint16_t(v); }
    }

    const Json c = j.value("components", Json::object());
    if (c.value("v", false)) s.components |= kComponentV;
    if (c.value("s", false)) s.components |= kComponentS;
    if (c.contains("m")) {
        const Json& m = c["m"];
        if (!m.is_object() || !m.contains("text")) return err("components.m needs text");
        s.components |= kComponentM;
        int cost = 0;
        if (m.contains("cost_cp")) { if (!small_int(m["cost_cp"], 1, 2147483647, cost)) return err("components.m.cost_cp invalid"); s.material_cost_cp = uint32_t(cost); }
        s.material_consumed = m.value("consumed", false);
        if (m.contains("consumed") && !m.contains("cost_cp")) return err("'cost_cp' is a dependency of 'consumed'");
    }
    if (s.components == 0) return err("components: at least one of v, s, m");

    const Json du = j.value("duration", Json::object());
    static const std::map<std::string, SpellDuration> kDur{{"instantaneous", SpellDuration::Instantaneous},
        {"rounds", SpellDuration::Rounds}, {"minutes", SpellDuration::Minutes}, {"hours", SpellDuration::Hours},
        {"until_start_of_caster_turn", SpellDuration::UntilStartOfCasterTurn},
        {"until_dispelled", SpellDuration::UntilDispelled}, {"special", SpellDuration::Special}};
    if (!lookup(kDur, du.value("kind", Json()), s.duration)) return err("duration.kind invalid");
    const bool timed = s.duration == SpellDuration::Rounds || s.duration == SpellDuration::Minutes || s.duration == SpellDuration::Hours;
    int amt = 0;
    if (timed != du.contains("amount")) return err("duration.amount is required for (and only for) rounds/minutes/hours");
    if (timed) { if (!small_int(du["amount"], 1, 65535, amt)) return err("duration.amount invalid"); s.duration_amount = uint16_t(amt); }
    s.concentration = du.value("concentration", false);
    if (du.contains("concentration") && (s.duration == SpellDuration::Instantaneous || s.duration == SpellDuration::UntilStartOfCasterTurn))
        return err("duration.concentration needs a lasting duration");

    // targets: omitted = one creature (Save/Heal), or per dart/beam, or self.
    const bool has_targets = j.contains("targets");
    if (has_targets) {
        const Json& t = j["targets"];
        int n = 0;
        if (!t.contains("count") || !small_int(t["count"], 1, 255, n)) return err("targets.count invalid");
        s.targets = uint8_t(n);
        if (t.contains("creature_type")) {
            for (const Json& ty : t["creature_type"]) {
                CreatureType ct2;
                if (!lookup(kCreatureType, ty, ct2)) return err("targets.creature_type invalid");
                s.target_types |= bit(ct2);
            }
        }
        if (t.contains("max_spread_ft")) {
            int v = 0;
            if (!small_int(t["max_spread_ft"], 5, 65535, v)) return err("targets.max_spread_ft invalid");
            s.max_spread_ft = uint16_t(v);
        }
    }

    if (!j.contains("effects") || !j["effects"].is_array() || j["effects"].empty()) return err("effects missing");
    // One effect object -> the per-effect fields of `t` (kind, damage, save, condition, ...). Returns "" or an error.
    auto parse_effect = [&](const Json& e, SpellDef& s, std::string& kind) -> std::string {
        kind = e.is_object() ? e.value("kind", "") : "";
        if (kind == "spell_attack") {
            s.kind = SpellEffectKind::SpellAttack;
            const std::string atk = e.value("attack", "");
            if (atk != "melee" && atk != "ranged") return ("effect attack: '" + atk + "' is not one of ['melee', 'ranged']");
            s.melee = atk == "melee";
            if (!e.contains("damage") || !read_damage(e["damage"], s)) return ("effect damage invalid");
            int b = 1;
            if (e.contains("beams") && !small_int(e["beams"], 1, 255, b)) return ("effect beams invalid");
            s.beams = uint8_t(b);
        } else if (kind == "save") {
            s.kind = SpellEffectKind::Save;
            if (!e.contains("ability") || !lookup(kAbility, e["ability"], s.save_ability)) return ("effect ability invalid");
            if (e.contains("damage") != e.contains("on_success"))
                return (e.contains("damage") ? "'on_success' is a dependency of 'damage'" : "effect on_success needs damage ('damage' is a dependency of 'on_success')");
            if (e.contains("damage") && !read_damage(e["damage"], s)) return ("effect damage invalid");
            if (e.contains("on_success")) {
                const std::string os = e.value("on_success", "");
                if (os != "half" && os != "none") return ("effect on_success must be half or none");
                s.on_save = os == "half" ? OnSave::Half : OnSave::None;
            }
            if (e.contains("condition")) {
                if (!lookup(kCondition, e["condition"], s.condition)) return ("effect condition invalid");
                s.applies_condition = true;
            }
            if (e.contains("repeat_save")) {
                if (!s.applies_condition) return ("effect repeat_save needs condition ('condition' is a dependency of 'repeat_save')");
                static const std::map<std::string, RepeatSave> kRep{{"start_of_turn", RepeatSave::StartOfTurn},
                    {"end_of_turn", RepeatSave::EndOfTurn}, {"on_damage", RepeatSave::OnDamage}};
                if (!lookup(kRep, e["repeat_save"], s.repeat_save)) return ("effect repeat_save invalid");
            }
            if (!e.contains("damage") && !s.applies_condition) return ("save effect needs damage or condition");
        } else if (kind == "heal") {
            s.kind = SpellEffectKind::Heal;
            auto d = e.contains("dice") && e["dice"].is_string() ? parse_dice(e["dice"].get<std::string>()) : std::nullopt;
            if (!d) return ("effect dice invalid");
            s.dice = *d;
            s.add_spellcasting_modifier = e.value("add_spellcasting_modifier", false);
            if (e.contains("excludes_creature_type")) {
                for (const Json& ty : e["excludes_creature_type"]) {
                    CreatureType ct2;
                    if (!lookup(kCreatureType, ty, ct2)) return ("effect excludes_creature_type invalid");
                    s.excluded_types |= bit(ct2);
                }
            }
        } else if (kind == "auto_hit") {
            s.kind = SpellEffectKind::AutoHit;
            int darts = 0;
            if (!e.contains("darts") || !small_int(e["darts"], 1, 255, darts)) return ("effect darts invalid");
            s.darts = uint8_t(darts);
            if (!e.contains("damage") || !read_damage(e["damage"], s)) return ("effect damage invalid");
            static const std::map<std::string, DartDistribution> kDist{{"any", DartDistribution::Any},
                {"single", DartDistribution::Single}, {"distinct", DartDistribution::Distinct}};
            if (!lookup(kDist, e.value("distribution", Json()), s.distribution)) return ("effect distribution must be any, single or distinct");
            if (!e.contains("simultaneous") || !e["simultaneous"].is_boolean()) return ("effect simultaneous required");
            s.simultaneous = e["simultaneous"].get<bool>();
            s.targets = 0;   // per dart (distribution), not a creature count
        } else if (kind == "ac_bonus") {
            s.kind = SpellEffectKind::AcBonus;
            int b = 0;
            if (!e.contains("bonus") || !small_int(e["bonus"], 1, 20, b)) return ("effect bonus invalid");
            s.ac_bonus = int8_t(b);
            if (e.contains("blocks_spell")) {
                if (!e["blocks_spell"].is_string() || e["blocks_spell"].get<std::string>().find(".spell.") == std::string::npos)
                    return ("effect blocks_spell must be a spell id");
                s.blocks_spell = spell_id_of(e["blocks_spell"].get<std::string>());
                refs.emplace_back("effect blocks_spell", e["blocks_spell"].get<std::string>());
            }
        } else if (kind == "counter") {
            s.kind = SpellEffectKind::Counter;
            if (e.value("auto_max_level", "") != "slot") return ("effect auto_max_level must be slot");
            const Json chk = e.value("check", Json::object());
            int b = 0;
            if (chk.value("ability", "") != "spellcasting" || chk.value("plus", "") != "spell_level" ||
                !chk.contains("dc_base") || !small_int(chk["dc_base"], 1, 99, b))
                return ("effect check must be {ability: spellcasting, dc_base, plus: spell_level}");
            s.counter_dc_base = uint8_t(b);
        } else if (kind == "apply_condition") {
            s.kind = SpellEffectKind::ApplyCondition;
            if (!e.contains("condition") || !lookup(kCondition, e["condition"], s.condition)) return ("effect condition invalid");
            s.applies_condition = true;
        } else return ("effect kind invalid");
        return "";
    };
    if (!j["effects"].is_array()) return err("effects missing");
    std::string kind;
    if (auto m = parse_effect(j["effects"][0], s, kind); !m.empty()) return err(m);
    // Effects 2..n: parsed into a scratch SpellDef, then copied into SpellEffect. Reaction kinds stand alone.
    const std::size_t n_effects = j["effects"].size();
    for (std::size_t i = 1; i < n_effects; ++i) {
        SpellDef x;
        std::string xk;
        if (auto m = parse_effect(j["effects"][i], x, xk); !m.empty()) return err("effects[" + std::to_string(i) + "]: " + m);
        if (x.kind == SpellEffectKind::AcBonus || x.kind == SpellEffectKind::Counter)
            return err("effects: ac_bonus and counter reactions cannot be combined with other effects");
        SpellEffect fx;
        fx.kind = x.kind; fx.melee = x.melee; fx.beams = x.beams; fx.dice = x.dice; fx.damage_type = x.damage_type;
        fx.add_spellcasting_modifier = x.add_spellcasting_modifier; fx.excluded_types = x.excluded_types;
        fx.save_ability = x.save_ability; fx.on_save = x.on_save; fx.applies_condition = x.applies_condition;
        fx.condition = x.condition; fx.repeat_save = x.repeat_save; fx.darts = x.darts;
        fx.distribution = x.distribution; fx.simultaneous = x.simultaneous;
        fx.affected_only = j["effects"][i].value("applies_to", "all") == "affected";
        s.extra_effects.push_back(fx);
    }
    if (n_effects > 1 && (s.kind == SpellEffectKind::AcBonus || s.kind == SpellEffectKind::Counter))
        return err("effects: ac_bonus and counter reactions cannot be combined with other effects");
    if (j["effects"][0].value("applies_to", "all") != "all")
        return err("effects[0].applies_to needs a previous effect");
    const bool reaction_kind = s.kind == SpellEffectKind::AcBonus || s.kind == SpellEffectKind::Counter;
    if (reaction_kind != (s.casting_time == CastingTime::Reaction))
        return err("effect kind " + kind + " and casting_time.kind " + ck + " disagree (ac_bonus/counter are reactions, and only they)");
    if (reaction_kind && s.reaction_trigger != (s.kind == SpellEffectKind::AcBonus ? ReactionTrigger::BeingHit : ReactionTrigger::SpellCast))
        return err("effect kind " + kind + " needs casting_time.trigger.kind " + (s.kind == SpellEffectKind::AcBonus ? "being_hit" : "spell_cast"));
    if ((s.kind == SpellEffectKind::AcBonus) != (s.duration == SpellDuration::UntilStartOfCasterTurn))
        return err("ac_bonus effects last until_start_of_caster_turn (and only they use it)");
    if (has_targets && s.kind != SpellEffectKind::Save && s.kind != SpellEffectKind::Heal && s.kind != SpellEffectKind::ApplyCondition)
        return err("targets not allowed on a " + kind + " effect (one target, per dart/beam, or self)");

    const Json sc = j.value("scaling", Json::object());
    if (sc.contains("cantrip")) {
        if (s.level != 0) return err("scaling.cantrip is only for cantrips (level 0)");
        const Json lv = sc["cantrip"].value("levels", Json::array());
        if (!lv.is_array() || lv.empty() || lv.size() > 3) return err("scaling.cantrip.levels invalid");
        s.cantrip_tiers = {0, 0, 0};
        for (std::size_t i = 0; i < lv.size(); ++i) {
            int t = 0;
            if (!small_int(lv[i], 2, 20, t)) return err("scaling.cantrip.levels invalid");
            if (i > 0 && t <= s.cantrip_tiers[i - 1]) return err("scaling.cantrip.levels must be ascending");
            s.cantrip_tiers[i] = uint8_t(t);
        }
        const std::string adds = sc["cantrip"].value("adds", "");
        if (adds != "dice" && adds != "beams") return err("scaling.cantrip.adds must be dice or beams");
        if (adds == "beams" && s.kind != SpellEffectKind::SpellAttack) return err("scaling.cantrip.adds beams needs a spell_attack effect");
        s.cantrip_adds_beams = adds == "beams";
        s.cantrip_scales = true;
    }
    if (sc.contains("upcast")) {
        if (s.level == 0) return err("scaling.upcast is only for leveled spells");
        const Json& u = sc["upcast"];
        auto allowed = [&](const char* key) {
            const std::string k = key;
            if (k == "dice") return s.kind == SpellEffectKind::SpellAttack || s.kind == SpellEffectKind::Save ||
                                    s.kind == SpellEffectKind::Heal || s.kind == SpellEffectKind::AutoHit;
            if (k == "darts") return s.kind == SpellEffectKind::AutoHit;
            if (k == "beams") return s.kind == SpellEffectKind::SpellAttack;
            return s.kind == SpellEffectKind::Save || s.kind == SpellEffectKind::Heal || s.kind == SpellEffectKind::ApplyCondition;   // targets
        };
        for (const char* key : {"beams", "darts", "dice", "targets"}) {   // sorted, like the validator
            if (!u.contains(key)) continue;
            if (!allowed(key)) return err(std::string("scaling.upcast.") + key + " not allowed on a " + kind + " effect");
            int v = 0;
            if (!small_int(u[key], 1, 255, v)) return err(std::string("scaling.upcast.") + key + " invalid");
            const std::string k = key;
            if (k == "dice") {
                if (s.dice.count == 0) return err("scaling.upcast.dice needs effect dice or damage");
                s.upcast_dice = uint8_t(v);
            } else if (k == "darts") s.upcast_darts = uint8_t(v);
            else if (k == "beams") s.upcast_beams = uint8_t(v);
            else s.upcast_targets = uint8_t(v);
        }
    }
    return {std::move(s), {}, std::move(refs)};
}

std::string collision_error(const SpellBook& book, const SpellDef& d) {
    const SpellDef* other = book.find(d.id);
    char hex[11];
    std::snprintf(hex, sizeof hex, "0x%08x", unsigned(d.id));
    return "spell id collision: '" + d.content_id + "' and '" + (other ? other->content_id : std::string("?")) +
           "' both hash to " + hex + " (rename one)";
}

bool add_spell(SpellBook& book, SpellDef d, std::string& error) {
    if (const SpellDef* other = book.find(d.id)) {
        error = other->content_id == d.content_id ? "duplicate spell id '" + d.content_id + "'" : collision_error(book, d);
        return false;
    }
    book.add(std::move(d));
    return true;
}

SpellBookLoad load_spellbook(const std::filesystem::path& dir) {
    SpellBookLoad out;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    if (ec) { out.errors.push_back(dir.string() + ": " + ec.message()); return out; }
    std::sort(files.begin(), files.end());
    eng::assets::SchemaRegistry reg;
    eng::assets::register_content_schemas(reg);
    auto book = std::make_shared<SpellBook>();
    std::vector<std::pair<std::filesystem::path, std::pair<std::string, std::string>>> refs;
    for (const auto& f : files) {
        auto doc = reg.load_file(f);   // same pipeline as packs: $schema tag, floats rejected, migrations
        if (!doc.ok) {
            if (doc.declared.type == "spell") out.errors.push_back(f.string() + ": " + doc.error);
            continue;
        }
        if (doc.current.type != "spell") continue;
        auto r = spell_from_json(doc.data);
        if (!r.def) { out.errors.push_back(f.string() + ": " + r.error); continue; }
        for (auto& ref : r.refs) refs.emplace_back(f, std::move(ref));
        std::string e;
        if (!add_spell(*book, std::move(*r.def), e)) out.errors.push_back(f.string() + ": " + e);
    }
    // References must resolve to a loaded spell (same rule as the validator).
    for (const auto& [f, ref] : refs) {
        const SpellDef* d = book->find(spell_id_of(ref.second));
        if (!d || d->content_id != ref.second)
            out.errors.push_back(f.string() + ": " + ref.first + " '" + ref.second + "' is not a spell@1 in this pack or its depends");
    }
    if (out.errors.empty()) out.book = std::move(book);   // any error (including a collision) is fatal
    return out;
}

} // namespace rules5e_content
