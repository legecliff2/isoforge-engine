#!/usr/bin/env python3
"""SRD 5.1 Fighter (Champion), Cleric (Life) and Wizard (Evocation) levels 1-5 in srd51.core vs the 5e-database
tables (ctest: content_validate_srd51_classes).

`data/srd51_classes_5e_database_a6212be.json` holds fields copied verbatim from 5e-bits/5e-database commit a6212be
(src/2014/en/5e-SRD-Classes.json, -Levels.json, -Features.json, -Subclasses.json, and -Spells.json for the spells the
pack ships). One character@4 document per class per level (`srd51.character.<class>_level_<n>`), a human pre-generated
PC built from the standard array (+1 to every score, SRD human) with the ASI at level 4 on the class's main ability.

Asserted per document: class / subclass / hit die / level / proficiency bonus / XP threshold, saving throws and skills
(ability modifier + proficiency), hit points (max die at level 1, then the fixed average + Con), AC and every weapon
attack derived from the pack's own item@1 documents, spell slots and caster level, and that every spell is a shipped
spell@1 on the class (or domain) list. Also pins the authored/deferred split of every class and subclass feature of
levels 1-5 (docs/SRD_CLASSES_DEFERRED.md) and the mechanics that are data, not text.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
DATA = json.loads((Path(__file__).parent / "data/srd51_classes_5e_database_a6212be.json").read_text(encoding="utf-8"))
TABLE, SPELLS = DATA["classes"], DATA["spells"]
LEVELS = range(1, 6)
XP = {1: 0, 2: 300, 3: 900, 4: 2700, 5: 6500}   # SRD 5.1 Character Advancement table
SUBCLASS_AT = {"fighter": 3, "cleric": 1, "wizard": 2}
MAIN = {"fighter": "str", "cleric": "wis", "wizard": "int"}   # the ASI at level 4 goes here (+2)
SKILLS = {"athletics": "str", "perception": "wis", "insight": "wis", "religion": "int", "arcana": "int", "history": "int"}

# Every class / subclass feature of levels 1-5 (mirror index) -> where it lives. "feature:<id>" = a features[] entry;
# "subclass" = classes[0].subclass; "spellcasting" = the spellcasting block; "abilities" = baked into the scores.
AUTHORED = {
    "fighter-fighting-style": "feature:fighting_style_dueling",
    "second-wind": "feature:second_wind",
    "action-surge-1-use": "feature:action_surge",
    "martial-archetype": "subclass",
    "improved-critical": "feature:improved_critical",
    "fighter-ability-score-improvement-1": "abilities",
    "extra-attack-1": "feature:extra_attack",
    "spellcasting-cleric": "spellcasting",
    "divine-domain": "subclass",
    "divine-domain-improvement-1": "subclass",
    "domain-spells-1": "spellcasting",
    "domain-spells-2": "spellcasting",
    "domain-spells-3": "spellcasting",
    "bonus-proficiency": "feature:bonus_proficiency",
    "cleric-ability-score-improvement-1": "abilities",
    "spellcasting-wizard": "spellcasting",
    "arcane-tradition": "subclass",
    "arcane-recovery": "feature:arcane_recovery",   # slot_recovery kind (7.2n, sim #148 loader)
    "evocation-savant": "feature:evocation_savant",
    "sculpt-spells": "feature:sculpt_spells",   # R-G14 (sim #143): open kind sculpt_spells {}
    "wizard-ability-score-improvement-1": "abilities",
}
# Not expressible today (docs/SRD_CLASSES_DEFERRED.md): mirror index -> the feature id it would get (must be absent).
DEFERRED = {
    "disciple-of-life": "disciple_of_life",
    "channel-divinity-1-rest": "channel_divinity",
    "channel-divinity-turn-undead": "turn_undead",
    "channel-divinity-preserve-life": "preserve_life",
    "destroy-undead-cr-1-2-or-below": "destroy_undead",
}
# Life domain spells and class cantrips the pack does not ship yet (listed in the deferred doc).
DEFERRED_SPELLS = ["bless", "lesser-restoration", "spiritual-weapon", "beacon-of-hope", "revivify"]


def mod(score: int) -> int:
    return (score - 10) // 2


def load(cls: str, level: int) -> dict:
    return json.loads((PACK / "characters" / f"{cls}_level_{level}.json").read_text(encoding="utf-8"))


def item(ref: str) -> dict:
    assert ref.startswith("srd51.item."), ref
    return json.loads((PACK / "items" / f"{ref.removeprefix('srd51.item.')}.json").read_text(encoding="utf-8"))


def docs():
    for cls in TABLE:
        for level in LEVELS:
            yield cls, level, load(cls, level)


def features(d: dict) -> dict:
    return {f["id"]: f for f in d.get("features", [])}


def dice_plus(die: str, bonus: int) -> str:
    return die if bonus == 0 else f"{die}{bonus:+d}"


class ClassTable(unittest.TestCase):
    def test_identity_and_progression(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                t, row = TABLE[cls], TABLE[cls]["levels"][str(level)]
                self.assertEqual(d["$schema"], "character@4")
                self.assertEqual(d["id"], f"srd51.character.{cls}_level_{level}")
                self.assertEqual(d["level"], level)
                self.assertEqual(len(d["classes"]), 1)
                c = d["classes"][0]
                self.assertEqual((c["class"], c["level"], c["hit_die"]), (cls, level, f"d{t['hit_die']}"))
                self.assertEqual(c.get("subclass"), t["subclass"] if level >= SUBCLASS_AT[cls] else None)
                self.assertEqual(d["proficiency_bonus"], row["prof_bonus"])
                self.assertEqual(d["xp"], XP[level])
                self.assertEqual((d["race"], d["size"], d["type"], d["speed"]), ("human", "medium", "humanoid", {"walk": 30}))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertTrue(d["name"].startswith(f"{t['name']}, level {level}"))

    def test_hit_points(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                hd, con = TABLE[cls]["hit_die"], mod(d["abilities"]["con"])
                self.assertEqual(d["hit_points"]["average"], hd + con + (level - 1) * (hd // 2 + 1 + con))
                self.assertEqual(d["hit_points"]["formula"], f"{level}d{hd}+{level * con}")

    def test_abilities_and_asi(self):
        for cls in TABLE:
            base = load(cls, 1)["abilities"]
            self.assertEqual(sorted(base.values()), [9, 11, 13, 14, 15, 16])   # standard array, +1 each (human)
            for level in LEVELS:
                with self.subTest(cls=cls, level=level):
                    asis = TABLE[cls]["levels"][str(level)]["ability_score_bonuses"]   # the mirror's running count
                    want = dict(base)
                    want[MAIN[cls]] += 2 * asis
                    self.assertEqual(load(cls, level)["abilities"], want)

    def test_saves_skills_senses(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                ab, pb = d["abilities"], d["proficiency_bonus"]
                self.assertEqual(d["saving_throws"], {s: mod(ab[s]) + pb for s in TABLE[cls]["saving_throws"]})
                self.assertEqual(len(d["skills"]), 2)   # every class here picks two skills
                for sk, v in d["skills"].items():
                    self.assertEqual(v, mod(ab[SKILLS[sk]]) + pb, sk)
                self.assertEqual(d["senses"]["passive_perception"], 10 + d["skills"].get("perception", mod(ab["wis"])))

    def test_armor_class_from_items(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                worn = [item(e["item"]) for e in d["equipment"] if isinstance(e, dict) and e.get("equipped")]
                armor = [i for i in worn if i["category"] == "armor"]
                dex = mod(d["abilities"]["dex"])
                if armor:
                    a = armor[0]
                    ac = a["base_ac"] + (min(dex, a.get("dex_cap", dex)) if a["dex_bonus"] else 0)
                    self.assertLessEqual(a.get("str_min", 0), d["abilities"]["str"])
                else:
                    ac = 10 + dex
                ac += sum(i["ac_bonus"] for i in worn if i["category"] == "shield")
                self.assertEqual(d["armor_class"]["value"], ac)

    def test_attacks_from_items(self):
        """Every weapon action is an equipment item: to-hit = ability modifier + proficiency (all three classes are
        proficient with their weapons), damage = the item's die + the same modifier (base damage, no feature folded in)."""
        for cls, level, d in docs():
            items = {i["id"].removeprefix("srd51.item."): i for i in (item(e["item"]) for e in d["equipment"] if isinstance(e, dict))}
            for a in d["actions"]:
                with self.subTest(cls=cls, level=level, action=a["id"]):
                    key = {"light_crossbow": "crossbow_light", "dagger_thrown": "dagger"}.get(a["id"], a["id"])
                    it = items[key]
                    props = it.get("properties", [])
                    ab = d["abilities"]
                    if "finesse" in props:
                        m = max(mod(ab["str"]), mod(ab["dex"]))
                    else:
                        m = mod(ab["dex"]) if a["kind"] == "ranged_weapon_attack" and "thrown" not in props else mod(ab["str"])
                    self.assertEqual(a["to_hit"], m + d["proficiency_bonus"])
                    self.assertEqual(a["damage"], [{"dice": dice_plus(it["damage"][0]["dice"], m), "type": it["damage"][0]["type"]}])
                    self.assertEqual(a.get("properties", []), props)
                    if a["kind"] == "melee_weapon_attack":
                        self.assertEqual(a["reach"], it["reach"])
                    else:
                        self.assertEqual(a["range"], it["range"])

    def test_spellcasting(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                row, t = TABLE[cls]["levels"][str(level)], TABLE[cls]
                sc = d.get("spellcasting")
                if t["spellcasting_ability"] is None:
                    self.assertIsNone(sc)
                    continue
                self.assertEqual((sc["ability"], sc["caster_level"]), (t["spellcasting_ability"], level))
                self.assertEqual(sc["slots"], row["spell_slots"])
                self.assertNotIn("dc", sc)              # derived: 8 + proficiency + modifier
                self.assertNotIn("attack_bonus", sc)
                top = max(int(k) for k in row["spell_slots"])
                domain = {s["spell"] for s in t.get("subclass_spells", []) if s["level"] <= level}
                cantrips = 0
                for ref in sc["spells"]:
                    idx = ref.removeprefix("srd51.spell.").replace("_", "-")
                    self.assertTrue((PACK / "spells" / f"{ref.removeprefix('srd51.spell.')}.json").exists(), ref)
                    sp = SPELLS[idx]
                    self.assertTrue(cls in sp["classes"] or idx in domain, ref)
                    self.assertLessEqual(sp["level"], top, ref)
                    cantrips += sp["level"] == 0
                self.assertLessEqual(cantrips, row["cantrips_known"])
                for s in domain:   # domain spells are always prepared: present unless deferred (not shipped)
                    self.assertTrue(f"srd51.spell.{s.replace('-', '_')}" in sc["spells"] or s in DEFERRED_SPELLS, s)

    def test_mechanics_are_data(self):
        for level in LEVELS:
            f = features(load("fighter", level))
            self.assertEqual(f["fighting_style_dueling"]["effect"],
                             {"kind": "damage_bonus", "amount": 2, "condition": "one_handed_melee_no_other_weapon"})
            self.assertEqual(f["second_wind"]["effect"], {"kind": "self_heal", "dice": "1d10", "plus": "fighter_level", "uses": 1,
                                                          "recharge": "short_rest", "cost": "bonus_action"})
            self.assertEqual("action_surge" in f, level >= 2)
            if level >= 2:
                self.assertEqual(f["action_surge"]["effect"], {"kind": "grant_action", "cost": "free",
                                                               "usage": {"kind": "per_rest", "uses": 1, "rest": "short"}})
            self.assertEqual("improved_critical" in f, level >= 3)
            if level >= 3:
                self.assertEqual(f["improved_critical"]["effect"], {"kind": "crit_range", "min": 19})
            self.assertEqual("extra_attack" in f, level >= 5)
            if level >= 5:
                self.assertEqual(f["extra_attack"]["effect"],
                                 {"kind": "multiattack", "options": [{"id": "longsword_x2", "attacks": ["longsword", "longsword"]}]})
        for level in LEVELS:   # Evocation (Arcane Tradition at 2): Sculpt Spells is the fieldless sim #143 kind
            f = features(load("wizard", level))
            self.assertEqual("sculpt_spells" in f, level >= 2)
            if level >= 2:
                self.assertEqual(f["sculpt_spells"]["effect"], {"kind": "sculpt_spells"})
        # descriptive entries carry no effect (no combat rule; the heavy-armor proficiency is the worn chain mail)
        for cls, fid in (("cleric", "bonus_proficiency"), ("wizard", "evocation_savant")):
            for level in LEVELS:
                f = features(load(cls, level))
                if fid in f:
                    self.assertNotIn("effect", f[fid])

    def test_arcane_recovery_slot_recovery(self):
        # VERSION_BUMP_BATCH 7.2n: budget ceil(wizard level / 2) is derived from classes[], not authored.
        for level in LEVELS:
            with self.subTest(level=level):
                f = features(load("wizard", level))["arcane_recovery"]
                self.assertEqual(f["effect"], {"kind": "slot_recovery", "class": "wizard", "max_slot_level": 5, "uses": 1})
                self.assertIn("half your wizard level (rounded up)", f["description"])

    def test_authored_and_deferred_split(self):
        doc = (ROOT / "docs/SRD_CLASSES_DEFERRED.md").read_text(encoding="utf-8")
        for cls in TABLE:
            for level in LEVELS:
                row = TABLE[cls]["levels"][str(level)]
                d = load(cls, level)
                f = features(d)
                for idx in list(row["features"]) + list(row.get("subclass_features", {})):
                    with self.subTest(cls=cls, level=level, feature=idx):
                        self.assertTrue((idx in AUTHORED) != (idx in DEFERRED), "classify every feature exactly once")
                        where = AUTHORED.get(idx)
                        if where and where.startswith("feature:"):
                            fid = where.split(":", 1)[1]
                            for later in range(level, 6):   # a feature, once gained, stays
                                self.assertIn(fid, features(load(cls, later)))
                        elif where == "subclass":
                            self.assertIn("subclass", d["classes"][0])
                        elif where == "spellcasting":
                            self.assertIn("spellcasting", d)
                        elif where == "abilities":
                            pass   # pinned by test_abilities_and_asi
                        else:
                            self.assertIn(idx, DEFERRED)
                            self.assertIn(f"`{idx}`", doc)
                for fid in DEFERRED.values():
                    self.assertNotIn(fid, f)   # deferred, not approximated
        for s in DEFERRED_SPELLS:
            self.assertIn(f"srd51.spell.{s.replace('-', '_')}", doc)
            self.assertFalse((PACK / "spells" / f"{s.replace('-', '_')}.json").exists(), s)

    def test_manifest_and_ids(self):
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        have = sorted(p.name for p in (PACK / "characters").glob("*.json"))
        self.assertEqual(have, sorted(f"{c}_level_{n}.json" for c in TABLE for n in LEVELS))
        for p in have:
            self.assertIn(f"characters/{p}", man["contents"])
        self.assertEqual(man["contents"], sorted(man["contents"]))
        self.assertEqual(man["api_level"], 2)
        # no id collides with the sample pack (which depends on srd51.core and ships srd51.character.fighter_1)
        sample = {json.loads(p.read_text(encoding="utf-8"))["id"]
                  for p in (ROOT / "content/packs/sample.vertical_slice/characters").glob("*.json")}
        self.assertFalse(sample & {d["id"] for _c, _l, d in docs()})


if __name__ == "__main__":
    unittest.main()
