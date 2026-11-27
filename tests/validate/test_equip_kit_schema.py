"""equip_kit@1: schema, cross-checks, and the shipped (empty) H4 catalog.

equip_kit@1 is the first version of this document. item@1, spell@1 and manifest@1
likewise have no migration into their first version. Optional pivot and back_role
stay valid when absent, which is the compatible shape. The type is not registered
in the engine schema registry: it is presentation art, not a sim content document,
and it does not move a combat hash.
"""
from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "validate"))
import equip_kit  # noqa: E402
import validate_packs as vp  # noqa: E402

CORE = ROOT / "content" / "packs" / "srd51.core"
ITEMS = equip_kit.item_docs([CORE])


def write_png(path: Path, w: int, h: int, color_type: int = 6) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    channels = 4 if color_type == 6 else 3 if color_type == 2 else 4

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0)
    raw = b"".join(b"\x00" + bytes(w * channels) for _ in range(h))
    blob = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    path.write_bytes(blob)


def check(doc: dict, files: tuple[str, ...] = (), atlas: tuple[str, int, int] | None = None,
          extra: tuple[str, ...] = ()) -> list[str]:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for rel in files:
            write_png(root / rel, 24, 24)
        for rel in extra:
            write_png(root / rel, 24, 24)
        if atlas:
            write_png(root / atlas[0], atlas[1], atlas[2])
        (root / "kit.json").write_text(json.dumps(doc), encoding="utf-8")
        return equip_kit.validate_kit(root / "kit.json", ITEMS)


def base(**kw) -> dict:
    doc = {"$schema": "equip_kit@1", "cell": [24, 24]}
    doc.update(kw)
    return doc


def views(names: tuple[str, ...], pivot=None, region_for=None, part: str = "") -> list[dict]:
    out = []
    for name in names:
        view = {"view": name}
        if pivot is not None:
            view["pivot"] = pivot
        if region_for is not None:
            # region_for is (slug, x). Name is slug_part.view, matched whole.
            view["region"] = {"name": f"{region_for[0]}_{part}.{name}", "x": region_for[1], "y": 0, "w": 24, "h": 24}
        out.append(view)
    return out


def entry(cid: str, part: str, role: str | None = None, view_names: tuple[str, ...] = ("se", "ne"),
          pivot=None, bin_name: str | None = None, region_for=None) -> dict:
    part_doc = {"part": part, "views": views(view_names, pivot, region_for, part)}
    if bin_name:
        part_doc["bin"] = bin_name
    doc = {"content_id": cid, "parts": [part_doc]}
    if role is not None:
        doc["back_role"] = role
    return doc


def flat(cid: str, part: str, view_names: tuple[str, ...] = ("se", "ne"), bin_name: str = "") -> tuple[str, ...]:
    leaf = f"{bin_name}/" if bin_name else ""
    return tuple(f"{cid}/{leaf}{part}.{name}.png" for name in view_names)


def body_bins() -> set[str]:
    """Size bins equip_kit@1 allows. The schema enum is the bin field's allow-list."""
    schema = json.loads((ROOT / "schemas" / "equip_kit" / "v1.schema.json").read_text(encoding="utf-8"))
    return set(schema["$defs"]["bin_name"]["enum"])


class EquipKitSchema(unittest.TestCase):
    def test_v1_is_the_first_version_and_optional_fields_may_be_absent(self):
        # No equip_kit@0, and no engine migration: the first published body is @1.
        names = sorted(p.name for p in (ROOT / "schemas" / "equip_kit").glob("v*.schema.json"))
        self.assertEqual(names, ["v1.schema.json"])
        self.assertEqual(check(base()), [])
        self.assertEqual(check(base(items=[])), [])
        # Pivot and back_role are additive. Absent still means bottom-centre and no role.
        wired = base(items=[entry("srd51.item.quiver", "back", role="quiver", pivot=[12, 20])])
        self.assertEqual(wired["$schema"], "equip_kit@1")
        self.assertEqual(check(wired, files=flat("srd51.item.quiver", "back")), [])

    def test_neck_is_accepted_and_neck_ne_is_rejected(self):
        ok = base(items=[entry("srd51.item.holy_symbol_amulet", "neck", view_names=("se",), pivot=[12, 20])])
        self.assertEqual(check(ok, files=flat("srd51.item.holy_symbol_amulet", "neck", ("se",))), [])
        bad = base(items=[entry("srd51.item.holy_symbol_amulet", "neck", view_names=("ne",))])
        errs = check(bad, files=flat("srd51.item.holy_symbol_amulet", "neck", ("ne",)))
        self.assertTrue(errs, errs)
        self.assertFalse(any("missing view file" in e for e in errs), errs)
        self.assertTrue(any("neck" in e or "se" in e for e in errs), errs)

    def test_unknown_part_unknown_back_role_and_a_pivot_outside_the_cell(self):
        for part in ("hip-main", "hip_off", "hip-left", "hip-right", "waist"):
            errs = check(base(items=[entry("srd51.item.pouch", part)]))
            self.assertTrue(errs, part)
            self.assertTrue(any(part in e or "not one of" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.quiver", "back", role="slung")]))
        self.assertTrue(any("slung" in e or "not one of" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.quiver", "back", role="quiver", pivot=[24, 20])]),
                     files=flat("srd51.item.quiver", "back"))
        self.assertTrue(any("outside the 24x24 cell" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.quiver", "back", role="quiver", pivot=[12, 24])]),
                     files=flat("srd51.item.quiver", "back"))
        self.assertTrue(any("outside the 24x24 cell" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.quiver", "back", role="quiver", pivot=[-1, 0])]))
        self.assertTrue(errs, errs)
        # H4's provisional pivot is inside a 24x24 cell.
        ok = base(items=[entry("srd51.item.quiver", "back", role="quiver", pivot=[12, 20])])
        self.assertEqual(check(ok, files=flat("srd51.item.quiver", "back")), [])

    def test_west_is_not_a_view_and_a_non_neck_part_needs_both_views(self):
        errs = check(base(items=[entry("srd51.item.backpack", "back", role="pack", view_names=("sw", "ne"))]))
        self.assertTrue(errs, errs)
        errs = check(base(items=[entry("srd51.item.backpack", "back", role="pack", view_names=("se",))]),
                     files=flat("srd51.item.backpack", "back", ("se",)))
        self.assertTrue(any("missing view ne" in e for e in errs), errs)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_png(root / "srd51.item.quiver" / "back.sw.png", 24, 24)
            (root / "kit.json").write_text(json.dumps(base()), encoding="utf-8")
            disk = equip_kit.validate_kit(root / "kit.json", ITEMS)
        self.assertTrue(any("west is mirrored" in e for e in disk), disk)

    def test_content_ids_match_exactly_and_back_roles_are_specific(self):
        # A longer id that contains "quiver" is not the quiver.
        errs = check(base(items=[entry("srd51.item.quiver_extra", "back", role="quiver")]))
        self.assertTrue(any("unknown content_id" in e for e in errs), errs)
        # The bolt case is its own id. Calling it a quiver is rejected.
        errs = check(base(items=[entry("srd51.item.case_crossbow_bolt", "back", role="quiver")]),
                     files=flat("srd51.item.case_crossbow_bolt", "back"))
        self.assertTrue(any("back_role must be bolt_case" in e for e in errs), errs)
        self.assertFalse(any("unknown content_id" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.quiver", "back")]),
                     files=flat("srd51.item.quiver", "back"))
        self.assertTrue(any("back_role must be quiver" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.backpack", "back", role="other")]),
                     files=flat("srd51.item.backpack", "back"))
        self.assertTrue(any("back_role must be pack" in e for e in errs), errs)
        ok = base(items=[
            entry("srd51.item.quiver", "back", role="quiver", pivot=[12, 20]),
            entry("srd51.item.case_crossbow_bolt", "back", role="bolt_case", pivot=[12, 20]),
            entry("srd51.item.backpack", "back", role="pack", pivot=[12, 20]),
        ])
        files = (flat("srd51.item.quiver", "back") + flat("srd51.item.case_crossbow_bolt", "back")
                 + flat("srd51.item.backpack", "back"))
        self.assertEqual(check(ok, files=files), [])

    def test_shields_and_two_handed_weapons_have_no_back_role(self):
        errs = check(base(items=[entry("srd51.item.shield", "back", role="other")]),
                     files=flat("srd51.item.shield", "back"))
        self.assertTrue(any("shield" in e and "back_role" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.greatsword", "back", role="other")]),
                     files=flat("srd51.item.greatsword", "back"))
        self.assertTrue(any("two-handed" in e for e in errs), errs)
        # The back-slung shield view is allowed. The role stays absent.
        self.assertEqual(check(base(items=[entry("srd51.item.shield", "back")]),
                               files=flat("srd51.item.shield", "back")), [])
        self.assertEqual(check(base(items=[entry("srd51.item.longbow", "back")]),
                               files=flat("srd51.item.longbow", "back")), [])

    def test_holy_symbol_is_neck_and_the_component_pouch_is_hip(self):
        errs = check(base(items=[entry("srd51.item.holy_symbol_amulet", "back")]),
                     files=flat("srd51.item.holy_symbol_amulet", "back"))
        self.assertTrue(any("neck" in e for e in errs), errs)
        errs = check(base(items=[entry("srd51.item.component_pouch", "back")]),
                     files=flat("srd51.item.component_pouch", "back"))
        self.assertTrue(any("hip" in e for e in errs), errs)
        ok = base(items=[entry("srd51.item.component_pouch", "hip", pivot=[12, 20])])
        self.assertEqual(check(ok, files=flat("srd51.item.component_pouch", "hip")), [])

    def test_missing_file_unlisted_sprite_and_a_region_outside_the_atlas(self):
        errs = check(base(items=[entry("srd51.item.quiver", "back", role="quiver")]),
                     files=flat("srd51.item.quiver", "back", ("se",)))
        self.assertTrue(any("missing view file" in e and "back.ne.png" in e for e in errs), errs)
        errs = check(base(), extra=("srd51.item.quiver/back.se.png",))
        self.assertTrue(any("not in the manifest" in e for e in errs), errs)
        item = entry("srd51.item.quiver", "back", role="quiver", region_for=("quiver", 0))
        # Second view's region was placed at x=0 as well; give ne its own x via a hand-built entry.
        item["parts"][0]["views"][1]["region"]["x"] = 24
        ok = base(atlas="equip.png", items=[item])
        self.assertEqual(check(ok, files=flat("srd51.item.quiver", "back"), atlas=("equip.png", 48, 24)), [])
        item["parts"][0]["views"][1]["region"]["x"] = 40
        errs = check(base(atlas="equip.png", items=[item]),
                     files=flat("srd51.item.quiver", "back"), atlas=("equip.png", 48, 24))
        self.assertTrue(any("outside the atlas" in e for e in errs), errs)
        # The bolt case does not wear the quiver's region name.
        bolt = entry("srd51.item.case_crossbow_bolt", "back", role="bolt_case", region_for=("quiver", 0))
        bolt["parts"][0]["views"][1]["region"]["name"] = "quiver.ne"
        bolt["parts"][0]["views"][1]["region"]["x"] = 24
        errs = check(base(atlas="equip.png", items=[bolt]),
                     files=flat("srd51.item.case_crossbow_bolt", "back"), atlas=("equip.png", 48, 24))
        self.assertTrue(any("case_crossbow_bolt_back.se" in e for e in errs), errs)
        self.assertFalse(any("unknown content_id" in e for e in errs), errs)

    def test_size_bin_is_optional_and_a_kind_has_no_bin(self):
        # The second key is a known body bin. small is one of them. The shipped manifest may name one.
        allowed = body_bins()
        self.assertIn("small", allowed)
        doc = base(items=[entry("srd51.item.backpack", "back", role="pack", bin_name="large", pivot=[12, 20])])
        files = flat("srd51.item.backpack", "back", bin_name="large")
        self.assertEqual(check(doc, files=files), [])
        small = base(items=[entry("srd51.item.backpack", "back", role="pack", bin_name="small", pivot=[12, 20])])
        self.assertEqual(check(small, files=flat("srd51.item.backpack", "back", bin_name="small")), [])
        self.assertNotIn("gargantuan", allowed)
        errs = check(base(items=[entry("srd51.item.backpack", "back", role="pack", bin_name="gargantuan")]))
        self.assertTrue(errs, errs)
        self.assertTrue(any("gargantuan" in e for e in errs), errs)
        kind = {"kind": "weapon", "parts": [{"part": "main_hand", "bin": "large",
                                             "views": views(("se", "ne"))}]}
        errs = check(base(kinds=[kind]))
        self.assertTrue(any("no size bin" in e for e in errs), errs)

    def test_a_float_pivot_is_rejected(self):
        doc = base(items=[entry("srd51.item.quiver", "back", role="quiver")])
        doc["items"][0]["parts"][0]["views"][0]["pivot"] = [12.0, 20]
        errs = check(doc, files=flat("srd51.item.quiver", "back"))
        self.assertTrue(any("float" in e for e in errs), errs)

    def test_rgb_without_alpha_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("se", "ne"):
                write_png(root / "srd51.item.quiver" / f"back.{name}.png", 24, 24, color_type=2)
            doc = base(items=[entry("srd51.item.quiver", "back", role="quiver")])
            (root / "kit.json").write_text(json.dumps(doc), encoding="utf-8")
            errs = equip_kit.validate_kit(root / "kit.json", ITEMS)
        self.assertTrue(any("straight-alpha" in e for e in errs), errs)

    def test_shipped_manifest_matches_the_files_on_disk(self):
        live = [p for p in vp.discover() if vp.pack_set_key(p) == "live"]
        self.assertEqual(equip_kit.validate_live(live), [])
        doc = json.loads((ROOT / "content" / "art" / "equipment" / "kit.json").read_text(encoding="utf-8"))
        self.assertEqual(doc["$schema"], "equip_kit@1")
        self.assertEqual(doc["cell"], [24, 24])
        self.assertNotIn("atlas", doc)

        def bins(node):
            found = []
            if isinstance(node, dict):
                if "bin" in node:
                    found.append(node["bin"])
                for value in node.values():
                    found += bins(value)
            elif isinstance(node, list):
                for value in node:
                    found += bins(value)
            return found

        allowed = body_bins()
        self.assertIn("small", allowed)
        used = bins(doc)
        self.assertTrue(set(used) <= allowed, used)


if __name__ == "__main__":
    unittest.main()
