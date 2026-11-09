#!/usr/bin/env python3
"""Call tools/validate the same way ``validate_packs.py`` main does.

No Tk. No single-file validator and no second check implementation. A whole pack
is validated with ``validate_packs.main``. An edited document is written only
into a temporary symlink overlay of that pack, then the same main path runs.
Printed lines use the real pack's CLI name (``ok   `` / ``FAIL `` / ``warning ``
plus the summary), matching ``python3 tools/validate/validate_packs.py <pack>``.

``monster_derive`` calls ``checks_v4.check_monster_derive`` for the editor pane.
It does not add a public derive API.
"""
from __future__ import annotations

import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

_VALIDATE_DIR = Path(__file__).resolve().parents[1] / "validate"
if str(_VALIDATE_DIR) not in sys.path:
    sys.path.insert(0, str(_VALIDATE_DIR))

import validate_packs as vp  # noqa: E402

__all__ = [
    "canonical_json",
    "monster_derive",
    "validate_documents",
    "validate_pack_output",
]


def canonical_json(doc: object) -> str:
    """Pretty JSON the editor writes on Save. Key order is the parsed order.
    ``allow_nan`` is off, matching the validator's integer-only content rule."""
    return json.dumps(doc, indent=2, ensure_ascii=False, allow_nan=False) + "\n"


def validate_pack_output(pack: Path) -> tuple[int, str]:
    """Stdout and exit code of ``validate_packs.main`` for one pack directory.

    Same path as ``python3 tools/validate/validate_packs.py <pack>``: one pack,
    no ``--script-check``, no effect-case or scenario pass (those run only when
    main is given no pack arguments).
    """
    return _capture_main(Path(pack))


def validate_documents(pack: Path, replacements: dict[str, dict] | None = None) -> tuple[int, str]:
    """Validate ``pack``. ``replacements`` maps pack-relative posix paths to
    documents swapped in for this call. The real pack is not written."""
    pack = Path(pack).resolve()
    if not replacements:
        return _capture_main(pack)
    texts = {rel: canonical_json(doc) for rel, doc in replacements.items()}
    with tempfile.TemporaryDirectory(prefix="bestiary_validate_") as tmp:
        overlay = Path(tmp) / pack.name
        mirror_pack(pack, overlay, texts)
        code, out = _capture_main(overlay)
        printed = _printed_pack_name(overlay)
    label = _printed_pack_name(pack)
    if printed != label:
        out = out.replace(printed, label)
    return code, out


def mirror_pack(src: Path, dst: Path, replacements: dict[str, str]) -> None:
    """Copy ``src`` to ``dst`` by symlink, writing ``replacements`` as real files.

    ``replacements`` values are file text, keys are pack-relative posix paths.
    """
    src = Path(src).resolve()
    dst.mkdir(parents=True, exist_ok=True)
    pending = dict(replacements)
    for path in src.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src).as_posix()
        dest = dst / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if rel in pending:
            dest.write_text(pending.pop(rel), encoding="utf-8")
        else:
            dest.symlink_to(path)
    for rel, text in pending.items():
        dest = dst / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding="utf-8")


def monster_derive(pack: Path, doc: dict) -> tuple[list[str], list[str]]:
    """``checks_v4.check_monster_derive`` for one document.

    Returns ``([], [])`` when ``derive`` is not true, which is what that
    function returns. Item documents are the pack's ``items/*.json``, the same
    ``ctx['item_docs']`` entries the pack validator passes for in-pack items.
    """
    if not isinstance(doc, dict) or doc.get("derive") is not True:
        return [], []
    return vp.c4.check_monster_derive(doc, {"item_docs": load_item_docs(pack)})


_ITEM_DOCS: dict[str, dict[str, dict]] = {}


def load_item_docs(pack: Path) -> dict[str, dict]:
    key = str(Path(pack).resolve())
    cached = _ITEM_DOCS.get(key)
    if cached is not None:
        return cached
    docs: dict[str, dict] = {}
    items = Path(pack) / "items"
    if items.is_dir():
        for path in sorted(items.glob("*.json")):
            try:
                doc = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            if isinstance(doc, dict) and isinstance(doc.get("id"), str):
                docs[doc["id"]] = doc
    _ITEM_DOCS[key] = docs
    return docs


def _capture_main(pack: Path) -> tuple[int, str]:
    buf = io.StringIO()
    with redirect_stdout(buf):
        code = vp.main([str(pack)])
    return code, buf.getvalue()


def _printed_pack_name(pack: Path) -> str:
    """The pack token ``validate_packs.main`` prints (``ok   <name>``)."""
    pack = Path(pack).resolve()
    if pack.is_relative_to(vp.ROOT):
        return str(pack.relative_to(vp.ROOT))
    return str(pack)
