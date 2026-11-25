"""C-T TP1: variant_of, target_max_size, the swallow rider, and instead."""
import json
import unittest
from pathlib import Path

import v4_helpers as h

CORE = Path(__file__).resolve().parents[2] / "content" / "packs" / "srd51.core"


def goblin():
    doc = json.loads((CORE / "monsters" / "goblin.json").read_text(encoding="utf-8"))
    doc["$schema"] = "monster@7"
    doc["id"] = "test.v4.swallow"
    return doc


def bite_and_swallow(doc):
    doc["actions"] = [
        {"id": "bite", "name": "Bite", "kind": "melee_weapon_attack", "to_hit": 19, "reach": 10, "targets": 1,
         "damage": [{"dice": "4d12+10", "type": "piercing"}]},
        {"id": "swallow", "name": "Swallow", "kind": "melee_weapon_attack", "description": "A bite variant.",
         "variant_of": "bite", "target_requires_any": ["grappled_by_self"], "target_max_size": "large",
         "on_hit": [{"kind": "swallow", "damage": {"dice": "16d6", "type": "acid"}, "capacity": "unlimited",
                     "regurgitate": {"dc": 20, "damage": 60, "all": True}, "exit_ft": 30}]},
    ]
    doc["traits"] = [{
        "id": "multiattack", "name": "Multiattack",
        "effect": {"kind": "multiattack", "options": [{
            "id": "full", "attacks": ["bite"], "instead": {"bite": "swallow"},
        }]},
    }]


class SwallowSchema(unittest.TestCase):
    def test_variant_and_instead_load(self):
        doc = goblin()
        bite_and_swallow(doc)
        errs, _warns = h.validate_in_memory(doc)
        self.assertEqual(errs, [], errs)

    def test_copied_key_and_a_bad_alternate(self):
        doc = goblin()
        bite_and_swallow(doc)
        doc["actions"][1]["to_hit"] = 19
        errs, _warns = h.validate_in_memory(doc)
        self.assertTrue(any("variant_of copies to_hit from bite" in e for e in errs), errs)

        doc = goblin()
        bite_and_swallow(doc)
        doc["traits"][0]["effect"]["options"][0]["instead"] = {"claw": "swallow"}
        errs, _warns = h.validate_in_memory(doc)
        self.assertTrue(any("instead names no attack of this option" in e for e in errs), errs)

    def test_capacity_is_required(self):
        doc = goblin()
        bite_and_swallow(doc)
        del doc["actions"][1]["on_hit"][0]["capacity"]
        errs, _warns = h.validate_in_memory(doc)
        self.assertTrue(any("capacity" in e for e in errs), errs)

    def test_attack_effect_still_has_no_variant(self):
        doc = goblin()
        doc["$schema"] = "monster@5"
        doc["actions"] = [{
            "id": "swallow", "name": "Swallow", "kind": "attack_effect", "attack": "melee", "to_hit": 4, "range_ft": 5,
            "apply": [{"condition": "restrained", "contain": {"kind": "swallow", "damage": {"dice": "3d6", "type": "acid"}},
                       "max_size": "medium"}],
        }]
        errs, _warns = h.validate_in_memory(doc)
        self.assertFalse(any("variant_of" in e for e in errs), errs)
