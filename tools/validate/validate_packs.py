#!/usr/bin/env python3
"""Validate content packs against the versioned JSON Schemas (ARCHITECTURE.md §3).

Usage:  python3 tools/validate/validate_packs.py [PACK_DIR ...]
With no arguments it validates every pack under content/packs/ and every frozen
compat pack under tests/compat/fl*/ . Exit code 0 = all valid.

Each data file declares "$schema": "<type>@<N>", resolved to schemas/<type>/v<N>.schema.json.
Beyond schema validation it runs cross-checks the schemas can't express. Everything
here uses integer math only (§4.3), and files/errors are processed in sorted order
so output is stable.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource
except ImportError:  # pragma: no cover
    sys.exit("error: needs 'jsonschema>=4.18' (pip install -r tools/validate/requirements.txt)")

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"
CHUNK_SIZE = 32
CHUNK_LAYERS = 4
CR_XP = {"0": 10, "1/8": 25, "1/4": 50, "1/2": 100, "1": 200, "2": 450, "3": 700, "4": 1100,
         "5": 1800, "6": 2300, "7": 2900, "8": 3900, "9": 5000, "10": 5900, "11": 7200,
         "12": 8400, "13": 10000, "14": 11500, "15": 13000, "16": 15000, "17": 18000,
         "18": 20000, "19": 22000, "20": 25000, "21": 33000, "22": 41000, "23": 50000,
         "24": 62000, "25": 75000, "26": 90000, "27": 105000, "28": 120000, "29": 135000,
         "30": 155000}
DICE_RE = re.compile(r"^(\d+)d(\d+)([+-]\d+)?$")
SKILL_ABILITY = {"athletics": "str", "acrobatics": "dex", "sleight_of_hand": "dex", "stealth": "dex",
                 "arcana": "int", "history": "int", "investigation": "int", "nature": "int",
                 "religion": "int", "animal_handling": "wis", "insight": "wis", "medicine": "wis",
                 "perception": "wis", "survival": "wis", "deception": "cha", "intimidation": "cha",
                 "performance": "cha", "persuasion": "cha"}


def modifier(score: int) -> int:
    return (score - 10) // 2  # floor division == SRD table


def cr_proficiency(cr: str) -> int:
    n = 0 if "/" in cr else int(cr)
    return 2 if n < 5 else 2 + (n - 1) // 4


def level_proficiency(level: int) -> int:
    return 2 + (level - 1) // 4


def dice_average(expr: str) -> int:
    """SRD stat-block average: floor(count * (sides + 1) / 2) + bonus, integers only."""
    m = DICE_RE.match(expr)
    count, sides, bonus = int(m[1]), int(m[2]), int(m[3] or 0)
    return (count * (sides + 1)) // 2 + bonus


def load_registry() -> tuple[Registry, dict[str, dict]]:
    resources, by_tag = [], {}
    for path in sorted(SCHEMAS.glob("*/v*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        resources.append((schema["$id"], Resource.from_contents(schema)))
        by_tag[f"{path.parent.name}@{path.name.split('.')[0][1:]}"] = schema
    return Registry().with_resources(resources), by_tag


def reject_floats(obj, where: str, errs: list[str]) -> None:
    if isinstance(obj, float):
        errs.append(f"{where}: float value {obj!r} not allowed (§4.3: integers or decimal strings)")
    elif isinstance(obj, dict):
        for k in sorted(obj):
            reject_floats(obj[k], f"{where}/{k}", errs)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            reject_floats(v, f"{where}/{i}", errs)


def check_stat_block(doc: dict, errs: list[str], pb_expected: int, rolled_hp: bool = True) -> None:
    ab = doc["abilities"]
    pb = doc["proficiency_bonus"]
    if pb != pb_expected:
        errs.append(f"proficiency_bonus {pb} != expected {pb_expected}")
    for a, v in sorted(doc.get("saving_throws", {}).items()):
        if v != modifier(ab[a]) + pb:
            errs.append(f"saving_throws.{a} = {v}, expected {modifier(ab[a]) + pb} (mod + proficiency)")
    for s, v in sorted(doc.get("skills", {}).items()):
        base = modifier(ab[SKILL_ABILITY[s]])
        if v not in (base + pb, base + 2 * pb):
            errs.append(f"skills.{s} = {v}, expected {base + pb} (or {base + 2 * pb} with expertise)")
    perc = doc.get("skills", {}).get("perception", modifier(ab["wis"]))
    if doc["senses"]["passive_perception"] != 10 + perc:
        errs.append(f"senses.passive_perception {doc['senses']['passive_perception']} != {10 + perc}")
    hp = doc["hit_points"]
    if rolled_hp and dice_average(hp["formula"]) != hp["average"]:
        errs.append(f"hit_points.average {hp['average']} != average of {hp['formula']} ({dice_average(hp['formula'])})")
    ids = [x["id"] for k in ("actions", "bonus_actions", "reactions") for x in doc.get(k, [])]
    if len(ids) != len(set(ids)):
        errs.append("duplicate action ids")


def check_monster(doc, errs, ctx):
    ch = doc["challenge"]
    if CR_XP[ch["rating"]] != ch["xp"]:
        errs.append(f"challenge.xp {ch['xp']} != {CR_XP[ch['rating']]} for CR {ch['rating']}")
    check_stat_block(doc, errs, cr_proficiency(ch["rating"]))


def check_character(doc, errs, ctx):
    if sum(c["level"] for c in doc["classes"]) != doc["level"]:
        errs.append("level != sum of classes[].level")
    check_stat_block(doc, errs, level_proficiency(doc["level"]), rolled_hp=False)
    if doc["level"] == 1:  # 1st level: max hit die + CON mod
        hd = int(doc["classes"][0]["hit_die"][1:])
        want = hd + modifier(doc["abilities"]["con"])
        if doc["hit_points"]["average"] != want:
            errs.append(f"level-1 hit_points {doc['hit_points']['average']} != {want} (max hit die + CON)")


def check_chunk(doc, errs, ctx):
    if int(doc["content_hash"]) > 0xFFFFFFFFFFFFFFFF:
        errs.append("content_hash exceeds uint64")
    layers = doc["layers"]
    if len(layers) != CHUNK_LAYERS:
        errs.append(f"layers: {len(layers)} != kChunkLayers {CHUNK_LAYERS}")
    for li, layer in enumerate(layers):
        if len(layer) != CHUNK_SIZE * CHUNK_SIZE:
            errs.append(f"layers[{li}]: {len(layer)} cells != {CHUNK_SIZE}*{CHUNK_SIZE}")
        for i, cell in enumerate(layer):
            if cell["tile"] >= len(doc["palette"]):
                errs.append(f"layers[{li}][{i}] (x={i % CHUNK_SIZE},y={i // CHUNK_SIZE}): tile {cell['tile']} outside palette ({len(doc['palette'])})")
            if cell["tile"] == 0 and (cell["elevation"] or cell["flags"]):
                errs.append(f"layers[{li}][{i}]: empty tile with elevation/flags set")
    ids = [s["stable_id"] for s in doc["spawns"]]
    if len(ids) != len(set(ids)):
        errs.append("spawns: duplicate stable_id")
    for s in doc["spawns"]:
        if s["archetype"] not in ctx["ids"] and not ctx["external_ok"](s["archetype"]):
            errs.append(f"spawn archetype '{s['archetype']}' not defined in pack or its depends")
        x, y = s["tile"]["x"], s["tile"]["y"]
        walls = layers[2][y * CHUNK_SIZE + x]
        if walls["flags"] & 1:
            errs.append(f"spawn {s['stable_id']} at ({x},{y}) is on a blocks_move cell")
        if s["overrides_json"]:
            try:
                json.loads(s["overrides_json"])
            except ValueError as e:
                errs.append(f"spawn {s['stable_id']}: overrides_json is not valid JSON: {e}")


CHECKS = {"monster": check_monster, "character": check_character, "chunk": check_chunk}


def validate_pack(pack: Path, registry: Registry, by_tag: dict) -> list[str]:
    out: list[str] = []
    files = sorted(p for p in pack.rglob("*.json"))
    docs = {}
    for f in files:
        rel = f.relative_to(pack).as_posix()
        try:
            docs[rel] = json.loads(f.read_text(encoding="utf-8"))
        except ValueError as e:
            out.append(f"{rel}: invalid JSON: {e}")
    man = docs.get("manifest.json")
    if man is None:
        return out + ["manifest.json missing"]
    ids = {d.get("id") for r, d in docs.items() if r != "manifest.json" and isinstance(d, dict)}
    deps = [d["id"] for d in man.get("depends", [])]
    ctx = {"ids": ids, "external_ok": lambda a: any(a.startswith(dep + ".") for dep in deps)}
    if "contents" in man:
        listed = set(man["contents"])
        actual = set(docs) - {"manifest.json"}
        for m in sorted(listed - actual):
            out.append(f"manifest.json: contents lists missing file {m}")
        for m in sorted(actual - listed):
            out.append(f"manifest.json: {m} not listed in contents")
        if man["contents"] != sorted(man["contents"]):
            out.append("manifest.json: contents must be sorted (stable order)")
    for rel in sorted(docs):
        doc, errs = docs[rel], []
        tag = doc.get("$schema") if isinstance(doc, dict) else None
        if tag not in by_tag:
            out.append(f"{rel}: unknown or missing $schema {tag!r}")
            continue
        typ = tag.split("@")[0]
        if typ != "manifest" and not str(doc.get("id", "")).startswith(man.get("id", "") + ".") and typ != "chunk":
            errs.append(f"id '{doc.get('id')}' must be namespaced under pack id '{man.get('id')}'")
        reject_floats(doc, "", errs)
        v = Draft202012Validator(by_tag[tag], registry=registry)
        schema_errs = sorted(v.iter_errors(doc), key=lambda e: list(map(str, e.absolute_path)))
        errs += [f"/{'/'.join(map(str, e.absolute_path))}: {e.message[:200]}" for e in schema_errs]
        if not schema_errs and typ in CHECKS:
            CHECKS[typ](doc, errs, ctx)
        out += [f"{rel}: {e}" for e in errs]
    return out


def discover() -> list[Path]:
    packs = sorted(p.parent for p in (ROOT / "content" / "packs").glob("*/manifest.json"))
    packs += sorted(p.parent for p in (ROOT / "tests" / "compat").glob("fl*/*/manifest.json"))
    return packs


def main(argv: list[str]) -> int:
    registry, by_tag = load_registry()
    packs = [Path(a).resolve() for a in argv] or discover()
    if not packs:
        print("no packs found", file=sys.stderr)
        return 1
    failed = 0
    for pack in packs:
        errs = validate_pack(pack, registry, by_tag)
        name = pack.relative_to(ROOT) if pack.is_relative_to(ROOT) else pack
        if errs:
            failed += 1
            print(f"FAIL {name}")
            for e in errs:
                print(f"  {e}")
        else:
            print(f"ok   {name}")
    print(f"{len(packs) - failed}/{len(packs)} packs valid ({len(by_tag)} schemas)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
