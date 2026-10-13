"""Pack-set resolution for the validator. Reference semantics for the engine's
eng::assets::PackSet (docs/proposals/PACKSET_API.md); keep the two in sync.

- Packs are indexed by manifest id within one set (the live packs, or one tests/compat/<level>).
- resolve(requested ids) -> packs in load order (deps first; ties broken by pack id, so the order
  never depends on filesystem order) plus a structured error list. All errors are collected.
- Range dialect (integers only, no prerelease): X.Y.Z (exact), ^X.Y, ^X.Y.Z, ~X.Y.Z, >=X.Y.Z.
- Duplicate content ids across a resolved set are errors (no override in v1).
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

# Error kinds (PackSetError::Kind in C++).
MISSING_DEP = "missing_dep"
VERSION_MISMATCH = "version_mismatch"
BAD_RANGE = "bad_range"
CYCLE = "cycle"
DUPLICATE_PACK = "duplicate_pack_id"
DUPLICATE_CONTENT = "duplicate_content_id"
BAD_MANIFEST = "bad_manifest"

_VER = r"(0|[1-9][0-9]*)"
_RANGE_RE = re.compile(rf"^(\^|~|>=)?{_VER}\.{_VER}(?:\.{_VER})?$")
_PACK_VER_RE = re.compile(rf"^{_VER}\.{_VER}\.{_VER}(-[0-9A-Za-z.-]+)?$")


@dataclass(frozen=True)
class Error:
    kind: str
    pack: str      # pack the error is reported against
    id: str        # dependency / content id involved ("" if none)
    detail: str

    def __str__(self) -> str:
        return f"{self.kind}: {self.detail}"


def parse_range(text: str):
    """Returns (lo, hi_exclusive) as int triples, hi None = unbounded; None if not in the dialect."""
    m = _RANGE_RE.match(text) if isinstance(text, str) else None
    if not m:
        return None
    op, a, b, c = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    if c is None and op != "^":
        return None   # X.Y only allowed as ^X.Y
    z = int(c) if c is not None else 0
    lo = (a, b, z)
    if op is None:
        return lo, (a, b, z + 1)
    if op == ">=":
        return lo, None
    if op == "~":
        return lo, (a, b + 1, 0)
    # caret: bump the left-most non-zero component (npm semantics)
    if a > 0:
        return lo, (a + 1, 0, 0)
    if b > 0:
        return lo, (0, b + 1, 0)
    return lo, (0, 0, z + 1)


def parse_version(text: str):
    """Pack version -> int triple; None if malformed or prerelease (prerelease never satisfies)."""
    m = _PACK_VER_RE.match(text) if isinstance(text, str) else None
    if not m or m.group(4):
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def satisfies(version: str, rng: str) -> bool:
    v, r = parse_version(version), parse_range(rng)
    if v is None or r is None:
        return False
    lo, hi = r
    return v >= lo and (hi is None or v < hi)


@dataclass
class Pack:
    id: str
    version: str
    dir: Path
    manifest: dict
    content_ids: dict = field(default_factory=dict)   # content id -> pack-relative file

    @property
    def depends(self) -> list[dict]:
        return [d for d in self.manifest.get("depends", []) if isinstance(d, dict)]


def load_pack(dir: Path) -> Pack | None:
    try:
        man = json.loads((dir / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    p = Pack(str(man.get("id", "")), str(man.get("version", "")), dir, man)
    for rel in sorted(man.get("contents", [])):
        if not rel.endswith(".json"):
            continue
        try:
            d = json.loads((dir / rel).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        typ = str(d.get("$schema", "")).split("@")[0] if isinstance(d, dict) else ""
        if typ in ("monster", "character", "spell") and isinstance(d.get("id"), str):
            p.content_ids.setdefault(d["id"], rel)   # in-pack duplicates are per-pack errors elsewhere
    return p


class PackSet:
    """One search set: the packs it may resolve against, indexed by id."""

    def __init__(self, dirs: list[Path]):
        self.errors: list[Error] = []
        self.by_id: dict[str, Pack] = {}
        for d in sorted(dirs, key=lambda p: str(p)):
            p = load_pack(d)
            if p is None:
                self.errors.append(Error(BAD_MANIFEST, str(d), "", f"{d}: unreadable manifest.json"))
                continue
            if p.id in self.by_id:
                self.errors.append(Error(DUPLICATE_PACK, p.id, p.id,
                                         f"pack id '{p.id}' is defined by both {self.by_id[p.id].dir} and {d}"))
                continue
            self.by_id[p.id] = p

    def resolve(self, requested: list[str]) -> tuple[list[Pack], list[Error]]:
        """Load order for `requested` and their transitive depends (deps first, ties by id)."""
        errs: list[Error] = []
        needed: dict[str, Pack] = {}
        edges: dict[str, set[str]] = {}

        def visit(pid: str, stack: list[str]) -> None:
            if pid in stack:
                cyc = stack[stack.index(pid):] + [pid]
                # Reported against the smallest pack id in the cycle, rotated to start there, so
                # every root that reaches the cycle reports the same single error.
                ring = cyc[:-1]
                k = ring.index(min(ring))
                ring = ring[k:] + ring[:k]
                errs.append(Error(CYCLE, ring[0], ring[0], "dependency cycle: " + " -> ".join(ring + [ring[0]])))
                return
            if pid in needed:
                return
            p = self.by_id[pid]
            needed[pid] = p
            edges[pid] = set()
            for dep in sorted(p.depends, key=lambda d: str(d.get("id"))):
                did, rng = str(dep.get("id")), dep.get("version")
                if parse_range(rng) is None:
                    errs.append(Error(BAD_RANGE, pid, did, f"{pid}: depends '{did}' range {rng!r} is not "
                                      "X.Y.Z, ^X.Y, ^X.Y.Z, ~X.Y.Z or >=X.Y.Z"))
                    continue
                q = self.by_id.get(did)
                if q is None:
                    errs.append(Error(MISSING_DEP, pid, did, f"{pid}: depends on '{did}' {rng}, which is not in the pack set"))
                    continue
                if not satisfies(q.version, rng):
                    errs.append(Error(VERSION_MISMATCH, pid, did,
                                      f"{pid}: depends on '{did}' {rng}, but the set has {did} {q.version}"))
                    continue
                edges[pid].add(did)
                visit(did, stack + [pid])

        for r in sorted(set(requested)):
            if r not in self.by_id:
                errs.append(Error(MISSING_DEP, r, r, f"requested pack '{r}' is not in the pack set"))
            else:
                visit(r, [])
        # Kahn's algorithm; the ready set is kept sorted by pack id -> deterministic order.
        order: list[Pack] = []
        remaining = {k: set(v) for k, v in edges.items()}
        while remaining:
            ready = sorted(k for k, v in remaining.items() if not v)
            if not ready:
                break   # cycle (already reported)
            k = ready[0]
            order.append(needed[k])
            del remaining[k]
            for v in remaining.values():
                v.discard(k)
        # Duplicate content ids across the resolved set: never an override in v1.
        owner: dict[str, Pack] = {}
        for p in order:
            for cid, rel in sorted(p.content_ids.items()):
                if cid in owner and owner[cid] is not p:
                    errs.append(Error(DUPLICATE_CONTENT, p.id, cid,
                                      f"content id '{cid}' is defined in both {owner[cid].id} "
                                      f"({owner[cid].content_ids[cid]}) and {p.id} ({rel})"))
                else:
                    owner.setdefault(cid, p)
        return order, errs

    def closure(self, pid: str) -> list[Pack]:
        """The pack and its transitive deps (only those that resolve), load order."""
        return self.resolve([pid])[0]


def owner_of(order: list[Pack], content_id: str) -> Pack | None:
    for p in order:
        if content_id in p.content_ids:
            return p
    return None
