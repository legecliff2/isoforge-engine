"""Per-tag table and cross-reference checks for the @4 documents (monster@4, character@4, item@1).

docs/proposals/VERSION_BUMP_BATCH.md section 1.2 (per-tag table, "no ad hoc `if version >= 4` branches")
and section 8.3 (validator contract). Numbers in messages ("check N") are the numbers of the section 8.3
list. The checks assume the document already passed its JSON Schema (the caller runs them only then), so
shapes are trusted; every walker still tolerates absent optional keys.

Everything is integer math; documents are plain json.load results.
"""
from __future__ import annotations

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
                                   "metamagic", "sculpt_spells"})
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
    TAG_TABLE[f"{_typ}@4"] = TagRow(True, V2_ACTION_KINDS, _OPEN | NATIVE_EFFECT_KINDS_AT_4 | STATIC_EFFECT_KINDS_R_G5,
                                    EFFECT_COST_V2, _keys | _AT4_KEYS, cross_refs=True, needs_api_level=2,
                                    legacy_stamps=True)
TAG_TABLE["item@1"] = TagRow(False, frozenset(), frozenset(), frozenset(), frozenset(), cross_refs=True,
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
        if eff.get("kind") == "heal_pool":
            need_pool(path, eff.get("pool"), None)
        if eff.get("kind") == "wild_shape":
            referenced.add(eff.get("pool"))   # R-G12 uses pool: resolved by check_transforms
        if eff.get("kind") == "metamagic":
            referenced.add(eff.get("pool"))   # sorcery points: spent by the sim per option (check 12); resolved by check_metamagic
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
    check_transforms(doc, ctx, errs, warns)
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


def check_metamagic(doc: dict) -> tuple[list[str], list[str]]:
    """R-G14 (sim #143, features.cpp apply_metamagic / apply_sculpt_spells), for documents whose effects are active.
    metamagic {options, pool}: the options shape is the schema arm (non-empty, METAMAGIC_OPTIONS names); the pool must be a
    limited resources[] pool (load error, the loader's wording); several declarations OR their options but must name the
    same pool (load error); an option listed twice adds nothing (warning). sculpt_spells {}: no fields (others ignored: warning)."""
    errs: list[str] = []
    warns: list[str] = []
    limited = limited_pool_ids(doc)
    first = None
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
        elif kind == "sculpt_spells":
            extra = sorted(k for k in eff if k != "kind")
            if extra:
                warns.append(f"{path}: sculpt_spells takes no fields; {extra} are ignored by the sim (R-G14)")
    return errs, warns


def check_on_event_usage(doc: dict) -> tuple[list[str], list[str]]:
    """sim #158 compile_on_event usage: at_will = no cost; per_day / per_rest {uses 1..255} = a trigger-owned pool
    '<trait id>#usage' recharged by rests; pool {pool, cost 1..255} = a Spend from a declared resources[] pool (check 2 / schema);
    recharge is not compiled: the sim lists the trait as unsupported ('on_event: unsupported usage') -> warning."""
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
            if is_ae and "escape" not in x:
                warns.append(f"{path}/apply/{j}: attack_effect '{a.get('id')}': unsupported (attack_effect: unsupported usage or "
                             "condition duration): every apply[] entry needs escape; the sim lists the action and never uses it (sim #170)")
            if not is_ae and "escape" in x:
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


# ---- R-G12 transforms (VERSION_BUMP_BATCH 7.2q, Rules #156) ------------------------------------------------------
# polymorph (spell@1, rules spell loader), wild_shape / shapechanger (common@2, Engine Core build_form) and the trait flag
# requires_humanoid_form. There is no RESERVED_EFFECT_KINDS table yet, so wild_shape / shapechanger stay out of the tag
# table's effect_kinds and get the unknown-kind warning (the open-kind pattern) until their loader lands; polymorph gets
# polymorph_spell_warnings' reserved warning. The messages below are the loaders' wording (Rules parses polymorph with them).
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
    out = ["effects[0]: polymorph is reserved: the rules spell loader does not parse it yet (R-G12 7.2q, "
           "SpellEffectKind::Polymorph), so it has no effect until that loader lands"]
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
    if doc.get("category") != "weapon":
        return errs, []
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
