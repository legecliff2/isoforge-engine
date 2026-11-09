"""VERSION_BUMP_BATCH 7.2zj positive specimens validate as one pack."""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import run_negative as rn

HERE = Path(__file__).resolve().parent

SPELLS = {
    "charm_person", "suggestion", "calm_emotions", "fly", "cone_of_cold", "ice_storm",
    "greater_invisibility", "dispel_evil_and_good", "greater_restoration", "geas",
}


class SpellBatchZjPositive(unittest.TestCase):
    def test_specimens_validate(self):
        docs = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted((HERE / "fixtures" / "spell_batch_zj").glob("*.json"))}
        self.assertEqual(set(docs), SPELLS)
        self.assertEqual(docs["charm_person"]["id"], "srd51.spell.charm_person")
        self.assertTrue(docs["charm_person"]["effects"][0]["hostile_save_advantage"])
        self.assertEqual(docs["calm_emotions"]["effects"][0]["suppress_conditions"], ["charmed", "frightened"])
        self.assertEqual(docs["fly"]["effects"][0]["fly_ft"], 60)
        self.assertEqual(docs["ice_storm"]["effects"][0]["also_damage"]["type"], "cold")
        self.assertNotIn("ends_on", docs["greater_invisibility"]["effects"][0])
        self.assertEqual(docs["geas"]["casting_time"]["kind"], "minute")
        self.assertEqual(docs["geas"]["duration"]["amount"], 720)
        self.assertNotIn("item", docs["greater_restoration"]["components"]["m"])
        self.assertEqual(docs["greater_restoration"]["effects"][0]["options"][0], "exhaustion")
        with tempfile.TemporaryDirectory() as tmp:
            rc, out = rn.run(rn.make_pack(Path(tmp), "test.neg", docs))
        self.assertEqual(rc, 0, out)
        self.assertNotIn("WARN", out)
        self.assertIn("cost_cp 10000 without item", out)


if __name__ == "__main__":
    unittest.main()
