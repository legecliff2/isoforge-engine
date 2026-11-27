"""Column-0 warning and part-qualified atlas region names.

Sprites are tiny PNGs written in the test. Column 0 is the column the west
mirror clips, so ink there warns and a clear column does not. Region names
carry the part, so a shield's back.se and off_hand.se do not both become
shield.se.
"""
from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "validate"))
import equip_kit  # noqa: E402
import validate_packs as vp  # noqa: E402

CORE = ROOT / "content" / "packs" / "srd51.core"
ITEMS = equip_kit.item_docs([CORE])


def write_png(path: Path, w: int, h: int, column0_opaque: int = 0, column0_clear_rgb: int = 0,
              ink_x: int | None = None) -> None:
    """8-bit RGBA. column 0 stays alpha 0 unless ``column0_opaque`` rows are painted.

    ``column0_clear_rgb`` paints RGB with alpha 0 (fully transparent, not a zero
    byte). ``ink_x`` paints a different column opaque so a clear column 0 is not
    just an empty image.
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    rows = []
    for y in range(h):
        row = bytearray(w * 4)
        if y < column0_clear_rgb:
            row[0:4] = b"\xff\x00\x00\x00"
        if y < column0_opaque:
            row[0:4] = b"\xff\x00\x00\xff"
        if ink_x is not None:
            start = ink_x * 4
            row[start:start + 4] = b"\x00\xff\x00\xff"
        rows.append(b"\x00" + bytes(row))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    blob = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
    path.write_bytes(blob)


def _validate(root: Path, doc: dict) -> tuple[list[str], str]:
    (root / "kit.json").write_text(json.dumps(doc), encoding="utf-8")
    buf = io.StringIO()
    with redirect_stdout(buf):
        errs = equip_kit.validate_kit(root / "kit.json", ITEMS)
    return errs, buf.getvalue()


def _quiver(root: Path, se_opaque: int = 0, se_clear_rgb: int = 0, ink_x: int | None = None) -> tuple[list[str], str]:
    for view, opaque, clear in (("se", se_opaque, se_clear_rgb), ("ne", 0, 0)):
        write_png(root / "srd51.item.quiver" / f"back.{view}.png", 24, 24,
                  column0_opaque=opaque, column0_clear_rgb=clear, ink_x=ink_x)
    doc = {
        "$schema": "equip_kit@1",
        "cell": [24, 24],
        "items": [{
            "content_id": "srd51.item.quiver",
            "back_role": "quiver",
            "parts": [{"part": "back", "views": [{"view": "se"}, {"view": "ne"}]}],
        }],
    }
    return _validate(root, doc)


def _region(name: str, x: int) -> dict:
    return {"name": name, "x": x, "y": 0, "w": 24, "h": 24}


class Column0Warning(unittest.TestCase):
    def test_clear_column_0_does_not_warn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            errs, out = _quiver(root, ink_x=1)
        self.assertEqual(errs, [])
        self.assertNotIn("column 0", out)
        self.assertNotIn("warning", out)

    def test_transparent_rgb_in_column_0_does_not_warn(self):
        # Alpha 0 is fully transparent even when the color bytes are not zero.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            errs, out = _quiver(root, se_clear_rgb=24, ink_x=2)
        self.assertEqual(errs, [])
        self.assertNotIn("column 0", out)

    def test_opaque_column_0_warns_and_does_not_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            errs, out = _quiver(root, se_opaque=4)
        self.assertEqual(errs, [], errs)
        self.assertIn("warning srd51.item.quiver/back.se.png: column 0 has 4 non-transparent pixels;", out)
        self.assertIn("west mirror flips around the pivot column and clips column 0", out)
        self.assertNotIn("back.ne.png", out)


class PartQualifiedRegions(unittest.TestCase):
    def test_shield_back_and_off_hand_do_not_collide(self):
        self.assertEqual(equip_kit.region_name("srd51.item.shield", "back", "se"), "shield_back.se")
        self.assertEqual(equip_kit.region_name("srd51.item.shield", "off_hand", "se"), "shield_off_hand.se")
        self.assertNotEqual(
            equip_kit.region_name("srd51.item.shield", "back", "se"),
            equip_kit.region_name("srd51.item.shield", "off_hand", "se"),
        )
        # The old name dropped the part. Both files would have been shield.se.
        self.assertNotEqual(equip_kit.region_name("srd51.item.shield", "back", "se"), "shield.se")

        parts = []
        x = 0
        files = []
        for part in ("back", "off_hand"):
            views = []
            for view in ("se", "ne"):
                rel = f"srd51.item.shield/{part}.{view}.png"
                files.append(rel)
                views.append({"view": view, "region": _region(equip_kit.region_name("srd51.item.shield", part, view), x)})
                x += 24
            parts.append({"part": part, "views": views})
        doc = {"$schema": "equip_kit@1", "cell": [24, 24], "atlas": "equip.png",
               "items": [{"content_id": "srd51.item.shield", "parts": parts}]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for rel in files:
                write_png(root / rel, 24, 24, ink_x=1)
            write_png(root / "equip.png", 96, 24)
            errs, out = _validate(root, doc)
        self.assertEqual(errs, [], errs)
        self.assertNotIn("duplicate region name", out)
        names = [view["region"]["name"] for part in parts for view in part["views"]]
        self.assertEqual(names, ["shield_back.se", "shield_back.ne", "shield_off_hand.se", "shield_off_hand.ne"])
        self.assertEqual(len(names), len(set(names)))

    def test_duplicate_region_name_is_an_error(self):
        # off_hand.se reuses back's name. The part-qualified form would not.
        parts = []
        x = 0
        files = []
        for part in ("back", "off_hand"):
            views = []
            for view in ("se", "ne"):
                rel = f"srd51.item.shield/{part}.{view}.png"
                files.append(rel)
                name = "shield_back.se" if (part, view) == ("off_hand", "se") else equip_kit.region_name(
                    "srd51.item.shield", part, view)
                views.append({"view": view, "region": _region(name, x)})
                x += 24
            parts.append({"part": part, "views": views})
        doc = {"$schema": "equip_kit@1", "cell": [24, 24], "atlas": "equip.png",
               "items": [{"content_id": "srd51.item.shield", "parts": parts}]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for rel in files:
                write_png(root / rel, 24, 24)
            write_png(root / "equip.png", 96, 24)
            errs, _out = _validate(root, doc)
        self.assertTrue(any("duplicate region name 'shield_back.se'" in e for e in errs), errs)

    def test_size_bin_has_its_own_region_name(self):
        # A large cell is a different sprite. Its name does not reuse the flat one.
        self.assertEqual(equip_kit.region_name("srd51.item.backpack", "back", "se"), "backpack_back.se")
        self.assertEqual(
            equip_kit.region_name("srd51.item.backpack", "back", "se", "large"),
            "backpack_back_large.se",
        )
        parts = []
        x = 0
        files = []
        for bin_name in ("", "large"):
            views = []
            leaf = "large/" if bin_name else ""
            for view in ("se", "ne"):
                rel = f"srd51.item.backpack/{leaf}back.{view}.png"
                files.append(rel)
                name = equip_kit.region_name("srd51.item.backpack", "back", view, bin_name)
                views.append({"view": view, "region": _region(name, x)})
                x += 24
            part = {"part": "back", "views": views}
            if bin_name:
                part["bin"] = bin_name
            parts.append(part)
        doc = {
            "$schema": "equip_kit@1",
            "cell": [24, 24],
            "atlas": "equip.png",
            "items": [{
                "content_id": "srd51.item.backpack",
                "back_role": "pack",
                "parts": parts,
            }],
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for rel in files:
                write_png(root / rel, 24, 24)
            write_png(root / "equip.png", 96, 24)
            errs, _out = _validate(root, doc)
        self.assertEqual(errs, [], errs)

    def test_shipped_sprites_do_not_warn(self):
        live = [p for p in vp.discover() if vp.pack_set_key(p) == "live"]
        buf = io.StringIO()
        with redirect_stdout(buf):
            errs = equip_kit.validate_live(live)
        self.assertEqual(errs, [])
        self.assertNotIn("column 0", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
