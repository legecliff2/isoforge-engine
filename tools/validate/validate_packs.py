#!/usr/bin/env python3
"""Validate content packs against the versioned JSON Schemas (ARCHITECTURE.md §3).

Usage:  python3 tools/validate/validate_packs.py [--script-check SCRIPT_CHECK_EXE] [PACK_DIR ...]
        python3 tools/validate/validate_packs.py --rehash [PACK_DIR ...]   # update chunk content_hash
                                                                           # (never touches tests/compat)
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
    import xxhash
except ImportError:  # pragma: no cover
    sys.exit("error: needs jsonschema + xxhash (pip install -r tools/validate/requirements.txt)")

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


# ---- chunk@1 decode + hashing: mirrors engine/core/world/src/chunk_codec.cpp (PR #4) ----
CELLS = CHUNK_SIZE * CHUNK_SIZE
FLAG_BLOCKS_MOVE, FLAG_BLOCKS_SIGHT, FLAG_DIFFICULT, FLAG_WATER = 1, 2, 4, 8
FLAG_KNOWN_MASK = 0x0F


def compact_json(obj) -> str:
    """Same bytes as nlohmann::json::dump() for integer-only data: keys sorted, no spaces, raw UTF-8."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


class Canon:
    """Port of eng::CanonicalHasher: explicit little-endian, u32-length-prefixed strings."""
    def __init__(self) -> None:
        self.buf = bytearray()
    def u8(self, v: int) -> None: self.buf += (v & 0xFF).to_bytes(1, "little")
    def u16(self, v: int) -> None: self.buf += (v & 0xFFFF).to_bytes(2, "little")
    def u32(self, v: int) -> None: self.buf += (v & 0xFFFFFFFF).to_bytes(4, "little")
    def i16(self, v: int) -> None: self.u16(v)
    def i32(self, v: int) -> None: self.u32(v)
    def str(self, s: str) -> None:
        b = s.encode("utf-8"); self.u32(len(b)); self.buf += b
    def value(self) -> int: return xxhash.xxh3_64_intdigest(bytes(self.buf))  # seed 0


def spawn_stable_id(x: int, y: int, level: int, index: int) -> int:
    h = Canon(); h.i32(x); h.i32(y); h.i16(level); h.u32(index)
    v = h.value()
    return (v & 0xFFFFFFFF) ^ (v >> 32)


def decode_layers(doc: dict) -> list[list[tuple[int, int, int]]]:
    """Dense 4 x 1024 (tile, elevation, flags); omitted arrays are zeros."""
    out = []
    for layer in doc["layers"]:
        t = layer.get("tiles", [0] * CELLS)
        e = layer.get("elevation", [0] * CELLS)
        f = layer.get("flags", [0] * CELLS)
        out.append(list(zip(t, e, f)))
    return out


def compute_content_hash(doc: dict) -> int:
    """compute_content_hash(Chunk) over the decoded chunk; origin and content_hash excluded."""
    c, h = doc["coord"], Canon()
    h.u16(1)  # format_version (kChunkFormatVersion)
    h.i32(c["x"]); h.i32(c["y"]); h.i16(c["level"])
    h.u32(len(doc["palette"]))
    for p in doc["palette"]:
        h.str(p)
    for layer in decode_layers(doc):
        for tile, elev, flags in layer:
            h.u16(tile); h.u8(elev); h.u8(flags)
    h.u32(len(doc["spawns"]))
    for i, s in enumerate(doc["spawns"]):
        h.str(s["archetype"]); h.i32(s["tile"]["x"]); h.i32(s["tile"]["y"])
        h.u32(spawn_stable_id(c["x"], c["y"], c["level"], i))
        ov = s.get("overrides", {})
        h.str(compact_json(ov) if ov else "")
    h.u32(len(doc["triggers"]))
    for t in doc["triggers"]:
        h.str(compact_json(t))
    return h.value()


def hash_hex(v: int) -> str:
    return f"{v:016x}"


def self_test() -> None:
    """Golden vectors from engine/core/world/tests/chunk_tests.cpp (PR #4)."""
    assert spawn_stable_id(0, 0, 0, 0) == 0xE9CA1F53
    assert spawn_stable_id(-1, 7, -2, 3) == 0x3660E539


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
    pal = len(doc["palette"])
    layers = decode_layers(doc)
    for li, layer in enumerate(layers):
        for i, (tile, elev, flags) in enumerate(layer):
            at = f"layers[{li}] cell {i} (x={i % CHUNK_SIZE},y={i // CHUNK_SIZE})"
            if tile >= pal:
                errs.append(f"{at}: tile {tile} outside palette ({pal})")
            if flags & ~FLAG_KNOWN_MASK:
                errs.append(f"{at}: reserved flag bits 4-7 set ({flags:#x})")
            if tile == 0 and (elev or flags):
                errs.append(f"{at}: empty tile with elevation/flags set")
    ids = [spawn_stable_id(doc["coord"]["x"], doc["coord"]["y"], doc["coord"]["level"], i)
           for i in range(len(doc["spawns"]))]
    if len(ids) != len(set(ids)):
        errs.append("spawns: computed stable_id collision")
    for i, s in enumerate(doc["spawns"]):
        if s["archetype"] not in ctx["ids"] and not ctx["external_ok"](s["archetype"]):
            errs.append(f"spawns[{i}]: archetype '{s['archetype']}' not defined in pack or its depends")
        x, y = s["tile"]["x"], s["tile"]["y"]
        if any(layer[y * CHUNK_SIZE + x][2] & FLAG_BLOCKS_MOVE for layer in layers):
            errs.append(f"spawns[{i}] at ({x},{y}) is on a blocks_move cell")
    tids = [t["id"] for t in doc["triggers"]]
    if len(tids) != len(set(tids)):
        errs.append("triggers: duplicate id")
    if "content_hash" in doc:
        want = hash_hex(compute_content_hash(doc))
        if doc["content_hash"] != want:
            errs.append(f"content_hash {doc['content_hash']} != computed {want} (run with --rehash to update)")


REACTION_EFFECT_TRIGGER = {"ac_bonus": "being_hit", "counter": "spell_cast"}


def check_spell(doc, errs, ctx):
    """Cross-checks for spell@1 that the schema can't express (rules5e_content mirrors these)."""
    lvl, eff, ct = doc["level"], doc["effect"], doc["casting_time"]
    scaling = doc.get("scaling", {})
    if "cantrip" in scaling and lvl != 0:
        errs.append("scaling.cantrip is only for cantrips (level 0)")
    if "upcast" in scaling and lvl == 0:
        errs.append("scaling.upcast is only for leveled spells")
    if "darts" in scaling.get("upcast", {}) and eff["kind"] != "auto_hit":
        errs.append("scaling.upcast.darts needs effect.kind auto_hit")
    if "dice" in scaling.get("upcast", {}) and not ("dice" in eff or "damage" in eff):
        errs.append("scaling.upcast.dice needs effect dice or damage")
    want = REACTION_EFFECT_TRIGGER.get(eff["kind"])
    if (ct["kind"] == "reaction") != (want is not None):
        errs.append(f"effect.kind {eff['kind']} and casting_time.kind {ct['kind']} disagree (ac_bonus/counter are reactions)")
    elif want and ct.get("trigger") != want:
        errs.append(f"effect.kind {eff['kind']} needs casting_time.trigger {want}")
    if "repeat_save" in eff and "condition" not in eff:
        errs.append("effect.repeat_save needs effect.condition")
    if eff.get("on_success") and "damage" not in eff:
        errs.append("effect.on_success needs effect.damage")
    ref = eff.get("blocks_spell")
    if ref and ref not in ctx["ids"] and not ctx["external_ok"](ref):
        errs.append(f"effect.blocks_spell '{ref}' not defined in pack or its depends")
    if not any(doc["components"].get(k) for k in ("v", "s", "m")):
        errs.append("components: at least one of v, s, m")


CHECKS = {"monster": check_monster, "character": check_character, "chunk": check_chunk, "spell": check_spell}


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
    # srd51.* ids may be defined provisionally in-pack (until packs/srd51.core exists) or
    # referenced without a definition; anything else must resolve in-pack or via depends.
    ctx = {"ids": ids, "external_ok": lambda a: a.startswith("srd51.")
           or any(a.startswith(dep + ".") for dep in deps)}
    scripts = {f.relative_to(pack).as_posix() for f in pack.glob("scripts/**/*.luau")}
    script_re = re.compile(r"^scripts/.+\.luau$")

    def check_script_ref(where: str, path) -> None:
        if not isinstance(path, str) or not script_re.match(path):
            out.append(f"{where}: script path {path!r} must match ^scripts/.+\\.luau$")
        elif path not in scripts:
            out.append(f"{where}: script {path} does not exist in pack")

    if "entry_script" in man:
        check_script_ref("manifest.json: entry_script", man["entry_script"])
    for rel in sorted(docs):
        d = docs[rel]
        if isinstance(d, dict):
            if isinstance(d.get("behavior"), dict) and "ai" in d["behavior"]:
                check_script_ref(f"{rel}: behavior.ai", d["behavior"]["ai"])
            if (d.get("$schema") == "monster@1" and isinstance(d.get("presentation"), dict)
                    and "ai" in d["presentation"]):
                print(f"info {pack.name}/{rel}: monster@1 presentation.ai (engine migrates it to behavior.ai; prefer monster@2)")
                check_script_ref(f"{rel}: presentation.ai", d["presentation"]["ai"])
    if "contents" in man or scripts:
        listed = set(man.get("contents", []))
        actual = (set(docs) - {"manifest.json"}) | scripts
        for m in sorted(listed - actual):
            out.append(f"manifest.json: contents lists missing file {m}")
        for m in sorted(actual - listed):
            out.append(f"manifest.json: {m} not listed in contents")
        if man.get("contents", []) != sorted(man.get("contents", [])):
            out.append("manifest.json: contents must be sorted (stable order)")
    for rel in sorted(docs):
        doc, errs = docs[rel], []
        tag = doc.get("$schema") if isinstance(doc, dict) else None
        if tag not in by_tag:
            out.append(f"{rel}: unknown or missing $schema {tag!r}")
            continue
        typ = tag.split("@")[0]
        did = str(doc.get("id", ""))
        if typ not in ("manifest", "chunk") and not (did.startswith(man.get("id", "") + ".") or did.startswith("srd51.")):
            errs.append(f"id '{did}' must be namespaced under pack id '{man.get('id')}' or srd51.")
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


def rehash(packs: list[Path]) -> None:
    """Author helper: rewrite content_hash in chunk files in place (text replace keeps formatting)."""
    for pack in packs:
        for f in sorted(pack.rglob("*.json")):
            text = f.read_text(encoding="utf-8")
            doc = json.loads(text)
            if isinstance(doc, dict) and doc.get("$schema", "").startswith("chunk@"):
                if "content_hash" not in doc:
                    continue  # optional; only refresh hashes that are present
                new = re.sub(r'"content_hash": "[^"]*"', f'"content_hash": "{hash_hex(compute_content_hash(doc))}"', text, count=1)
                if new != text:
                    f.write_text(new, encoding="utf-8")
                    print(f"rehashed {f}")


def run_script_check(exe: str, pack: Path) -> list[str]:
    """Strict Luau typecheck of the pack's scripts/**/*.luau with Engine Core's script_check
    (exact pack-load environment, §3.3). Returns error lines; empty on success."""
    import subprocess
    try:
        r = subprocess.run([exe, str(pack)], capture_output=True, text=True, timeout=300)
    except OSError as e:
        return [f"script_check: cannot run {exe}: {e}"]
    if r.returncode == 0:
        return []
    lines = [l for l in (r.stderr + r.stdout).splitlines() if l.strip() and not l.startswith("ok ")]
    return [f"script_check: {l}" for l in lines] or [f"script_check: exit code {r.returncode}"]


def main(argv: list[str]) -> int:
    self_test()
    registry, by_tag = load_registry()
    if argv[:1] == ["--rehash"]:
        rehash([Path(a).resolve() for a in argv[1:]] or [p for p in discover() if "compat" not in p.parts])
        return 0
    script_check = None
    if argv[:1] == ["--script-check"]:
        if len(argv) < 2:
            print("--script-check needs an executable path", file=sys.stderr)
            return 2
        script_check, argv = argv[1], argv[2:]
    packs = [Path(a).resolve() for a in argv] or discover()
    if not packs:
        print("no packs found", file=sys.stderr)
        return 1
    failed = 0
    for pack in packs:
        errs = validate_pack(pack, registry, by_tag)
        if script_check:
            errs += run_script_check(script_check, pack)
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
