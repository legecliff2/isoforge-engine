"""Per-tag table and cross-reference checks for the @4 documents (monster@4, character@4, item@1).

docs/proposals/VERSION_BUMP_BATCH.md section 1.2 (per-tag table, "no ad hoc `if version >= 4` branches")
and section 8.3 (validator contract). Numbers in messages ("check N") are the numbers of the section 8.3
list. The checks assume the document already passed its JSON Schema (the caller runs them only then), so
shapes are trusted; every walker still tolerates absent optional keys.

Everything is integer math; documents are plain json.load results.
"""
from __future__ import annotations

import re
from dataclasses import dataclass

# ---- the per-tag table ----------------------------------------------------------------------------

V1_ACTION_KINDS = frozenset({"melee_weapon_attack", "ranged_weapon_attack", "melee_spell_attack",
                             "ranged_spell_attack", "special"})
V2_ACTION_KINDS = V1_ACTION_KINDS | {"save_effect", "attack_effect"}   # attack_effect: sim #170 (giant spider Web)
# Effect kinds with a registered engine hook at @3 (eng::sim::FeatureRegistry::builtin()): live at @3,
# carried unchanged by @4, never marked inert by the 3 -> 4 migration.
LIVE_AT_3_EFFECT_KINDS = frozenset({"damage_bonus", "bonus_action_grant", "self_heal", "damage_mitigation",
                                    "magic_weapons", "multiattack"})
# R-G7 static kinds (sim #114 features.cpp): registered with no minimum version ("open kinds", like the sim's R-G5
# kinds), so the sim applies them at every tag whose effects are active (@3 and @4; @1/@2 effects are inert). They are
# not in the frozen 3 -> 4 list, so the migration never stamps them. Shapes: common@1 $defs/effect.
OPEN_EFFECT_KINDS_RG7 = frozenset({"reroll_ones", "great_weapon_fighting", "evasion",
                                   # T&C-G7 (sim #127): SRD Pack Tactics; no fields, a duplicate adds nothing (warned)
                                   "pack_tactics",
                                   # T&C-G7 (sim #129): orc Aggressive, Reckless Attack / berserker Reckless; no fields
                                   "aggressive", "reckless_attack",
                                   # R-G14 (sim #143): Sorcerer Metamagic {options, pool}, Evocation Sculpt Spells {}
                                   "metamagic", "sculpt_spells",
                                   # 7.2n (sim #148): Arcane Recovery / Natural Recovery {class, max_slot_level, uses}
                                   "slot_recovery",
                                   # 7.2o (sim #152): Font of Magic {pool}, the sorcery-point pool without Metamagic
                                   "font_of_magic"})
# Open kinds whose second declaration in one document adds nothing (the sim dedupes): the validator warns.
NO_STACK_EFFECT_KINDS = frozenset({"pack_tactics", "aggressive", "reckless_attack", "sculpt_spells"})
# R-G14 (sim #143, features.cpp apply_metamagic kNames): the metamagic options, in rules5e kMeta* bit order.
METAMAGIC_OPTIONS = ("careful", "distant", "empowered", "extended", "heightened", "quickened", "subtle", "twinned")
# The kinds @4 adds; also the frozen literal list of the 3 -> 4 migration (VERSION_BUMP_BATCH 8.2 step 4).
# save_effect and reaction are action / reaction kinds, not trait effect kinds: they exist in the list so
# a stamped one validates (effect_inert) but a native one is a schema error.
NEW_EFFECT_KINDS_AT_4 = frozenset({"on_event", "apply_modifier", "grant_action", "heal_pool", "save_effect", "reaction"})
NATIVE_EFFECT_KINDS_AT_4 = frozenset({"on_event", "apply_modifier", "grant_action", "heal_pool"})
# R-G5 static kinds (sim #107: eng::sim::FeatureRegistry::builtin() hooks attack_bonus, crit_range, ac_bonus). Open
# common@1 kinds (no schema arm, not in the frozen 3 -> 4 list): known at @4, their fields are checked by
# check_static_kinds below, mirroring the sim's load errors (sim/src/features.cpp apply_attack_bonus / _crit_range / _ac_bonus).
STATIC_EFFECT_KINDS_R_G5 = frozenset({"attack_bonus", "crit_range", "ac_bonus"})
# VERSION_BUMP_BATCH 7.2p (sim #153 content.cpp compile_gaze): kinds the sim compiles itself, gated at @4 (a file that declares
# an older version lists them inert: unknown-kind warning there). Shape: the closed inline gaze arm of common@2 $defs/effect.
# Loaded at @4 since: wild_shape / shapechanger (EC #176, the common@2 keys of 7.2q; closed arms wild_shape_effect /
# shapechanger_effect), extra_attack (EC #179 features.cpp apply_extra_attack; closed arm extra_attack_effect; characters
# only). Below @4 they are inert in the sim (extra_attack: the hook's @4 gate, the frozen 3 -> 4 list cannot carry it), so a
# @1..@3 file gets the unknown-kind warning.
# transparent (7.2w, features.cpp apply_transparent; closed arm transparent_effect; monsters only) likewise.
COMPILED_EFFECT_KINDS_AT_4 = frozenset({"gaze", "wild_shape", "shapechanger", "extra_attack", "transparent", "bonus_attack",
                                       "grant_die", "half_proficiency",   # 7.2za: gated in their hooks
                                       "spell_mod",   # 7.2zb: gated in its hook
                                       "heal_bonus"})   # 7.2zc: gated in its hook
# compile_gaze defaults for the optional fields the validator warns about when omitted.
GAZE_DEFAULTS = {"condition": "restrained", "repeat_save": "end_of_turn", "escalate_to": "petrified"}
# 7.2n (sim #148, features.cpp apply_slot_recovery): the classes slot_recovery accepts.
SLOT_RECOVERY_CLASSES = frozenset({"wizard", "druid"})
EFFECT_COST_V1 = frozenset({"action", "bonus_action"})            # reaction is reserved up to @3
EFFECT_COST_V2 = frozenset({"action", "bonus_action", "reaction"})  # grant_action additionally accepts "free"

_SB_KEYS = frozenset({"$schema", "id", "name", "source", "size", "type", "tags", "alignment", "armor_class",
                      "hit_points", "speed", "abilities", "saving_throws", "skills", "damage_vulnerabilities",
                      "damage_resistances", "damage_immunities", "condition_immunities", "senses", "languages",
                      "proficiency_bonus", "traits", "actions", "bonus_actions", "reactions", "presentation"})
_MONSTER_KEYS = frozenset({"challenge", "behavior", "legendary_actions", "spellcasting"})
_CHARACTER_KEYS = frozenset({"race", "background", "level", "classes", "xp", "features", "equipment", "behavior",
                             "spellcasting"})
_AT4_KEYS = frozenset({"resources", "forward_compat", "inert_mechanics",
                       "no_breath", "shapechanger", "evil"})   # R-G11 creature facts (reserved, CONTENT_CONVENTIONS 4.6)


@dataclass(frozen=True)
class TagRow:
    """What the validator accepts for one schema tag. @<=3 rows are frozen; @>=4 rows come from the proposal."""
    active_effects: bool                 # engine applies feature effects (kind contract: errors, not warnings)
    action_kinds: frozenset              # allowed actions[] / bonus_actions[] kinds
    effect_kinds: frozenset              # kinds with a hook at this tag (others: unknown-kind warning)
    cost_values: frozenset               # allowed effect `cost` values (grant_action adds "free" at @4)
    top_level_keys: frozenset            # every top-level key the schema declares for the tag
    cross_refs: bool = False             # run the section 8.3 reference checks
    needs_api_level: int = 1             # manifest api_level the pack must declare (8.5)
    legacy_stamps: bool = False          # documents may carry migration stamps (inert) in memory (rejected on disk)


TAG_TABLE: dict[str, TagRow] = {}
_OPEN = LIVE_AT_3_EFFECT_KINDS | OPEN_EFFECT_KINDS_RG7   # open kinds join every row (inert at @1/@2: findings are warnings)
for _typ, _keys in (("monster", _SB_KEYS | _MONSTER_KEYS), ("character", _SB_KEYS | _CHARACTER_KEYS)):
    TAG_TABLE[f"{_typ}@1"] = TagRow(False, V1_ACTION_KINDS, _OPEN, EFFECT_COST_V1, _keys - {"spellcasting"})
    TAG_TABLE[f"{_typ}@2"] = TagRow(False, V1_ACTION_KINDS, _OPEN, EFFECT_COST_V1, _keys - {"spellcasting"})
    TAG_TABLE[f"{_typ}@3"] = TagRow(True, V1_ACTION_KINDS, _OPEN, EFFECT_COST_V1, _keys, cross_refs=False)
    TAG_TABLE[f"{_typ}@4"] = TagRow(True, V2_ACTION_KINDS, _OPEN | NATIVE_EFFECT_KINDS_AT_4 | STATIC_EFFECT_KINDS_R_G5
                                    | COMPILED_EFFECT_KINDS_AT_4,
                                    EFFECT_COST_V2, _keys | _AT4_KEYS, cross_refs=True, needs_api_level=2,
                                    legacy_stamps=True)
TAG_TABLE["item@1"] = TagRow(False, frozenset(), frozenset(), frozenset(), frozenset(), cross_refs=True,
                             needs_api_level=2)
# EQUIPMENT 7.1 (#245/#246): monster@5 / character@5 carry @4 in full plus optional equipment keys; item@2 carries item@1.
_MONSTER_AT5_KEYS = frozenset({"derive", "equipment", "natural_armor", "ac_adjust", "weapon_dice_multiplier", "weapons", "hands",
                               "proficiency_bonus_reason", "emits_light"})
# monster@5-local trait kinds (Rules decision on #247: no common@3): weapon_extra_dice (Brute), closed arm in monster@5 $defs.
MONSTER_AT5_EFFECT_KINDS = frozenset({"weapon_extra_dice"})
_CHARACTER_AT5_KEYS = frozenset({"derive", "weapon_proficiencies", "armor_proficiencies"})
for _typ, _extra in (("monster", _MONSTER_AT5_KEYS), ("character", _CHARACTER_AT5_KEYS)):
    _r4 = TAG_TABLE[f"{_typ}@4"]
    TAG_TABLE[f"{_typ}@5"] = TagRow(True, _r4.action_kinds,
                                    _r4.effect_kinds | (MONSTER_AT5_EFFECT_KINDS if _typ == "monster" else frozenset()),
                                    _r4.cost_values, _r4.top_level_keys | _extra,
                                    cross_refs=True, needs_api_level=2, legacy_stamps=True)
TAG_TABLE["item@2"] = TagRow(False, frozenset(), frozenset(), frozenset(), frozenset(), cross_refs=True,
                             needs_api_level=2)

FORWARD_COMPAT_KNOWN = frozenset({"elevation"})
# Reaction windows the sim compiles (content.cpp compile_reactions); every other window is listed as unsupported and
# skipped. roll_attack (R-G7, AttackRolled) was reserved by check 9 (Q6) until sim #114 compiled it.
COMPILED_WINDOWS = frozenset({"damaged", "ally_attacked", "roll_attack"})
U16_MAX = 65535


def tag_row(tag: object) -> TagRow | None:
    return TAG_TABLE.get(tag) if isinstance(tag, str) else None


def is_int(x) -> bool:
    """JSON Schema `integer` accepts 2.0; the content rules do not (and bool is not an int here)."""
    return isinstance(x, int) and not isinstance(x, bool)


# ---- walkers -------------------------------------------------------------------------------------

def _is_stamped(entry) -> bool:
    return isinstance(entry, dict) and entry.get("inert") is True


def mechanical_actions(doc: dict):
    """(path, action) for actions[] and the *mechanical* bonus_actions[] entries (not stamped, not special)."""
    for i, a in enumerate(doc.get("actions") or []):
        if isinstance(a, dict) and not _is_stamped(a):
            yield f"/actions/{i}", a
    for i, a in enumerate(doc.get("bonus_actions") or []):
        if isinstance(a, dict) and not _is_stamped(a) and a.get("kind") != "special":
            yield f"/bonus_actions/{i}", a


def mechanical_reactions(doc: dict):
    for i, r in enumerate(doc.get("reactions") or []):
        if isinstance(r, dict) and not _is_stamped(r) and "window" in r:
            yield f"/reactions/{i}", r


def live_effects(doc: dict):
    """(path, owner id, effect) for traits[] / features[] effects that are not stamped inert."""
    for key in ("traits", "features"):
        for i, t in enumerate(doc.get(key) or []):
            eff = t.get("effect") if isinstance(t, dict) else None
            if isinstance(eff, dict) and eff.get("inert") is not True:
                yield f"/{key}/{i}/effect", str(t.get("id")), eff


def rider_sites(doc: dict):
    """(path, owner id, index, rider): on_hit[] riders of mechanical attack actions (index = position in
    on_hit[]) and the effects[] riders of non-inert on_event trait effects (index 0, one trigger per trait)."""
    for path, a in mechanical_actions(doc):
        for j, r in enumerate(a.get("on_hit") or []):
            yield f"{path}/on_hit/{j}", str(a.get("id")), j, r
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") == "on_event":
            for j, r in enumerate(eff.get("effects") or []):
                yield f"{path}/effects/{j}", owner, 0, r


def usage_sites(doc: dict):
    """(path, usage) for every usage object in mechanical content."""
    for path, a in mechanical_actions(doc):
        if isinstance(a.get("usage"), dict):
            yield f"{path}/usage", a["usage"]
    for path, r in mechanical_reactions(doc):
        if isinstance(r.get("usage"), dict):
            yield f"{path}/usage", r["usage"]
    for path, _owner, eff in live_effects(doc):
        if isinstance(eff.get("usage"), dict):
            yield f"{path}/usage", eff["usage"]


def modifier_sites(doc: dict):
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") == "apply_modifier":
            if isinstance(eff.get("modifier"), dict):
                yield f"{path}/modifier", eff["modifier"]
            for j, m in enumerate(eff.get("modifiers") or []):
                yield f"{path}/modifiers/{j}", m


# ---- level tables (section 7.1, check 16) ----------------------------------------------------------

def check_table(where: str, table: dict, errs: list[str], pool: bool) -> None:
    levels = [e.get("level") for e in table.get("by_level", [])]
    if any(not is_int(v) for v in levels) or any(b <= a for a, b in zip(levels, levels[1:])):
        errs.append(f"{where}: by_level levels must be strictly ascending (check 16.1)")
    entries = table.get("by_level", [])
    for k, e in enumerate(entries):
        if pool and e.get("unlimited") is True and k != len(entries) - 1:
            errs.append(f"{where}/by_level/{k}: 'unlimited' is only allowed on the last entry (check 16.2)")
        for key in ("value",):
            if key in e and not is_int(e[key]):
                errs.append(f"{where}/by_level/{k}: {key} must be an integer token, not {e[key]!r} (check 16.3)")


def table_values(table: dict) -> tuple[int | None, int | None]:
    """(largest finite value, level-1 value) of a pool table; None when there is no finite value."""
    vals = [e["value"] for e in table.get("by_level", []) if "value" in e]
    first = table["by_level"][0].get("value") if table.get("by_level") else None
    return (max(vals) if vals else None), first


# ---- R-G7 reactions (sim #114, docs/CONTENT_CONVENTIONS.md section 3) ------------------------------------

def check_roll_attack_reaction(path: str, r: dict, errs: list[str], warns: list[str]) -> None:
    """Mirror of the sim's compile_reactions (content.cpp) for the roll_attack window and the subtract_roll effect,
    in the loader's order: windows it does not compile are skipped; a usage is compiled only on roll_attack +
    subtract_roll and only as kind pool (any other usage, source != self or visible false: the reaction is listed as
    unsupported and skipped); then the window / effect pairing and melee_only + ranged_only are load errors.
    Unsupported combinations of the R-G7 names are warnings (the file loads, the reaction never fires)."""
    w = r.get("window") or {}
    eff = r.get("effect") or {}
    wk, ek = w.get("kind"), eff.get("kind")
    rg7 = wk == "roll_attack" or ek == "subtract_roll"
    if wk not in COMPILED_WINDOWS:
        if rg7:
            warns.append(f"{path}: subtract_roll in window '{wk}' is not compiled (only the roll_attack window takes it); "
                         "the sim lists the reaction as unsupported (R-G7 reactions)")
        return
    rolled = wk == "roll_attack"
    u = r.get("usage")
    if rolled and ek == "subtract_roll" and u is not None:
        if not isinstance(u, dict) or u.get("kind") != "pool":
            warns.append(f"{path}/usage: a roll_attack reaction compiles only a 'pool' usage; kind {u.get('kind') if isinstance(u, dict) else u!r} "
                         "makes the sim list it as unsupported (R-G7 reactions)")
            return
        cost = u.get("cost", 1)
        if not is_int(cost) or not 1 <= cost <= U16_MAX:
            errs.append(f"{path}/usage: cost must be an integer 1..{U16_MAX}, got {cost!r} (sim: bad usage cost) (R-G7 reactions)")
        # the pool must be a resources[].id: check 2 (usage_sites covers reactions[])
    elif u is not None:
        if rg7:
            warns.append(f"{path}/usage: a usage is compiled only on a roll_attack subtract_roll reaction; the sim lists this "
                         "reaction as unsupported (R-G7 reactions)")
        return
    if w.get("source", "self") != "self" or w.get("visible", True) is not True:
        if rg7:
            warns.append(f"{path}/window: source ally_within_5ft / visible false are not compiled; the sim lists this reaction "
                         "as unsupported (R-G7 reactions)")
        return
    if rolled and w.get("melee_only") is True and w.get("ranged_only") is True:
        errs.append(f"{path}/window: melee_only and ranged_only together (R-G7 reactions)")
    if ek == "subtract_roll" and not rolled:
        errs.append(f"{path}: subtract_roll needs the roll_attack window (R-G7 reactions)")
    elif rolled and ek != "subtract_roll":
        errs.append(f"{path}: the roll_attack window only takes subtract_roll, not '{ek}' (R-G7 reactions)")


# ---- the @4 stat-block checks (monster@4 / character@4) -----------------------------------------------

def check_stat_block_v4(typ: str, doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    errs: list[str] = []
    warns: list[str] = []
    action_ids = {a.get("id") for a in doc.get("actions") or [] if isinstance(a, dict)}
    resources = [r for r in doc.get("resources") or [] if isinstance(r, dict)]
    res_by_id = {}
    for i, r in enumerate(resources):
        rid = r.get("id")
        if rid in res_by_id:
            errs.append(f"/resources/{i}: duplicate resource id '{rid}' (check 8)")
        res_by_id.setdefault(rid, r)
    referenced: set[str] = set()

    def need_attack(where: str, aid) -> None:
        if aid not in action_ids:
            errs.append(f"{where}: attack_id '{aid}' is not an id in this document's actions[] (check 1)")

    def need_pool(where: str, pool, cost) -> None:
        referenced.add(pool)
        r = res_by_id.get(pool)
        if r is None:
            errs.append(f"{where}: pool '{pool}' is not a resources[].id of this document (check 2)")
            return
        c = 1 if cost is None else cost
        mx = r.get("max")
        if is_int(mx) and c > mx:
            errs.append(f"{where}: cost {c} exceeds the pool's max {mx} (check 2)")
        elif isinstance(mx, dict) and isinstance(mx.get("table"), dict):
            top, first = table_values(mx["table"])
            if top is not None and c > top:
                errs.append(f"{where}: cost {c} exceeds the largest finite value {top} of the pool table (check 16.6)")
            elif first is not None and c > first:
                warns.append(f"{where}: cost {c} exceeds the level-1 value {first} of the pool table: "
                             "legal but unusable at low levels (check 16.6)")

    # check 1 + 17-adjacent: attack references
    for path, owner, j, r in rider_sites(doc):
        if r.get("kind") == "bonus_attack":
            need_attack(path, r.get("attack_id"))
        if r.get("kind") == "spend" and "pool" in r:
            need_pool(path, r["pool"], r.get("cost"))
    for path, r in mechanical_reactions(doc):
        eff = r.get("effect") or {}
        if eff.get("kind") == "attack":
            need_attack(f"{path}/effect", eff.get("attack_id"))
        # R-G7 (sim #114): roll_attack / subtract_roll are live (check 9's reservation is lifted)
        check_roll_attack_reaction(path, r, errs, warns)
    # check 2: pools
    for path, u in usage_sites(doc):
        if u.get("kind") == "pool":
            need_pool(path, u.get("pool"), u.get("cost"))
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") == "heal_pool" and "per_class_level" not in eff:   # 7.2zd split form: its usage pool (above)
            need_pool(path, eff.get("pool"), None)
        if eff.get("kind") in ("metamagic", "font_of_magic"):
            referenced.add(eff.get("pool"))   # sorcery points: spent by the sim per option (check 12); resolved by check_metamagic (7.2o: font_of_magic resolves it)
        if eff.get("kind") == "wild_shape":
            referenced.add(eff.get("pool"))   # R-G12 uses pool: resolved by check_transforms
    # check 3: spell references
    spell_refs: list[tuple[str, str]] = []
    sc = doc.get("spellcasting")
    if isinstance(sc, dict):
        spell_refs += [(f"/spellcasting/spells/{i}", s) for i, s in enumerate(sc.get("spells") or [])]
        inn = sc.get("innate")
        if isinstance(inn, dict):
            spell_refs += [(f"/spellcasting/innate/at_will/{i}", s) for i, s in enumerate(inn.get("at_will") or [])]
            for k in sorted(inn.get("per_day") or {}):
                spell_refs += [(f"/spellcasting/innate/per_day/{k}/{i}", s) for i, s in enumerate(inn["per_day"][k])]
            spell_refs += [(f"/spellcasting/innate/recharge/{i}/spell", e["spell"]) for i, e in enumerate(inn.get("recharge") or [])]
    for path, r in mechanical_reactions(doc):
        if (r.get("effect") or {}).get("kind") == "cast":
            spell_refs.append((f"{path}/effect/spell", r["effect"]["spell"]))
    for where, ref in spell_refs:
        if ref not in ctx["spell_ids"] and not ctx["external_ok"](ref):
            errs.append(f"{where}: '{ref}' is not a spell@1 in this pack or its depends (check 3)")
    # check 4: save_effect ids (one '#' in "<archetype id>#<id>")
    seen: dict[str, str] = {}
    for path, a in mechanical_actions(doc):
        if a.get("kind") in ("save_effect", "attack_effect"):   # one synthetic-spell id space (sim #170)
            sid = str(a.get("id"))
            if sid in seen:
                errs.append(f"{path}: {a.get('kind')} id '{sid}' is already used by {seen[sid]} (check 4)")
            seen.setdefault(sid, path)
            if f"{doc.get('id')}#{sid}".count("#") != 1:
                errs.append(f"{path}: '{doc.get('id')}#{sid}' must contain exactly one '#' (check 4)")
    # check 5: multiattack
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") == "multiattack":
            oids = [o.get("id") for o in eff.get("options") or []]
            if len(oids) != len(set(oids)):
                errs.append(f"{path}: multiattack option ids must be unique (check 5)")
            for j, o in enumerate(eff.get("options") or []):
                for k, aid in enumerate(o.get("attacks") or []):
                    if aid not in action_ids:
                        errs.append(f"{path}/options/{j}/attacks/{k}: '{aid}' is not an id in actions[] (check 5)")
            for k, aid in enumerate(eff.get("attacks") or []):
                if aid not in action_ids:
                    errs.append(f"{path}/attacks/{k}: '{aid}' is not an id in actions[] (check 5)")
    # check 6 / 11: equipment item references
    equipment = doc.get("equipment") or []
    objs = [(i, e) for i, e in enumerate(equipment) if isinstance(e, dict)]
    for i, e in objs:
        # A priced entry (value_gp: a material component such as a 300 gp diamond) needs no item@1 document (Director ruling).
        if "value_gp" in e:
            continue
        if e.get("item") not in ctx["items"] and not ctx["external_ok"](e.get("item")):
            errs.append(f"/equipment/{i}: item '{e.get('item')}' is not an item@1 in this pack or its depends (check 6/11)")
    if objs and "equipment" in (doc.get("inert_mechanics") or []):
        warns.append("equipment is marked inert but contains item objects; they are ignored (the marker wins, no AC is recomputed)")
    # check 7: forward_compat
    fc = doc.get("forward_compat")
    if fc is not None:
        bad = [v for v in fc if v not in FORWARD_COMPAT_KNOWN]
        if bad:
            errs.append(f"/forward_compat: unknown value(s) {sorted(set(map(str, bad)))} (check 7; known: {sorted(FORWARD_COMPAT_KNOWN)})")
        if len(fc) != len(set(fc)):
            errs.append("/forward_compat: values must be unique (check 7)")
    # check 8: derived trigger ids <owner id>#<index> unique (a trait carries one trigger: index 0)
    tids: dict[str, str] = {}

    def add_trigger(tid: str, where: str) -> None:
        if tid in tids:
            errs.append(f"{where}: derived trigger id '{tid}' is already used by {tids[tid]} (check 8)")
        tids.setdefault(tid, where)

    for path, a in mechanical_actions(doc):
        for j, r in enumerate(a.get("on_hit") or []):
            if not is_grapple(r):   # a grapple rider compiles to Archetype::grapple_on_hit, not to a trigger
                add_trigger(f"{a.get('id')}#{j}", f"{path}/on_hit/{j}")
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") == "on_event":
            add_trigger(f"{owner}#0", path)
    # check 13: range_ft iff not (origin self and area present)
    for path, a in mechanical_actions(doc):
        if a.get("kind") == "save_effect" and "range_ft" in a and "area" in a and a.get("origin", "self") == "self":
            errs.append(f"{path}: range_ft is forbidden when origin is self and area is present (check 13)")
    # check 14 / 16.4 / 16.5: resource.max forms
    for i, r in enumerate(resources):
        mx = r.get("max")
        where = f"/resources/{i}"
        if typ == "monster" and isinstance(mx, dict):
            errs.append(f"{where}/max: a monster pool must use an integer max (object and table forms are character@4-only) (check 14)")
        if isinstance(mx, dict) and isinstance(mx.get("table"), dict):
            check_table(f"{where}/max/table", mx["table"], errs, pool=True)
            if "start" in r:
                errs.append(f"{where}: 'start' is not allowed with max.table (pools start full) (check 16.5)")
            if typ == "monster" and "class" in mx["table"]:
                errs.append(f"{where}/max/table: 'class' is character@4-only (check 16.4)")
    for path, m in modifier_sites(doc):
        bt = m.get("bonus_table")
        if isinstance(bt, dict):
            check_table(f"{path}/bonus_table", bt, errs, pool=False)
            if typ == "monster":
                errs.append(f"{path}/bonus_table: level tables are character@4-only (check 16.4)")
    # bonus-action attack option conventions (docs/CONTENT_CONVENTIONS.md section 2; sim content.cpp compile_bonus_attacks):
    # the two errors are load errors in the sim, the two warnings are options that compile but can never be used.
    feature_ids = {t.get("id") for key in ("traits", "features") for t in doc.get(key) or [] if isinstance(t, dict)}
    unarmed = any(isinstance(a, dict) and a.get("kind") == "melee_weapon_attack" and "unarmed" in (a.get("properties") or [])
                  for a in doc.get("actions") or [])
    for fid in ("martial_arts", "flurry_of_blows"):
        if fid in feature_ids and not unarmed:
            errs.append(f"{doc.get('id')}: feature '{fid}' needs a melee_weapon_attack action with the 'unarmed' property (bonus attacks)")
    if "frenzy" in feature_ids and "rage" not in feature_ids:
        errs.append(f"{doc.get('id')}: feature 'frenzy' needs a feature with id 'rage' (bonus attacks)")
    if "flurry_of_blows" in feature_ids:
        referenced.add("ki")   # the sim spends it by convention (pool id 'ki'): not a declaration-only pool (check 12)
    if "flurry_of_blows" in feature_ids and "ki" not in res_by_id:
        warns.append(f"{doc.get('id')}: feature 'flurry_of_blows' spends 1 from the pool 'ki' but resources[] declares no 'ki' pool: "
                     "the option can never be paid (bonus attacks)")
    if "frenzy" in feature_ids and "rage" in feature_ids:
        rage_live = {o: e for _p, o, e in live_effects(doc) if o == "rage"}
        if rage_live.get("rage", {}).get("kind") != "apply_modifier":
            warns.append(f"{doc.get('id')}: feature 'frenzy' requires the Rage modifier '<id>#rage#m0', but the 'rage' feature has no live "
                         "apply_modifier effect: the option is never available (bonus attacks)")
    errs += check_static_kinds(doc)
    check_grapple_riders(doc, errs, warns)
    check_untimed_save_condition_riders(doc, errs)
    check_save_max_size(doc, errs)
    check_refused_condition_durations(doc, errs, warns)
    check_transforms(doc, ctx, errs, warns)
    errs += check_class_hooks(doc)   # 7.2y
    errs += check_barbarian_hooks(doc)   # 7.2z
    errs += check_bard_hooks(doc)   # 7.2za
    check_monk_warlock_hooks(doc, ctx, errs, warns)   # 7.2zb
    errs += check_spell_gap_hooks(doc)   # 7.2zc
    check_spell_batch2_hooks(doc, errs, warns)   # 7.2zd
    # check 12: declared but never referenced pools (Legendary Resistance is declaration-only until row 13)
    for i, r in enumerate(resources):
        if r.get("id") not in referenced:
            warns.append(f"/resources/{i}: pool '{r.get('id')}' is never referenced by a usage.pool / spend.pool / "
                         "heal_pool.pool (declaration-only until the pool rows land) (check 12)")
    return errs, warns


def check_static_kinds(doc: dict) -> list[str]:
    """R-G5 static kinds: the shapes the sim accepts (anything else is a load error there).
    attack_bonus {attacks: melee | ranged, amount: integer}, crit_range {min: 2..20}, ac_bonus {amount: -10..10, requires_armor?: bool}."""
    errs = []
    for path, _owner, eff in live_effects(doc):
        kind = eff.get("kind")
        if kind == "attack_bonus":
            if eff.get("attacks") not in ("melee", "ranged"):
                errs.append(f"{path}: attack_bonus.attacks must be 'melee' or 'ranged' (R-G5)")
            if not is_int(eff.get("amount")) or not -20 <= eff["amount"] <= 20:
                errs.append(f"{path}: attack_bonus.amount must be an integer in -20..20 (R-G5)")
        elif kind == "crit_range":
            if not is_int(eff.get("min")) or not 2 <= eff["min"] <= 20:
                errs.append(f"{path}: crit_range.min must be an integer in 2..20 (R-G5)")
        elif kind == "ac_bonus":
            if not is_int(eff.get("amount")) or not -10 <= eff["amount"] <= 10:
                errs.append(f"{path}: ac_bonus.amount must be an integer in -10..10 (R-G5)")
            if "requires_armor" in eff and not isinstance(eff["requires_armor"], bool):
                errs.append(f"{path}: ac_bonus.requires_armor must be a boolean (R-G5)")
    return errs


def _level_in(doc: dict, cls: str) -> int:
    """Mirror of the sim's level_in (content.cpp): the summed level of class `cls`, or the document level (min 1)."""
    if cls:
        return sum(c.get("level", 0) for c in doc.get("classes") or []
                   if isinstance(c, dict) and c.get("class") == cls and is_int(c.get("level", 0)))
    lvl = doc.get("level", 1)
    return max(1, lvl if is_int(lvl) else 1)


def limited_pool_ids(doc: dict) -> set:
    """resources[] ids the sim compiles into limited pools (content.cpp compile_resources -> Archetype::resources): every
    pool except one whose max.table entry at the document's level is `unlimited` (Archetype::unlimited_resources)."""
    out = set()
    for r in doc.get("resources") or []:
        if not isinstance(r, dict) or not isinstance(r.get("id"), str):
            continue
        mx = r.get("max")
        unlimited = False
        if isinstance(mx, dict) and isinstance(mx.get("table"), dict):
            t = mx["table"]
            lvl, best = _level_in(doc, t.get("class", "") or ""), 0
            for e in t.get("by_level") or []:
                el = e.get("level") if isinstance(e, dict) else None
                if is_int(el) and el <= lvl and el >= best:
                    best, unlimited = el, e.get("unlimited", False) is True
        if not unlimited:
            out.add(r["id"])
    return out


def sorcery_points_max(level: int) -> int:
    """rules5e font_of_magic.hpp: the sorcerer level from 2nd, 0 below."""
    return level if level >= 2 else 0


def sorcerer_level(doc: dict) -> int:
    """features.cpp apply_font_of_magic: the summed level of the classes[] entries with class sorcerer (no fallback)."""
    return sum(c.get("level", 0) for c in doc.get("classes") or []
               if isinstance(c, dict) and c.get("class") == "sorcerer" and is_int(c.get("level", 0)))


def pool_max_at_level(doc: dict, r: dict):
    """A resources[] pool's max as the sim compiles it (content.cpp compile_resources), or None when it depends on an
    ability score (plus_ability) or cannot be resolved here."""
    mx = r.get("max")
    if is_int(mx):
        return mx
    if isinstance(mx, dict) and "table" not in mx:
        if "plus_ability" in mx or not is_int(mx.get("per_level")):
            return None
        return max(mx["per_level"] * _level_in(doc, mx.get("class", "") or ""), mx.get("min", 0) if is_int(mx.get("min")) else 0)
    if isinstance(mx, dict) and isinstance(mx.get("table"), dict):
        t = mx["table"]
        lvl, best, val = _level_in(doc, t.get("class", "") or ""), 0, None
        for e in t.get("by_level") or []:
            el = e.get("level") if isinstance(e, dict) else None
            if is_int(el) and best <= el <= lvl:
                best, val = el, e.get("value")
        return val if is_int(val) else None
    return None


def check_metamagic(doc: dict) -> tuple[list[str], list[str]]:
    """R-G14 / 7.2o (sim #143 / #152, features.cpp apply_metamagic / apply_font_of_magic / apply_sculpt_spells), for documents
    whose effects are active. Effects are applied in document order, as the loader does.
    metamagic {options, pool}: the options shape is the schema arm (non-empty, METAMAGIC_OPTIONS names); the pool must be a
    limited resources[] pool, or one an earlier font_of_magic registered (load error, the loader's wording); several
    declarations OR their options but must name the same pool (load error); an option listed twice adds nothing (warning).
    font_of_magic {pool}: the pool must not be unlimited and the creature needs sorcerer level 2+ (load errors); it must be the
    metamagic pool (load error either order). The loader resolves the pool to max = start = sorcerer level, recharge long_rest
    (registering it when resources[] lacks it): a pool authored with another max or recharge, or no pool, is a warning.
    sculpt_spells {}: no fields (others ignored: warning)."""
    errs: list[str] = []
    warns: list[str] = []
    limited = limited_pool_ids(doc)
    pools = {r["id"]: r for r in doc.get("resources") or [] if isinstance(r, dict) and isinstance(r.get("id"), str)}
    first = None    # (pool, path): the sorcery pool, set by the first metamagic or font_of_magic (Combatant::sorcery_pool)
    for path, owner, eff in live_effects(doc):
        kind = eff.get("kind")
        if kind == "metamagic":
            opts = eff.get("options")
            if isinstance(opts, list):
                dups = sorted({o for o in opts if isinstance(o, str) and opts.count(o) > 1})
                if dups:
                    warns.append(f"{path}: metamagic option(s) {dups} listed more than once; a repeat adds nothing (R-G14)")
            pid = eff.get("pool")
            if pid not in limited:
                errs.append(f"{path}: {owner}: metamagic pool '{pid if isinstance(pid, str) else ''}' is not a limited "
                            "resources[] pool (R-G14)")
            if first is None:
                first = (pid, path)
            elif pid != first[0]:
                errs.append(f"{path}: {owner}: metamagic declarations name different pools ('{pid}' here, '{first[0]}' at "
                            f"{first[1]}) (R-G14)")
        elif kind == "font_of_magic":
            pid = eff.get("pool")
            if not isinstance(pid, str) or not pid:
                continue   # schema arm (required, minLength 1): "font_of_magic needs pool"
            if pid in pools and pid not in limited:
                errs.append(f"{path}: {owner}: font_of_magic pool '{pid}' is unlimited (7.2o)")
                continue
            lvl = sorcerer_level(doc)
            want = sorcery_points_max(lvl)
            if want <= 0:
                errs.append(f"{path}: {owner}: font_of_magic needs sorcerer level 2+ (sorcerer level {lvl}) (7.2o)")
                continue
            if first is not None and first[0] != pid:
                errs.append(f"{path}: {owner}: font_of_magic and metamagic name different pools ('{pid}' here, '{first[0]}' at "
                            f"{first[1]}) (7.2o)")
                continue
            if pid not in pools:
                warns.append(f"{path}: font_of_magic pool '{pid}' is not in resources[]; the sim registers it (max {want} = "
                             "sorcerer level, recharge long_rest); declare it there so the document shows the pool (7.2o)")
                limited.add(pid)   # registered: a later metamagic naming it loads
            else:
                got = pool_max_at_level(doc, pools[pid])
                if got != want:
                    warns.append(f"{path}: font_of_magic pool '{pid}' max is {got if got is not None else 'not a fixed number'}, "
                                 f"not the sorcerer level {want}; the sim resets it to {want} (7.2o)")
                if pools[pid].get("recharge") != "long_rest":
                    warns.append(f"{path}: font_of_magic pool '{pid}' recharge is {pools[pid].get('recharge')!r}; the sim sets "
                                 "long_rest (7.2o)")
                if is_int(pools[pid].get("start")) and pools[pid]["start"] != want:
                    warns.append(f"{path}: font_of_magic pool '{pid}' start is {pools[pid]['start']}; the sim sets {want} (7.2o)")
            if first is None:
                first = (pid, path)
        elif kind == "sculpt_spells":
            extra = sorted(k for k in eff if k != "kind")
            if extra:
                warns.append(f"{path}: sculpt_spells takes no fields; {extra} are ignored by the sim (R-G14)")
    return errs, warns


def check_on_event_usage(doc: dict) -> tuple[list[str], list[str]]:
    """sim #158 compile_on_event usage: at_will = no cost; per_day / per_rest {uses 1..255} = a trigger-owned pool
    '<trait id>#usage' recharged by rests; pool {pool, cost 1..255} = a Spend from a declared resources[] pool (check 2 / schema);
    recharge {min 2..6}: compiled since sim #170, no finding."""
    errs: list[str] = []
    warns: list[str] = []
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") != "on_event" or not isinstance(eff.get("usage"), dict):
            continue
        u = eff["usage"]
        c = u.get("cost")
        if u.get("kind") == "pool" and isinstance(c, int) and not isinstance(c, bool) and c > 255:
            errs.append(f"{path}/usage: {owner} on_event: bad usage pool (cost must be 1..255, got {c}) (sim #158)")
        # recharge {min 2..6, schema}: compiled since sim #170 (rolled at the owner's turn start), no finding
    return errs, warns


def _predicates(when):
    return [p for p in (when if isinstance(when, list) else []) if isinstance(p, dict)]


def check_sim170(doc: dict) -> tuple[list[str], list[str]]:
    """sim #170 (EC deferred monsters): attack_effect actions, on_hit rider when[], target_tag / not_target_tag.
    Findings mirror the loader: an attack_effect apply[] entry without escape is listed unsupported (warning); a rider
    when[] outside on_hit[] and an escape outside attack_effect are ignored (warning); a predicate tag that is not lower
    case never matches (content tags are case-folded, predicate tags are not) (warning)."""
    errs: list[str] = []
    warns: list[str] = []
    for path, a in mechanical_actions(doc):
        is_ae = a.get("kind") == "attack_effect"
        for j, x in enumerate(a.get("apply") or []):
            if not isinstance(x, dict):
                continue
            swallow = isinstance(x.get("contain"), dict) and x["contain"].get("kind") == "swallow"   # 7.2w: no escape
            if is_ae and "escape" not in x and not swallow:
                warns.append(f"{path}/apply/{j}: attack_effect '{a.get('id')}': unsupported (attack_effect: unsupported usage or "
                             "condition duration): every apply[] entry needs escape; the sim lists the action and never uses it (sim #170)")
            if not is_ae and "escape" in x and a.get("kind") != "save_effect":   # 7.2w: save_effect reads escape (Engulf)
                warns.append(f"{path}/apply/{j}: escape is read only by attack_effect actions; ignored here (sim #170)")
    for path, owner, j, r in rider_sites(doc):
        if isinstance(r, dict) and "when" in r and "/on_hit/" not in path:
            warns.append(f"{path}: rider when[] is read only in on_hit[] of an attack action; ignored in on_event effects[] (sim #170)")
        for x in (r.get("apply") or []) if isinstance(r, dict) else []:
            if isinstance(x, dict) and "escape" in x:
                warns.append(f"{path}: escape is read only by attack_effect actions; ignored in a rider (sim #170)")
    sites = [(f"{p}/when", e.get("when")) for p, _o, e in live_effects(doc) if e.get("kind") == "on_event"]
    sites += [(f"{p}/when", r.get("when")) for p, _o, _j, r in rider_sites(doc) if isinstance(r, dict)]
    for where, when in sites:
        for i, pr in enumerate(_predicates(when)):
            t = pr.get("tag")
            if pr.get("kind") in ("target_tag", "not_target_tag") and isinstance(t, str) and t != t.lower():
                warns.append(f"{where}/{i}: tag '{t}' is not lower case; content tags are case-folded, so it never matches (sim #170)")
    return errs, warns



def check_sim172_174(doc: dict) -> tuple[list[str], list[str]]:
    """sim #172 (swarms) / #174 (giant spider follow-up), @4 documents. Errors mirror the loader's refusals that the
    schema cannot express: half_hp_damage needs at least one die ('<id>: bad half_hp_damage in action <aid>');
    attack_effect long_range_ft ranged only and > range_ft ('bad long_range_ft (ranged, > range_ft)'); zero_hp needs
    damage and condition != while ('zero_hp needs damage', 'bad zero_hp condition / while')."""
    errs: list[str] = []
    warns: list[str] = []
    did = doc.get("id", "?")
    for path, a in mechanical_actions(doc):
        hh = a.get("half_hp_damage")
        m = re.match(r"\s*(\d+)\s*d", hh) if isinstance(hh, str) else None
        if isinstance(hh, str) and (m is None or int(m.group(1)) == 0):
            errs.append(f"{path}: {did}: bad half_hp_damage in action {a.get('id', '?')} (needs at least one die) (sim #172)")
        if a.get("kind") == "attack_effect" and "long_range_ft" in a:
            lr, rng = a.get("long_range_ft"), a.get("range_ft", 5)
            if a.get("attack", "ranged") == "melee" or not isinstance(lr, int) or not isinstance(rng, int) or lr <= rng:
                errs.append(f"{path}: attack_effect '{a.get('id')}': bad long_range_ft (ranged, > range_ft) (sim #174)")
    sites = [(p, a) for p, a in mechanical_actions(doc) if a.get("kind") == "save_effect"]
    sites += [(p, r) for p, _o, _j, r in rider_sites(doc) if isinstance(r, dict) and r.get("kind") == "save_condition"]
    for path, x in sites:
        zh = x.get("zero_hp")
        if not isinstance(zh, dict):
            continue
        if not x.get("damage"):
            errs.append(f"{path}: zero_hp needs damage (sim #174)")
        if zh.get("condition") == zh.get("while"):
            errs.append(f"{path}: bad zero_hp condition / while (they must differ) (sim #174)")
    return errs, warns



GRAPPLE_RIDER_KEYS = ("restrains", "exclusive", "max_grapples")


def check_grapple_rider(doc: dict) -> tuple[list[str], list[str]]:
    """Druid gap G: on_hit grapple riders' restrains / exclusive / max_grapples (common@2 additive) load only for monster@5
    (the loader reads them on any weapon attack; the validator keeps them to monster@5 like the Director's ruling)."""
    errs: list[str] = []
    schema = str(doc.get("$schema", ""))
    is_m5 = schema.split("@")[0] == "monster" and schema.split("@")[-1].startswith("5")
    for path, a in mechanical_actions(doc):
        for j, r in enumerate(a.get("on_hit") or []):
            if not is_grapple(r):
                continue
            used = [k for k in GRAPPLE_RIDER_KEYS if k in r]
            if used and not is_m5:
                errs.append(f"{path}/on_hit/{j}: grapple {', '.join(used)} need monster@5 (Druid gap G)")
    return errs, []


GAZE_KEYS = frozenset({"kind", "range_ft", "save", "condition", "repeat_save", "escalates", "escalate_to", "escalate_margin"})
GAZE_CONDITIONS = frozenset({"blinded", "charmed", "deafened", "frightened", "grappled", "incapacitated", "invisible",
                             "paralyzed", "petrified", "poisoned", "prone", "restrained", "stunned", "unconscious",
                             "exhaustion"})   # content.cpp condition_of
GAZE_ABILITIES = frozenset({"str", "dex", "con", "int", "wis", "cha"})


def check_gaze(doc: dict) -> tuple[list[str], list[str]]:
    """7.2p (sim #153 content.cpp compile_gaze), @4 documents. Errors: compile_gaze's refusals in its wording
    (`gaze '<id>': bad range_ft` / `missing save` / `bad save` / `bad condition` / `bad repeat_save` / `bad escalate_to` /
    `bad escalate_margin`), which the closed common@2 arm also rejects (its anyOf message names no field), plus keys outside
    the arm. Warnings: condition, repeat_save or escalate_to left to the loader's defaults (restrained, end_of_turn,
    petrified), so the authored data says what the sim does."""
    errs: list[str] = []
    warns: list[str] = []
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") != "gaze":
            continue
        where = f"{path}: gaze '{owner}'"
        bad = []
        r = eff.get("range_ft")
        if not is_int(r) or not 5 <= r <= 65530 or r % 5:
            bad.append("bad range_ft (an integer multiple of 5 in 5..65530)")
        sv = eff.get("save")
        if not isinstance(sv, dict):
            bad.append("missing save")
        elif sv.get("ability") not in GAZE_ABILITIES or not is_int(sv.get("dc")) or not 1 <= sv["dc"] <= 30 or set(sv) - {"ability", "dc"}:
            bad.append("bad save ({ability, dc 1..30})")
        if "condition" in eff and eff["condition"] not in GAZE_CONDITIONS:
            bad.append("bad condition")
        if "repeat_save" in eff and eff["repeat_save"] not in ("end_of_turn", "start_of_turn", "none"):
            bad.append("bad repeat_save (end_of_turn, start_of_turn or none)")
        if "escalates" in eff and not isinstance(eff["escalates"], bool):
            bad.append("escalates must be a boolean")
        if "escalate_to" in eff and eff["escalate_to"] not in GAZE_CONDITIONS:
            bad.append("bad escalate_to")
        m = eff.get("escalate_margin", 0)
        if not is_int(m) or not 0 <= m <= 30:
            bad.append("bad escalate_margin (0..30)")
        extra = sorted(set(eff) - GAZE_KEYS)
        if extra:
            bad.append(f"unknown key(s) {extra} (the common@2 gaze arm is closed)")
        errs += [f"{where}: {m} (7.2p)" for m in bad]
        missing = [k for k in GAZE_DEFAULTS if k not in eff]
        if missing:
            warns.append(f"{path}: gaze '{owner}' omits {', '.join(missing)}; the sim uses "
                         f"{', '.join(f'{k} {GAZE_DEFAULTS[k]}' for k in missing)} (7.2p)")
    return errs, warns


def check_slot_recovery(doc: dict) -> list[str]:
    """7.2n (sim #148, features.cpp apply_slot_recovery), for documents whose effects are active. The class / integer /
    range shape is the schema arm; this mirrors the loader errors the arm cannot express: the class must be in classes[]
    (a monster has none), its summed level must be 1..20, and a second declaration for one class fails to load."""
    errs: list[str] = []
    seen: dict = {}
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") != "slot_recovery":
            continue
        cls = eff.get("class")
        if cls not in SLOT_RECOVERY_CLASSES:
            continue   # schema arm (enum): "slot_recovery class must be wizard or druid"
        rows = [c for c in doc.get("classes") or [] if isinstance(c, dict) and c.get("class") == cls]
        if not rows:
            errs.append(f"{path}: {owner}: slot_recovery class '{cls}' is not in classes[] (7.2n)")
            continue
        level = sum(c.get("level") for c in rows if is_int(c.get("level")))
        if level < 1 or level > 20:
            errs.append(f"{path}: {owner}: slot_recovery class level out of range 1..20 ({cls} level {level}) (7.2n)")
        if cls in seen:
            errs.append(f"{path}: {owner}: slot_recovery declared twice for class '{cls}' (first at {seen[cls]}) (7.2n)")
        else:
            seen[cls] = path
    return errs


# ---- R-G12 transforms (VERSION_BUMP_BATCH 7.2q, Rules #156) ------------------------------------------------------
# polymorph (spell@1, rules spell loader), wild_shape / shapechanger (common@2, Engine Core build_form) and the trait flag
# requires_humanoid_form. wild_shape / shapechanger are loaded kinds at @4 since EC #176 (COMPILED_EFFECT_KINDS_AT_4: no
# unknown-kind warning there); polymorph is parsed by the rules spell loader since #171 (no reserved warning; polymorph_spell_warnings keeps above-max_cr). The messages below are the loaders' wording (Rules parses polymorph with them).
CR_ORDER = ("0", "1/8", "1/4", "1/2") + tuple(str(i) for i in range(1, 31))


def cr8(cr) -> int | None:
    """A challenge.rating string as CR*8 (integer math: 1/8 -> 1, 1/4 -> 2, 1/2 -> 4, n -> 8n)."""
    if not isinstance(cr, str) or cr not in CR_ORDER:
        return None
    return {"0": 0, "1/8": 1, "1/4": 2, "1/2": 4}.get(cr, 8 * int(cr) if cr.isdigit() else None)


def monster_facts(doc) -> tuple | None:
    """(creature type, challenge.rating) of a monster document, for form resolution."""
    if not isinstance(doc, dict):
        return None
    ch = doc.get("challenge")
    return doc.get("type"), ch.get("rating") if isinstance(ch, dict) else None


def has_effect_kind(doc: dict, kind: str) -> bool:
    return any(e.get("kind") == kind for _p, _o, e in live_effects(doc))


def check_forms(label: str, forms, ctx: dict, errs: list[str]) -> list[tuple[str, int]]:
    """Errors for a beast forms[] list (unknown / not a beast / duplicate); returns [(id, CR*8)] of the resolved beasts.
    srd51.* ids under the deprecated escape hatch (no srd51.core dependency) are accepted unresolved."""
    monsters = ctx.get("monsters") or {}
    ext = ctx.get("external_ok") or (lambda _a: False)
    seen, out = set(), []
    for f in forms if isinstance(forms, list) else []:
        if not isinstance(f, str):
            continue
        if f in seen:
            errs.append(f"{label}: duplicate form '{f}'")
            continue
        seen.add(f)
        facts = monsters.get(f)
        if facts is None:
            if not ext(f):
                errs.append(f"{label}: unknown form '{f}'")
            continue
        if facts[0] != "beast":
            errs.append(f"{label}: form '{f}' is not a beast")
            continue
        c = cr8(facts[1])
        if c is not None:
            out.append((f, c))
    return out


def check_polymorph_spell(doc: dict, ctx: dict) -> list[str]:
    """spell@1 polymorph (the rules spell loader's load errors, same wording)."""
    errs: list[str] = []
    effs = doc.get("effects") or []
    for i, e in enumerate(effs[1:], 1):
        if isinstance(e, dict) and e.get("kind") == "polymorph":
            errs.append("polymorph: primary effect only")
            return errs
    if not effs or effs[0].get("kind") != "polymorph":
        return errs
    tg = doc.get("targets")
    if "area" in doc or (isinstance(tg, dict) and tg.get("count", 1) != 1):
        errs.append("polymorph: one target")
    if (doc.get("duration") or {}).get("concentration") is not True:
        errs.append("polymorph: needs concentration")
    check_forms("polymorph", effs[0].get("forms"), ctx, errs)
    return errs


def polymorph_spell_warnings(doc: dict, ctx: dict) -> list[str]:
    effs = doc.get("effects") or []
    if not effs or not isinstance(effs[0], dict) or effs[0].get("kind") != "polymorph":
        return []
    eff = effs[0]
    out: list[str] = []   # Rules #171 parses polymorph: no reserved warning
    lim = cr8(eff.get("max_cr", "target_level"))
    if lim is not None:   # fixed max_cr only (target_level is checked by sim build_form at cast time)
        for f, c in check_forms("polymorph", eff.get("forms"), ctx, []):
            if c > lim:
                out.append(f"polymorph: form '{f}' above max_cr")
    return out


def check_transforms(doc: dict, ctx: dict, errs: list[str], warns: list[str]) -> None:
    """common@2 wild_shape / shapechanger and requires_humanoid_form (monster@4 / character@4)."""
    res = {r.get("id"): r for r in doc.get("resources") or [] if isinstance(r, dict)}
    action_ids = {a.get("id") for a in doc.get("actions") or [] if isinstance(a, dict)}
    classes = {c.get("class") for c in doc.get("classes") or [] if isinstance(c, dict)}
    seen: dict[str, str] = {}
    for path, _owner, eff in live_effects(doc):
        kind = eff.get("kind")
        if kind not in ("wild_shape", "shapechanger"):
            continue
        if kind in seen:
            errs.append(f"{path}: {kind}: one declaration per document")
            continue
        seen[kind] = path
        if kind == "wild_shape":
            pid = eff.get("pool")
            r = res.get(pid)
            if r is None:
                errs.append(f"{path}: wild_shape: unknown pool '{pid}'")
            else:
                if r.get("recharge") != "short_rest":
                    errs.append(f"{path}: wild_shape: pool '{pid}' must recharge on short_rest")
                if not (is_int(r.get("max")) and r.get("max") == 2):
                    warns.append(f"{path}: wild_shape: pool '{pid}' max should be 2")
            rows = [x for x in eff.get("max_cr_by_level") or [] if isinstance(x, dict)]
            lv = [x.get("level") for x in rows]
            if any(not (a < b) for a, b in zip(lv, lv[1:])):
                errs.append(f"{path}: wild_shape: max_cr_by_level levels must be strictly ascending")
            top = max((cr8(x.get("max_cr")) or 0 for x in rows), default=0)
            for f, c in check_forms(f"{path}: wild_shape", eff.get("forms"), ctx, errs):
                if c > top:
                    warns.append(f"{path}: wild_shape: form '{f}' above every max_cr_by_level row")
            cls = eff.get("class", "druid")
            if cls not in classes:
                warns.append(f"{path}: wild_shape: class '{cls}' not in classes[]")
        else:
            if doc.get("shapechanger") is not True:
                errs.append(f"{path}: shapechanger trait requires shapechanger: true")
            ids = set()
            for j, fm in enumerate(eff.get("forms") or []):
                if not isinstance(fm, dict):
                    continue
                fid = fm.get("id")
                if fid in ids:
                    errs.append(f"{path}/forms/{j}: shapechanger: duplicate form id '{fid}'")
                ids.add(fid)
                for a in fm.get("actions") or []:
                    if a not in action_ids:
                        errs.append(f"{path}/forms/{j}: shapechanger: form '{fid}' names unknown action '{a}'")
                if not set(fm) - {"id", "name"}:
                    warns.append(f"{path}/forms/{j}: shapechanger: form '{fid}' changes nothing")
                if fm.get("actions") == []:
                    warns.append(f"{path}/forms/{j}: shapechanger: form '{fid}' allows no actions")
    for key in ("traits", "features"):
        for i, t in enumerate(doc.get(key) or []):
            if not isinstance(t, dict) or t.get("requires_humanoid_form") is not True:
                continue
            eff = t.get("effect") if isinstance(t.get("effect"), dict) else {}
            if eff.get("kind") == "wild_shape":
                errs.append(f"/{key}/{i}: requires_humanoid_form on the wild_shape feature itself")
            elif "wild_shape" not in seen:
                warns.append(f"/{key}/{i}: requires_humanoid_form has no effect without wild_shape")


# ---- extra_attack (EC #179, sim features.cpp apply_extra_attack) -------------------------------------------------
EXTRA_ATTACK_KEYS = frozenset({"kind", "attacks_per_action"})


def check_extra_attack(doc: dict, typ: str) -> tuple[list[str], list[str]]:
    """@4 documents. Errors: the loader's refusals in its order and wording, prefixed with the document id as
    ContentLibrary::add reports them ('<id>: extra_attack: unknown key '<k>'', '<id>: extra_attack: only valid on
    characters', '<id>: extra_attack: attacks_per_action must be an integer 2..4'). Warning: several declarations (the
    sim keeps the highest, no stacking)."""
    errs: list[str] = []
    warns: list[str] = []
    did = doc.get("id", "?")
    first: tuple[str, object] | None = None
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") != "extra_attack":
            continue
        extra = sorted(k for k in eff if k not in EXTRA_ATTACK_KEYS)
        n = eff.get("attacks_per_action")
        if extra:
            errs.append(f"{path}: {did}: extra_attack: unknown key '{extra[0]}'")
        elif typ != "character":
            errs.append(f"{path}: {did}: extra_attack: only valid on characters")
        elif not is_int(n) or not 2 <= n <= 4:
            errs.append(f"{path}: {did}: extra_attack: attacks_per_action must be an integer 2..4")
        if first is None:
            first = (path, n)
        else:
            warns.append(f"{path}: extra_attack declared more than once (first at {first[0]}); the sim keeps the highest "
                         "attacks_per_action, they do not stack (sim #179)")
    return errs, warns


TRANSPARENT_KEYS = frozenset({"kind", "dc"})


def check_transparent(doc: dict, typ: str) -> tuple[list[str], list[str]]:
    """7.2w transparent {dc 1..30} (sim features.cpp apply_transparent): the loader's refusals as errors (same text), and a
    warning when it is declared more than once (the sim keeps the highest dc)."""
    errs: list[str] = []
    warns: list[str] = []
    did = doc.get("id", "?")
    first: str | None = None
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") != "transparent":
            continue
        extra = sorted(k for k in eff if k not in TRANSPARENT_KEYS)
        dc = eff.get("dc")
        if extra:
            errs.append(f"{path}: {did}: transparent: unknown key '{extra[0]}'")
        elif typ != "monster":
            errs.append(f"{path}: {did}: transparent: only valid on monsters")
        elif not is_int(dc) or not 1 <= dc <= 30:
            errs.append(f"{path}: {did}: transparent: dc must be an integer 1..30")
        if first is None:
            first = path
        else:
            warns.append(f"{path}: transparent declared more than once (first at {first}); the sim keeps the highest dc (7.2w)")
    return errs, warns


CONTROL_KEYS_7_2Y = ("control", "control_excludes_creature_types", "destroy_undead_max_cr8", "target_creature_types", "duration")


def _bonus_vs_sites(doc: dict):
    for path, a in mechanical_actions(doc):
        for j, d in enumerate(a.get("damage") or []):
            if isinstance(d, dict) and "bonus_vs" in d:
                yield f"{path}/damage/{j}", a, d["bonus_vs"]
    for path, _owner, _j, r in rider_sites(doc):
        for j, d in enumerate((r.get("damage") or []) if isinstance(r, dict) else []):
            if isinstance(d, dict) and "bonus_vs" in d:
                yield f"{path}/damage/{j}", None, d["bonus_vs"]


def check_class_hooks(doc: dict) -> list[str]:
    """7.2y class feature hooks (sim content.cpp / features.cpp): the loader's refusals for the keys on the wrong host,
    with the same meaning. control / target_creature_types / duration: save_effect actions only; scope: apply_modifier
    effects only, exactly {weapon: "wielded"}; bonus_vs: weapon attack damage and rider damage only."""
    errs: list[str] = []
    did = doc.get("id", "?")
    for path, a in mechanical_actions(doc):
        if a.get("kind") != "save_effect":
            for k in CONTROL_KEYS_7_2Y:
                if k in a:
                    errs.append(f"{path}: {did}: {k}: save_effect actions only (7.2y)")
        elif "control" not in a:
            for k in ("control_excludes_creature_types", "destroy_undead_max_cr8", "duration"):
                if k in a:
                    errs.append(f"{path}: {did}: {k} needs control (7.2y)")
        elif a.get("control") == "command":
            errs.append(f"{path}: {did}: control command needs a spell (the word is a cast choice) (7.2y)")
    for path, _owner, eff in live_effects(doc):
        if "scope" not in eff:
            continue
        if eff.get("kind") != "apply_modifier":
            errs.append(f"{path}: {did}: scope: apply_modifier effects only (7.2y)")
        else:   # 7.2z: one closed object, at least one key, each key's enum
            sc = eff["scope"]
            allowed = {"weapon": ("wielded",), "attack": ("melee_weapon", "ranged_weapon"), "ability": ("str", "dex"),
                       "target": ("marked",)}   # 7.2zc: a feature refuses target marked (check_spell_gap_hooks)
            if not isinstance(sc, dict) or not sc:
                errs.append(f"{path}: {did}: scope needs at least one of weapon, attack, ability (7.2z)")
            else:
                for k, v in sc.items():
                    if k == "from_types":   # 7.2zd: a creature-type list (schema); rows need applies_to attackers (check_spell_batch2_hooks)
                        continue
                    if k not in allowed or v not in allowed[k]:
                        errs.append(f"{path}: {did}: scope: bad key or value '{k}' (7.2z)")
    for path, a, bv in _bonus_vs_sites(doc):
        if a is not None and a.get("kind") not in ("melee_weapon_attack", "ranged_weapon_attack"):
            errs.append(f"{path}: {did}: bonus_vs: weapon attack damage only (7.2y)")
        if not isinstance(bv, dict) or set(bv) != {"creature_types", "dice"}:
            errs.append(f"{path}: {did}: bonus_vs needs exactly creature_types and dice (7.2y)")
    return errs


PASSIVE_FORBIDDEN_7_2Z = ("cost", "usage", "duration", "modifier", "modifiers", "scope", "ends_early", "blocks", "on_end")


def check_barbarian_hooks(doc: dict) -> list[str]:
    """7.2z (sim features.cpp apply_apply_modifier / content.cpp compile_bonus_attacks): requires_effect names an
    apply_modifier feature of this document; a passive effect has advantage_on {saves} and when and nothing activated."""
    errs: list[str] = []
    did = doc.get("id", "?")
    live = {owner: eff for _p, owner, eff in live_effects(doc)}
    for path, owner, eff in live_effects(doc):
        if eff.get("kind") == "bonus_attack":
            req = eff.get("requires_effect")
            tgt = live.get(req)
            if not isinstance(tgt, dict) or tgt.get("kind") != "apply_modifier" or tgt.get("passive") is True:
                errs.append(f"{path}: {did}: requires_effect '{req}' is not an apply_modifier feature of this document (7.2z)")
        if eff.get("kind") != "apply_modifier":
            for k in ("passive", "ends_early", "blocks", "advantage_on", "on_end"):   # `when` is on_event's too
                if k in eff:
                    errs.append(f"{path}: {did}: {k}: apply_modifier effects only (7.2z)")
            continue
        if eff.get("passive") is True:
            for k in PASSIVE_FORBIDDEN_7_2Z:
                if k in eff:
                    errs.append(f"{path}: {did}: passive: '{k}' is not allowed (7.2z)")
            ao, w = eff.get("advantage_on"), eff.get("when")
            if not isinstance(ao, dict) or set(ao) != {"saves"} or not isinstance(w, dict):
                errs.append(f"{path}: {did}: passive needs advantage_on {{saves}} only and when (7.2z)")
            elif w.get("save") not in (ao.get("saves") or []):
                errs.append(f"{path}: {did}: when.save must be one of advantage_on.saves (7.2z)")
        elif "when" in eff:
            errs.append(f"{path}: {did}: when needs passive (7.2z)")
    return errs


def check_bard_hooks(doc: dict) -> list[str]:
    """7.2za (sim features.cpp apply_grant_die / apply_half_proficiency): grant_die scale levels strictly ascend, grant_die
    half_proficiency only on characters (the loader's wording)."""
    errs: list[str] = []
    did = doc.get("id", "?")
    for path, _owner, eff in live_effects(doc):
        k = eff.get("kind")
        if k == "grant_die":
            levels = [e.get("level") for e in eff.get("scale") or [] if isinstance(e, dict)]
            if any(not is_int(b) or not is_int(a) or b <= a for a, b in zip([1] + levels, levels)):
                errs.append(f"{path}: {did}: grant_die scale levels must ascend (2..20)")
            cls = eff.get("class")
            if isinstance(cls, str) and cls not in {c.get("class") for c in doc.get("classes") or [] if isinstance(c, dict)}:
                errs.append(f"{path}: {did}: grant_die class '{cls}' is not one of the document's classes")
        elif k == "half_proficiency" and str(doc.get("$schema", "")).split("@")[0] == "monster":
            errs.append(f"{path}: {did}: half_proficiency: not valid on monsters")
    return errs


# 7.2zb: bonus-attack features by local id (sim content.cpp compile_bonus_attacks) besides a `bonus_attack` effect.
BONUS_ATTACK_FEATURE_IDS = frozenset({"martial_arts", "flurry_of_blows", "frenzy"})


def check_monk_warlock_hooks(doc: dict, ctx: dict, errs: list[str], warns: list[str]) -> None:
    """7.2zb (T&C #207 / #210; sim content.cpp compile_on_event / features.cpp apply_spell_mod): attack_from_feature names a
    bonus-attack feature of this document; pick_one needs choice ask on a hit or crit trigger; spell_mod is characters-only, its
    spell must be a spell@1 of the pack (error) and should be one the document casts (warning)."""
    did = doc.get("id", "?")
    is_monster = str(doc.get("$schema", "")).split("@")[0] == "monster"
    live = {owner: eff for _p, owner, eff in live_effects(doc)}
    feature_ids = {t.get("id") for key in ("traits", "features") for t in doc.get(key) or [] if isinstance(t, dict)}
    bonus_ids = {f for f in feature_ids if f in BONUS_ATTACK_FEATURE_IDS} | {o for o, e in live.items() if e.get("kind") == "bonus_attack"}
    sc = doc.get("spellcasting") if isinstance(doc.get("spellcasting"), dict) else {}
    cast = set(sc.get("spells") or []) | set(sc.get("known") or [])
    for path, _owner, eff in live_effects(doc):
        k = eff.get("kind")
        if k == "on_event":
            for j, p in enumerate(eff.get("when") or []):
                if isinstance(p, dict) and p.get("kind") == "attack_from_feature":   # same strings as the sim loader
                    fid = p.get("feature")
                    if fid not in feature_ids:
                        errs.append(f"{path}/when/{j}: {did}: attack_from_feature names unknown feature '{fid}'")
                    elif fid not in bonus_ids:
                        errs.append(f"{path}/when/{j}: {did}: attack_from_feature '{fid}' is not a bonus-attack feature of this document")
            if eff.get("pick_one") is True and (eff.get("choice") != "ask" or eff.get("on") not in ("hit", "crit")):
                errs.append(f"{path}: {did}: pick_one needs choice ask on a hit or crit trigger")
        elif k == "spell_mod":
            if is_monster:
                errs.append(f"{path}: {did}: spell_mod: not valid on monsters")
            ref = eff.get("spell")
            if isinstance(ref, str):
                if ref not in ctx.get("spell_ids", set()) and not ctx.get("external_ok", lambda _r: False)(ref):
                    errs.append(f"{path}/spell: '{ref}' is not a spell@1 in this pack or its depends")
                elif ref not in cast:
                    warns.append(f"{path}/spell: {did}: spell_mod spell '{ref}' is not in spellcasting.spells / known: it never applies")


def check_spell_gap_hooks(doc: dict) -> list[str]:
    """7.2zc (sim features.cpp apply_heal_bonus / apply_apply_modifier): heal_bonus is characters-only; the generic mark keys
    (scope target marked, modifier ability chosen) are spell@1 keys, refused on a feature with the loader's wording."""
    errs: list[str] = []
    did = doc.get("id", "?")
    is_monster = str(doc.get("$schema", "")).split("@")[0] == "monster"
    for path, _owner, eff in live_effects(doc):
        k = eff.get("kind")
        if k == "heal_bonus" and is_monster:
            errs.append(f"{path}: {did}: heal_bonus: not valid on monsters")
        if k != "apply_modifier":
            continue
        sc = eff.get("scope")
        if isinstance(sc, dict) and sc.get("target") == "marked":
            errs.append(f"{path}: {did}: scope: target marked is for spells only")
        mods = ([eff["modifier"]] if isinstance(eff.get("modifier"), dict) else []) + list(eff.get("modifiers") or [])
        for m in mods:
            if isinstance(m, dict) and m.get("ability") == "chosen":
                errs.append(f"{path}: {did}: modifier ability chosen needs scope target marked")
                break
    return errs


def check_spell_batch2_hooks(doc: dict, errs: list[str], warns: list[str]) -> None:
    """7.2zd (sim features.cpp apply_heal_pool / apply_apply_modifier, same wording): Preserve Life's split heal_pool (a usage
    pool; omitted excludes_types warns) and a feature scope from_types (rows need applies_to attackers, attack_roll only)."""
    did = doc.get("id", "?")
    for path, _owner, eff in live_effects(doc):
        k = eff.get("kind")
        if k == "heal_pool" and "per_class_level" in eff:
            u = eff.get("usage") if isinstance(eff.get("usage"), dict) else {}
            if u.get("kind") != "pool":
                errs.append(f"{path}: {did}: heal_pool per_class_level needs a usage pool (Channel Divinity)")
            # features.cpp apply_heal_pool: `class` sums that class's classes[] levels; absent, every entry (fallback: level).
            cls = eff.get("class")
            rows = [c for c in doc.get("classes") or [] if isinstance(c, dict)]
            level = sum(c.get("level", 0) for c in rows if cls is None or c.get("class") == cls
                        if isinstance(c.get("level", 0), int))
            if level == 0 and cls is None and isinstance(doc.get("level"), int):
                level = doc["level"]
            if level < 1:
                errs.append(f"{path}: {did}: heal_pool per_class_level has no class level to resolve against")
            if "excludes_types" not in eff:
                warns.append(f"{path}: {did}: heal_pool per_class_level without excludes_types heals every creature type "
                             "(Preserve Life: undead, construct)")
        if k != "apply_modifier":
            continue
        mods = ([eff["modifier"]] if isinstance(eff.get("modifier"), dict) else []) + list(eff.get("modifiers") or [])
        mods = [m for m in mods if isinstance(m, dict)]
        bad = next((m for m in mods if m.get("applies_to") == "attackers" and m.get("stat") != "attack_roll"), None)
        if bad is not None:
            errs.append(f"{path}: {did}: modifier applies_to attackers: attack_roll modifiers only")
        elif isinstance(eff.get("scope"), dict) and "from_types" in eff["scope"] and \
                any(m.get("applies_to", "target") != "attackers" for m in mods):
            errs.append(f"{path}: {did}: scope from_types needs applies_to attackers")


SPELL_TIMED_DURATIONS = ("rounds", "minutes", "hours")


def check_spell_batch2(doc: dict) -> list[str]:
    """7.2zd spell@1 cross-field rules (rules5e_content spell_loader.cpp, same wording and order): applies_to attackers and
    scope from_types (Protection from Evil and Good), max_hp_bonus amounts (Aid), and the lasting-duration rule of ward,
    condition_immunity_from and max_hp_bonus. Shapes (ward / warded pairing, enums, uniqueness) are the schema's."""
    errs: list[str] = []
    effs = [e for e in (doc.get("effects") or []) if isinstance(e, dict)]
    for i, e in enumerate(effs):
        pre = f"effects[{i}]: " if i > 0 else ""
        k = e.get("kind")
        if k == "apply_modifier":
            m = e.get("modifier") if isinstance(e.get("modifier"), dict) else {}
            applies = m.get("applies_to", "target")
            if applies == "attackers" and m.get("stat") != "attack_roll":
                errs.append(f"{pre}effect modifier applies_to attackers: attack_roll modifiers only")
            elif isinstance(e.get("scope"), dict) and "from_types" in e["scope"] and applies != "attackers":
                errs.append(f"{pre}effect scope from_types needs applies_to attackers")
        elif k == "max_hp_bonus" and e.get("flat", 0) == 0 and e.get("per_slot_above", 0) == 0:
            errs.append(f"{pre}effect max_hp_bonus needs flat or per_slot_above above 0")
    if errs:
        return errs
    du = doc.get("duration") if isinstance(doc.get("duration"), dict) else {}
    lasting = du.get("concentration") is True or du.get("kind") in SPELL_TIMED_DURATIONS
    if lasting or any(e.get("kind") in ("apply_condition", "save") and "ends_on" in e for e in effs):
        return errs   # ends_on without a lasting duration is check_spell_gaps' (it comes first in the loader too)
    if any(e.get("kind") == "apply_condition" and "ward" in e for e in effs):
        errs.append("effect ward needs concentration or a rounds, minutes or hours duration")
    elif any(e.get("kind") == "condition_immunity_from" for e in effs):
        errs.append("effect condition_immunity_from needs concentration or a rounds, minutes or hours duration")
    elif any(e.get("kind") == "max_hp_bonus" for e in effs):
        errs.append("effect max_hp_bonus needs concentration or a rounds, minutes or hours duration")
    return errs


def check_spell_imbue_mirror(doc: dict) -> list[str]:
    """7.2zf spell@1 cross-field rules (rules5e_content spell_loader.cpp, same wording and order): weapon_imbue at_slot slot
    levels ascend and the imbue lasts; duplicates thresholds (index = remaining duplicates - 1; exactly 3 entries is the
    schema's) must not increase with the remaining count, and Mirror Image is self with a rounds / minutes / hours duration."""
    errs: list[str] = []
    effs = [e for e in (doc.get("effects") or []) if isinstance(e, dict)]
    if not effs:
        return errs
    e = effs[0]
    du = doc.get("duration") if isinstance(doc.get("duration"), dict) else {}
    timed = du.get("kind") in SPELL_TIMED_DURATIONS
    if e.get("kind") == "weapon_imbue":
        mb = e.get("magic_bonus") if isinstance(e.get("magic_bonus"), dict) else {}
        slots = [p[0] for p in mb.get("at_slot") or [] if isinstance(p, list) and p]
        if any(b <= a for a, b in zip(slots, slots[1:])):
            errs.append("effect weapon_imbue at_slot slot levels must ascend")
        elif not (du.get("concentration") is True or timed):
            errs.append("effect weapon_imbue needs concentration or a rounds, minutes or hours duration")
    elif e.get("kind") == "duplicates":
        th = e.get("thresholds") or []
        if len(th) == 3 and (th[1] > th[0] or th[2] > th[1]):
            errs.append("effect duplicates thresholds must not increase with the remaining count")
        elif not timed:
            errs.append("effect duplicates needs a rounds, minutes or hours duration")
        elif (doc.get("range") or {}).get("kind") != "self":
            errs.append("effect duplicates is self only")
    return errs


def check_spell_batch3(doc: dict) -> list[str]:
    """7.2ze spell@1 cross-field rules (rules5e_content spell_loader.cpp parse_modifier / parse_effect, same wording and order):
    the modifier sign (Bless / Bane) and an applies_to roll list, the apply_modifier save gate (Bane), and choices (with
    condition chosen, Blindness/Deafness) on the first effect only. Shapes (enums, sizes, the chosen / choices pairing) are the
    schema's."""
    errs: list[str] = []
    effs = [e for e in (doc.get("effects") or []) if isinstance(e, dict)]
    for i, e in enumerate(effs):
        pre = f"effects[{i}]: " if i > 0 else ""
        k = e.get("kind")
        if i > 0 and k == "save" and "choices" in e:
            errs.append(f"effects[{i}]: choices is only allowed on the first effect")
        if k != "apply_modifier":
            continue
        m = e.get("modifier") if isinstance(e.get("modifier"), dict) else {}
        die = m.get("die")
        rolls = m.get("applies_to")
        if isinstance(rolls, list):
            stat = m.get("stat")
            if stat not in ("attack_roll", "save"):
                errs.append(f"{pre}effect modifier applies_to rolls: attack_roll or save modifiers only")
                continue
            if ("attack" if stat == "attack_roll" else "save") not in rolls:
                errs.append(f"{pre}effect modifier applies_to rolls must include the modifier's own stat")
                continue
            if "die" not in m:
                errs.append(f"{pre}effect modifier applies_to rolls needs die")
                continue
        if "save" in e:
            if i > 0:
                errs.append(f"effects[{i}]: an apply_modifier save is only allowed on the first effect")
            elif not isinstance(rolls, list) and m.get("applies_to", "target") != "target":
                errs.append("effect save needs a modifier on the targets (applies_to target or rolls)")
    return errs


def check_spell_gaps(doc: dict) -> list[str]:
    """7.2zc spell@1 cross-field rules (rules5e_content spell_loader.cpp parse_effect / spell_from_json, same wording and
    order): the generic mark (scope target marked, ability chosen, move_mark) and Invisibility's ends_on."""
    errs: list[str] = []
    effs = [e for e in (doc.get("effects") or []) if isinstance(e, dict)]
    marked = False
    for i, e in enumerate(effs):
        pre = f"effects[{i}]: " if i > 0 else ""
        if e.get("kind") != "apply_modifier":
            continue
        m = e.get("modifier") if isinstance(e.get("modifier"), dict) else {}
        chosen = m.get("ability") == "chosen"
        stat = m.get("stat")
        applies = m.get("applies_to", "target")
        if chosen and stat != "ability_check":
            errs.append(f"{pre}effect modifier ability chosen: ability_check modifiers only")
            continue
        has_scope = isinstance(e.get("scope"), dict) and e["scope"].get("target") == "marked"
        if chosen and not has_scope:
            errs.append(f"{pre}effect modifier ability chosen needs scope target marked")
            continue
        if not has_scope:
            continue
        marked = True
        if stat not in ("damage", "ability_check"):
            # 7.2zi True Strike: attack_roll + on_next_turn + applies_to self. Any other attack_roll mark stays the old error.
            strike = stat == "attack_roll" and e.get("on_next_turn") is True and applies == "self"
            if not strike:
                errs.append(f"{pre}effect scope target marked: damage or ability_check modifiers only")
        elif stat == "damage" and applies != "self":
            errs.append(f"{pre}effect scope target marked: a damage modifier needs applies_to self")
        elif applies == "allies_within_30":
            errs.append(f"{pre}effect scope target marked: applies_to self or target")
    if errs:
        return errs
    du = doc.get("duration") if isinstance(doc.get("duration"), dict) else {}
    conc = du.get("concentration") is True
    if any(e.get("kind") in ("apply_condition", "save") and "ends_on" in e for e in effs) and not conc and du.get("kind") not in SPELL_TIMED_DURATIONS:
        errs.append("effect ends_on needs concentration or a rounds, minutes or hours duration")
    elif "move_mark" in doc:
        if not conc:
            errs.append("move_mark needs a concentration spell")
        elif not marked:
            errs.append("move_mark needs an apply_modifier with scope target marked")
    return errs


# ---- untimed save_condition riders (sim content.cpp compile_rider -> compile_save_action) ----------------------
# Conditions an on_hit / on_event save_condition rider may apply with no duration and no repeat_save: the sim loader's
# kEndsByOwnRules (#133), conditions that end by their own rules and are applied untracked, like Shove. Every other such
# apply[] entry is an unsupported duration, which on a rider is a load error. A save_effect action is not a rider: an
# untimed condition there (prone included) is still listed unsupported by the loader, not a load error, and not checked here.
UNTIMED_ON_HIT_CONDITIONS: frozenset = frozenset({"prone"})


def check_untimed_save_condition_riders(doc: dict, errs: list[str]) -> None:
    """Mirror of the sim loader: a save_condition rider (on_hit[] of an attack action or an on_event effects[] entry)
    whose apply[] entry has no duration and no repeat_save fails to load ("save_condition uses an unsupported duration")."""
    for path, _owner, _j, r in rider_sites(doc):
        if not isinstance(r, dict) or r.get("kind") != "save_condition":
            continue
        for x in r.get("apply") or []:
            if (isinstance(x, dict) and "duration" not in x and "repeat_save" not in x
                    and x.get("condition") not in UNTIMED_ON_HIT_CONDITIONS):
                errs.append(f"{path}: save_condition uses an unsupported duration (apply condition {x.get('condition')} has "
                            "no duration and no repeat_save; the sim fails to load it)")
                break


# ---- size-gated saves (sim content.cpp compile_save_action) ---------------------------------------------------------
def check_save_max_size(doc: dict, errs: list[str]) -> None:
    """Mirror of the sim loader: an apply[] max_size without contain loads only on the first apply[] entry of a
    save_condition rider (a larger target makes no save and gets no condition; SRD Gladiator Shield Bash, "a Medium or
    smaller creature"). On a save_effect action, or a later entry, it is a load error with the loader's wording."""
    def bare(x):
        return isinstance(x, dict) and "max_size" in x and "contain" not in x
    for path, _owner, _j, r in rider_sites(doc):
        if isinstance(r, dict) and r.get("kind") == "save_condition":
            for k, x in enumerate(r.get("apply") or []):
                if k > 0 and bare(x):
                    errs.append(f"{path}/apply/{k}: max_size on a save_condition rider goes on the first apply[] entry")
    for path, a in mechanical_actions(doc):
        if a.get("kind") == "save_effect":
            for k, x in enumerate(a.get("apply") or []):
                if bare(x):
                    errs.append(f"{path}/apply/{k}: max_size on a save_effect needs contain "
                                "(only a containing condition skips a larger target)")


# ---- refused condition durations (sim content.cpp compile_save_action / compile_rider, EC #212) -----------------
# Condition apply[].duration kinds the schema accepts but the sim loader has no rules5e expiry for (docs/CONTENT_EFFECTS.md,
# "Engine Core loader durations and temp HP"). Loaded since #212: rounds, until_end_of_caster_next_turn,
# until_start_of_target_turn (and until_save / until_end_of_target_next_turn where the schema allows them).
REFUSED_CONDITION_DURATIONS: frozenset = frozenset({"until_start_of_caster_turn", "until_end_of_target_turn",
                                                    "concentration", "until_rest"})


def _refused_durations(apply_list):
    for k, x in enumerate(apply_list or []):
        if isinstance(x, dict) and isinstance(x.get("duration"), dict):
            dk = x["duration"].get("kind")
            if dk in REFUSED_CONDITION_DURATIONS:
                yield k, dk


def check_refused_condition_durations(doc: dict, errs: list[str], warns: list[str]) -> None:
    """Mirror of the sim loader for the condition durations it refuses (REFUSED_CONDITION_DURATIONS), with its wording.
    Errors (the whole document fails to load): a save_condition rider ("save_condition uses an unsupported duration"), an apply_condition rider fails
    to load ("unsupported apply_condition duration"), an area_save rider fails to load ("area_save uses an unsupported
    usage or condition duration"). Warning (the document loads, only that action is skipped): a save_effect /
    attack_effect action is listed and never used ("<id> (save_effect: unsupported usage or condition duration)")."""
    for path, _owner, _j, r in rider_sites(doc):
        if not isinstance(r, dict):
            continue
        rk = r.get("kind")
        msg = {"save_condition": "save_condition uses an unsupported duration",
               "apply_condition": "unsupported apply_condition duration",
               "area_save": "area_save uses an unsupported usage or condition duration"}.get(rk)
        if msg is None:
            continue
        for k, dk in _refused_durations(r.get("apply")):
            errs.append(f"{path}: {msg} (apply[{k}] duration '{dk}' has no rules5e expiry; the sim refuses to load this document)")
            break
    for path, a in mechanical_actions(doc):
        ak = a.get("kind")
        if ak not in ("save_effect", "attack_effect"):
            continue
        for k, dk in _refused_durations(a.get("apply")):
            warns.append(f"{path}: {a.get('id', '?')} ({ak}: unsupported usage or condition duration) (apply[{k}] duration "
                         f"'{dk}' has no rules5e expiry; the sim lists the action and never uses it)")
            break


# ---- R-G8 grapple hit rider (sim #104, docs/CONTENT_CONVENTIONS.md section 5) ---------------------------

GRAPPLE_ESCAPE_DC_MAX = 30


def is_grapple(r) -> bool:
    return isinstance(r, dict) and r.get("kind") == "grapple"


def check_grapple_riders(doc: dict, errs: list[str], warns: list[str]) -> None:
    """Mirror of the sim loader (content.cpp, on_hit[] of the archetype's attack actions): only actions[] entries of kind
    melee_weapon_attack / ranged_weapon_attack read on_hit[]; there a grapple rider sets Archetype::grapple_on_hit for that
    attack (no trigger) and escape_dc must be an integer 0..30 (absent = 0 = Athletics contest), else the load error
    'bad grapple escape_dc'. In an on_event effects[] list it reaches compile_rider: load error "unknown rider kind
    'grapple'". bonus_actions[] on_hit riders are never read (warning); two on one attack: the last one wins (warning)."""
    for i, a in enumerate(doc.get("actions") or []):
        if not isinstance(a, dict) or a.get("inert") is True:
            continue
        seen = None
        for j, r in enumerate(a.get("on_hit") or []):
            if not is_grapple(r):
                continue
            where = f"/actions/{i}/on_hit/{j}"
            dc = r.get("escape_dc", 0)
            if not is_int(dc) or not 0 <= dc <= GRAPPLE_ESCAPE_DC_MAX:
                errs.append(f"{where}: grapple escape_dc must be an integer 0..{GRAPPLE_ESCAPE_DC_MAX}, got {dc!r} "
                            "(sim: bad grapple escape_dc) (R-G8 grapple)")
            if seen is not None:
                warns.append(f"{where}: second grapple rider on action '{a.get('id')}' (first at {seen}); the sim keeps only "
                             "the last one (R-G8 grapple)")
            seen = seen or where
    for i, a in enumerate(doc.get("bonus_actions") or []):
        if not isinstance(a, dict) or a.get("inert") is True:
            continue
        for j, r in enumerate(a.get("on_hit") or []):
            if is_grapple(r):
                warns.append(f"/bonus_actions/{i}/on_hit/{j}: the sim reads on_hit[] only on actions[] weapon attacks; this "
                             "grapple rider never applies (R-G8 grapple)")
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") == "on_event":
            for j, r in enumerate(eff.get("effects") or []):
                if is_grapple(r):
                    errs.append(f"{path}/effects/{j}: grapple is an on_hit[] rider of a weapon attack only; in on_event effects "
                                "the sim fails to load (unknown rider kind 'grapple') (R-G8 grapple)")


# ---- item@1 (section 10, checks 15 and 17) ------------------------------------------------------------

def check_item(doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    errs: list[str] = []
    if doc.get("category") == "gear":
        kinds = [h.get("ammo_kind") for h in (doc.get("container") or {}).get("holds") or []]
        dup = ["/container/holds: ammo_kind values must be unique"] if len(kinds) != len(set(kinds)) else []
        return check_item_light(doc) + dup, []
    if doc.get("category") != "weapon":
        return errs, []
    if "ammo_kind" in doc and "ammunition" not in (doc.get("properties") or []):
        errs.append("/ammo_kind: only a weapon with the 'ammunition' property fires ammunition (EQUIPMENT 2.5)")
    rng = doc.get("range")
    if rng is not None and rng[1] < rng[0]:
        errs.append(f"/range: long range {rng[1]} is below normal range {rng[0]} (check 15)")
    props = doc.get("properties") or []
    reach = doc.get("reach")
    if "reach" in props:
        if reach != 10:
            errs.append(f"/reach: must be 10 when properties includes 'reach', got {reach!r} (check 15)")
    elif reach is not None and reach != 5:
        errs.append(f"/reach: {reach} is only allowed with the 'reach' property (10); otherwise 5 or absent (check 15)")
    for j, r in enumerate(doc.get("on_hit") or []):
        if r.get("kind") == "bonus_attack":
            errs.append(f"/on_hit/{j}: bonus_attack riders are rejected in items: they have no actions[] to reference (check 17)")
        elif is_grapple(r):
            errs.append(f"/on_hit/{j}: grapple riders are rejected in items: the sim reads them only from monster@4 / character@4 "
                        "actions[] weapon attacks (check 17, R-G8 grapple)")
    return errs, []


def check_item_light(doc: dict) -> list[str]:
    """item@2 gear light block (EQUIPMENT 6.2): dim is additional beyond bright, so bright + dim <= 120 in every state;
    state ids are unique; a cone needs a non-zero length (bright_ft)."""
    errs: list[str] = []
    lt = doc.get("light")
    if not isinstance(lt, dict):
        return errs
    rows = [("/light", lt)] + [(f"/light/states/{i}", st) for i, st in enumerate(lt.get("states") or [])]
    for where, r in rows:
        if r.get("bright_ft", 0) + r.get("dim_ft", 0) > 120:
            errs.append(f"{where}: bright_ft + dim_ft {r.get('bright_ft', 0) + r.get('dim_ft', 0)} exceeds 120 (dim is additional)")
    if lt.get("shape") == "cone" and lt.get("bright_ft", 0) == 0:
        errs.append("/light: a cone's length is bright_ft, which must be above 0")
    ids = [st.get("id") for st in lt.get("states") or []]
    if len(ids) != len(set(ids)):
        errs.append("/light/states: state ids must be unique")
    return errs


# ---- monster@5 / character@5 structure (EQUIPMENT 3.1, 5.3-5.6) ------------------------------------------

CR_PB_TABLE = ((4, 2), (8, 3), (12, 4), (16, 5), (20, 6), (24, 7), (28, 8), (30, 9))   # EQUIPMENT 5.2


def cr_table_pb(rating: str) -> int:
    cr = int(rating) if "/" not in rating else 0
    return next(pb for top, pb in CR_PB_TABLE if cr <= top)


def _held_slots(entry: dict, item_docs: dict) -> int:
    """Hand slots an equipped entry takes while held (EQUIPMENT 3.1): weapons and shields 1 (a two-handed weapon takes 1 to
    hold), gear its `hands`; armor, valuables, ammunition and items outside this pack's documents 0."""
    it = item_docs.get(entry.get("item"))
    if not isinstance(it, dict) or not entry.get("equipped"):
        return 0
    cat = it.get("category")
    if cat in ("weapon", "shield"):
        return 1
    if cat == "gear":
        return int(it.get("hands", 0))
    return 0


def check_equipment_v5(typ: str, doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    """Structural checks of the @5 equipment keys. The derived-vs-printed assertion (EQUIPMENT 5.6: derived AC / to-hit /
    damage, 'is not needed') needs rules5e::derive_monster (#247) and is not run here."""
    errs: list[str] = []
    warns: list[str] = []
    did = doc.get("id", "")
    item_docs = ctx.get("item_docs", {})
    equipment = [e for e in doc.get("equipment") or [] if isinstance(e, dict)]
    if typ == "character":
        for hand in ("main", "off"):
            n = sum(1 for e in equipment if e.get("hand") == hand)
            if n > 1:
                errs.append(f"{did}: hand '{hand}' holds {n} items")
        eq_ids = {e.get("item") for e in equipment}
        for i, a in enumerate(doc.get("actions") or []):
            if isinstance(a, dict) and isinstance(a.get("weapon"), str):
                if a["weapon"] not in eq_ids:
                    errs.append(f"/actions/{i}/weapon: '{a['weapon']}' is not listed in equipment[]")
                if a.get("kind") not in ("melee_weapon_attack", "ranged_weapon_attack"):
                    errs.append(f"/actions/{i}: weapon / natural / attack_bonus need a melee_weapon_attack or ranged_weapon_attack")
        return errs, warns
    # monster@5
    held = sum(_held_slots(e, item_docs) for e in equipment)
    hands = doc.get("hands")
    if hands is not None and held > hands:
        errs.append(f"{did}: holds {held} items but has {hands} hands")
    ch = doc.get("challenge") or {}
    pb, want = doc.get("proficiency_bonus"), cr_table_pb(str(ch.get("rating", "0")))
    if pb is not None and pb != want and not str(doc.get("proficiency_bonus_reason", "")).strip():
        errs.append(f"{did}: proficiency_bonus {pb} != CR table {want}")
    adj = doc.get("ac_adjust")
    if isinstance(adj, dict):
        if not str(adj.get("reason", "")).strip():
            errs.append(f"{did}: ac_adjust needs a reason")
        warns.append(f"{did}: ac_adjust {adj.get('value')} ({adj.get('reason')})")
    local = [w.get("id") for w in doc.get("weapons") or [] if isinstance(w, dict)]
    if len(local) != len(set(local)):
        errs.append("/weapons: local weapon ids must be unique")
    eq_ids = {e.get("item") for e in equipment}
    for i, a in enumerate(doc.get("actions") or []):
        if not isinstance(a, dict):
            continue
        aid, where = a.get("id"), f"/actions/{i}"
        if "weapon" in a and "natural" in a:
            errs.append(f"{where}: 'weapon' and 'natural' are exclusive")
        if any(k in a for k in ("weapon", "natural", "attack_bonus", "damage_override")) and a.get("kind") not in ("melee_weapon_attack", "ranged_weapon_attack"):
            errs.append(f"{where}: weapon / natural / attack_bonus need a melee_weapon_attack or ranged_weapon_attack")
        nat = a.get("natural")
        if isinstance(nat, dict) and "flat" in nat:   # CONTENT_CONVENTIONS 19: natural.flat is the printed flat damage[0]
            d0 = (a.get("damage") or [{}])[0]
            got = d0.get("flat") if isinstance(d0, dict) else None
            if got != nat["flat"]:
                errs.append(f"{where}: natural.flat {nat['flat']} != damage[0] flat {got if got is not None else 'none'}")
        w = a.get("weapon")
        if isinstance(w, str):
            if w.startswith("#"):
                if w[1:] not in local:
                    errs.append(f"{where}/weapon: '{w}' is not a weapons[] id of this document")
            elif w not in eq_ids:
                errs.append(f"{where}/weapon: '{w}' is not listed in equipment[]")
        ab = a.get("attack_bonus")
        if isinstance(ab, dict):
            if not str(ab.get("reason", "")).strip():
                errs.append(f"{did}.{aid}: attack_bonus needs a reason")
            warns.append(f"{did}.{aid}: attack_bonus {ab.get('value')} ({ab.get('reason')})")
        dov = a.get("damage_override")
        if isinstance(dov, dict):
            if not str(dov.get("reason", "")).strip():
                errs.append(f"{did}.{aid}: damage_override needs a reason")
            warns.append(f"{did}.{aid}: damage_override {dov.get('dice')} ({dov.get('reason')})")
    el = doc.get("emits_light")
    if isinstance(el, dict) and el.get("bright_ft", 0) + el.get("dim_ft", 0) > 120:
        errs.append(f"/emits_light: bright_ft + dim_ft {el.get('bright_ft', 0) + el.get('dim_ft', 0)} exceeds 120 (dim is additional)")
    return errs, warns


# ---- spell@1 additive apply_modifier (check 18) --------------------------------------------------------

def check_spell_modifiers(doc: dict) -> list[str]:
    """modifier.bonus_table is forbidden in spell@1's additive apply_modifier effect: tables resolve from
    class level, which spells do not have (check 18). The spell@1 schema does not declare apply_modifier
    yet (row 12), so today the schema rejects the kind first; this check is exercised directly in tests
    and is wired into check_spell so it holds the day the kind lands."""
    errs = []
    for i, eff in enumerate(doc.get("effects") or []):
        if not isinstance(eff, dict) or eff.get("kind") != "apply_modifier":
            continue
        mods = ([eff["modifier"]] if isinstance(eff.get("modifier"), dict) else []) + list(eff.get("modifiers") or [])
        for m in mods:
            if isinstance(m, dict) and "bonus_table" in m:
                errs.append(f"effects[{i}]: modifier.bonus_table is forbidden in spell@1 apply_modifier (check 18)")
    return errs


# ---- race -> darkvision (character@2+; EC #236 reads senses.darkvision into Combatant::darkvision_ft) -----------------
# SRD 5.1 races. Darkvision 60 ft: dwarf (hill), elf (high), gnome (rock), half-elf, half-orc, tiefling.
# No darkvision: human, halfling (lightfoot), dragonborn. Other race strings are not checked.
RACE_DARKVISION = {r: 60 for r in ("dwarf", "hill_dwarf", "elf", "high_elf", "gnome", "rock_gnome", "half_elf", "half_orc", "tiefling")}
RACE_DARKVISION.update({r: 0 for r in ("human", "halfling", "lightfoot_halfling", "dragonborn")})


def check_race_senses(doc: dict) -> list[str]:
    """character@2+ (stat block with senses): a race with SRD Darkvision needs senses.darkvision 60; a race without it
    must not declare darkvision."""
    race, senses = doc.get("race"), doc.get("senses")
    if not isinstance(race, str) or not isinstance(senses, dict) or race not in RACE_DARKVISION:
        return []
    want, have = RACE_DARKVISION[race], senses.get("darkvision")
    if want and have != want:
        return [f"race '{race}': senses.darkvision must be {want} (SRD racial Darkvision), got {have}"]
    if not want and have is not None:
        return [f"race '{race}': senses.darkvision {have} not allowed (the race has no Darkvision in SRD 5.1)"]
    return []



# ---- monster@5 derive: true (EQUIPMENT 5.6; mirror of rules5e::derive_monster, engine/rules/src/equipment.cpp) ------------
# Same checks, same order (PB, hands, AC, attacks), same strings. Weapon dice parse as NdM[+-K] or a bare flat integer.

_DICE_RE = re.compile(r"^(\d+)d(\d+)([+-]\d+)?$")


def parse_dice(s) -> tuple[int, int, int]:
    s = str(s).replace(" ", "")
    m = _DICE_RE.match(s)
    if m:
        return int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)
    return 0, 0, int(s)


def format_dice(d: tuple[int, int, int]) -> str:
    n, sides, k = d
    if n == 0:
        return str(k)
    return f"{n}d{sides}" + (f"+{k}" if k > 0 else (str(k) if k < 0 else ""))


def _mod(score: int) -> int:
    return (score - 10) // 2


def _blank(r) -> bool:
    return not isinstance(r, str) or r.strip(" \t\n\r\f\v") == ""


def _cr_fraction(rating: str) -> tuple[int, int]:
    if "/" in rating:
        a, b = rating.split("/")
        return int(a), int(b)
    return int(rating), 1


def proficiency_for_cr(num: int, den: int) -> int:
    den = den if den > 0 else 1
    cr = (num + den - 1) // den
    if cr <= 4:
        return 2
    if cr >= 29:
        return 9
    return 2 + (cr - 1) // 4


def _weapon_def(src: dict, natural: bool = False) -> dict:
    """item@N weapon / monster-local weapon -> the WeaponDef fields derive_weapon_attack reads."""
    def first(key):
        d = (src.get(key) or [None])[0]
        if not isinstance(d, dict):
            return (0, 0, 0)
        return parse_dice(d["dice"]) if "dice" in d else (0, 0, int(d.get("flat", 0)))
    rng = src.get("range") or [0, 0]
    return {"props": set(src.get("properties") or []), "reach": src.get("reach", 0 if "range" in src else 5),
            "range": rng[0], "dice": first("damage"), "vdice": first("versatile_damage"),
            "magic": int(src.get("magic_bonus", 0)), "atk_magic": src.get("attack_magic"), "dmg_magic": src.get("damage_magic"),
            "improvised": bool(src.get("improvised")), "natural": natural}


def _derive_weapon_attack(w: dict, v: dict, use: str) -> tuple[int, tuple[int, int, int], str]:
    """(to_hit, damage dice, error) of rules5e::derive_weapon_attack."""
    pure_ranged = w["reach"] == 0 and w["range"] > 0
    props = w["props"]
    if use == "two_hands" and "versatile" not in props and "two_handed" not in props:
        return 0, (0, 0, 0), "weapon: not a versatile weapon"
    if use in ("one_hand", "off_hand") and "two_handed" in props and not pure_ranged:
        return 0, (0, 0, 0), "weapon: a two-handed weapon needs both hands"
    if use == "thrown" and "thrown" not in props and not w["improvised"]:
        return 0, (0, 0, 0), "weapon: not a thrown weapon"
    if use == "off_hand" and ("light" not in props or pure_ranged):
        return 0, (0, 0, 0), "weapon: the off-hand weapon must be a light melee weapon"
    melee_use = use != "thrown" and not pure_ranged
    ab = v["abilities"]
    if v.get("ability"):
        abil = v["ability"]
    elif "finesse" in props:
        abil = "dex" if _mod(ab["dex"]) > _mod(ab["str"]) else "str"
    elif pure_ranged:
        abil = "dex"
    else:
        abil = "str"
    mod = _mod(ab[abil])
    atk_magic = w["atk_magic"] if w["atk_magic"] is not None else w["magic"]
    dmg_magic = w["dmg_magic"] if w["dmg_magic"] is not None else w["magic"]
    proficient = not w["improvised"]
    to_hit = mod + (v["pb"] if proficient else 0) + atk_magic
    if v.get("to_hit_override") is not None:
        to_hit = v["to_hit_override"]
    n, sides, k = w["vdice"] if (use == "two_hands" and w["vdice"][0] > 0) else w["dice"]
    if n > 0 and not w["natural"]:
        n = n * max(1, v["mult"])
        if melee_use and v["brute"] > 0:
            n += v["brute"]
        n = max(0, min(255, n))
    ab_term = 0 if v.get("damage_ability_none") else mod
    if use == "off_hand":
        ab_term = min(0, ab_term)
    return max(-128, min(127, to_hit)), (n, sides, k + ab_term + dmg_magic), ""


def _signed(v: int) -> str:
    return f"+{v}" if v >= 0 else str(v)


def check_monster_derive(doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    """monster@5 with derive: true: derived AC / to-hit / damage[0] must equal the printed numbers (EQUIPMENT 5.6). Errors are
    rules5e::derive_monster's strings except the PB and hands checks, which check_equipment_v5 already reports (same strings).
    Item references that do not resolve are reported by check 6/11 and skipped here."""
    errs: list[str] = []
    if doc.get("derive") is not True:
        return errs, []
    did = doc.get("id", "")
    items = ctx.get("item_docs", {})
    ab = doc["abilities"]
    num, den = _cr_fraction(str(doc["challenge"]["rating"]))
    pb = doc.get("proficiency_bonus", proficiency_for_cr(num, den))
    dex = _mod(ab["dex"])
    armor = shield = None
    for e in doc.get("equipment") or []:
        it = items.get(e.get("item")) if isinstance(e, dict) and e.get("equipped") else None
        if isinstance(it, dict) and it.get("category") == "armor" and armor is None:
            armor = it
        elif isinstance(it, dict) and it.get("category") == "shield" and shield is None:
            shield = it
    na = doc.get("natural_armor")

    def ac_of(use_natural: bool, adjust: int) -> int:
        if armor is not None:
            dt = 0
            if armor.get("dex_bonus"):
                dt = min(dex, armor["dex_cap"]) if "dex_cap" in armor else dex
            ac = armor["base_ac"] + dt + armor.get("magic_bonus", 0)
        else:
            ac = 10 + dex
            if use_natural and na is not None:
                ac = max(ac, na["base"] + (dex if na.get("dex", True) else 0))
        if shield is not None:
            ac += shield.get("ac_bonus", 2) + shield.get("magic_bonus", 0)
        return max(0, min(100, ac + adjust))

    printed_ac = doc["armor_class"]["value"]
    adj = doc.get("ac_adjust")
    without = ac_of(True, 0)
    ac = ac_of(True, adj["value"] if adj else 0)
    if adj and _blank(adj.get("reason")):
        errs.append(f"{did}: ac_adjust needs a reason")
    if adj and without == printed_ac:
        errs.append(f"{did}: ac_adjust is not needed")
    if na is not None and (armor is not None or ac_of(False, 0) == ac_of(True, 0)):
        errs.append(f"{did}: natural_armor is not needed")
    if ac != printed_ac:
        errs.append(f"{did}: derived AC {ac} != printed {printed_ac}")

    mult = int(doc.get("weapon_dice_multiplier", 1))
    brute = sum(int(t["effect"].get("dice", 0)) for t in doc.get("traits") or []
                if isinstance(t, dict) and isinstance(t.get("effect"), dict) and t["effect"].get("kind") == "weapon_extra_dice")
    local = {w["id"]: w for w in doc.get("weapons") or [] if isinstance(w, dict)}
    used = False
    for a in doc.get("actions") or []:
        if not isinstance(a, dict) or not ("weapon" in a or "natural" in a):
            continue
        pre = f"{did}.{a.get('id')}"
        if "natural" in a:
            nat = a["natural"]
            w = {"props": set(), "reach": a.get("reach", 0 if a.get("range") else 5), "range": (a.get("range") or [0])[0],
                 "dice": parse_dice(nat["dice"]) if "dice" in nat else (0, 0, int(nat.get("flat", 0))), "vdice": (0, 0, 0), "magic": 0, "atk_magic": None, "dmg_magic": None,
                 "improvised": False, "natural": True}
            v = {"abilities": ab, "pb": pb, "mult": mult, "brute": brute, "ability": nat["ability"],
                 "damage_ability_none": nat.get("damage_ability") == "none" or "flat" in nat}   # flat: the whole amount
        else:
            ref = a["weapon"]
            src = local.get(ref[1:]) if ref.startswith("#") else items.get(ref)
            if not isinstance(src, dict):
                continue
            w = _weapon_def(src)
            v = {"abilities": ab, "pb": pb, "mult": mult, "brute": brute}
        use = a.get("use", "one_hand")
        th, dmg, err = _derive_weapon_attack(w, v, use)
        if err:
            errs.append(f"{pre}: {err}")
            continue
        ab_ov = a.get("attack_bonus")
        if ab_ov:
            if _blank(ab_ov.get("reason")):
                errs.append(f"{pre}: attack_bonus needs a reason")
            if th == a.get("to_hit"):
                errs.append(f"{pre}: attack_bonus is not needed")
            v = dict(v, to_hit_override=ab_ov["value"])
            th, dmg, _e = _derive_weapon_attack(w, v, use)
        dov = a.get("damage_override")
        if isinstance(dov, dict):   # monster@5: the printed damage[0] stands, with a reason (CONTENT_CONVENTIONS 21)
            if _blank(dov.get("reason")):
                errs.append(f"{pre}: damage_override needs a reason")
            ov = parse_dice(str(dov.get("dice", "0")))
            if dmg == ov:
                errs.append(f"{pre}: damage_override is not needed")
            dmg = ov
        if th != a.get("to_hit"):
            errs.append(f"{pre}: derived to-hit {_signed(th)} != printed {_signed(a.get('to_hit', 0))}")
        printed = parse_dice((a.get("damage") or [{}])[0].get("dice", "0")) if (a.get("damage") or [{}])[0].get("dice") is not None \
            else (0, 0, int((a.get("damage") or [{}])[0].get("flat", 0)))
        if not w["natural"] and mult > 1:
            if _derive_weapon_attack(w, dict(v, mult=1), use)[1] != printed:
                used = True
        if dmg != printed:
            e = f"{pre}: derived damage {format_dice(dmg)} != printed {format_dice(printed)}"
            if not w["natural"]:
                for k in (2, 3, 4):
                    if _derive_weapon_attack(w, dict(v, mult=k), use)[1] == printed:
                        e += f" (weapon_dice_multiplier {k} would match)"
                        break
            errs.append(e)
    if mult > 1 and not used:
        errs.append(f"{did}: weapon_dice_multiplier is not needed")
    return errs, []


# ---- character@5 proficiencies and derive: true (EQUIPMENT 1.2, 2.1; T&C PR C) --------------------------------------------
# SRD 5.1 class / race weapon and armor proficiencies, from 5e-database a6212be (tests/validate/data/
# srd51_proficiencies_5e_database_a6212be.json; test_equip_schemas_v5 checks this table against it). Named weapons are item ids.

def _w(*names: str) -> frozenset:
    return frozenset(n if n in ("simple", "martial") else f"srd51.item.{n}" for n in names)


SRD_CLASS_PROFICIENCIES = {   # class: (weapon_proficiencies, armor_proficiencies)
    "barbarian": (_w("simple", "martial"), frozenset({"light", "medium", "shields"})),
    "bard": (_w("simple", "longsword", "rapier", "shortsword", "crossbow_hand"), frozenset({"light"})),
    "cleric": (_w("simple"), frozenset({"light", "medium", "shields"})),
    "druid": (_w("club", "dagger", "javelin", "mace", "quarterstaff", "sickle", "spear", "dart", "sling", "scimitar"),
              frozenset({"light", "medium", "shields"})),
    "fighter": (_w("simple", "martial"), frozenset({"light", "medium", "heavy", "shields"})),
    "monk": (_w("simple", "shortsword"), frozenset()),
    "paladin": (_w("simple", "martial"), frozenset({"light", "medium", "heavy", "shields"})),
    "ranger": (_w("simple", "martial"), frozenset({"light", "medium", "shields"})),
    "rogue": (_w("simple", "longsword", "rapier", "shortsword", "crossbow_hand"), frozenset({"light"})),
    "sorcerer": (_w("dagger", "dart", "sling", "quarterstaff", "crossbow_light"), frozenset()),
    "warlock": (_w("simple"), frozenset({"light"})),
    "wizard": (_w("dagger", "dart", "sling", "quarterstaff", "crossbow_light"), frozenset()),
}
SRD_RACE_WEAPON_PROFICIENCIES = {   # Dwarven Combat Training (dwarf), Elf Weapon Training (high elf)
    "dwarf": _w("battleaxe", "handaxe", "light_hammer", "warhammer"),
    "hill_dwarf": _w("battleaxe", "handaxe", "light_hammer", "warhammer"),
    "high_elf": _w("longsword", "shortsword", "shortbow", "longbow"),
}
# Feature-granted AC base candidates (EQUIPMENT 1.2 step 1; rules5e::AcBaseCandidate): (class, feature id) ->
# (fixed, ability or None, forbids_shield). Armor of Shadows is at-will Mage Armor, counted as in force (the printed AC assumes it).
FEATURE_AC_BASES = {
    ("barbarian", "unarmored_defense"): (10, "con", False),
    ("monk", "unarmored_defense"): (10, "wis", True),
    ("sorcerer", "draconic_resilience"): (13, None, False),
    ("warlock", "armor_of_shadows"): (13, None, False),
}


def expected_proficiencies(doc: dict) -> tuple[set, set]:
    weapons, armor = set(), set()
    for c in doc.get("classes") or []:
        w, a = SRD_CLASS_PROFICIENCIES.get(c.get("class"), (frozenset(), frozenset()))
        weapons |= w
        armor |= a
    weapons |= SRD_RACE_WEAPON_PROFICIENCIES.get(doc.get("race"), frozenset())
    return weapons, armor


def _covered(entry: str, pool: set, items: dict) -> bool:
    if entry in pool:
        return True
    it = items.get(entry)
    return isinstance(it, dict) and it.get("weapon_class") in pool


def check_character_proficiencies(doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    """character@5: authored weapon_proficiencies / armor_proficiencies against the SRD class and race grants. Missing is an
    error, extra is a warning. A named weapon counts as covered by its weapon_class ('simple' / 'martial')."""
    errs: list[str] = []
    warns: list[str] = []
    if doc.get("derive") is not True and "weapon_proficiencies" not in doc and "armor_proficiencies" not in doc:
        return errs, warns
    did = doc.get("id", "")
    items = ctx.get("item_docs", {})
    want_w, want_a = expected_proficiencies(doc)
    have_w, have_a = set(doc.get("weapon_proficiencies") or []), set(doc.get("armor_proficiencies") or [])
    for key, want, have in (("weapon_proficiencies", want_w, have_w), ("armor_proficiencies", want_a, have_a)):
        for e in sorted(want):
            if not _covered(e, have, items):
                errs.append(f"{did}: {key} missing '{e}' (SRD 5.1 class / race)")
        for e in sorted(have):
            if not _covered(e, want, items):
                warns.append(f"{did}: {key} '{e}' is not granted by the SRD 5.1 class / race")
    return errs, warns


def _weapon_proficient(doc: dict, item_id: str, items: dict) -> bool:
    return _covered(item_id, set(doc.get("weapon_proficiencies") or []), items)


def check_character_derive(doc: dict, ctx: dict) -> tuple[list[str], list[str]]:
    """character@5 with derive: true: AC from equipped armor / shield / feature bases, and each action with a `weapon` from
    the item, ability, PB (when proficient) and magic, must equal the printed numbers. Feature rows (Defense, Archery,
    Dueling, ...) are applied by the engine on top of the printed numbers, so they are not part of the derivation. Strings
    follow rules5e::derive_monster's 5.6 forms."""
    errs: list[str] = []
    if doc.get("derive") is not True:
        return errs, []
    did = doc.get("id", "")
    items = ctx.get("item_docs", {})
    ab = doc["abilities"]
    dex = _mod(ab["dex"])
    armor = shield = None
    for e in doc.get("equipment") or []:
        it = items.get(e.get("item")) if isinstance(e, dict) and e.get("equipped") else None
        if isinstance(it, dict) and it.get("category") == "armor" and armor is None:
            armor = it
        elif isinstance(it, dict) and it.get("category") == "shield" and shield is None:
            shield = it
    if armor is not None:
        dt = (min(dex, armor["dex_cap"]) if "dex_cap" in armor else dex) if armor.get("dex_bonus") else 0
        ac = armor["base_ac"] + dt + armor.get("magic_bonus", 0)
    else:
        ac = 10 + dex
        classes = {c.get("class") for c in doc.get("classes") or []}
        for f in doc.get("features") or []:
            for cls in classes:
                base = FEATURE_AC_BASES.get((cls, f.get("id")))
                if base and not (base[2] and shield is not None):
                    ac = max(ac, base[0] + dex + (_mod(ab[base[1]]) if base[1] else 0))
    if shield is not None:
        ac += shield.get("ac_bonus", 2) + shield.get("magic_bonus", 0)
    printed_ac = doc["armor_class"]["value"]
    if ac != printed_ac:
        errs.append(f"{did}: derived AC {ac} != printed {printed_ac}")
    pb = doc["proficiency_bonus"]
    monk = any(c.get("class") == "monk" for c in doc.get("classes") or [])
    for a in doc.get("actions") or []:
        if not isinstance(a, dict) or "weapon" not in a:
            continue
        src = items.get(a["weapon"])
        if not isinstance(src, dict):
            continue
        w = _weapon_def(src)
        prof = _weapon_proficient(doc, a["weapon"], items)
        v = {"abilities": ab, "pb": pb if prof else 0, "mult": 1, "brute": 0}
        # Monk Martial Arts: Dex instead of Str with unarmed strikes and monk weapons (shortswords, simple melee weapons
        # without two-handed or heavy); the higher one is used.
        if monk and (a["weapon"] == "srd51.item.shortsword" or (src.get("weapon_class") == "simple" and "range" not in src
                     and not ({"two_handed", "heavy"} & w["props"]))):
            v["ability"] = "dex" if _mod(ab["dex"]) > _mod(ab["str"]) else "str"
        pre = f"{did}.{a.get('id')}"
        th, dmg, err = _derive_weapon_attack(w, v, a.get("use", "one_hand"))
        if err:
            errs.append(f"{pre}: {err}")
            continue
        if th != a.get("to_hit"):
            errs.append(f"{pre}: derived to-hit {_signed(th)} != printed {_signed(a.get('to_hit', 0))}")
        d0 = (a.get("damage") or [{}])[0]
        printed = parse_dice(d0["dice"]) if "dice" in d0 else (0, 0, int(d0.get("flat", 0)))
        if dmg != printed:
            errs.append(f"{pre}: derived damage {format_dice(dmg)} != printed {format_dice(printed)}")
    return errs, []


# ---- VERSION_BUMP_BATCH 7.2zg: the spell@1 key names of this batch, in one place (rules5e_content spell_keys_zg.hpp) ----------
ZG_KEYS = {"haste": "haste", "slow": "slow", "summon_weapon": "summon_weapon", "temp_hp": "temp_hp", "dice": "dice",
           "per_slot_above": "per_slot_above", "ability": "ability", "per_turn": "per_turn",
           "condition_immunity": "condition_immunity", "on_miss": "on_miss", "damaged": "damaged",
           "damage_type_choices": "damage_type_choices", "exempt_senses": "exempt_senses", "outlined": "outlined",
           "shake_awake": "shake_awake"}


def check_spell_batch_zg(doc: dict) -> list[str]:
    """7.2zg spell@1 cross-field rules (spell_loader.cpp, same wording and order)."""
    errs: list[str] = []
    effs = [e for e in (doc.get("effects") or []) if isinstance(e, dict)]
    if not effs:
        return errs
    e = effs[0]
    du = doc.get("duration") if isinstance(doc.get("duration"), dict) else {}
    if e.get("kind") in (ZG_KEYS["haste"], ZG_KEYS["slow"]) and du.get("concentration") is not True:
        errs.append(f"effect {e['kind']} needs concentration")
    timed = du.get("kind") in SPELL_TIMED_DURATIONS
    if e.get("kind") == ZG_KEYS["summon_weapon"] and not (du.get("concentration") is True or timed):
        errs.append("effect summon_weapon needs concentration or a rounds, minutes or hours duration")
    if (e.get("kind") == ZG_KEYS["temp_hp"] and (e.get(ZG_KEYS["per_turn"]) or e.get(ZG_KEYS["condition_immunity"]))
            and not (du.get("concentration") is True or timed)):
        errs.append("effect temp_hp per_turn or condition_immunity needs concentration or a rounds, minutes or hours duration")
    if any(x.get("kind") == "save" and "ends_on" in x and "condition" not in x for x in effs):   # Hypnotic Pattern
        errs.append("effect ends_on on a save effect needs a condition")
    es = ZG_KEYS["exempt_senses"]   # Blur (apply_modifier); Mirror Image (duplicates) takes the same key and list
    for x in effs:
        if es in x and x.get("kind") == "apply_modifier":
            mod = x.get("modifier") if isinstance(x.get("modifier"), dict) else {}
            if mod.get("stat") != "attacks_against":
                errs.append("effect exempt_senses needs an attacks_against modifier")
                break
    dtc = ZG_KEYS["damage_type_choices"]   # Protection from Energy
    if dtc in e:
        mod = e.get("modifier") if isinstance(e.get("modifier"), dict) else {}
        if mod.get("stat") != "resistance":
            errs.append("effect damage_type_choices needs a resistance modifier")
        elif mod.get("damage_type") not in e[dtc]:
            errs.append("effect damage_type_choices must include the modifier damage_type")
    for i, x in enumerate(effs[1:], 1):
        if dtc in x:
            errs.append(f"effects[{i}]: damage_type_choices is only allowed on the first effect")
            break
    return errs


# ---- VERSION_BUMP_BATCH 7.2zh: the spell@1 key names of this batch, in one place (rules5e_content spell_keys_zh.hpp) ----------
ZH_KEYS = {"weapon_smite": "weapon_smite", "reveal_invisible": "reveal_invisible", "light": "light", "dim_ft": "dim_ft",
           "bright_ft": "bright_ft", "held_object": "held_object", "held": "held", "save": "save", "on_fail": "on_fail",
           "while_held": "while_held", "disadvantage": "disadvantage", "until": "until"}

_HELD_OBJECT_SHAPE = ("effect held_object must be {save: con, on_fail: drop, while_held: "
                      "{disadvantage: [attack_rolls, ability_checks], until: start_of_caster_next_turn}}")


def _feet5(n) -> bool:
    return isinstance(n, int) and not isinstance(n, bool) and n >= 5 and n % 5 == 0


def _held_object_shape(ho) -> bool:
    if not isinstance(ho, dict) or set(ho) != {"save", "on_fail", "while_held"}:
        return False
    if ho.get("save") != "con" or ho.get("on_fail") != "drop":
        return False
    wh = ho.get("while_held")
    if not isinstance(wh, dict) or set(wh) != {"disadvantage", "until"}:
        return False
    if wh.get("until") != "start_of_caster_next_turn":
        return False
    d = wh.get("disadvantage")
    return isinstance(d, list) and sorted(d) == ["ability_checks", "attack_rolls"]


def check_spell_batch_zh(doc: dict) -> list[str]:
    """7.2zh spell@1 cross-field rules (spell_loader.cpp, same wording and order)."""
    errs: list[str] = []
    effs = [x for x in (doc.get("effects") or []) if isinstance(x, dict)]
    if not effs:
        return errs
    e = effs[0]
    du = doc.get("duration") if isinstance(doc.get("duration"), dict) else {}
    timed = du.get("kind") in SPELL_TIMED_DURATIONS
    conc = du.get("concentration") is True
    if e.get("kind") == ZH_KEYS["weapon_smite"] and not (conc or timed):
        errs.append("weapon_smite: needs concentration or a rounds, minutes or hours duration")
    light = e.get(ZH_KEYS["light"]) if e.get("kind") == ZH_KEYS["weapon_smite"] else None
    if isinstance(light, dict) and not _feet5(light.get(ZH_KEYS["dim_ft"])):
        errs.append("effect weapon_smite light.dim_ft must be a positive multiple of 5")
    if e.get("kind") == "summon_weapon" and e.get(ZH_KEYS["held"]) is True:
        if "move_ft" in e:
            errs.append("summon_weapon: held needs no move_ft")
        if e.get("add_spellcasting_modifier") is True:
            errs.append("summon_weapon: held takes no add_spellcasting_modifier")
    if e.get("kind") == "summon_weapon" and isinstance(e.get(ZH_KEYS["light"]), dict):
        bright = e[ZH_KEYS["light"]]
        if not _feet5(bright.get(ZH_KEYS["bright_ft"])) or not _feet5(bright.get(ZH_KEYS["dim_ft"])):
            errs.append("effect summon_weapon light.bright_ft and dim_ft must be positive multiples of 5")
    if e.get("kind") == "sustained_damage" and ZH_KEYS["held_object"] in e and not _held_object_shape(e[ZH_KEYS["held_object"]]):
        errs.append(_HELD_OBJECT_SHAPE)
    return errs


# ---- VERSION_BUMP_BATCH 7.2zi: the spell@1 key names of this batch, in one place (rules5e_content spell_keys_zi.hpp) ----------
ZI_KEYS = {"stabilize": "stabilize", "hit_only": "hit_only", "on_next_turn": "on_next_turn", "bonus_dash": "bonus_dash",
           "dash_on_cast": "dash_on_cast",
           "save_vs_conditions": "save_vs_conditions", "halve_weapon_damage": "halve_weapon_damage",
           "death_saves": "death_saves", "heal_maximum": "heal_maximum", "share_damage": "share_damage",
           "all_damage": "all_damage", "end_beyond_ft": "end_beyond_ft", "end_at_zero": "end_at_zero", "caster": "caster"}


def check_spell_batch_zi(doc: dict) -> list[str]:
    """7.2zi spell@1 cross-field rules (spell_loader.cpp, same wording). Extra effects are prefixed effects[i]:."""
    errs: list[str] = []
    effs = [x for x in (doc.get("effects") or []) if isinstance(x, dict)]
    for i, e in enumerate(effs):
        pre = f"effects[{i}]: " if i else ""
        kind = e.get("kind")
        if kind == "spell_attack" and ZI_KEYS["hit_only"] in e:
            if e[ZI_KEYS["hit_only"]] is not True:
                errs.append(f"{pre}effect hit_only must be true")
            elif "damage" in e:
                errs.append(f"{pre}effect hit_only and damage together")
        if kind != "apply_modifier":
            continue
        m = e.get("modifier") if isinstance(e.get("modifier"), dict) else {}
        if ZI_KEYS["on_next_turn"] in e:
            if e[ZI_KEYS["on_next_turn"]] is not True:
                errs.append(f"{pre}effect on_next_turn must be true")
            elif m.get("stat") != "attack_roll":
                errs.append(f"{pre}effect on_next_turn needs an attack_roll modifier")
        if ZI_KEYS["dash_on_cast"] in e:
            applies = m.get("applies_to", "target")
            if e[ZI_KEYS["dash_on_cast"]] is not True:
                errs.append(f"{pre}effect dash_on_cast must be true")
            elif m.get("stat") != ZI_KEYS["bonus_dash"] or applies != "self":
                errs.append(f"{pre}effect dash_on_cast needs a bonus_dash modifier on self")
        if ZI_KEYS["save_vs_conditions"] in e:
            cv = e[ZI_KEYS["save_vs_conditions"]]
            if m.get("stat") != "save" or "mode" not in m:
                errs.append(f"{pre}effect save_vs_conditions needs a save modifier with a mode")
            elif not isinstance(cv, list) or not cv:
                errs.append(f"{pre}effect save_vs_conditions must be a non-empty list of conditions")
            else:
                seen: set[str] = set()
                for c in cv:
                    if not isinstance(c, str) or c not in GAZE_CONDITIONS:
                        errs.append(f"{pre}effect save_vs_conditions must be a non-empty list of conditions")
                        break
                    if c in seen:
                        errs.append(f"{pre}effect save_vs_conditions items must be unique")
                        break
                    seen.add(c)
        if ZI_KEYS["death_saves"] in e and e[ZI_KEYS["death_saves"]] is not True:
            errs.append(f"{pre}effect death_saves must be true")
        if ZI_KEYS["heal_maximum"] in e and e[ZI_KEYS["heal_maximum"]] is not True:
            errs.append(f"{pre}effect heal_maximum must be true")
        if ZI_KEYS["end_beyond_ft"] in e:
            ft = e[ZI_KEYS["end_beyond_ft"]]
            if not _feet5(ft) or ft > 65535:
                errs.append(f"{pre}effect end_beyond_ft must be a positive multiple of 5")
        if ZI_KEYS["end_at_zero"] in e and e[ZI_KEYS["end_at_zero"]] != ZI_KEYS["caster"]:
            errs.append(f"{pre}effect end_at_zero must be caster")
        if "repeat_save" in e:
            rs = e["repeat_save"]
            if not isinstance(rs, dict) or set(rs) != {"ability"} or rs.get("ability") not in GAZE_ABILITIES:
                errs.append(f"{pre}effect repeat_save must be {{ability: <ability>}}")
        if "all_damage" in m:
            if m.get("stat") != "resistance" or m.get("all_damage") is not True:
                errs.append(f"{pre}effect modifier all_damage must be true on a resistance modifier")
            elif "damage_type" in m:
                errs.append(f"{pre}effect modifier all_damage and damage_type together")
        if m.get("stat") == ZI_KEYS["halve_weapon_damage"] and m.get("ability") != "str":
            errs.append(f"{pre}effect halve_weapon_damage needs ability str")
    return errs


def check_spell_descriptive(doc: dict) -> list[str]:
    """Text-only spell@1 kind descriptive (spell_loader.cpp, same wording and order).

    The effect object allows no key but kind. It must be the spell's only effect. An empty effects
    list is still the schema's error (minItems 1; the loader says "effects missing") and is not
    re-reported here.
    """
    raw = doc.get("effects")
    if not isinstance(raw, list) or not raw:
        return []
    first = raw[0]
    if isinstance(first, dict) and first.get("kind") == "descriptive":
        for k in first:
            if k != "kind":
                return [f"descriptive effect: unknown key '{k}'"]
    if any(isinstance(e, dict) and e.get("kind") == "descriptive" for e in raw) and len(raw) != 1:
        return ["descriptive effect must be the spell's only effect"]
    return []
