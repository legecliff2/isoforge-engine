#!/usr/bin/env python3
"""3 -> 4 migrations and @4 schema shape tests (ctest: content_validate_migrations).

docs/proposals/VERSION_BUMP_BATCH.md 8.2 (what 3->4 writes), 4.1 (reactions[] / bonus_actions[] arms),
8.2.1 (the effect anyOf), 8.4 (decorated fixtures) and 10 (equipment stays inert). The migration under test is
tools/validate/migrate_v4.py, the Python reference; the shipping C++ port (row 3, Engine Core) must produce
the same documents from the same fixtures (tests/validate/fixtures/v4/at3). Migrated documents are validated
IN MEMORY: the schema accepts the migration's `inert` stamps, a file on disk that carries them is an error.
No frozen file is edited: the compat corpus is only read (and re-hashed against MANIFEST.sha256).
"""
from __future__ import annotations

import copy
import hashlib
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import v4_helpers as h  # noqa: E402
import checks_v4  # noqa: E402
import migrate_v4 as m  # noqa: E402

ROOT = h.ROOT
COMPAT = ROOT / "tests" / "compat"
SPELLS = {"srd51.spell.shield", "srd51.spell.magic_missile"}


def errs_of(doc, **kw):
    e, w = h.validate_in_memory(doc, **kw)
    return e


class Migration(unittest.TestCase):
    def test_decorated_goblin(self):
        src = h.fixture("at3/decorated_goblin.json")
        out = m.monster_3_to_4(src)
        self.assertEqual(out["$schema"], "monster@4")
        self.assertTrue(out["reactions"][0]["inert"] is True)
        self.assertNotIn("inert", out["bonus_actions"][0])          # special bonus action: neither loaded nor stamped
        self.assertTrue(out["bonus_actions"][1]["inert"] is True)    # kind != special: @4 would load it
        self.assertEqual(out["inert_mechanics"], ["bonus_actions", "reactions"])
        self.assertEqual(out["actions"], src["actions"])             # actions[] never touched: special stays descriptive
        self.assertNotIn("inert", out["actions"][-1])
        self.assertEqual(errs_of(out), [])

    def test_special_only_bonus_actions_add_no_marker(self):
        src = h.fixture("at3/decorated_goblin.json")
        src["bonus_actions"] = [src["bonus_actions"][0]]
        src["reactions"] = []
        out = m.monster_3_to_4(src)
        self.assertNotIn("inert_mechanics", out)
        self.assertEqual(out["bonus_actions"], src["bonus_actions"])
        self.assertEqual(errs_of(out), [])

    def test_reactions_stamp_every_entry_including_special(self):
        src = h.fixture("at3/decorated_goblin.json")
        src["reactions"].append({"id": "sp", "name": "Sp", "kind": "special"})
        out = m.monster_3_to_4(src)
        self.assertEqual([r["inert"] for r in out["reactions"]], [True, True])
        self.assertEqual(errs_of(out), [])

    def test_decorated_fighter(self):
        src = h.fixture("at3/decorated_fighter.json")
        out = m.character_3_to_4(src)
        kinds = {f["id"]: f["effect"] for f in out["features"]}
        self.assertTrue(kinds["sneak_like"]["inert"] is True)       # on_event
        self.assertTrue(kinds["rage_like"]["inert"] is True)        # apply_modifier
        self.assertNotIn("inert", kinds["fighting_style_dueling"])  # damage_bonus: live at @3
        self.assertNotIn("inert", kinds["second_wind"])             # self_heal: live at @3
        self.assertEqual(out["inert_mechanics"], ["equipment", "reactions"])
        self.assertTrue(out["reactions"][0]["inert"])
        self.assertEqual(errs_of(out), [])

    def test_live_at_3_kinds_are_never_marked(self):
        doc = h.fixture("at3/decorated_fighter.json")
        doc["features"] = [{"id": k, "name": k, "effect": e} for k, e in [
            ("db", {"kind": "damage_bonus", "amount": 1}), ("sh", {"kind": "self_heal", "dice": "1d4"}),
            ("ba", {"kind": "bonus_action_grant", "actions": ["dash"]}),
            ("dm", {"kind": "damage_mitigation", "resistances": {"nonmagical": ["slashing"]}}),
            ("mw", {"kind": "magic_weapons"}), ("ma", {"kind": "multiattack", "attacks": ["longsword"]}),
            ("unk", {"kind": "mystery_kind"})]]
        out = m.character_3_to_4(doc)
        self.assertTrue(all("inert" not in f["effect"] for f in out["features"]))
        for k in ("on_event", "apply_modifier", "grant_action", "heal_pool", "save_effect", "reaction"):
            d = copy.deepcopy(doc); d["features"] = [{"id": "x", "name": "x", "effect": {"kind": k}}]
            self.assertTrue(m.character_3_to_4(d)["features"][0]["effect"]["inert"], k)

    def test_frozen_list_is_one_list_everywhere(self):
        lst = set(m.NEW_EFFECT_KINDS_AT_4)
        self.assertEqual(lst, set(checks_v4.NEW_EFFECT_KINDS_AT_4))
        self.assertEqual(lst, set(m.effect_kinds_introduced_at(4)))
        self.assertEqual(m.effect_kinds_introduced_at(3), ())
        _reg, by_tag = h.registry()
        self.assertEqual(lst, set(by_tag["common@2"]["$defs"]["effect_inert"]["properties"]["kind"]["enum"]))
        self.assertEqual(set(by_tag["common@2"]["$defs"]["effect_live"]["allOf"][1]["properties"]["kind"]["not"]["enum"]), lst)
        self.assertEqual(set(m.LIVE_AT_3_EFFECT_KINDS), set(checks_v4.LIVE_AT_3_EFFECT_KINDS))
        self.assertFalse(lst & set(m.LIVE_AT_3_EFFECT_KINDS))

    def test_merge_never_overwrites_inert_mechanics(self):
        src = h.fixture("at3/merge_goblin.json")
        self.assertEqual(src["inert_mechanics"], ["damage_resistances", "damage[1..]"])
        out = m.monster_3_to_4(src)
        self.assertEqual(out["inert_mechanics"], sorted({"damage_resistances", "damage[1..]", "reactions", "bonus_actions"}))
        # the key keeps its position (assigned in place), and nothing else moved
        self.assertEqual(list(out), list(src))
        # a file with nothing to add keeps exactly what the 2->3 migration wrote
        bare = copy.deepcopy(src); bare["reactions"] = []; bare["bonus_actions"] = []
        self.assertEqual(m.monster_3_to_4(bare)["inert_mechanics"], ["damage_resistances", "damage[1..]"])
        # duplicates collapse
        dup = copy.deepcopy(src); dup["inert_mechanics"] = ["reactions", "reactions", "zz"]
        self.assertEqual(m.monster_3_to_4(dup)["inert_mechanics"], ["bonus_actions", "reactions", "zz"])
        self.assertEqual(errs_of(out), [])

    def test_pure_idempotent_key_order(self):
        for rel, fn in (("at3/decorated_goblin.json", m.monster_3_to_4), ("at3/decorated_fighter.json", m.character_3_to_4),
                        ("at3/merge_goblin.json", m.monster_3_to_4), ("at3/bogus_inert_fighter.json", m.character_3_to_4)):
            with self.subTest(rel):
                src = h.fixture(rel)
                before = copy.deepcopy(src)
                once = fn(src)
                self.assertEqual(src, before, "the input must not be modified")
                self.assertEqual(fn(once), once, "idempotent")
                self.assertEqual(fn(fn(src)), once)
                # key order of the input is preserved (new keys only appended)
                self.assertEqual([k for k in once if k in src], list(src))
                self.assertEqual(list(once)[:len(src)], list(src))
                # a document that is not @3 is returned as is
                self.assertEqual(m.migrate_3_to_4({"$schema": "monster@2", "x": 1}, "monster"), {"$schema": "monster@2", "x": 1})

    def test_nothing_else_is_touched(self):
        src = h.fixture("at3/decorated_fighter.json")
        out = m.character_3_to_4(src)
        for k in src:
            if k not in ("$schema", "reactions", "features", "inert_mechanics"):
                self.assertEqual(out[k], src[k], k)
        for a, b in zip(src["features"], out["features"]):
            self.assertEqual({k: v for k, v in a.items() if k != "effect"}, {k: v for k, v in b.items() if k != "effect"})

    def test_equipment_inert_for_migrated_files(self):
        src = h.fixture("at3/decorated_fighter.json")
        self.assertIn("equipment", m.character_3_to_4(src)["inert_mechanics"])
        src["equipment"] = []
        self.assertNotIn("equipment", m.character_3_to_4(src).get("inert_mechanics", []))
        mon = h.fixture("at3/decorated_goblin.json")
        self.assertNotIn("equipment", m.monster_3_to_4(mon)["inert_mechanics"])

    def test_inert_effect_with_unvalidated_parameters(self):
        """8.4 last row / 8.3 relaxed rule: a migrated file with an inert on_event whose `on` is bogus validates;
        unstamped it is a schema error; in a file on disk the stamp itself is an error."""
        out = m.character_3_to_4(h.fixture("at3/bogus_inert_fighter.json"))
        eff = out["features"][0]["effect"]
        self.assertEqual((eff["kind"], eff["on"], eff["inert"]), ("on_event", "whenever_i_like", True))
        e, w = h.validate_in_memory(out)
        self.assertEqual(e, [])
        self.assertEqual(w, [], "an inert new-set effect is not unknown-kind noise")
        bad = copy.deepcopy(out); del bad["features"][0]["effect"]["inert"]
        self.assertTrue(errs_of(bad), "without the stamp the bogus `on` is a schema error")
        on_disk, _ = h.validate_docs({"characters/bogus.json": out})
        self.assertTrue(any("'inert' is reserved" in x for x in on_disk), on_disk)

    def test_migrated_files_on_disk_are_rejected_for_authored_inert(self):
        goblin = m.monster_3_to_4(h.fixture("at3/decorated_goblin.json"))
        errs, _ = h.validate_docs({"monsters/g.json": goblin})
        self.assertTrue(any("/reactions/0: 'inert' is reserved" in e for e in errs), errs)
        self.assertTrue(any("/bonus_actions/1: 'inert' is reserved" in e for e in errs), errs)
        self.assertFalse(any("/bonus_actions/0" in e for e in errs), "the special bonus action carries no stamp")

    def test_every_at3_fixture_migrates_and_validates_as_at4(self):
        n = 0
        for rel in sorted(p.relative_to(h.FIXTURES).as_posix() for p in (h.FIXTURES / "at3").glob("*.json")):
            src = h.fixture(rel)
            typ = src["$schema"].split("@")[0]
            self.assertTrue(src["$schema"].endswith("@3"), rel)
            if "inert_mechanics" not in src:    # a 2->3 migration result (in memory only) is not a valid authored @3 file
                e3, _ = h.validate_in_memory(src)   # the @3 original is valid at @3 (ahead-of-engine kinds only warn)
                self.assertEqual(e3, [], (rel, e3))
            out = m.migrate_3_to_4(src, typ)
            self.assertEqual(errs_of(out), [], rel)
            n += 1
        self.assertGreaterEqual(n, 4)

    def test_every_at3_document_of_the_repo_migrates_and_validates(self):
        count = 0
        for root in (ROOT / "content" / "packs", ROOT / "tests" / "compat"):
            for f in sorted(root.rglob("*.json")):
                d = json.loads(f.read_text(encoding="utf-8"))
                if isinstance(d, dict) and d.get("$schema") in ("monster@3", "character@3"):
                    typ = d["$schema"].split("@")[0]
                    out = m.migrate_3_to_4(d, typ)
                    e, _ = h.validate_in_memory(out, spell_ids=SPELLS)
                    self.assertEqual(e, [], f.as_posix())
                    count += 1
        print(f"(migrated {count} repo @3 documents)", file=sys.stderr)

    def test_fl1_frozen_corpus_migrates_as_documented_and_is_not_edited(self):
        """8.4: fl* files gain inert_mechanics / stamps only where they have non-empty reactions / loadable
        bonus_actions; the stamps exist only in memory. fl1 is monster@1 / character@1 on disk; the C++ chain
        brings it to @3 first, so here its content is retagged to @3 (this port starts at 3)."""
        goblin = json.loads((COMPAT / "fl1/sample.vertical_slice/monsters/goblin.json").read_text(encoding="utf-8"))
        fighter = json.loads((COMPAT / "fl1/sample.vertical_slice/characters/fighter_1.json").read_text(encoding="utf-8"))
        goblin["$schema"], fighter["$schema"] = "monster@3", "character@3"
        g4, f4 = m.monster_3_to_4(goblin), m.character_3_to_4(fighter)
        self.assertEqual({k: v for k, v in g4.items() if k != "$schema"}, {k: v for k, v in goblin.items() if k != "$schema"})
        self.assertNotIn("inert_mechanics", g4)                                   # no reactions, no bonus_actions
        self.assertEqual(f4["inert_mechanics"], ["equipment"])                    # non-empty equipment list stays inert
        self.assertEqual(f4["bonus_actions"], [])                                 # empty list: nothing stamped
        self.assertEqual(errs_of(g4), [])
        self.assertEqual(errs_of(f4), [])
        # nothing under tests/compat was touched (compat_freeze would also catch it)
        for line in (COMPAT / "MANIFEST.sha256").read_text(encoding="utf-8").splitlines():
            digest, rel = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((COMPAT / rel).read_bytes()).hexdigest(), digest, rel)


class V4SchemaShapes(unittest.TestCase):
    """reactions[] / bonus_actions[] arms (4.1), effect arms (8.2.1), qualifiers, per-tag table."""

    @classmethod
    def setUpClass(cls):
        cls.dragon = h.pack_fixtures()["monsters/red_dragon.json"]

    def bonus(self, *entries):
        d = copy.deepcopy(self.dragon)
        d["bonus_actions"] = list(entries)
        d["actions"] = [a for a in d["actions"]]
        e, _ = h.validate_in_memory(d, spell_ids={"srd51.spell.shield", "srd51.spell.magic_missile", "srd51.spell.fire_bolt",
                                                  "srd51.spell.hold_person", "srd51.spell.cure_wounds", "srd51.spell.counterspell"})
        return [x for x in e if "/bonus_actions" in x or "bonus_actions" in x]

    SPECIAL = {"id": "preen", "name": "Preen", "kind": "special"}
    MECH = {"id": "buffet", "name": "Buffet", "kind": "save_effect", "save": {"ability": "str", "dc": 12}, "apply": [{"condition": "prone"}]}
    LEGACY = {"id": "kick", "name": "Kick", "kind": "melee_weapon_attack", "to_hit": 4, "reach": 5,
              "damage": [{"dice": "1d4", "type": "bludgeoning"}], "inert": True}

    def test_bonus_actions_three_arms(self):
        self.assertEqual(self.bonus(self.SPECIAL), [], "special unstamped is valid (arm 3)")
        self.assertEqual(self.bonus({**self.SPECIAL, "inert": True}), [], "special stamped is valid (arm 3)")
        self.assertEqual(self.bonus(self.MECH), [], "mechanical non-special v2 action is valid (arm 1)")
        self.assertEqual(self.bonus(self.LEGACY), [], "stamped non-special is valid (arm 2)")
        self.assertTrue(self.bonus({**self.SPECIAL, "save": {"ability": "str", "dc": 12}}), "unstamped special read as mechanical is invalid")
        self.assertTrue(self.bonus({**self.SPECIAL, "usage": {"kind": "at_will"}}))
        self.assertTrue(self.bonus({**self.LEGACY, "inert": False}), "non-special with inert:false is invalid")
        legacy_unstamped = {k: v for k, v in self.LEGACY.items() if k != "inert"}
        self.assertEqual(self.bonus(legacy_unstamped), [], "an unstamped v1-shaped non-special is a valid MECHANICAL v2 action (arm 1)")
        self.assertTrue(self.bonus({**self.MECH, "inert": True}), "a v2-only shape cannot be stamped")

    def test_bonus_action_arms_are_pairwise_disjoint(self):
        from jsonschema import Draft202012Validator
        reg, by_tag = h.registry()
        defs = "https://isoforge.dev/schemas/common/v2.schema.json#/$defs/"
        arm = lambda n: Draft202012Validator({"$ref": defs + n}, registry=reg)
        a1 = Draft202012Validator({"allOf": [{"$ref": defs + "action"}, {"properties": {"kind": {"not": {"const": "special"}}}}]}, registry=reg)
        a2, a3 = arm("legacy_nonspecial_action"), arm("legacy_special_action")
        samples = [self.SPECIAL, {**self.SPECIAL, "inert": True}, self.MECH, self.LEGACY,
                   {k: v for k, v in self.LEGACY.items() if k != "inert"}, {**self.LEGACY, "kind": "special"}]
        for s in samples:
            hits = [a.is_valid(s) for a in (a1, a2, a3)]
            self.assertLessEqual(sum(hits), 1, (s, hits))

    def test_reactions_two_arms(self):
        def reactions(*entries):
            d = copy.deepcopy(self.dragon); d["reactions"] = list(entries)
            e, _ = h.validate_in_memory(d, spell_ids={"srd51.spell.shield", "srd51.spell.magic_missile", "srd51.spell.fire_bolt", "srd51.spell.hold_person", "srd51.spell.cure_wounds", "srd51.spell.counterspell"})
            return [x for x in e if "reactions" in x]
        mech = {"id": "p", "name": "P", "window": {"kind": "being_hit"}, "effect": {"kind": "ac_bonus", "bonus": 2}}
        for kind in ("melee_weapon_attack", "ranged_weapon_attack", "melee_spell_attack", "ranged_spell_attack", "special"):
            extra = {} if kind == "special" else {"to_hit": 1, "damage": [{"dice": "1d4", "type": "fire"}],
                                                  **({"reach": 5} if kind.startswith("melee") else {"range": [5, 10]})}
            self.assertEqual(reactions({"id": "l", "name": "L", "kind": kind, "inert": True, **extra}), [], kind)   # every v1 kind, stamped
            self.assertTrue(reactions({"id": "l", "name": "L", "kind": kind, **extra}), f"{kind} unstamped legacy is invalid")
        self.assertEqual(reactions(mech), [])
        self.assertTrue(reactions({**mech, "inert": True}), "reaction arm forbids inert; legacy arm lacks window")
        self.assertTrue(reactions({"id": "x", "name": "X", "kind": "special", "inert": True, "window": {"kind": "being_hit"}}))

    def test_effect_arms(self):
        def effect(eff):
            d = copy.deepcopy(self.dragon); d["traits"] = [{"id": "t", "name": "T", "effect": eff}]
            e, w = h.validate_in_memory(d, spell_ids={"srd51.spell.shield", "srd51.spell.magic_missile", "srd51.spell.fire_bolt", "srd51.spell.hold_person", "srd51.spell.cure_wounds", "srd51.spell.counterspell"})
            return [x for x in e if "traits" in x], w
        self.assertEqual(effect({"kind": "on_event", "on": "bogus", "inert": True, "effects": []})[0], [])
        self.assertTrue(effect({"kind": "on_event", "on": "bogus", "effects": []})[0])
        self.assertEqual(effect({"kind": "save_effect", "inert": True})[0], [], "stamped action kinds validate as effect_inert")
        self.assertTrue(effect({"kind": "save_effect"})[0])
        self.assertEqual(effect({"kind": "self_heal", "dice": "1d4", "inert": True})[0], [], "a stamped live-at-@3 kind (2->3 chain) validates")
        self.assertTrue(effect({"kind": "self_heal"})[0], "live kinds keep the v1 contract")
        e, w = effect({"kind": "on_evnt", "on": "hit"})
        self.assertEqual(e, [], "an unknown kind stays a warning, never an error")
        self.assertTrue(any("unknown effect kind 'on_evnt'" in x for x in w), w)
        self.assertEqual(effect({"kind": "apply_modifier", "modifier": {"stat": "ac", "bonus": 1, "duration": {"kind": "rounds", "n": 2}}})[0], [])
        self.assertEqual(effect({"kind": "grant_action", "cost": "free", "count": 2})[0], [])
        self.assertTrue(effect({"kind": "grant_action", "count": 3})[0])
        self.assertTrue(effect({"kind": "heal_pool"})[0])
        self.assertEqual(effect({"kind": "heal_pool", "pool": "wing_dust", "cost": "reaction"})[0], [], "cost: reaction is legal at @4")
        self.assertEqual(effect({"kind": "multiattack", "attacks": ["bite"]})[0], [], "multiattack carried unchanged")
        self.assertEqual(effect({"kind": "damage_mitigation", "resistances": {"nonmagical": ["slashing"]}})[0], [])
        self.assertEqual(effect({"kind": "magic_weapons"})[0], [])

    def test_resources_and_reaction_cost_are_rejected_before_4(self):
        fighter = h.fixture("at3/decorated_fighter.json")
        e = errs_of(h.edit(fighter, [(["resources"], [{"id": "r", "max": 1, "recharge": "never"}])]))
        self.assertTrue(any("resources" in x for x in e), e)
        e = errs_of(h.edit(fighter, [(["features", 1, "effect", "cost"], "reaction")]))
        self.assertTrue(any("cost 'reaction' is reserved" in x for x in e), e)
        for key in ("forward_compat", "inert_mechanics"):
            e = errs_of(h.edit(fighter, [([key], [])]))
            self.assertTrue(any(key in x for x in e), (key, e))
        # ... and accepted at @4
        f4 = m.character_3_to_4(fighter)
        f4["features"][1]["effect"]["cost"] = "reaction"
        self.assertEqual(errs_of(f4), [])

    def test_qualifiers_on_every_action_def(self):
        _reg, by_tag = h.registry()
        defs = by_tag["common@2"]["$defs"]
        for name in ("action", "legacy_action", "legacy_nonspecial_action", "legacy_special_action"):
            q = defs[name]["properties"]["qualifiers"]
            self.assertEqual(q["items"]["enum"], ["magical", "silvered", "adamantine"], name)
            self.assertIs(q["uniqueItems"], True, name)
        d = copy.deepcopy(self.dragon)
        d["actions"][1]["qualifiers"] = ["magical", "magical"]
        self.assertTrue(errs_of(d, spell_ids={"srd51.spell.shield"}))

    def test_legacy_defs_share_v1_property_list(self):
        _reg, by_tag = h.registry()
        v1 = by_tag["common@1"]["$defs"]["action"]["properties"]
        v2 = by_tag["common@2"]["$defs"]
        for name in ("legacy_action", "legacy_nonspecial_action", "legacy_special_action"):
            self.assertEqual(set(v2[name]["properties"]), set(v1) | {"inert"}, name)
            for k in set(v1) - {"kind"}:
                want = json.dumps(v1[k], sort_keys=True).replace('"#/$defs/', '"../common/v1.schema.json#/$defs/')   # v1's local refs become absolute
                self.assertEqual(json.dumps(v2[name]["properties"][k], sort_keys=True), want, (name, k))
        self.assertEqual(v2["legacy_action"]["allOf"], by_tag["common@1"]["$defs"]["action"]["allOf"])
        self.assertEqual(v2["legacy_nonspecial_action"]["allOf"], by_tag["common@1"]["$defs"]["action"]["allOf"])
        self.assertNotIn("allOf", v2["legacy_special_action"])
        self.assertEqual(v2["legacy_special_action"]["required"], ["id", "name", "kind"])

    def test_tag_table_matches_the_schemas(self):
        reg, by_tag = h.registry()
        c2, c1 = by_tag["common@2"]["$defs"], by_tag["common@1"]["$defs"]
        self.assertEqual(set(checks_v4.TAG_TABLE["monster@4"].action_kinds), set(c2["action"]["properties"]["kind"]["enum"]))
        self.assertEqual(set(checks_v4.TAG_TABLE["monster@3"].action_kinds), set(c1["action"]["properties"]["kind"]["enum"]))
        self.assertEqual(set(checks_v4.TAG_TABLE["character@4"].cost_values), set(c1["effect_cost"]["enum"]))   # v1's enum already has reaction
        self.assertEqual(set(checks_v4.TAG_TABLE["character@3"].cost_values), set(c1["effect_cost"]["enum"]) - {"reaction"})
        for tag in ("monster@3", "monster@4", "character@3", "character@4"):
            sb = c2["stat_block"] if tag.endswith("@4") else c1["stat_block"]
            declared = set(sb["properties"]) | set(by_tag[tag]["properties"])
            self.assertEqual(set(checks_v4.TAG_TABLE[tag].top_level_keys), declared, tag)
        self.assertEqual(set(checks_v4.TAG_TABLE["monster@4"].top_level_keys) - set(checks_v4.TAG_TABLE["monster@3"].top_level_keys),
                         {"resources", "forward_compat", "inert_mechanics", "no_breath", "shapechanger", "evil"})   # + R-G11 reserved creature facts (4.6)
        self.assertEqual(set(checks_v4.TAG_TABLE["character@4"].top_level_keys) - set(checks_v4.TAG_TABLE["character@3"].top_level_keys),
                         {"resources", "forward_compat", "inert_mechanics", "no_breath", "shapechanger", "evil"})
        self.assertEqual(sorted(t for t, r in checks_v4.TAG_TABLE.items() if r.needs_api_level > 1),
                         ["character@4", "character@5", "character@6", "item@1", "item@2", "monster@4", "monster@5", "monster@6"])   # @5 / item@2: EQUIPMENT 7.1; @6: visual

    def test_every_v3_property_is_carried_by_v4(self):
        _reg, by_tag = h.registry()
        c2sb = by_tag["common@2"]["$defs"]["stat_block"]["properties"]
        for typ in ("monster", "character"):
            carried = set(c2sb) | set(by_tag[f"{typ}@4"]["properties"])
            v3 = set(by_tag["common@1"]["$defs"]["stat_block"]["properties"]) | set(by_tag[f"{typ}@3"]["properties"])
            self.assertEqual(sorted(v3 - carried), [], typ)
            self.assertEqual(set(by_tag[f"{typ}@4"]["required"]), set(by_tag[f"{typ}@3"]["required"]))
        self.assertEqual(set(by_tag["common@2"]["$defs"]["stat_block"]["required"]), set(by_tag["common@1"]["$defs"]["stat_block"]["required"]))

    def test_integers_only(self):
        e, _ = h.validate_docs(dict(h.pack_fixtures(), **{"monsters/red_dragon.json": h.edit(self.dragon, [(["actions", 4, "save", "dc"], 21.0)])}))
        self.assertTrue(any("float value" in x for x in e), e)


if __name__ == "__main__":
    unittest.main()
