"""Shared helpers for the @4 / item@1 validator tests: build a throwaway pack from fixture documents
and validate it in-process (fast) against the real schemas and the real srd51.core dependency."""
from __future__ import annotations

import copy
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "validate"))
import validate_packs as vp  # noqa: E402
import migrate_v4  # noqa: E402,F401

FIXTURES = HERE / "fixtures" / "v4"
CORE = ROOT / "content" / "packs" / "srd51.core"
PACK_ID = "test.v4"
ATTRIBUTION = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]

_REGISTRY = None


def registry():
    global _REGISTRY
    if _REGISTRY is None:
        _REGISTRY = vp.load_registry()
    return _REGISTRY


def fixture(rel: str) -> dict:
    return json.loads((FIXTURES / rel).read_text(encoding="utf-8"))


def pack_fixtures() -> dict[str, dict]:
    """{pack-relative path: document} of the valid @4 + item@1 fixture pack."""
    out = {}
    for f in sorted((FIXTURES / "pack").rglob("*.json")):
        out[f.relative_to(FIXTURES / "pack").as_posix()] = json.loads(f.read_text(encoding="utf-8"))
    return out


def validate_docs(docs: dict[str, dict], api_level: int = 2, tmp: Path | None = None) -> tuple[list[str], list[str]]:
    """Write `docs` as pack `test.v4` (depends on srd51.core) and run validate_pack in-process."""
    own = tmp is None
    tmpdir = tempfile.TemporaryDirectory(prefix="v4_pack_") if own else None
    root = Path(tmpdir.name) if own else tmp
    try:
        pack = root / PACK_ID
        for rel, d in docs.items():
            (pack / rel).parent.mkdir(parents=True, exist_ok=True)
            (pack / rel).write_text(json.dumps(d, indent=2), encoding="utf-8")
        man = {"$schema": "manifest@1", "id": PACK_ID, "name": "v4 test", "version": "1.0.0", "engine_min": "0.1.0",
               "feature_level": 1, "api_level": api_level, "rules_profile": "srd51", "requires": [], "optional": [],
               "depends": [{"id": "srd51.core", "version": "^1.0"}], "attribution": ATTRIBUTION,
               "contents": sorted(docs)}
        (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
        reg, by_tag = registry()
        warns: list[str] = []
        errs = vp.validate_pack(pack, reg, by_tag, warns, vp.packset.PackSet([CORE, pack]))
        return errs, warns
    finally:
        if tmpdir is not None:
            tmpdir.cleanup()


def edit(doc: dict, sets=(), dels=()) -> dict:
    """Copy of `doc` with [path, value] sets (a list index == len appends) and path deletions applied."""
    d = copy.deepcopy(doc)
    for path, value in sets:
        parent = d
        for k in path[:-1]:
            parent = parent[k]
        if isinstance(parent, list) and path[-1] == len(parent):
            parent.append(value)
        else:
            parent[path[-1]] = value
    for path in dels:
        parent = d
        for k in path[:-1]:
            parent = parent[k]
        del parent[path[-1]]
    return d


def validate_in_memory(doc: dict, rel: str = "doc.json", spell_ids=(), item_ids=()) -> tuple[list[str], list[str]]:
    """Validate one document as an in-memory migration result (on_disk=False: inert stamps are accepted)."""
    reg, by_tag = registry()
    ctx = {"ids": set(), "spell_ids": set(spell_ids), "items": set(item_ids), "external_ok": lambda a: a.startswith("srd51.")}
    return vp.validate_doc(rel, doc, doc["$schema"], reg, by_tag, PACK_ID, ctx, on_disk=False)
