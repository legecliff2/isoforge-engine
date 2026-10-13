#!/usr/bin/env python3
"""Every archetype id procgen can emit must be defined in the default pack set.

The sim skips spawns whose archetype isn't loaded (#36 streaming), so a generator that emits an
undefined id silently produces empty rooms. Inputs:
  check_procgen_archetypes.py <procgen_dump exe> [dumped_pack_dir]
- `procgen_dump --list-archetypes` gives the generator's id table (every gen_version).
- Optionally, the spawns in a procgen_dump pack are checked too (they must be in the table).
DEFAULT_PACKS is the set the engine ships and loads by default; extend it when that changes.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PACKS = [ROOT / "content" / "packs" / "sample.vertical_slice"]


def defined_ids(pack: Path) -> set[str]:
    man = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    out = set()
    for rel in man.get("contents", []):
        if rel.endswith(".json"):
            d = json.loads((pack / rel).read_text(encoding="utf-8"))
            if isinstance(d, dict) and isinstance(d.get("id"), str) and \
                    str(d.get("$schema", "")).split("@")[0] in ("monster", "character"):
                out.add(d["id"])
    return out


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    r = subprocess.run([argv[0], "--list-archetypes"], capture_output=True, text=True, check=True)
    table = {}
    for line in r.stdout.splitlines():
        ver, aid = line.split(" ", 1)
        table.setdefault(aid, []).append(int(ver))
    if not table:
        print("FAIL procgen_dump --list-archetypes printed nothing")
        return 1
    defined = set().union(*(defined_ids(p) for p in DEFAULT_PACKS))
    errs = [f"procgen gen_version {','.join(map(str, v))} can emit '{a}', which no default pack defines "
            f"({', '.join(p.name for p in DEFAULT_PACKS)}); the sim would skip it"
            for a, v in sorted(table.items()) if a not in defined]
    if len(argv) > 1:
        for f in sorted(Path(argv[1]).glob("chunks/*.json")):
            for i, s in enumerate(json.loads(f.read_text(encoding="utf-8")).get("spawns", [])):
                if s["archetype"] not in table:
                    errs.append(f"{f.name}: spawns[{i}] archetype '{s['archetype']}' is not in procgen's id table")
    for e in errs:
        print(f"FAIL {e}")
    print(f"{len(table)} procgen archetype ids checked against {len(defined)} default-pack archetypes")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
