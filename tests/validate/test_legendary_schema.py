"""monster@7 legendary actions: the pool, the cost, and resolving use ids."""
import json
import unittest
from pathlib import Path

import v4_helpers as h

CORE = Path(__file__).resolve().parents[2] / "content" / "packs" / "srd51.core"


def goblin():
    doc = json.loads((CORE / "monsters" / "goblin.json").read_text(encoding="utf-8"))
    doc["$schema"] = "monster@7"
    doc["id"] = "test.v4.legendary"
    return doc


class LegendarySchema(unittest.TestCase):
    def test_attack_check_and_move(self):
        doc = goblin()
        doc["legendary_actions_per_round"] = 3
        doc["legendary_actions"] = [
            {"id": "bite", "name": "Bite", "description": "One bite.", "cost": 1,
             "use": {"attack": "scimitar"}},
            {"id": "sniff", "name": "Sniff", "description": "Look around.",
             "use": {"check": "perception"}},
            {"id": "stride", "name": "Stride", "description": "Move.",
             "use": {"move": "speed", "no_opportunity_attacks": True}},
        ]
        errs, _warns = h.validate_in_memory(doc)
        self.assertEqual(errs, [], errs)

    def test_cost_above_the_pool_and_a_missing_attack(self):
        doc = goblin()
        doc["legendary_actions_per_round"] = 2
        doc["legendary_actions"] = [
            {"id": "heavy", "name": "Heavy", "description": "Too expensive.", "cost": 3,
             "use": {"attack": "no_such_attack"}},
        ]
        errs, _warns = h.validate_in_memory(doc)
        self.assertTrue(any("exceeds legendary_actions_per_round" in e for e in errs), errs)
        self.assertTrue(any("no_such_attack" in e for e in errs), errs)

    def test_spell_must_be_one_action(self):
        doc = goblin()
        doc["legendary_actions_per_round"] = 1
        doc["legendary_actions"] = [
            {"id": "step", "name": "Step", "description": "A bonus-action spell.",
             "use": {"spell": "srd51.spell.misty_step"}},
        ]
        errs, _warns = h.validate_in_memory(
            doc, spell_times={"srd51.spell.misty_step": "bonus_action"})
        self.assertTrue(any("1 action" in e for e in errs), errs)
        ok, _warns = h.validate_in_memory(
            doc, spell_times={"srd51.spell.misty_step": "action"})
        self.assertEqual(ok, [], ok)
        missing, _warns = h.validate_in_memory(doc)
        self.assertTrue(any("was not found" in e for e in missing), missing)

    def test_older_schema_rejects_a_mechanical_list(self):
        doc = goblin()
        doc["$schema"] = "monster@6"
        doc["legendary_actions_per_round"] = 3
        doc["legendary_actions"] = [
            {"id": "bite", "name": "Bite", "description": "x", "use": {"attack": "scimitar"}},
        ]
        errs, _warns = h.validate_in_memory(doc)
        self.assertTrue(errs, errs)


if __name__ == "__main__":
    unittest.main()
