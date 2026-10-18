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
V2_ACTION_KINDS = V1_ACTION_KINDS | {"save_effect"}
# Effect kinds with a registered engine hook at @3 (eng::sim::FeatureRegistry::builtin()): live at @3,
# carried unchanged by @4, never marked inert by the 3 -> 4 migration.
LIVE_AT_3_EFFECT_KINDS = frozenset({"damage_bonus", "bonus_action_grant", "self_heal", "damage_mitigation",
                                    "magic_weapons", "multiattack"})
# The kinds @4 adds; also the frozen literal list of the 3 -> 4 migration (VERSION_BUMP_BATCH 8.2 step 4).
# save_effect and reaction are action / reaction kinds, not trait effect kinds: they exist in the list so
# a stamped one validates (effect_inert) but a native one is a schema error.
NEW_EFFECT_KINDS_AT_4 = frozenset({"on_event", "apply_modifier", "grant_action", "heal_pool", "save_effect", "reaction"})
NATIVE_EFFECT_KINDS_AT_4 = frozenset({"on_event", "apply_modifier", "grant_action", "heal_pool"})
EFFECT_COST_V1 = frozenset({"action", "bonus_action"})            # reaction is reserved up to @3
EFFECT_COST_V2 = frozenset({"action", "bonus_action", "reaction"})  # grant_action additionally accepts "free"

_SB_KEYS = frozenset({"$schema", "id", "name", "source", "size", "type", "tags", "alignment", "armor_class",
                      "hit_points", "speed", "abilities", "saving_throws", "skills", "damage_vulnerabilities",
                      "damage_resistances", "damage_immunities", "condition_immunities", "senses", "languages",
                      "proficiency_bonus", "traits", "actions", "bonus_actions", "reactions", "presentation"})
_MONSTER_KEYS = frozenset({"challenge", "behavior", "legendary_actions", "spellcasting"})
_CHARACTER_KEYS = frozenset({"race", "background", "level", "classes", "xp", "features", "equipment", "behavior",
                             "spellcasting"})
_AT4_KEYS = frozenset({"resources", "forward_compat", "inert_mechanics"})


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
for _typ, _keys in (("monster", _SB_KEYS | _MONSTER_KEYS), ("character", _SB_KEYS | _CHARACTER_KEYS)):
    TAG_TABLE[f"{_typ}@1"] = TagRow(False, V1_ACTION_KINDS, LIVE_AT_3_EFFECT_KINDS, EFFECT_COST_V1, _keys - {"spellcasting"})
    TAG_TABLE[f"{_typ}@2"] = TagRow(False, V1_ACTION_KINDS, LIVE_AT_3_EFFECT_KINDS, EFFECT_COST_V1, _keys - {"spellcasting"})
    TAG_TABLE[f"{_typ}@3"] = TagRow(True, V1_ACTION_KINDS, LIVE_AT_3_EFFECT_KINDS, EFFECT_COST_V1, _keys, cross_refs=False)
    TAG_TABLE[f"{_typ}@4"] = TagRow(True, V2_ACTION_KINDS, LIVE_AT_3_EFFECT_KINDS | NATIVE_EFFECT_KINDS_AT_4,
                                    EFFECT_COST_V2, _keys | _AT4_KEYS, cross_refs=True, needs_api_level=2,
                                    legacy_stamps=True)
TAG_TABLE["item@1"] = TagRow(False, frozenset(), frozenset(), frozenset(), frozenset(), cross_refs=True,
                             needs_api_level=2)

FORWARD_COMPAT_KNOWN = frozenset({"elevation"})
RESERVED_WINDOWS = frozenset({"roll_attack"})
RESERVED_REACTION_EFFECTS = frozenset({"subtract_roll"})


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
        # check 9: reserved names
        if (r.get("window") or {}).get("kind") in RESERVED_WINDOWS:
            errs.append(f"{path}/window: kind '{r['window']['kind']}' is reserved and rejected at @4 (check 9, Q6)")
        if eff.get("kind") in RESERVED_REACTION_EFFECTS:
            errs.append(f"{path}/effect: kind '{eff['kind']}' is reserved and rejected at @4 (check 9, Q6)")
    # check 2: pools
    for path, u in usage_sites(doc):
        if u.get("kind") == "pool":
            need_pool(path, u.get("pool"), u.get("cost"))
    for path, _owner, eff in live_effects(doc):
        if eff.get("kind") == "heal_pool":
            need_pool(path, eff.get("pool"), None)
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
        if a.get("kind") == "save_effect":
            sid = str(a.get("id"))
            if sid in seen:
                errs.append(f"{path}: save_effect id '{sid}' is already used by {seen[sid]} (check 4)")
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
        for j, _r in enumerate(a.get("on_hit") or []):
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
    # check 12: declared but never referenced pools (Legendary Resistance is declaration-only until row 13)
    for i, r in enumerate(resources):
        if r.get("id") not in referenced:
            warns.append(f"/resources/{i}: pool '{r.get('id')}' is never referenced by a usage.pool / spend.pool / "
                         "heal_pool.pool (declaration-only until the pool rows land) (check 12)")
    return errs, warns


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
