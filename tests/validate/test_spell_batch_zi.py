"""VERSION_BUMP_BATCH 7.2zi positive specimens validate as one pack; negatives are cases/."""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import run_negative as rn

HERE = Path(__file__).resolve().parent


class SpellBatchZiPositive(unittest.TestCase):
    def test_specimens_validate(self):
        docs = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted((HERE / "fixtures" / "spell_batch_zi").glob("*.json"))}
        self.assertTrue({
            "spare_the_dying", "true_strike", "expeditious_retreat", "protection_from_poison",
            "ray_of_enfeeblement", "beacon_of_hope", "warding_bond",
        } <= set(docs))
        self.assertEqual(docs["beacon_of_hope"]["targets"]["count"], 255)
        self.assertNotIn("concentration", docs["warding_bond"]["duration"])
        self.assertTrue(docs["ray_of_enfeeblement"]["effects"][0]["hit_only"])
        self.assertNotIn("damage", docs["ray_of_enfeeblement"]["effects"][0])
        with tempfile.TemporaryDirectory() as tmp:
            rc, out = rn.run(rn.make_pack(Path(tmp), "test.neg", docs))
        self.assertEqual(rc, 0, out)
        self.assertNotIn("WARN", out)


if __name__ == "__main__":
    unittest.main()
