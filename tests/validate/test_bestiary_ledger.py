#!/usr/bin/env python3
"""Headless tests for the bestiary ledger and the validate bridge.

Run with the validator's unittest runner (same pattern as the other
content_validate_* files; this PR does not register a ctest name):

  python3 tools/validate/run_unittest_file.py tests/validate/test_bestiary_ledger.py

ledger.py and validate_bridge.py must not import Tk. Counts are pinned to the
ledger after shambling mound left Deferred (Lightning Absorption and both-hits Engulf): 199 authored, 11 deferred.
"""
from __future__ import annotations

import ast
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bestiary_editor"))

import ledger  # noqa: E402

EDITOR = ROOT / "tools" / "bestiary_editor"
PACK = ROOT / "content" / "packs" / "srd51.core"
VALIDATOR = ROOT / "tools" / "validate" / "validate_packs.py"

# Deferred table ids after shambling mound left Deferred (## Deferred (11)).
PARKED_IDS = (
    "srd51.monster.mimic",
    "srd51.monster.doppelganger",
    "srd51.monster.green_hag",
    "srd51.monster.sea_hag",
    "srd51.monster.succubus",
    "srd51.monster.ghost",
    "srd51.monster.water_elemental",
    "srd51.monster.unicorn",
    "srd51.monster.invisible_stalker",
    "srd51.monster.mage",
    "srd51.monster.shield_guardian",
)

_FIXTURE = """\
## Authored (2)
| monster | CR | mechanics carried as data | notes |
|---|---|---|---|
| Wolf | 1/4 | bite | authored |
| Not A Real Monster | 0 | none | **partial**: missing file |

### What the sim plays today (main at `deadbeef`)

| kind | compiles from | applies |
|---|---|---|
| extra_damage | on_hit | Hit |

## Deferred (1)

Prose mentions `srd51.monster.swarm_of_rats` and must not park it.

| monster | CR | blocking mechanic |
|---|---|---|
| Mimic (`srd51.monster.mimic`) | 2 | Shapechanger has no shape. |

### Re-check at `cfb5eed` (2026-10-19)

| landed mechanic | loads from content? | shape | unblocks |
|---|---|---|---|
| Pack Tactics | yes | trait | wolf |
"""


def _imports_tk(path: Path) -> list[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    hits: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                if alias.name.split(".")[0] == "tkinter":
                    hits.append(alias.name)
        elif isinstance(node, ast.ImportFrom) and node.module:
            if node.module.split(".")[0] == "tkinter":
                hits.append(node.module)
    return hits


class LedgerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.book = ledger.load_ledger()

    def test_modules_under_test_do_not_import_tkinter(self):
        for name in ("ledger.py", "validate_bridge.py"):
            self.assertEqual(_imports_tk(EDITOR / name), [], name)

    def test_counts_match_headings_199_and_11(self):
        book = self.book
        self.assertEqual(book.authored_expected, 199)
        self.assertEqual(len(book.authored), 199)
        self.assertEqual(book.parked_expected, 11)
        self.assertEqual(len(book.parked), 11)
        self.assertEqual(book.count_errors(), ())

    def test_partial_is_subset_of_authored(self):
        authored_ids = [row.monster_id for row in self.book.authored]
        partial_ids = [row.monster_id for row in self.book.partial]
        self.assertEqual(len(authored_ids), len(set(authored_ids)))
        self.assertTrue(set(partial_ids) < set(authored_ids))
        self.assertIn("srd51.monster.kobold", partial_ids)
        self.assertNotIn("srd51.monster.wolf", partial_ids)
        for row in self.book.authored:
            self.assertEqual(row.partial, ledger.PARTIAL_MARK in row.notes)

    def test_demo_rows(self):
        by_id = {row.monster_id: row for row in self.book.authored}
        wolf = by_id["srd51.monster.wolf"]
        self.assertEqual(wolf.name, "Wolf")
        self.assertFalse(wolf.partial)
        self.assertEqual(wolf.path, ledger.DEFAULT_MONSTERS_DIR / "wolf.json")
        self.assertTrue(wolf.path.is_file())
        kobold = by_id["srd51.monster.kobold"]
        self.assertTrue(kobold.partial)
        roper = by_id["srd51.monster.roper"]
        self.assertTrue(roper.partial)
        self.assertEqual(roper.path, ledger.DEFAULT_MONSTERS_DIR / "roper.json")
        mound = by_id["srd51.monster.shambling_mound"]
        self.assertTrue(mound.partial)
        self.assertEqual(mound.name, "Shambling Mound")
        self.assertEqual(mound.path, ledger.DEFAULT_MONSTERS_DIR / "shambling_mound.json")
        self.assertEqual(kobold.path, ledger.DEFAULT_MONSTERS_DIR / "kobold.json")
        will = by_id["srd51.monster.will_o_wisp"]
        self.assertEqual(will.name, "Will-o'-Wisp")
        self.assertTrue(will.path.is_file())
        parked = {row.monster_id: row for row in self.book.parked}
        self.assertNotIn("srd51.monster.roper", parked)
        self.assertNotIn("srd51.monster.shambling_mound", parked)
        mimic = parked["srd51.monster.mimic"]
        self.assertEqual(mimic.name, "Mimic")
        self.assertIsNone(mimic.path)
        self.assertFalse((ledger.DEFAULT_MONSTERS_DIR / "mimic.json").exists())
        self.assertEqual(parked["srd51.monster.succubus"].name, "Succubus/Incubus")

    def test_parked_ids_are_the_deferred_table(self):
        self.assertEqual(tuple(row.monster_id for row in self.book.parked), PARKED_IDS)
        authored = {row.monster_id for row in self.book.authored}
        self.assertTrue(authored.isdisjoint(PARKED_IDS))
        # The Deferred prose names an authored monster; it must not become parked.
        self.assertIn("srd51.monster.swarm_of_rats", authored)
        self.assertNotIn("srd51.monster.goblin", authored)
        names = {row.name for row in self.book.authored}
        self.assertNotIn("extra_damage", names)
        self.assertNotIn("set_hp", names)
        parked_names = {row.name for row in self.book.parked}
        self.assertNotIn("Pack Tactics", parked_names)
        self.assertNotIn("Undead Fortitude", parked_names)

    def test_real_ledger_has_no_unmapped_names(self):
        self.assertEqual(self.book.warnings, ())
        for row in self.book.authored:
            self.assertIsNotNone(row.path, row.name)
            self.assertTrue(row.path.is_file())

    def test_fixture_skips_subsections_and_does_not_guess(self):
        with tempfile.TemporaryDirectory() as tmp:
            monsters = Path(tmp)
            (monsters / "wolf.json").write_text("{}\n", encoding="utf-8")
            book = ledger.parse_ledger(_FIXTURE, monsters, source=Path("fixture.md"))
        self.assertEqual(book.count_errors(), ())
        self.assertEqual(len(book.authored), 2)
        self.assertEqual(len(book.parked), 1)
        self.assertEqual(book.authored[0].monster_id, "srd51.monster.wolf")
        self.assertIsNotNone(book.authored[0].path)
        missing = book.authored[1]
        self.assertTrue(missing.partial)
        self.assertIsNone(missing.path)
        self.assertEqual(missing.monster_id, "srd51.monster.not_a_real_monster")
        self.assertEqual(book.parked[0].monster_id, "srd51.monster.mimic")
        self.assertTrue(any("unmapped authored name" in warning for warning in book.warnings))
        self.assertNotIn("srd51.monster.swarm_of_rats", {row.monster_id for row in book.parked})
        self.assertNotIn("extra_damage", {row.name for row in book.authored})
        self.assertNotIn("Pack Tactics", {row.name for row in book.parked})

    def test_heading_mismatch_fails_the_self_check(self):
        text = _FIXTURE.replace("## Authored (2)", "## Authored (9)", 1)
        book = ledger.parse_ledger(text, Path("."), source=Path("fixture.md"))
        self.assertIn("authored rows 2 != heading 9", book.count_errors())


class SoftBlockTests(unittest.TestCase):
    """Parked ids are warned out of playable scenario rosters. Partials are not."""

    @classmethod
    def setUpClass(cls):
        cls.book = ledger.load_ledger()

    def _lines(self, doc: dict, book: ledger.Ledger | None = None) -> list[str]:
        blocks = ledger.soft_blocks_for_scenario(doc, self.book if book is None else book)
        return [block.line() for block in blocks]

    def test_shipped_scenarios_have_no_parked_roster_ids(self):
        root = ROOT / "scenarios"
        found = sorted(root.glob("*/scenario.json"))
        self.assertGreaterEqual(len(found), 1)
        for path in found:
            doc = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(self._lines(doc), [], path.name)

    def test_parked_mimic_is_soft_blocked_on_either_roster_side(self):
        doc = {
            "party": [
                {"id": "fighter", "archetype": "srd51.character.fighter_1"},
                {"id": "hidden", "archetype": "srd51.monster.mimic"},
            ],
            "monsters": [
                "not-an-actor",
                {"id": "also", "archetype": "srd51.monster.mimic"},
                {"id": "blank"},
            ],
        }
        lines = self._lines(doc)
        self.assertEqual(len(lines), 2)
        self.assertIn("party[1]", lines[0])
        self.assertIn("monsters[1]", lines[1])
        for line in lines:
            self.assertIn("'srd51.monster.mimic'", line)
            self.assertIn("Mimic", line)
            self.assertIn("Shapechanger", line)
            self.assertIn("soft-blocked from this playable encounter", line)
            self.assertNotIn("stat block", line)

    def test_authored_partial_goblin_and_troll_are_playable(self):
        doc = {
            "monsters": [
                {"archetype": "srd51.monster.wolf"},
                {"archetype": "srd51.monster.kobold"},
                {"archetype": "srd51.monster.goblin"},
                {"archetype": "srd51.monster.troll"},
                {"archetype": 12},
            ]
        }
        self.assertEqual(self._lines(doc), [])
        self.assertIn("srd51.monster.kobold", {row.monster_id for row in self.book.partial})
        self.assertIn("srd51.monster.troll", {row.monster_id for row in self.book.authored})
        self.assertNotIn("srd51.monster.troll", {row.monster_id for row in self.book.parked})

    def test_every_deferred_id_soft_blocks_and_prose_ids_do_not(self):
        for monster_id in PARKED_IDS:
            doc = {"monsters": [{"archetype": monster_id}]}
            lines = self._lines(doc)
            self.assertEqual(len(lines), 1, monster_id)
            self.assertIn(monster_id, lines[0])
        with tempfile.TemporaryDirectory() as tmp:
            book = ledger.parse_ledger(_FIXTURE, Path(tmp), source=Path("fixture.md"))
        # The fixture's Deferred prose names an authored id. It must not block.
        prose = {"monsters": [{"archetype": "srd51.monster.swarm_of_rats"}]}
        self.assertEqual(self._lines(prose, book), [])
        partial = {"monsters": [{"archetype": "srd51.monster.not_a_real_monster"}]}
        self.assertEqual(self._lines(partial, book), [])
        parked = {"party": [{"archetype": "srd51.monster.mimic"}]}
        lines = self._lines(parked, book)
        self.assertEqual(len(lines), 1)
        self.assertIn("party[0]", lines[0])
        self.assertIn("Shapechanger has no shape", lines[0])

    def test_non_document_and_note(self):
        self.assertEqual(ledger.soft_blocks_for_scenario([], self.book), ())  # type: ignore[arg-type]
        self.assertIn("scenario@1", ledger.SOFT_BLOCK_NOTE)
        self.assertIn("does not invent a stat block", ledger.SOFT_BLOCK_NOTE)
        editor = (EDITOR / "bestiary_editor.py").read_text(encoding="utf-8")
        self.assertIn("ledger.SOFT_BLOCK_NOTE", editor)


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import validate_bridge
        cls.vb = validate_bridge
        cls.code, cls.out = validate_bridge.validate_pack_output(PACK)
        proc = subprocess.run(
            [sys.executable, str(VALIDATOR), str(PACK)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        cls.cli_code = proc.returncode
        cls.cli_out = proc.stdout
        cls.cli_err = proc.stderr

    def test_pack_output_matches_cli(self):
        self.assertEqual(self.cli_err, "")
        self.assertEqual(self.code, self.cli_code)
        self.assertEqual(self.out, self.cli_out)
        self.assertEqual(self.code, 0)
        self.assertIn("ok   content/packs/srd51.core\n", self.out)
        self.assertIn("packs valid", self.out)

    def test_unedited_wolf_round_trip_matches_pack_output(self):
        rel = "monsters/wolf.json"
        doc = json.loads((PACK / rel).read_text(encoding="utf-8"))
        text = self.vb.canonical_json(doc)
        self.assertEqual(json.loads(text), doc)
        self.assertTrue(text.endswith("\n"))
        code, out = self.vb.validate_documents(PACK, {rel: doc})
        self.assertEqual(code, self.code)
        self.assertEqual(out, self.out)

    def test_edited_wolf_fails_with_derive_error(self):
        rel = "monsters/wolf.json"
        doc = json.loads((PACK / rel).read_text(encoding="utf-8"))
        doc["armor_class"]["value"] = 1
        code, out = self.vb.validate_documents(PACK, {rel: doc})
        self.assertNotEqual(code, 0)
        self.assertIn("FAIL content/packs/srd51.core\n", out)
        self.assertIn("monsters/wolf.json:", out)
        self.assertIn("derived AC", out)
        self.assertNotIn("FAIL content/packs/srd51.core", self.out)

    def test_check_monster_derive_for_derive_true(self):
        wolf = json.loads((PACK / "monsters" / "wolf.json").read_text(encoding="utf-8"))
        kobold = json.loads((PACK / "monsters" / "kobold.json").read_text(encoding="utf-8"))
        self.assertIs(wolf.get("derive"), True)
        self.assertIs(kobold.get("derive"), True)
        self.assertEqual(self.vb.monster_derive(PACK, wolf), ([], []))
        self.assertEqual(self.vb.monster_derive(PACK, kobold), ([], []))
        plain = dict(wolf)
        plain["derive"] = False
        self.assertEqual(self.vb.monster_derive(PACK, plain), ([], []))
        broken = json.loads(json.dumps(wolf))
        broken["armor_class"] = dict(broken["armor_class"], value=1)
        errs, warns = self.vb.monster_derive(PACK, broken)
        self.assertTrue(any("derived AC" in err for err in errs), errs)
        self.assertEqual(warns, [])


if __name__ == "__main__":
    unittest.main()
