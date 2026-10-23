#!/usr/bin/env python3
"""Equipment proposal (docs/proposals/EQUIPMENT.md section 5) audit: can each SRD monster's printed AC, to-hit and
damage be reproduced from abilities + proficiency bonus by CR + item@1 gear / natural armor + explicit bonuses?

Read-only. Integer math only. Usage: python3 tools/audit/equipment_monster_audit.py [--pack DIR] [--markdown]
Exit code 0 always (it is a report, not a gate)."""
import argparse, glob, json, os, re, sys
from fractions import Fraction

PB_BY_CR = [(4, 2), (8, 3), (12, 4), (16, 5), (20, 6), (24, 7), (28, 8), (30, 9)]   # SRD 5.1 "Proficiency Bonus by Challenge Rating"

def cr_value(s):
    return Fraction(s)

def pb_for_cr(s):
    v = cr_value(s)
    for top, pb in PB_BY_CR:
        if v <= top:
            return pb
    return 9

def mod(score):
    return (score - 10) // 2          # floor division: 7 -> -2, 9 -> -1 (SRD "Ability Scores and Modifiers")

DICE = re.compile(r'^(\d+)d(\d+)([+-]\d+)?$')
def parse(d):
    m = DICE.match(d.replace(' ', ''))
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)

# monster action id -> item@1 id (suffixes _two_handed / _thrown select the versatile / thrown use of the same weapon)
ALIAS = {'light_crossbow': 'crossbow_light', 'hand_crossbow': 'crossbow_hand', 'heavy_crossbow': 'crossbow_heavy'}
# Monster-local weapons (section 5.4): stat-block weapons with no item@1 entry; dice authored on the monster.
LOCAL_WEAPONS = {'heavy_club', 'spiked_shield', 'spiked_bone_club'}
SIZE_MULT = {'tiny': 1, 'small': 1, 'medium': 1, 'large': 2, 'huge': 3, 'gargantuan': 4}
ARMOR = {'leather armor': 'leather_armor', 'studded leather': 'studded_leather_armor', 'studded leather armor': 'studded_leather_armor',
         'hide armor': 'hide_armor', 'chain shirt': 'chain_shirt', 'scale mail': 'scale_mail', 'breastplate': 'breastplate',
         'half plate': 'half_plate_armor', 'ring mail': 'ring_mail', 'chain mail': 'chain_mail', 'splint': 'splint_armor',
         'plate': 'plate_armor', 'padded armor': 'padded_armor'}

def load_items(pack):
    items = {}
    for f in glob.glob(os.path.join(pack, 'items', '*.json')):
        it = json.load(open(f))
        items[it['id'].split('.')[-1]] = it
    return items

def audit_ac(m, items):
    ab = m['abilities']; dex = mod(ab['dex'])
    printed = m['armor_class']['value']
    note = (m['armor_class'].get('note') or '').lower()
    parts = [p.strip() for p in note.split(',') if p.strip()]
    shield = 'shield' in parts
    parts = [p for p in parts if p != 'shield']
    sb = 2 if shield else 0
    if not parts:
        derived = 10 + dex + sb
        src = 'unarmored 10 + Dex' + (' + shield' if shield else '')
        return derived, printed, src, None
    if len(parts) == 1 and parts[0] == 'natural armor':
        base = printed - dex - sb
        src = f'natural_armor.base {base} + Dex' + (' + shield' if shield else '')
        return base + dex + sb, printed, src, None
    if len(parts) == 1 and parts[0] in ARMOR:
        a = items[ARMOR[parts[0]]]
        dt = 0
        if a.get('dex_bonus'):
            dt = min(dex, a['dex_cap']) if 'dex_cap' in a else dex
        derived = a['base_ac'] + dt + sb
        src = f"{ARMOR[parts[0]]} ({a['base_ac']}" + (f" + Dex" + (f" max {a['dex_cap']}" if 'dex_cap' in a else '') if a.get('dex_bonus') else '') + ')' + (' + shield' if shield else '')
        return derived, printed, src, None
    if len(parts) == 1 and parts[0] == 'armor scraps':
        return 10 + dex + sb, printed, 'unarmored 10 + Dex + ac_adjust', 'armor scraps: no item@1 armor (SRD prints 13 = 10 + Dex 2 + 1)'
    # unknown armor note (armor scraps, patchwork, ...): no item; unarmored formula plus an explicit ac_adjust
    derived = 10 + dex + sb
    return derived, printed, f'unarmored 10 + Dex' + (' + shield' if shield else ''), f'"{note}" has no item@1 armor'

def audit_attack(m, a, items, pb):
    ab = m['abilities']; s, d = mod(ab['str']), mod(ab['dex'])
    if not a.get('damage'):
        return None
    d0 = a['damage'][0].get('dice')
    p = parse(d0) if d0 else None
    if p is None:
        return None
    n, die, k = p
    aid = a['id']
    use = 'normal'
    for suf, u in (('_two_handed', 'versatile'), ('_thrown', 'thrown')):
        if aid.endswith(suf):
            aid, use = aid[:-len(suf)], u
    aid = ALIAS.get(aid, aid)
    it = None if aid in LOCAL_WEAPONS else items.get(aid)
    ranged_kind = a['kind'].startswith('ranged')
    notes = []
    if it is not None and it.get('category') == 'weapon':
        props = it.get('properties', [])
        wd = (it['versatile_damage'] if use == 'versatile' else it['damage'])[0]
        wp = parse(wd['dice']) if 'dice' in wd else (0, 0, wd['flat'])
        if 'finesse' in props:
            abil, am = ('str', s) if s >= d else ('dex', d)
        elif 'range' in it and 'reach' not in it:      # ranged weapon
            abil, am = 'dex', d
        else:                                           # melee, incl. thrown melee weapons (Str)
            abil, am = 'str', s
        to_hit = pb + am
        dice_die = wp[1]
        extra_die = 0
        mult = SIZE_MULT[m['size']] if a['to_hit'] is not None and n == wp[0] * SIZE_MULT[m['size']] and SIZE_MULT[m['size']] > 1 else 1
        if mult > 1:
            notes.append(f'weapon_dice_multiplier {mult} ({m["size"]})')
        dice_n = wp[0] * mult
        if any(t['id'] == 'brute' for t in m.get('traits', [])) and not ranged_kind:
            extra_die = 1                               # Bugbear "Brute": one extra die of the weapon in melee
            notes.append('trait brute: +1 weapon die in melee')
        src = f'item {aid}' + (f' ({use})' if use != 'normal' else '') + f', {abil}'
        dmg_ok = (n == dice_n + extra_die and die == dice_die)
        adj = {}
        if to_hit != a['to_hit']:
            adj['attack_bonus'] = a['to_hit'] - to_hit
        if not dmg_ok:
            adj['damage_dice'] = f'{n}d{die} printed vs {dice_n + extra_die}d{dice_die} derived'
        if k != am:
            adj['damage_adjust'] = k - am
        return src, a['to_hit'], to_hit, d0, adj, notes
    # natural or monster-local weapon: dice and ability authored on the action (5.4). The ability is whichever of Str / Dex
    # reproduces the to-hit; damage adds it unless the stat block prints none (damage_ability "none", e.g. Pseudopod 3d6).
    kind = 'local' if aid in LOCAL_WEAPONS else 'natural'
    for abil, am in (('str', s), ('dex', d)):
        if pb + am == a['to_hit'] and k == am:
            return f'{kind}, {abil}', a['to_hit'], pb + am, d0, {}, notes
    for abil, am in (('str', s), ('dex', d)):
        if pb + am == a['to_hit'] and k == 0:
            return f'{kind}, {abil}, damage_ability none', a['to_hit'], pb + am, d0, {}, notes
    for abil, am in (('str', s), ('dex', d)):
        if k == am:   # damage fits the ability, to-hit does not: explicit attack_bonus override with a reason
            return f'{kind}, {abil}', a['to_hit'], pb + am, d0, {'attack_bonus': a['to_hit'] - (pb + am)}, notes
    return f'{kind}, str', a['to_hit'], pb + s, d0, {'attack_bonus': a['to_hit'] - (pb + s), 'damage_adjust': k - s}, notes

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', default=os.path.join(os.path.dirname(__file__), '..', '..', 'content', 'packs', 'srd51.core'))
    ap.add_argument('--markdown', action='store_true')
    args = ap.parse_args()
    items = load_items(args.pack)
    rows = []; clean = 0; total = 0; adj_monsters = 0; n_adj = 0
    for f in sorted(glob.glob(os.path.join(args.pack, 'monsters', '*.json'))):
        m = json.load(open(f)); total += 1
        pb = pb_for_cr(m['challenge']['rating'])
        issues = []
        if 'proficiency_bonus' in m and m['proficiency_bonus'] != pb:
            issues.append(f"proficiency_bonus {m['proficiency_bonus']} != CR table {pb}")
        dac, pac, src, why = audit_ac(m, items)
        if dac != pac:
            issues.append(f'ac_adjust {pac - dac:+d} ({why or "derived " + str(dac)})')
        att = []
        for a in m.get('actions', []):
            if 'to_hit' not in a:
                continue
            r = audit_attack(m, a, items, pb)
            if r is None:
                continue
            asrc, pt, dt, d0, adj, notes = r
            att.append(f"{a['id']}: {asrc}")
            for k2, v in adj.items():
                if k2 == 'attack_bonus':   # explicit override of the total to-hit, with a reason (5.5)
                    issues.append(f"{a['id']} attack_bonus override {pt:+d} (derived {dt:+d})")
                else:
                    issues.append(f"{a['id']} {k2} {v:+d}" if isinstance(v, int) else f"{a['id']} {k2}: {v}")
            for nt in notes:
                att[-1] += f' [{nt}]'
        ok = not issues
        clean += ok
        if not ok:
            adj_monsters += 1; n_adj += len(issues)
        rows.append((m['id'].split('.')[-1], m['challenge']['rating'], pb, pac, src, '; '.join(att), 'clean' if ok else '; '.join(issues)))
    if args.markdown:
        print('| Monster | CR | PB | AC | AC derivation | Attacks (source, ability) | Result |')
        print('|---|---|---|---|---|---|---|')
        for r in rows:
            print('| ' + ' | '.join(str(x) for x in r) + ' |')
    else:
        for r in rows:
            print('\t'.join(str(x) for x in r))
    print(f'\n{total} monsters: {clean} reproduce cleanly, {adj_monsters} need explicit adjustments ({n_adj} adjustment fields)')

if __name__ == '__main__':
    main()
