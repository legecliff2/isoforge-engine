#!/usr/bin/env python3
"""common@2 lint (ctest: content_validate_lint).

docs/proposals/VERSION_BUMP_BATCH.md section 1.2: common@2 is a STANDALONE file that re-declares only the
defs that change (the section 1.1 list) and references every unchanged def absolutely as
`../common/v1.schema.json#/$defs/<name>`. Without this lint a typo silently resolves to nothing or to a
stale copy. Fails on
  * any local `#/$defs/x` ref whose target is not in the 1.1 list *and* declared in the same file,
  * any other kind of ref (a relative path other than the literal v1 one, a remote URL, a bare name),
  * an absolute ref to a v1 def that does not exist,
  * a re-declared def that is byte-identical to its v1 twin (it should be a ref instead),
  * a def declared in common@2 that is not in the list, or a listed def that is missing.
It also lints item@1's and the @4 document schemas' refs, the area def against spell@1's inline area
(shared fixtures), and the proposal's own snippets (its local refs must be in the list).
"""
from __future__ import annotations

import copy
import json
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"
V1_PREFIX = "../common/v1.schema.json#/$defs/"
V2_PREFIX = "../common/v2.schema.json#/$defs/"

# docs/proposals/VERSION_BUMP_BATCH.md 1.1, "common@2 (new file)": the complete local list.
SECTION_1_1 = ["action", "legacy_action", "legacy_nonspecial_action", "legacy_special_action", "effect", "effect_inert",
               "effect_live", "apply_modifier_effect", "grant_action_effect", "heal_pool_effect", "level_table",
               "level_table_entry", "pool_level_table", "pool_level_table_entry", "reaction", "reaction_effect",
               "on_event_effect", "apply", "item_damage", "rider", "predicate", "scaling", "usage", "area", "save",
               "resource", "modifier", "modifier_duration", "damage", "rider_damage_type", "rider_damage",
               "spellcasting", "trait", "trait_active", "stat_block",
               # R-G12 transforms (VERSION_BUMP_BATCH 7.2q, append-only additions; docs/CONTENT_CONVENTIONS.md section 9)
               "challenge_rating", "transform_forms", "wild_shape_level_row", "wild_shape_effect", "shapechanger_form",
               "shapechanger_effect",
               # EC #179 extra_attack (append-only addition; docs/CONTENT_EFFECTS.md)
               "extra_attack_effect",
               # Rules 7.2v control conditions (append-only additions; spell@1 save effect)
               "control_kind", "control_repeat_save", "destroy_undead_max_cr8",
               # Rules 7.2y class feature hooks (append-only addition; T&C spec)
               "bonus_vs",
               # Rules 7.2z barbarian hooks (append-only additions; T&C spec)
               "ability_list", "bonus_attack_effect",
               # Rules 7.2w grab and contain (append-only addition; T&C spec)
               "transparent_effect",
               # Rules 7.2za bard hooks (append-only additions; T&C #202)
               "roll_kind_list", "grant_die_effect", "half_proficiency_effect"]
ITEM_FILE_DEFS = {"item_damage"}          # listed in 1.1 but declared in item@1's file, not common@2's
COMMON2_DEFS = set(SECTION_1_1) - ITEM_FILE_DEFS


def load(rel: str) -> dict:
    return json.loads((SCHEMAS / rel).read_text(encoding="utf-8"))


def refs(node, path=""):
    """(json path, $ref string) for every $ref in `node`."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "$ref" and isinstance(v, str):
                yield path, v
            else:
                yield from refs(v, f"{path}/{k}")
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from refs(v, f"{path}/{i}")


def canon(x) -> str:
    return json.dumps(x, sort_keys=True, separators=(",", ":"))


def lint_common2(v2: dict, v1_defs: dict, allowed: set[str]) -> list[str]:
    problems = []
    defs = v2["$defs"]
    for name in sorted(set(defs) - allowed):
        problems.append(f"$defs/{name} is declared but is not in the 1.1 list")
    for name in sorted(allowed - set(defs)):
        problems.append(f"$defs/{name} is in the 1.1 list but not declared")
    for path, ref in refs(v2):
        if ref.startswith("#/$defs/"):
            target = ref[len("#/$defs/"):]
            if target not in allowed or target not in defs:
                problems.append(f"{path}: stray local ref {ref!r} (not in the 1.1 list and declared here)")
        elif ref.startswith(V1_PREFIX):
            target = ref[len(V1_PREFIX):]
            if target not in v1_defs:
                problems.append(f"{path}: {ref!r} names no def of common@1")
        else:
            problems.append(f"{path}: ref {ref!r} must be '#/$defs/<listed def>' or the literal '{V1_PREFIX}<name>'")
    for name in sorted(set(defs) & set(v1_defs)):
        if canon(defs[name]) == canon(v1_defs[name]):
            problems.append(f"$defs/{name} is byte-identical to its common@1 twin: use a ref")
    return problems


class Common2Lint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.v1 = load("common/v1.schema.json")["$defs"]
        cls.v2 = load("common/v2.schema.json")

    def test_common2_is_clean(self):
        self.assertEqual(lint_common2(self.v2, self.v1, COMMON2_DEFS), [])

    def test_lint_detects_stray_local_ref(self):
        bad = copy.deepcopy(self.v2)
        bad["$defs"]["usage"]["properties"]["pool"] = {"$ref": "#/$defs/local_id"}   # unchanged def: must be absolute
        self.assertTrue(any("stray local ref '#/$defs/local_id'" in p for p in lint_common2(bad, self.v1, COMMON2_DEFS)))
        typo = copy.deepcopy(self.v2)
        typo["$defs"]["rider"]["properties"]["scale"] = {"$ref": "#/$defs/scalng"}    # typo
        self.assertTrue(any("scalng" in p for p in lint_common2(typo, self.v1, COMMON2_DEFS)))

    def test_lint_detects_bad_absolute_refs(self):
        bad = copy.deepcopy(self.v2)
        bad["$defs"]["save"]["properties"]["ability"] = {"$ref": V1_PREFIX + "abilty"}
        bad["$defs"]["damage"]["properties"]["dice"] = {"$ref": "https://isoforge.dev/schemas/common/v1.schema.json#/$defs/dice"}
        probs = lint_common2(bad, self.v1, COMMON2_DEFS)
        self.assertTrue(any("names no def of common@1" in p for p in probs), probs)
        self.assertTrue(any("must be '#/$defs/<listed def>'" in p for p in probs), probs)

    def test_lint_detects_byte_identical_redeclaration(self):
        bad = copy.deepcopy(self.v2)
        bad["$defs"]["trait"] = copy.deepcopy(self.v1["trait"])
        self.assertTrue(any("byte-identical to its common@1 twin" in p for p in lint_common2(bad, self.v1, COMMON2_DEFS)))
        # a re-declared def that is not in the list at all
        extra = copy.deepcopy(self.v2)
        extra["$defs"]["dice"] = copy.deepcopy(self.v1["dice"])
        self.assertTrue(any("$defs/dice is declared but is not in the 1.1 list" in p for p in lint_common2(extra, self.v1, COMMON2_DEFS)))

    def test_lint_detects_missing_listed_def(self):
        bad = copy.deepcopy(self.v2)
        del bad["$defs"]["scaling"]
        probs = lint_common2(bad, self.v1, COMMON2_DEFS)
        self.assertTrue(any("scaling is in the 1.1 list but not declared" in p for p in probs), probs)

    def test_redeclared_defs_all_differ_from_v1(self):
        for name in sorted(set(self.v2["$defs"]) & set(self.v1)):
            self.assertNotEqual(canon(self.v2["$defs"][name]), canon(self.v1[name]), name)

    def test_unchanged_v1_defs_are_not_copied(self):
        # None of the many unchanged v1 defs may reappear (they are referenced, never copied).
        copied = sorted(n for n in self.v2["$defs"] if n in self.v1 and n not in COMMON2_DEFS)
        self.assertEqual(copied, [])

    def test_item_schema_refs(self):
        item = load("item/v1.schema.json")
        v2defs = self.v2["$defs"]
        for path, ref in refs(item):
            if ref.startswith("#/$defs/"):
                self.assertIn(ref[len("#/$defs/"):], ITEM_FILE_DEFS | {"item_damage"}, f"{path}: {ref}")
                self.assertIn(ref[len("#/$defs/"):], item["$defs"])
            elif ref.startswith(V1_PREFIX):
                self.assertIn(ref[len(V1_PREFIX):], self.v1, f"{path}: {ref}")
            elif ref.startswith(V2_PREFIX):
                self.assertIn(ref[len(V2_PREFIX):], v2defs, f"{path}: {ref}")
            else:
                self.fail(f"{path}: unexpected ref {ref!r}")
        self.assertEqual(set(item["$defs"]), ITEM_FILE_DEFS)

    def test_v4_document_schema_refs(self):
        for rel in ("monster/v4.schema.json", "character/v4.schema.json"):
            doc = load(rel)
            for path, ref in refs(doc):
                if ref.startswith(V1_PREFIX):
                    self.assertIn(ref[len(V1_PREFIX):], self.v1, f"{rel}{path}: {ref}")
                elif ref.startswith(V2_PREFIX):
                    self.assertIn(ref[len(V2_PREFIX):], self.v2["$defs"], f"{rel}{path}: {ref}")
                else:
                    self.fail(f"{rel}{path}: unexpected ref {ref!r}")

    def test_spell_area_and_common2_area_agree(self):
        """$defs/area mirrors spell@1's inline area exactly: one shared fixture set validates against both."""
        from jsonschema import Draft202012Validator
        spell_area = load("spell/v1.schema.json")["properties"]["area"]
        a = Draft202012Validator(spell_area)
        b = Draft202012Validator(self.v2["$defs"]["area"])
        self.assertEqual(canon(spell_area), canon(self.v2["$defs"]["area"]), "area must be spell@1's area verbatim")
        cases = [
            ({"shape": "cone", "size_ft": 60}, True), ({"shape": "cube", "size_ft": 15}, True),
            ({"shape": "sphere", "size_ft": 20}, True), ({"shape": "cylinder", "size_ft": 10, "height_ft": 40}, True),
            ({"shape": "line", "size_ft": 100, "width_ft": 5}, True),
            ({"shape": "line", "size_ft": 100}, False), ({"shape": "cone", "size_ft": 60, "width_ft": 5}, False),
            ({"shape": "cylinder", "size_ft": 10}, False), ({"shape": "sphere", "size_ft": 20, "height_ft": 5}, False),
            ({"shape": "cone", "size_ft": 12}, False), ({"shape": "cone", "size_ft": 0}, False),
            ({"shape": "cube", "size_ft": 15, "width_ft": 5}, False), ({"shape": "cone"}, False),
            ({"size_ft": 10}, False), ({"shape": "torus", "size_ft": 10}, False),
            ({"shape": "cone", "size_ft": 10, "extra": 1}, False), ({"shape": "line", "size_ft": 10, "width_ft": 7}, False),
            ({"shape": "cylinder", "size_ft": 10, "height_ft": 7}, False),
        ]
        for inst, ok in cases:
            with self.subTest(inst):
                self.assertEqual(a.is_valid(inst), ok)
                self.assertEqual(b.is_valid(inst), ok)

    def test_proposal_snippets_use_only_listed_local_refs(self):
        doc = ROOT / "docs" / "proposals" / "VERSION_BUMP_BATCH.md"
        if not doc.exists():
            self.skipTest("proposal not present")
        text = doc.read_text(encoding="utf-8")
        found = set(re.findall(r'"\$ref"\s*:\s*"#/\$defs/([a-z_0-9]+)"', text))
        self.assertTrue(found, "no local refs found in the proposal (pattern drifted?)")
        self.assertEqual(sorted(found - set(SECTION_1_1)), [], "the proposal's snippets use local refs outside the 1.1 list")
        literal = set(re.findall(r'\.\./common/v1\.schema\.json#/\$defs/([a-z_0-9]+)', text))
        self.assertEqual(sorted(n for n in literal if n not in self.v1), [], "the proposal references v1 defs that do not exist")


if __name__ == "__main__":
    unittest.main()
