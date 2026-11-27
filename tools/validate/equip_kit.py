"""equip_kit@1 checks the equipment art manifest cannot express in JSON Schema alone.

The manifest is content/art/equipment/kit.json. It is presentation only: not a pack
document, not an engine schema-registry type, and not part of any combat hash.
equip_kit@1 is the first version, so there is no migration into it.

Beyond the schema this checks:
  * every content_id is an item id, matched whole (a quiver is not a bolt case)
  * quiver, bolt case, and backpack carry their back_role; a shield or a
    two-handed weapon does not carry one
  * a holy symbol amulet is neck only; a component pouch has a hip part
  * neck has the toward view (se) and not the away view (ne)
  * every other part has both se and ne
  * a pivot lies inside the cell, and inside the PNG
  * an atlas region lies inside the atlas and matches the cell
  * a region name is the content id's last segment, the part, and the view,
    so two parts of one item do not share a name; a repeated name is an error
  * loose PNGs are the PR A paths, are the cell size, and are 8-bit RGBA
  * a non-transparent pixel in column 0 is a warning: the west mirror clips it
  * every recognized PNG on disk is listed, and every listed sprite is on disk
"""
from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIT_PATH = ROOT / "content" / "art" / "equipment" / "kit.json"
PARTS = ("back", "hip", "main_hand", "off_hand", "held_light", "neck")
VIEWS_BOTH = ("se", "ne")
BINS = ("tiny", "small", "medium", "large", "huge")
KINDS = ("weapon", "shield", "light", "other")
# Region names are {slug}_{part}.{view} (and _{bin} when a size bin is set),
# matched whole. The part is what keeps back.se and off_hand.se apart.
REQUIRED_BACK_ROLE = {
    "srd51.item.quiver": "quiver",
    "srd51.item.case_crossbow_bolt": "bolt_case",
    "srd51.item.backpack": "pack",
}
NECK_ONLY = "srd51.item.holy_symbol_amulet"
HIP_REQUIRED = "srd51.item.component_pouch"
_PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _png_ihdr(path: Path) -> tuple[int, int, int, int, int] | str:
    """(width, height, bit depth, color type, interlace) or an error string.

    Color type 6 is 8-bit RGBA. PNG alpha is straight (not premultiplied); that is
    the on-disk form H4 asks for. An sRGB chunk is not required: the color type is
    what a file can state.
    """
    try:
        data = path.read_bytes()
    except OSError as e:
        return f"unreadable PNG ({e})"
    if len(data) < 33 or data[:8] != _PNG_SIG or data[12:16] != b"IHDR":
        return "not a PNG"
    if struct.unpack(">I", data[8:12])[0] < 13:
        return "PNG IHDR is short"
    width, height, depth, color, _compression, _filter, interlace = struct.unpack(">IIBBBBB", data[16:29])
    expect = zlib.crc32(data[12:29]) & 0xFFFFFFFF
    got = struct.unpack(">I", data[29:33])[0]
    if got != expect:
        return "PNG IHDR checksum does not match"
    return width, height, depth, color, interlace


def sprite_rel(content_id: str, kind: str, part: str, view: str, bin_name: str) -> Path:
    """PR A path. Omitted bin is the flat medium file. A named bin is {id}/{bin}/."""
    leaf = f"{part}.{view}.png"
    if kind:
        return Path("_kind") / kind / leaf
    if bin_name:
        return Path(content_id) / bin_name / leaf
    return Path(content_id) / leaf


def _slug(content_id: str) -> str:
    return content_id.rsplit(".", 1)[-1]


def region_name(owner: str, part: str, view: str, bin_name: str = "") -> str:
    """Atlas region for one sprite, unique per content id (or kind), part, view, and bin.

    Files are ``{content_id}/{part}.{view}.png``. A name made of only the last
    segment and the view (``shield.se``) drops the part, so ``back.se`` and a
    later ``off_hand.se`` collide. The part is in the name. A size bin, when
    the sprite has one, is in the name too, because that cell is a different
    sprite and the flat name stays put. equip_kit@1 allows one token before
    ``.se`` or ``.ne``, so the pieces are joined with underscores
    (``shield_back.se``). Matched whole, not as a substring.
    """
    slug = _slug(owner)
    stem = f"{slug}_{part}"
    if bin_name:
        stem = f"{stem}_{bin_name}"
    return f"{stem}.{view}"


def column0_warning(label: str, count: int) -> str:
    """``count`` pixels in column 0 are not fully transparent.

    The west mirror flips around the pivot column and clips column 0, so ink
    there is dropped on the mirrored facing.
    """
    noun = "pixel" if count == 1 else "pixels"
    return (
        f"{label}: column 0 has {count} non-transparent {noun}; "
        "the west mirror flips around the pivot column and clips column 0"
    )


def _paeth(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    dl, du, dul = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
    if dl <= du and dl <= dul:
        return left
    if du <= dul:
        return up
    return up_left


def _png_rgba(path: Path) -> tuple[int, int, bytes] | str:
    """(width, height, tightly packed 8-bit RGBA) or an error string.

    Non-interlaced color type 6 only. This is how column 0 is scanned; the
    header check does not see pixels.
    """
    try:
        data = path.read_bytes()
    except OSError as e:
        return f"unreadable PNG ({e})"
    if len(data) < 8 or data[:8] != _PNG_SIG:
        return "not a PNG"
    width = height = 0
    depth = color = interlace = -1
    idat = bytearray()
    i = 8
    while i + 12 <= len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        tag = data[i + 4:i + 8]
        start = i + 8
        end = start + length
        if end + 4 > len(data):
            return "PNG chunk is short"
        chunk = data[start:end]
        if tag == b"IHDR":
            if length < 13:
                return "PNG IHDR is short"
            width, height, depth, color, _compression, _filter, interlace = struct.unpack(">IIBBBBB", chunk[:13])
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
        i = end + 4
    if depth != 8 or color != 6 or interlace != 0 or width <= 0 or height <= 0:
        return "PNG is not 8-bit non-interlaced RGBA"
    if width > 16384 or height > 16384:
        return "PNG is too large to scan"
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return "PNG pixels could not be read"
    channels = 4
    stride = width * channels
    if len(raw) < height * (stride + 1):
        return "PNG pixels could not be read"
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        filter_ty = raw[pos]
        pos += 1
        row = bytearray(raw[pos:pos + stride])
        pos += stride
        if filter_ty > 4:
            return "PNG pixels could not be read"
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up = prev[x]
            up_left = prev[x - channels] if x >= channels else 0
            value = row[x]
            if filter_ty == 1:
                value = (value + left) & 255
            elif filter_ty == 2:
                value = (value + up) & 255
            elif filter_ty == 3:
                value = (value + ((left + up) // 2)) & 255
            elif filter_ty == 4:
                value = (value + _paeth(left, up, up_left)) & 255
            row[x] = value
        out[y * stride:(y + 1) * stride] = row
        prev = row
    return width, height, bytes(out)


def column0_opaque_count(path: Path) -> int | str:
    """Pixels in column 0 whose alpha is not 0, or an error string.

    Alpha 0 is fully transparent. That is the empty column the west mirror
    expects: the mirrored facing never reads column 0.
    """
    decoded = _png_rgba(path)
    if isinstance(decoded, str):
        return decoded
    width, height, rgba = decoded
    count = 0
    for y in range(height):
        if rgba[(y * width) * 4 + 3] != 0:
            count += 1
    return count


def _report_column0(label: str, path: Path, errs: list[str]) -> None:
    count = column0_opaque_count(path)
    if isinstance(count, str):
        errs.append(f"{label}: {count}")
        return
    # No strict mode: ink in column 0 is dropped by the mirror, and the sprite
    # still loads. Say which file and how many pixels.
    if count:
        print("warning " + column0_warning(label, count))


def item_docs(packs: list[Path]) -> dict[str, dict]:
    """Item id -> document, from live packs. Exact ids only; art/ JSON is skipped."""
    found: dict[str, dict] = {}
    for pack in packs:
        for path in sorted(pack.rglob("*.json")):
            rel = path.relative_to(pack).as_posix()
            if rel == "art" or rel.startswith("art/"):
                continue
            try:
                doc = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            if not isinstance(doc, dict):
                continue
            if not str(doc.get("$schema", "")).startswith("item@"):
                continue
            cid = doc.get("id")
            if isinstance(cid, str):
                found.setdefault(cid, doc)
    return found


def _schema_errors(doc: dict) -> list[str]:
    import validate_packs as vp
    from jsonschema import Draft202012Validator

    registry, by_tag = vp.load_registry()
    schema = by_tag["equip_kit@1"]
    validator = Draft202012Validator(schema, registry=registry)
    errs = sorted(validator.iter_errors(doc), key=lambda e: list(map(str, e.absolute_path)))
    return [f"/{'/'.join(map(str, e.absolute_path))}: {vp.schema_message(e, registry)}" for e in errs]


def _two_handed(doc: dict) -> bool:
    props = doc.get("properties")
    return isinstance(props, list) and "two_handed" in props


def _add_sprite_key(keys: set[tuple], where: str, cid: str, kind: str, part: dict, errs: list[str]) -> None:
    part_name = part.get("part")
    bin_name = part.get("bin") or ""
    if kind and bin_name:
        errs.append(f"{where}: a kind fallback has no size bin")
        return
    views = part.get("views")
    if not isinstance(views, list):
        return
    seen: set[str] = set()
    for view in views:
        if not isinstance(view, dict):
            continue
        view_name = view.get("view")
        if not isinstance(view_name, str):
            continue
        if view_name in seen:
            errs.append(f"{where}: duplicate view {view_name}")
        seen.add(view_name)
        key = (kind or cid, part_name, view_name, bin_name)
        if key in keys:
            errs.append(f"{where}: duplicate sprite {part_name}.{view_name}")
        keys.add(key)


def _check_entry_rules(doc: dict, items: dict[str, dict], errs: list[str]) -> set[tuple]:
    keys: set[tuple] = set()
    seen_regions: dict[str, str] = {}
    seen_ids: set[str] = set()
    for i, entry in enumerate(doc.get("items") or []):
        if not isinstance(entry, dict):
            continue
        cid = entry.get("content_id")
        where = f"items[{i}]"
        if not isinstance(cid, str):
            continue
        where = f"{where} {cid}"
        if cid in seen_ids:
            errs.append(f"{where}: duplicate content_id")
        seen_ids.add(cid)
        # Whole-id lookup. 'quiver' does not select the bolt case, and a longer
        # id that merely contains this one is a different item.
        item = items.get(cid)
        role = entry.get("back_role")
        if item is None:
            errs.append(f"{where}: unknown content_id")
        else:
            if item.get("category") == "shield" and role is not None:
                errs.append(f"{where}: back_role is not set on a shield; the renderer derives it")
            if _two_handed(item) and role is not None:
                errs.append(f"{where}: back_role is not set on a two-handed weapon; the renderer derives it")
            required = REQUIRED_BACK_ROLE.get(cid)
            if required is not None and role != required:
                errs.append(f"{where}: back_role must be {required}")
        parts = entry.get("parts") if isinstance(entry.get("parts"), list) else []
        part_names = [p.get("part") for p in parts if isinstance(p, dict)]
        if role is not None and "back" not in part_names:
            errs.append(f"{where}: back_role needs a back part")
        if cid == NECK_ONLY and any(name != "neck" for name in part_names):
            errs.append(f"{where}: a holy symbol amulet uses a neck part only")
        if cid == HIP_REQUIRED and "hip" not in part_names:
            errs.append(f"{where}: a component pouch has a hip part")
        for part in parts:
            if not isinstance(part, dict):
                continue
            _check_part(where, cid, "", part, doc, errs, seen_regions)
            _add_sprite_key(keys, where, cid, "", part, errs)
    seen_kinds: set[str] = set()
    for i, entry in enumerate(doc.get("kinds") or []):
        if not isinstance(entry, dict):
            continue
        kind = entry.get("kind")
        where = f"kinds[{i}]"
        if not isinstance(kind, str):
            continue
        where = f"{where} {kind}"
        if kind in seen_kinds:
            errs.append(f"{where}: duplicate kind")
        seen_kinds.add(kind)
        if "back_role" in entry:
            errs.append(f"{where}: a kind fallback has no back_role")
        for part in entry.get("parts") or []:
            if not isinstance(part, dict):
                continue
            _check_part(where, "", kind, part, doc, errs, seen_regions)
            _add_sprite_key(keys, where, "", kind, part, errs)
    return keys


def _check_part(where: str, cid: str, kind: str, part: dict, doc: dict, errs: list[str],
                seen_regions: dict[str, str]) -> None:
    part_name = part.get("part")
    views = part.get("views") if isinstance(part.get("views"), list) else []
    names = [v.get("view") for v in views if isinstance(v, dict)]
    label = f"{where} {part_name}"
    if part_name == "neck":
        if names != ["se"]:
            errs.append(f"{label}: neck has the toward view se only")
    elif isinstance(part_name, str):
        for view_name in VIEWS_BOTH:
            if view_name not in names:
                errs.append(f"{label}: missing view {view_name}")
    cell = doc.get("cell")
    cell_w = cell[0] if isinstance(cell, list) and len(cell) == 2 else 0
    cell_h = cell[1] if isinstance(cell, list) and len(cell) == 2 else 0
    for view in views:
        if not isinstance(view, dict):
            continue
        view_name = view.get("view")
        pivot = view.get("pivot")
        if isinstance(pivot, list) and len(pivot) == 2 and all(isinstance(n, int) and not isinstance(n, bool) for n in pivot):
            x, y = pivot
            if cell_w <= 0 or cell_h <= 0 or not (0 <= x < cell_w and 0 <= y < cell_h):
                errs.append(f"{label}.{view_name}: pivot [{x}, {y}] is outside the {cell_w}x{cell_h} cell")
        region = view.get("region")
        if isinstance(region, dict) and isinstance(view_name, str) and isinstance(part_name, str):
            name = region.get("name")
            owner = cid or kind
            if isinstance(name, str) and owner:
                bin_name = part.get("bin") if isinstance(part.get("bin"), str) else ""
                expected = region_name(owner, part_name, view_name, bin_name)
                if name != expected:
                    errs.append(f"{label}.{view_name}: region name {name!r} is not {expected}")
                prior = seen_regions.get(name)
                if prior is not None:
                    errs.append(f"{label}.{view_name}: duplicate region name {name!r} (also {prior})")
                else:
                    seen_regions[name] = f"{label}.{view_name}"


def _check_files(root: Path, doc: dict, keys: set[tuple], errs: list[str]) -> None:
    cell = doc.get("cell") if isinstance(doc.get("cell"), list) else [0, 0]
    cell_w, cell_h = int(cell[0]), int(cell[1])
    atlas_name = doc.get("atlas") if isinstance(doc.get("atlas"), str) else ""
    atlas_box: tuple[int, int] | None = None
    if atlas_name:
        atlas_path = root / atlas_name
        if not atlas_path.is_file():
            errs.append(f"atlas {atlas_name} is missing")
        else:
            info = _png_ihdr(atlas_path)
            if isinstance(info, str):
                errs.append(f"atlas {atlas_name}: {info}")
            else:
                atlas_box = (info[0], info[1])
                _check_png_info(f"atlas {atlas_name}", info, info[0], info[1], errs, cell_sized=False)

    def consider(owner: str, kind: str, part: dict) -> None:
        bin_name = part.get("bin") or ""
        part_name = part.get("part")
        for view in part.get("views") or []:
            if not isinstance(view, dict):
                continue
            view_name = view.get("view")
            if not isinstance(part_name, str) or not isinstance(view_name, str):
                continue
            rel = sprite_rel(owner, kind, part_name, view_name, bin_name)
            path = root / rel
            label = rel.as_posix()
            if not path.is_file():
                errs.append(f"{label}: missing view file")
                continue
            info = _png_ihdr(path)
            if isinstance(info, str):
                errs.append(f"{label}: {info}")
            else:
                _check_png_info(label, info, cell_w, cell_h, errs, cell_sized=True)
                if info[2] == 8 and info[3] == 6 and info[4] == 0:
                    _report_column0(label, path, errs)
                pivot = view.get("pivot")
                if isinstance(pivot, list) and len(pivot) == 2 and all(isinstance(n, int) and not isinstance(n, bool) for n in pivot):
                    x, y = pivot
                    if not (0 <= x < info[0] and 0 <= y < info[1]):
                        errs.append(f"{label}: pivot [{x}, {y}] is outside the {info[0]}x{info[1]} sprite")
            region = view.get("region")
            if isinstance(region, dict):
                if not atlas_name:
                    errs.append(f"{label}: region needs an atlas")
                elif atlas_box is not None:
                    x, y, w, h = region.get("x"), region.get("y"), region.get("w"), region.get("h")
                    if not all(isinstance(n, int) and not isinstance(n, bool) for n in (x, y, w, h)):
                        continue
                    if w != cell_w or h != cell_h:
                        errs.append(f"{label}: region is {w}x{h}, cell is {cell_w}x{cell_h}")
                    if x < 0 or y < 0 or x + w > atlas_box[0] or y + h > atlas_box[1]:
                        errs.append(f"{label}: region [{x}, {y}, {w}, {h}] is outside the atlas")

    for entry in doc.get("items") or []:
        if isinstance(entry, dict) and isinstance(entry.get("content_id"), str):
            for part in entry.get("parts") or []:
                if isinstance(part, dict):
                    consider(entry["content_id"], "", part)
    for entry in doc.get("kinds") or []:
        if isinstance(entry, dict) and isinstance(entry.get("kind"), str):
            for part in entry.get("parts") or []:
                if isinstance(part, dict):
                    consider("", entry["kind"], part)

    listed = keys
    for rel, key in _disk_sprites(root, atlas_name, errs):
        if key not in listed:
            errs.append(f"{rel}: sprite is not in the manifest")


def _check_png_info(label: str, info: tuple[int, int, int, int, int], cell_w: int, cell_h: int,
                    errs: list[str], cell_sized: bool) -> None:
    width, height, depth, color, interlace = info
    if depth != 8 or color != 6 or interlace != 0:
        errs.append(f"{label}: needs 8-bit straight-alpha RGBA (sRGB), got depth {depth} color {color}")
    if cell_sized and (width != cell_w or height != cell_h):
        errs.append(f"{label}: sprite is {width}x{height}, cell is {cell_w}x{cell_h}")


def _disk_sprites(root: Path, atlas_name: str, errs: list[str]) -> list[tuple[str, tuple]]:
    """Recognized loose PNGs. Anything else under the kit root is an error."""
    if not root.is_dir():
        errs.append(f"{root}: kit root is missing")
        return []
    found: list[tuple[str, tuple]] = []
    allowed_root = {"kit.json"}
    if atlas_name:
        allowed_root.add(atlas_name)
    for path in sorted(root.iterdir()):
        name = path.name
        if path.is_file():
            if name not in allowed_root:
                errs.append(f"{name}: unexpected file in the kit root")
            continue
        if not path.is_dir():
            errs.append(f"{name}: unexpected kit entry")
            continue
        if name == "_kind":
            found += _kind_dir(path, errs)
            continue
        if not _content_id_ok(name):
            errs.append(f"{name}: directory is not a content_id")
            continue
        found += _item_dir(path, name, errs)
    return found


def _content_id_ok(name: str) -> bool:
    import re
    return re.fullmatch(r"[a-z0-9_]+(\.[a-z0-9_]+)+", name) is not None


def _parse_leaf(path: Path, display: str, errs: list[str]) -> tuple[str, str] | None:
    if path.suffix != ".png":
        errs.append(f"{display}: unexpected file")
        return None
    stem = path.stem
    part, dot, view = stem.rpartition(".")
    if not dot or not part:
        errs.append(f"{display}: file name must be part.view.png")
        return None
    if view in ("sw", "nw", "w", "west"):
        errs.append(f"{display}: west is mirrored and has no file")
        return None
    if part not in PARTS:
        errs.append(f"{display}: unknown part")
        return None
    if view not in VIEWS_BOTH:
        errs.append(f"{display}: unknown view")
        return None
    if part == "neck" and view == "ne":
        errs.append(f"{display}: neck.ne is not a view")
        return None
    return part, view


def _item_dir(directory: Path, content_id: str, errs: list[str]) -> list[tuple[str, tuple]]:
    found: list[tuple[str, tuple]] = []
    for path in sorted(directory.iterdir()):
        rel_dir = f"{content_id}/{path.name}"
        if path.is_file():
            parsed = _parse_leaf(path, rel_dir, errs)
            if parsed:
                part, view = parsed
                found.append((rel_dir, (content_id, part, view, "")))
            continue
        if path.is_dir() and path.name in BINS:
            for leaf in sorted(path.iterdir()):
                leaf_rel = f"{rel_dir}/{leaf.name}"
                if not leaf.is_file():
                    errs.append(f"{leaf_rel}: unexpected entry")
                    continue
                parsed = _parse_leaf(leaf, leaf_rel, errs)
                if parsed:
                    part, view = parsed
                    found.append((leaf_rel, (content_id, part, view, path.name)))
            continue
        errs.append(f"{rel_dir}: unknown directory")
    return found


def _kind_dir(directory: Path, errs: list[str]) -> list[tuple[str, tuple]]:
    found: list[tuple[str, tuple]] = []
    for path in sorted(directory.iterdir()):
        if not path.is_dir() or path.name not in KINDS:
            errs.append(f"_kind/{path.name}: unknown kind")
            continue
        for leaf in sorted(path.iterdir()):
            rel = f"_kind/{path.name}/{leaf.name}"
            if not leaf.is_file():
                errs.append(f"{rel}: a kind fallback has no size bin")
                continue
            parsed = _parse_leaf(leaf, rel, errs)
            if parsed:
                part, view = parsed
                found.append((rel, (path.name, part, view, "")))
    return found


def validate_kit(path: Path, items: dict[str, dict]) -> list[str]:
    """Errors for one manifest. `items` is the equipment catalog (id -> document)."""
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return [f"{path.name}: invalid JSON: {e}"]
    if not isinstance(doc, dict):
        return [f"{path.name}: document must be an object"]
    import validate_packs as vp
    errs: list[str] = []
    vp.reject_floats(doc, "", errs)
    errs += _schema_errors(doc)
    if errs:
        return errs
    keys = _check_entry_rules(doc, items, errs)
    _check_files(path.parent, doc, keys, errs)
    return errs


def validate_live(packs: list[Path]) -> list[str]:
    """The shipped manifest against the live packs' item ids."""
    if not KIT_PATH.is_file():
        return ["content/art/equipment/kit.json missing"]
    return validate_kit(KIT_PATH, item_docs(packs))
