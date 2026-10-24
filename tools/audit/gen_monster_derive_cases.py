#!/usr/bin/env python3
"""Emit rules5e::MonsterSpec cases (C++) from srd51.core monster@5 documents with derive: true (T&C #253) for the
derive_monster tests (derive_monster_tests.cpp asserts 0 mismatches). Mirrors checks_v4.check_monster_derive's inputs.
Usage: gen_monster_derive_cases.py <pack dir> > engine/rules/tests/derive_monster_srd_cases.inc"""
import glob, json, os, re, sys

ABIL = {"str": "Str", "dex": "Dex", "con": "Con", "int": "Int", "wis": "Wis", "cha": "Cha"}
CAT = {"light": "Light", "medium": "Medium", "heavy": "Heavy"}

def dice(d):
    if not isinstance(d, dict): return (0, 0, 0)
    if "dice" in d:
        m = re.fullmatch(r"(\d+)d(\d+)([+-]\d+)?", d["dice"].replace(" ", ""))
        if m: return (int(m[1]), int(m[2]), int(m[3] or 0))
        return (0, 0, int(d["dice"]))
    return (0, 0, int(d.get("flat", 0)))

def q(s): return json.dumps(s)
def b(x): return "true" if x else "false"
def de(t): return f"DiceExpr{{{t[0]}, {t[1]}, {t[2]}}}"

def weapon_lines(src, natural=None, act=None):
    if natural is not None:   # checks_v4: natural weapon from the action
        rng = act.get("range") or [0, 0]
        return [f"    a.weapon.natural = true; a.weapon.reach_ft = {act.get('reach', 0 if act.get('range') else 5)};",
                f"    a.weapon.range_ft = {rng[0]}; a.weapon.long_range_ft = {rng[1] if len(rng) > 1 else 0};"
                f" a.weapon.dice = {de(dice(natural))};"]
    rng = src.get("range") or [0, 0]
    props = " | ".join(f"prop({q(p)})" for p in src.get("properties") or []) or "0"
    L = [f"    a.weapon.properties = uint16_t({props}); a.weapon.reach_ft = {src.get('reach', 0 if 'range' in src else 5)};",
         f"    a.weapon.range_ft = {rng[0]}; a.weapon.long_range_ft = {rng[1] if len(rng) > 1 else 0};"
         f" a.weapon.dice = {de(dice((src.get('damage') or [None])[0]))};",
         f"    a.weapon.versatile_dice = {de(dice((src.get('versatile_damage') or [None])[0]))};"
         f" a.weapon.magic_bonus = {int(src.get('magic_bonus', 0))}; a.weapon.martial = {b(src.get('weapon_class') == 'martial')};"]
    if src.get("attack_magic") is not None: L.append(f"    a.weapon.attack_magic = {int(src['attack_magic'])};")
    if src.get("damage_magic") is not None: L.append(f"    a.weapon.damage_magic = {int(src['damage_magic'])};")
    if src.get("improvised"): L.append("    a.weapon.improvised = true;")
    return L

def held_slots(e, items):
    it = items.get(e.get("item"))
    if not isinstance(it, dict) or not e.get("equipped"): return 0
    cat = it.get("category")
    return 1 if cat in ("weapon", "shield") else int(it.get("hands", 0)) if cat == "gear" else 0

def main():
    pack = sys.argv[1]
    items = {}
    for f in glob.glob(os.path.join(pack, "items", "*.json")):
        d = json.load(open(f)); items[d["id"]] = d
    out, n_m, n_a = [], 0, 0
    for f in sorted(glob.glob(os.path.join(pack, "monsters", "*.json"))):
        d = json.load(open(f))
        if d.get("derive") is not True: continue
        n_m += 1
        ab = d["abilities"]; rating = str(d["challenge"]["rating"])
        num, den = (rating.split("/") + ["1"])[:2] if "/" in rating else (rating, "1")
        eq = [e for e in d.get("equipment") or [] if isinstance(e, dict)]
        L = [f"[] {{ MonsterSpec m; m.id = {q(d['id'])};",
             f"  m.abilities = scores({ab['str']}, {ab['dex']}, {ab['con']}, {ab['int']}, {ab['wis']}, {ab['cha']});"
             f" m.cr_num = {int(num)}; m.cr_den = {int(den)}; m.printed_ac = {d['armor_class']['value']};"]
        if "proficiency_bonus" in d:
            L.append(f"  m.has_pb = true; m.pb = {d['proficiency_bonus']}; m.pb_reason = {q(str(d.get('proficiency_bonus_reason', '')))};")
        if "hands" in d:
            L.append(f"  m.hands = {int(d['hands'])}; m.held = {sum(held_slots(e, items) for e in eq)};")
        armor = shield = None
        for e in eq:
            it = items.get(e.get("item")) if e.get("equipped") else None
            if isinstance(it, dict) and it.get("category") == "armor" and armor is None: armor = it
            elif isinstance(it, dict) and it.get("category") == "shield" and shield is None: shield = it
        if armor:
            L.append(f"  m.has_armor = true; m.armor = armor(ArmorCategory::{CAT[armor['armor_category']]}, {armor['base_ac']}, "
                     f"{b(armor.get('dex_bonus'))}, {armor.get('dex_cap', 0)}); m.armor.has_dex_cap = {b('dex_cap' in armor)};"
                     f" m.armor.magic_bonus = {armor.get('magic_bonus', 0)};")
        if shield:
            L.append(f"  m.has_shield = true; m.shield.ac_bonus = {shield.get('ac_bonus', 2)}; m.shield.magic_bonus = {shield.get('magic_bonus', 0)};")
        na = d.get("natural_armor")
        if na is not None:
            L.append(f"  m.has_natural_armor = true; m.natural_base = {na['base']}; m.natural_dex = {b(na.get('dex', True))};")
        adj = d.get("ac_adjust")
        if adj: L.append(f"  m.ac_adjust = Reasoned{{true, {int(adj['value'])}, {q(str(adj.get('reason') or ''))}}};")
        if "weapon_dice_multiplier" in d: L.append(f"  m.dice_multiplier = {int(d['weapon_dice_multiplier'])};")
        brute = sum(int(t["effect"].get("dice", 0)) for t in d.get("traits") or []
                    if isinstance(t, dict) and isinstance(t.get("effect"), dict) and t["effect"].get("kind") == "weapon_extra_dice")
        if brute: L.append(f"  m.brute_dice = {brute};")
        local = {w["id"]: w for w in d.get("weapons") or [] if isinstance(w, dict)}
        for a in d.get("actions") or []:
            if not isinstance(a, dict) or not ("weapon" in a or "natural" in a): continue
            if "natural" in a:
                wl = weapon_lines(None, a["natural"], a)
            else:
                ref = a["weapon"]
                src = local.get(ref[1:]) if ref.startswith("#") else items.get(ref)
                if not isinstance(src, dict): continue   # unresolved: reported by checks 6/11, skipped (as checks_v4)
                wl = weapon_lines(src)
            n_a += 1
            L.append(f"  {{ MonsterAttackSpec a; a.id = {q(a.get('id', ''))};")
            L += wl
            if "natural" in a:
                L.append(f"    a.ability = Ability::{ABIL[a['natural']['ability']]};"
                         f" a.damage_ability_none = {b(a['natural'].get('damage_ability') == 'none' or 'flat' in a['natural'])};")
            if "use" in a: L.append(f"    a.use = *weapon_use_from_string({q(a['use'])});")
            else: L.append("    a.has_use = false;")
            ab_ov = a.get("attack_bonus")
            if ab_ov: L.append(f"    a.attack_bonus = Reasoned{{true, {int(ab_ov['value'])}, {q(str(ab_ov.get('reason') or ''))}}};")
            dov = a.get("damage_override")
            if dov: L.append(f"    a.has_damage_override = true; a.damage_override = {de(dice({'dice': dov['dice']}))};"
                             f" a.damage_override_reason = {q(str(dov.get('reason') or ''))};")
            L.append(f"    a.printed_to_hit = {int(a.get('to_hit', 0))}; a.printed_damage = {de(dice((a.get('damage') or [None])[0]))};")
            L.append("    m.attacks.push_back(a); }")
        L.append("  return m; }(),")
        out.append("\n".join(L))
    print(f"// generated by tools/audit/gen_monster_derive_cases.py from srd51.core monster@5 derive: true (T&C #253): "
          f"{n_m} monsters, {n_a} attacks; do not edit")
    print("\n".join(out))

main()
