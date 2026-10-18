"""Reference implementation of the 3 -> 4 content migrations (monster_3_to_4, character_3_to_4).

docs/proposals/VERSION_BUMP_BATCH.md section 8.2 is the contract. This module is the *Python
reference* the validator tests use to produce in-memory migrated documents (the schema accepts the
migration's `inert` stamps; the validator rejects them in authored files on disk). The shipping
migrations live in engine/core/assets (content_migrations.cpp, row 3, Engine Core) and must produce
byte-identical documents; tests/validate/test_migrations_v4.py pins the expected outputs so the C++
port can be checked against the same fixtures.

Properties (tested): pure (the input is never modified), idempotent, key-order preserving, and a
function of the old document only.
"""
from __future__ import annotations

import copy

# The frozen literal list of section 8.2 step 4 -- NOT "whatever the batch introduces". A later
# batch gets its own 4 -> 5 list. self_heal, damage_bonus, bonus_action_grant, damage_mitigation,
# magic_weapons (#58) and multiattack (#59) are live at @3 and are never marked.
NEW_EFFECT_KINDS_AT_4 = ("on_event", "apply_modifier", "grant_action", "heal_pool", "save_effect", "reaction")
LIVE_AT_3_EFFECT_KINDS = ("self_heal", "damage_bonus", "bonus_action_grant", "damage_mitigation", "magic_weapons", "multiattack")


def effect_kinds_introduced_at(version: int) -> tuple[str, ...]:
    """Mirror of eng::assets::effect_kinds_introduced_at (content_migrations.hpp)."""
    return NEW_EFFECT_KINDS_AT_4 if version == 4 else ()


def _merge_inert_mechanics(doc: dict, add: set[str]) -> None:
    """Merge, never overwrite: union with the existing array (2 -> 3 writes e.g. 'damage[1..]'), sorted, de-duplicated."""
    if not add:
        return
    existing = doc.get("inert_mechanics")
    have = set(existing) if isinstance(existing, list) else set()
    doc["inert_mechanics"] = sorted(have | add)   # assigning an existing key keeps its position


def migrate_3_to_4(doc: dict, doc_type: str) -> dict:
    """Returns a migrated copy. Documents whose $schema is not '<doc_type>@3' are returned unchanged
    (a copy), which makes the migration idempotent."""
    out = copy.deepcopy(doc)
    if out.get("$schema") != f"{doc_type}@3":
        return out
    out["$schema"] = f"{doc_type}@4"                       # step 1 (in place: key order preserved)
    marks: set[str] = set()

    stamped = False                                         # step 2: every reactions[] entry
    for entry in out.get("reactions") or []:
        if isinstance(entry, dict):
            entry["inert"] = True
            stamped = True
    if stamped:
        marks.add("reactions")

    stamped = False                                         # step 3: bonus_actions[] that @4 would load
    for entry in out.get("bonus_actions") or []:
        if isinstance(entry, dict) and entry.get("kind") != "special":
            entry["inert"] = True
            stamped = True
    if stamped:
        marks.add("bonus_actions")

    for key in ("traits", "features"):                      # step 4: frozen literal list
        for entry in out.get(key) or []:
            eff = entry.get("effect") if isinstance(entry, dict) else None
            if isinstance(eff, dict) and eff.get("kind") in NEW_EFFECT_KINDS_AT_4:
                eff["inert"] = True

    if doc_type == "character" and out.get("equipment"):    # section 10: fl1's equipment list stays inert
        marks.add("equipment")

    _merge_inert_mechanics(out, marks)                      # step 5
    return out                                              # step 6: nothing else is touched


def monster_3_to_4(doc: dict) -> dict:
    return migrate_3_to_4(doc, "monster")


def character_3_to_4(doc: dict) -> dict:
    return migrate_3_to_4(doc, "character")


