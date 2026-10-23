#!/usr/bin/env python3
"""srd51.core spell@1 batch (spells refresh) vs the 5e-database table (ctest: content_validate_srd51_spells).

`data/srd51_spells_5e_database_a6212be.json` holds fields copied from 5e-bits/5e-database commit a6212be
(src/2014/en/5e-SRD-Spells.json). For every spell in the table this asserts level, school, classes, casting time, range,
components, duration / concentration, damage dice and type at the base level, upcast / cantrip scaling, the save, the
attack type and the area. The mechanics the loaders read (forced_move push, teleport, timed modifiers) are pinned in
test_mechanics. docs/SRD_SPELLS_DEFERRED.md lists the authored / deferred split.
"""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
TABLE = json.loads((Path(__file__).parent / "data/srd51_spells_5e_database_a6212be.json").read_text(encoding="utf-8"))["spells"]
CAST = {"1 action": "action", "1 bonus action": "bonus_action", "1 reaction": "reaction"}


def load(idx):
    return json.loads((PACK / "spells" / f"{idx.replace('-', '_')}.json").read_text(encoding="utf-8"))


def dice_count(d):
    return int(re.match(r"(\d+)d", d).group(1))


AREA_FROM_TEXT = {"hypnotic-pattern": {"shape": "cube", "size_ft": 30}}


class SpellTable(unittest.TestCase):
    def test_every_spell_matches_the_table(self):
        for idx, t in TABLE.items():
            with self.subTest(spell=idx):
                d = load(idx)
                self.assertEqual(d["$schema"], "spell@1")
                self.assertEqual(d["id"], "srd51.spell." + idx.replace("-", "_"))
                self.assertEqual((d["name"].casefold(), d["level"], d["school"]), (t["name"].casefold(), t["level"], t["school"]))   # the mirror spells it "Protection From Energy"
                self.assertEqual(d["classes"], t["classes"])
                self.assertEqual(d["casting_time"]["kind"], CAST[t["casting_time"]])
                rng = d["range"]
                if t["range"] in ("Self", "Touch"):
                    self.assertEqual(rng, {"kind": t["range"].lower()})
                else:
                    self.assertEqual(rng, {"kind": "ranged", "feet": int(t["range"].split()[0])})
                comp = {k.upper() for k in ("v", "s", "m") if k in d["components"]}
                self.assertEqual(comp, set(t["components"]))
                if "material" in t:
                    self.assertEqual(d["components"]["m"]["text"].lower(), t["material"].rstrip(".").lower())
                du = d["duration"]
                self.assertEqual(du.get("concentration", False), t["concentration"])
                if t["duration"] == "Instantaneous":
                    self.assertEqual(du, {"kind": "instantaneous"})
                else:
                    n, unit = t["duration"].replace("Up to ", "").split()
                    self.assertEqual((du["kind"], du["amount"]), (unit.rstrip("s") + "s", int(n)))
                eff = d["effects"][0]
                zone = eff.get("zone") if eff["kind"] == "zone" else None
                if zone is not None:   # zone spells: the save sits in the triggers, the area is the zone's shape
                    if "dc" in t:
                        saves = {tr["effect"]["ability"] for tr in zone["triggers"] if tr["effect"]["kind"] == "save"}
                        self.assertEqual(saves, {t["dc"]["ability"]})
                    if "area_of_effect" in t:
                        a = t["area_of_effect"]
                        self.assertEqual((zone["shape"], zone["size_ft"]), (a["type"], a["size"]))
                    self.assertNotIn("area", d)
                    continue
                if "damage_type" in t:
                    lv = t["damage_at"]
                    base = lv[str(min(int(k) for k in lv))]
                    if eff["kind"] == "apply_modifier":   # a damage rider (Divine Favor): no upcast in the SRD
                        m = eff["modifier"]
                        self.assertEqual((m["stat"], m["die"], m["damage_type"]), ("damage", base, t["damage_type"]))
                        self.assertNotIn("scaling", d)
                        continue
                    self.assertEqual(eff["damage"], {"dice": base, "type": t["damage_type"]})
                    if d["level"] == 0:
                        self.assertEqual(d["scaling"]["cantrip"], {"levels": [int(k) for k in sorted(lv, key=int)][1:], "adds": "dice"})
                    else:
                        step = dice_count(lv[str(d["level"] + 1)]) - dice_count(base)
                        self.assertEqual(d["scaling"]["upcast"], {"dice": step})
                if "dc" in t and eff["kind"] == "apply_modifier":   # 7.2ze Bane: the save gates the modifier (no damage, none on success)
                    self.assertEqual((eff["save"]["ability"], eff["save"]["dc"], t["dc"]["success"]), (t["dc"]["ability"], "caster", "none"))
                elif "dc" in t:
                    self.assertEqual((eff["kind"], eff["ability"], eff.get("on_success", "none")), ("save", t["dc"]["ability"], t["dc"]["success"]))   # none: the default (condition / control saves)
                if "attack_type" in t:
                    self.assertEqual((eff["kind"], eff["attack"]), ("spell_attack", t["attack_type"]))
                if "area_of_effect" in t:
                    a = t["area_of_effect"]
                    self.assertEqual((d["area"]["shape"], d["area"]["size_ft"]), (a["type"], a["size"]))
                elif idx in AREA_FROM_TEXT:   # the mirror has no area_of_effect; the SRD text gives it
                    self.assertEqual(d["area"], AREA_FROM_TEXT[idx])
                else:
                    self.assertNotIn("area", d)

    def test_manifest(self):
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        for idx in TABLE:
            self.assertIn(f"spells/{idx.replace('-', '_')}.json", man["contents"])

    def test_batch_3_mechanics(self):
        # Mage Armor: base AC 13 + Dex (ac_floor), 8 hours = 4800 rounds
        self.assertEqual(load("mage-armor")["effects"][0]["modifier"], {"stat": "ac", "ac_floor": 13, "ac_floor_dex": True,
                                                                     "applies_to": "target", "duration": {"kind": "rounds", "n": 4800}})
        # Divine Favor: weapon attacks only
        self.assertEqual(load("divine-favor")["effects"][0]["modifier"]["when"], [{"kind": "weapon_attack"}])
        # Moonbeam: Enter + StartTurn (one per turn, default group), shapechanger disadvantage, movable 60 ft for an action
        z = load("moonbeam")["effects"][0]["zone"]
        self.assertEqual((z["anchor"], z["height_ft"], z["move"]), ("point", 40, {"cost": "action", "feet": 60}))
        self.assertEqual([(tr["on"], tr["shapechanger_disadvantage"]) for tr in z["triggers"]],
                         [("zone_enter", True), ("zone_start_turn", True)])
        # Sleep: 5d8 pool (+2d8 per slot), unconscious, wakes on damage, undead skipped
        sl = load("sleep")
        self.assertEqual(sl["effects"], [{"kind": "hp_pool", "dice": "5d8", "condition": "unconscious", "ends_on_damage": True,
                                          "excludes_creature_type": ["undead"]}])
        self.assertEqual(sl["scaling"], {"upcast": {"dice": 2}})

    def test_mechanics(self):
        # Thunderwave: push 10 ft, failed saves only (R-G9 forced_move rider)
        self.assertEqual(load("thunderwave")["effects"][1], {"kind": "forced_move", "applies_to": "affected", "mode": "push", "feet": 10})
        self.assertEqual(load("lightning-bolt")["area"], {"shape": "line", "size_ft": 100, "width_ft": 5})
        self.assertEqual(load("misty-step")["effects"], [{"kind": "teleport", "max_feet": 30}])
        # Ray of Frost: -10 ft speed until the start of the caster's next turn, only on a hit
        rf = load("ray-of-frost")["effects"][1]
        self.assertEqual(rf["applies_to"], "affected")
        self.assertEqual(rf["modifier"], {"stat": "speed", "speed_scale": {"num": 1, "den": 1}, "speed_sub": 10, "applies_to": "target",
                                          "duration": {"kind": "until_start_of_caster_turn"}})
        self.assertEqual(load("shield-of-faith")["effects"][0]["modifier"],
                         {"stat": "ac", "bonus": 2, "applies_to": "target", "duration": {"kind": "concentration"}})
        self.assertEqual(load("barkskin")["effects"][0]["modifier"],
                         {"stat": "ac", "ac_floor": 16, "applies_to": "target", "duration": {"kind": "concentration"}})

    def test_next_roll_riders(self):
        # Rules #183 (7.2u): duration kinds 3 / 4, no_reactions / no_healing markers, subject_is_caster / owner_creature_type
        self.assertEqual(load("guiding-bolt")["effects"][1]["modifier"],
                         {"stat": "attacks_against", "mode": "advantage", "consume": "next_attack_against", "applies_to": "target",
                          "duration": {"kind": "until_end_of_caster_next_turn"}})
        self.assertEqual(load("vicious-mockery")["effects"][1]["modifier"],
                         {"stat": "attack_roll", "mode": "disadvantage", "consume": "next_attack", "applies_to": "target",
                          "duration": {"kind": "until_end_of_target_turn"}})
        self.assertEqual(load("shocking-grasp")["effects"][1]["modifier"],
                         {"stat": "no_reactions", "applies_to": "target", "duration": {"kind": "until_start_of_target_turn"}})
        ct = load("chill-touch")["effects"]
        self.assertEqual(ct[1]["modifier"], {"stat": "no_healing", "applies_to": "target", "duration": {"kind": "until_start_of_caster_turn"}})
        self.assertEqual(ct[2]["modifier"]["duration"], {"kind": "until_end_of_caster_next_turn"})
        self.assertEqual(ct[2]["modifier"]["when"], [{"kind": "owner_creature_type", "creature_types": ["undead"]}, {"kind": "subject_is_caster"}])
        for idx in ("guiding-bolt", "vicious-mockery", "shocking-grasp", "chill-touch"):
            self.assertTrue(all(e["applies_to"] == "affected" for e in load(idx)["effects"][1:]), idx)

    def test_control_spells(self):
        # Rules #188 (7.2v): control saves (Command word chosen at cast, Fear, Hideous Laughter)
        self.assertEqual(load("command")["effects"], [{"kind": "save", "ability": "wis", "control": "command",
                                                       "control_excludes_creature_types": ["undead"]}])
        self.assertEqual(load("command")["targets"], {"count": 1, "max_spread_ft": 30})
        self.assertEqual(load("command")["scaling"], {"upcast": {"targets": 1}})
        self.assertEqual(load("fear")["effects"], [{"kind": "save", "ability": "wis", "condition": "frightened", "control": "fear",
                                                    "repeat_save": "end_of_turn_unseen"}])
        self.assertEqual(load("hideous-laughter")["effects"], [{"kind": "save", "ability": "wis", "condition": "prone", "control": "laughter",
                                                                "control_min_int": 4, "repeat_save": "end_of_turn_or_damage_advantage"}])

    def test_spell_batch2(self):
        # Rules #228 (7.2zd): Sanctuary ward, Protection from Evil and Good attackers / condition_immunity_from, Aid max_hp_bonus
        self.assertEqual(load("sanctuary")["effects"], [{"kind": "apply_condition", "condition": "warded",
                                                         "ward": {"save": "wis", "on_fail": "retarget_or_lose"},
                                                         "ends_on": ["attack", "harmful_cast"]}])
        types = ["aberration", "celestial", "elemental", "fey", "fiend", "undead"]
        pfeg = load("protection-from-evil-and-good")
        self.assertEqual(pfeg["effects"][0], {"kind": "apply_modifier", "scope": {"from_types": types},
                                              "modifier": {"stat": "attack_roll", "mode": "disadvantage", "applies_to": "attackers",
                                                           "duration": {"kind": "concentration"}}})
        self.assertEqual(pfeg["effects"][1], {"kind": "condition_immunity_from", "conditions": ["charmed", "frightened"], "types": types})
        self.assertTrue(pfeg["components"]["m"]["consumed"])   # consumed with no cost_cp (#234): nothing is checked or removed
        self.assertNotIn("cost_cp", pfeg["components"]["m"])
        aid = load("aid")
        self.assertEqual(aid["effects"], [{"kind": "max_hp_bonus", "flat": 5, "per_slot_above": 5}])
        self.assertEqual(aid["targets"], {"count": 3})

    def test_spell_batch3(self):
        # Rules #237 (7.2ze): Bless / Bane sign + applies_to rolls (Bane's Cha save gate), Blindness/Deafness condition chosen
        for idx, sign in (("bless", "plus"), ("bane", "minus")):
            d = load(idx)
            self.assertEqual(d["effects"][0]["modifier"], {"stat": "attack_roll", "die": "1d4", "sign": sign, "applies_to": ["attack", "save"],
                                                           "duration": {"kind": "concentration"}})
            self.assertEqual((d["targets"], d["scaling"]), ({"count": 3}, {"upcast": {"targets": 1}}))
        self.assertEqual(load("bane")["effects"][0]["save"], {"ability": "cha", "dc": "caster"})
        self.assertNotIn("save", load("bless")["effects"][0])
        bd = load("blindness-deafness")
        self.assertEqual(bd["effects"], [{"kind": "save", "ability": "con", "condition": "chosen", "choices": ["blinded", "deafened"],
                                          "repeat_save": "end_of_turn"}])
        self.assertEqual(bd["scaling"], {"upcast": {"targets": 1}})

    def test_spell_imbue_mirror(self):
        # Rules #257 (7.2zf): weapon_imbue (Shillelagh, Magic Weapon) and duplicates (Mirror Image)
        self.assertEqual(load("shillelagh")["effects"], [{"kind": "weapon_imbue", "weapons": ["srd51.item.club", "srd51.item.quarterstaff"],
                                                         "ability": "spellcasting", "die": 8, "magical": True, "ends_on_release": True}])
        for item in ("club", "quarterstaff"):
            self.assertTrue((PACK / "items" / f"{item}.json").is_file())
        mw = load("magic-weapon")
        self.assertEqual(mw["effects"], [{"kind": "weapon_imbue", "magical": True, "magic_bonus": {"base": 1, "at_slot": [[4, 2], [6, 3]]},
                                          "requires_nonmagical": True}])
        self.assertEqual(mw["duration"], {"kind": "hours", "amount": 1, "concentration": True})
        self.assertNotIn("scaling", mw)   # the at_slot table is the upcast
        mi = load("mirror-image")
        e = dict(mi["effects"][0]); e.pop("description")
        self.assertEqual(e, {"kind": "duplicates", "count": 3, "thresholds": [11, 8, 6], "ac": {"base": 10, "ability": "dex"},
                             "exempt_senses": ["blindsight", "truesight"]})
        self.assertEqual((mi["range"], mi["duration"]), ({"kind": "self"}, {"kind": "minutes", "amount": 1}))
        for word in ("blind", "blindsight", "truesight", "exempt_senses", "dismiss"):
            self.assertIn(word, mi["effects"][0]["description"])
        # the negative-case strings are the loader's (rules5e_content spell_loader.cpp), verbatim
        loader = (ROOT / "engine/rules/content/src/spell_loader.cpp").read_text(encoding="utf-8")
        for msg in ("effect duplicates thresholds must not increase with the remaining count", "effect weapon_imbue at_slot slot levels must ascend",
                    "effect die must be one of 4, 6, 8, 10, 12", "effect weapon_imbue needs ability, die, magical or magic_bonus",
                    "effect weapon_imbue needs concentration or a rounds, minutes or hours duration",
                    "effect duplicates needs a rounds, minutes or hours duration", "effect duplicates is self only",
                    "effect thresholds must be exactly 3 integers 2..20", "magic_weapon: the weapon is already magical"):
            src = loader if msg != "magic_weapon: the weapon is already magical" else (ROOT / "engine/rules/src/encounter.cpp").read_text(encoding="utf-8")
            self.assertIn(f'"{msg}"', src, msg)
        for case in sorted((Path(__file__).parent / "cases").glob("*.expect")):
            if case.stem.startswith(("weapon_imbue_", "duplicates_")) and not case.read_text(encoding="utf-8").startswith("/"):
                self.assertIn(case.read_text(encoding="utf-8").strip(), loader, case.name)

    def test_spell_batch_zg(self):
        # Rules #263 (7.2zg): haste / slow, summon_weapon, temp_hp, spell_attack on_miss
        self.assertEqual(load("haste")["effects"], [{"kind": "haste"}])
        sl = load("slow")
        self.assertEqual(sl["effects"], [{"kind": "slow", "ability": "wis"}])
        self.assertEqual((sl["targets"], sl["area"]), ({"count": 6}, {"shape": "cube", "size_ft": 40}))
        for idx in ("haste", "slow", "heroism"):
            self.assertTrue(load(idx)["duration"]["concentration"])
        sw = load("spiritual-weapon")
        self.assertEqual(sw["effects"], [{"kind": "summon_weapon", "damage": {"dice": "1d8", "type": "force"}, "add_spellcasting_modifier": True,
                                          "upcast_every": 2, "reach_ft": 5, "move_ft": 20}])
        self.assertEqual((sw["casting_time"], sw["duration"], sw["scaling"]),
                         ({"kind": "bonus_action"}, {"kind": "minutes", "amount": 1}, {"upcast": {"dice": 1}}))
        self.assertEqual(load("false-life")["effects"], [{"kind": "temp_hp", "dice": "1d4+4", "per_slot_above": 5}])
        he = load("heroism")
        self.assertEqual(he["effects"], [{"kind": "temp_hp", "ability": "spellcasting", "per_turn": True, "condition_immunity": ["frightened"]}])
        self.assertEqual(he["scaling"], {"upcast": {"targets": 1}})
        aa = load("acid-arrow")
        self.assertEqual(aa["effects"][0]["on_miss"], "half")
        self.assertEqual(aa["effects"][1], {"kind": "delayed_damage", "applies_to": "affected", "damage": {"dice": "2d4", "type": "acid"},
                                            "expiry": {"kind": "until_end_of_target_next_turn"}})
        loader = (ROOT / "engine/rules/content/src/spell_loader.cpp").read_text(encoding="utf-8")
        for msg in ("effect haste needs concentration", "effect slow needs ability", "effect haste takes no ability",   # built as "effect " + kind
                    "effect on_miss must be half", "on_miss is only allowed on the first effect", "effect upcast_every must be an integer 1..9",
                    "effect reach_ft must be a positive multiple of 5", "effect move_ft must be a positive multiple of 5",
                    "effect summon_weapon needs concentration or a rounds, minutes or hours duration",
                    "effect temp_hp needs exactly one of dice or ability", "effect dice invalid", "effect ability must be spellcasting",
                    "effect per_slot_above must be an integer 1..50", "effect per_turn must be a boolean",
                    "effect condition_immunity must be a non-empty list of conditions", "effect condition_immunity items must be unique",
                    "effect temp_hp per_turn or condition_immunity needs concentration or a rounds, minutes or hours duration"):
            if msg == "effect haste needs concentration":
                self.assertIn('"effect " + kind + " needs concentration"', loader)
            else:
                self.assertIn(msg, loader, msg)

    def test_spell_batch_zg_rest(self):
        # Rules #263 (7.2zg) groups 5-9: the pack files carry the same mechanics as Rules' positive specimens
        fx = ROOT / "tests/validate/fixtures/spell_batch_zg"
        for idx in ("acid-arrow", "hellish-rebuke", "protection-from-energy", "blur", "faerie-fire", "hypnotic-pattern"):
            with self.subTest(spell=idx):
                d, f = load(idx), json.loads((fx / (idx.replace("-", "_") + ".json")).read_text(encoding="utf-8"))
                for k in ("casting_time", "range", "duration", "area", "targets", "scaling"):
                    self.assertEqual(d.get(k), f.get(k), k)
                strip = lambda effs: [{k: v for k, v in e.items() if k != "description"} for e in effs]
                self.assertEqual(strip(d["effects"]), strip(f["effects"]))
        self.assertEqual(load("acid-arrow")["scaling"], {"upcast": {"dice": 1}})   # slot 3: 5d4 up front, 3d4 later (Rules' C++ slot-3 test)
        self.assertEqual(load("hellish-rebuke")["casting_time"], {"kind": "reaction", "trigger": {"kind": "damaged", "requires_sight": True}})
        self.assertEqual(load("protection-from-energy")["effects"][0]["damage_type_choices"], ["acid", "cold", "fire", "lightning", "thunder"])
        self.assertEqual(load("blur")["effects"][0]["exempt_senses"], ["blindsight", "truesight"])
        self.assertEqual(load("faerie-fire")["effects"][0]["modifier"]["stat"], "outlined")
        hp = load("hypnotic-pattern")
        self.assertEqual([(e["kind"], e["condition"], e["ends_on"]) for e in hp["effects"]],
                         [("save", "charmed", ["damage", "shake_awake"]), ("apply_condition", "incapacitated", ["damage", "shake_awake"])])
        self.assertNotIn("Not modelled", hp["description"])   # speed 0 is modelled upstream (302f92f)
        loader = (ROOT / "engine/rules/content/src/spell_loader.cpp").read_text(encoding="utf-8")
        for msg in ("casting_time.trigger.kind must be being_hit, spell_cast or damaged",
                    "effect ends_on must be a non-empty list of attack, cast, harmful_cast, damage, shake_awake"):
            self.assertIn(msg, loader, msg)


    def test_spell_batch_zh(self):
        # Rules #276 (7.2zh): weapon_smite (Branding Smite), sustained_damage held_object (Heat Metal), summon_weapon.held (Flame Blade)
        bs = load("branding-smite")
        self.assertEqual(bs["effects"], [{"kind": "weapon_smite", "damage": {"dice": "2d6", "type": "radiant"},
                                          "reveal_invisible": True, "light": {"dim_ft": 5}}])
        self.assertEqual((bs["casting_time"], bs["range"], bs["duration"], bs["scaling"]),
                         ({"kind": "bonus_action"}, {"kind": "self"},
                          {"kind": "minutes", "amount": 1, "concentration": True}, {"upcast": {"dice": 1}}))
        hm = load("heat-metal")
        self.assertEqual(hm["effects"], [{"kind": "sustained_damage", "damage": {"dice": "2d8", "type": "fire"},
                                          "held_object": {"save": "con", "on_fail": "drop",
                                                          "while_held": {"disadvantage": ["attack_rolls", "ability_checks"],
                                                                         "until": "start_of_caster_next_turn"}}}])
        self.assertEqual((hm["casting_time"], hm["range"], hm["duration"], hm["scaling"]),
                         ({"kind": "action"}, {"kind": "ranged", "feet": 60},
                          {"kind": "minutes", "amount": 1, "concentration": True}, {"upcast": {"dice": 1}}))
        for word in ("Weapon", "Armor", "Shield", "metal"):
            self.assertIn(word, hm["description"])
        fb = load("flame-blade")
        self.assertEqual(fb["effects"], [{"kind": "summon_weapon", "damage": {"dice": "3d6", "type": "fire"},
                                          "upcast_every": 2, "reach_ft": 5, "held": True,
                                          "light": {"bright_ft": 10, "dim_ft": 10}}])
        self.assertEqual((fb["casting_time"], fb["range"], fb["duration"], fb["scaling"]),
                         ({"kind": "bonus_action"}, {"kind": "self"},
                          {"kind": "minutes", "amount": 10, "concentration": True}, {"upcast": {"dice": 1}}))
        self.assertNotIn("move_ft", fb["effects"][0])
        self.assertNotIn("add_spellcasting_modifier", fb["effects"][0])
        # loader strings (spell_loader.cpp) and runtime refusals (encounter.cpp), verbatim
        loader = (ROOT / "engine/rules/content/src/spell_loader.cpp").read_text(encoding="utf-8")
        encounter = (ROOT / "engine/rules/src/encounter.cpp").read_text(encoding="utf-8")
        for msg in ("weapon_smite: needs concentration or a rounds, minutes or hours duration",
                    "effect weapon_smite light.dim_ft must be a positive multiple of 5",
                    "effect reveal_invisible must be a boolean",
                    "effect damage invalid",
                    "effect held_object must be {save: con, on_fail: drop, while_held: {disadvantage: [attack_rolls, ability_checks], until: start_of_caster_next_turn}}",
                    "effect held_object is only for sustained_damage",
                    "summon_weapon: held needs no move_ft",
                    "summon_weapon: held takes no add_spellcasting_modifier",
                    "effect summon_weapon held must be a boolean",
                    "effect summon_weapon light.bright_ft and dim_ft must be positive multiples of 5"):
            self.assertIn(f'"{msg}"', loader, msg)
        self.assertIn("is only allowed as the first effect", loader)  # effects[i]: <kind> is only allowed as the first effect
        self.assertIn('" needs a rounds, minutes or hours duration"', loader)  # kind + fragment (sustained_damage, ...)
        for msg in ("weapon_smite: self only",
                    "weapon_smite: primary effect only",
                    "heat_metal: equipment required",
                    "heat_metal: no metal object",
                    "summon_weapon: held already active",
                    "summon_weapon: held is self only",
                    "summon_weapon: held needs a free hand",
                    "summon_weapon: held does not move"):
            self.assertIn(f'"{msg}"', encounter, msg)
        for case in sorted((Path(__file__).parent / "cases").glob("*.expect")):
            msg = case.read_text(encoding="utf-8").strip()
            if case.stem.startswith(("weapon_smite_", "summon_weapon_held_", "held_object_")) and not msg.startswith("/"):
                if "is only allowed as the first effect" in msg:
                    self.assertIn("is only allowed as the first effect", loader, case.name)
                else:
                    self.assertIn(f'"{msg}"', loader, case.name)
        # ZH_KEYS (checks_v4.py) match spell_keys_zh.hpp
        keys_hpp = (ROOT / "engine/rules/content/src/spell_keys_zh.hpp").read_text(encoding="utf-8")
        checks = (ROOT / "tools/validate/checks_v4.py").read_text(encoding="utf-8")
        for val in ("weapon_smite", "reveal_invisible", "light", "dim_ft", "bright_ft", "held_object", "held",
                    "save", "on_fail", "while_held", "disadvantage", "until"):
            self.assertIn(f'"{val}"', keys_hpp, val)
            self.assertIn(f'"{val}"', checks, val)


    def test_zone_and_kind_spells(self):
        self.assertEqual(load("silence")["effects"][0]["zone"]["flags"], ["silence"])
        self.assertEqual(load("spirit-guardians")["effects"][0]["zone"]["anchor"], "creature")
        self.assertEqual(load("dispel-magic")["effects"], [{"kind": "dispel", "check": {"ability": "spellcasting", "dc_base": 10, "plus": "spell_level"}}])
        self.assertEqual(load("revivify")["effects"], [{"kind": "revive", "within_rounds": 10}])
        self.assertEqual(load("revivify")["components"]["m"]["cost_cp"], 30000)
        self.assertEqual(load("lesser-restoration")["effects"][0]["conditions"], ["blinded", "deafened", "paralyzed", "poisoned"])


if __name__ == "__main__":
    unittest.main()
