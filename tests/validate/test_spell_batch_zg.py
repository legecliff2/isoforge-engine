"""VERSION_BUMP_BATCH 7.2zg positive specimens (Haste, Slow, Spiritual Weapon, False Life, Heroism, Acid Arrow) validate as one pack; negatives are cases/."""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import run_negative as rn

HERE = Path(__file__).resolve().parent


class SpellBatchZgPositive(unittest.TestCase):
    def test_specimens_validate(self):
        docs = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted((HERE / "fixtures" / "spell_batch_zg").glob("*.json"))}
        self.assertEqual(set(docs), {"acid_arrow", "false_life", "haste", "heroism", "slow", "spiritual_weapon"})
        with tempfile.TemporaryDirectory() as tmp:
            rc, out = rn.run(rn.make_pack(Path(tmp), "test.neg", docs))
        self.assertEqual(rc, 0, out)
        self.assertNotIn("WARN", out)


if __name__ == "__main__":
    unittest.main()
