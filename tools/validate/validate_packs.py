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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packset  # noqa: E402

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
TEAM_RE = re.compile(r"^[a-z][a-z0-9_]*$")   # spawn overrides.team (faction id)
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
    """Golden vectors from engine/core/world/tests/chunk_tests.cpp (PR #4) and rules5e spell ids."""
    assert fnv1a32("srd51.spell.x562789") == fnv1a32("srd51.spell.x779192") == 0xCE11440B
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
        team = s.get("overrides", {}).get("team")
        if team is not None and (not isinstance(team, str) or not TEAM_RE.match(team)):
            errs.append(f"spawns[{i}]: overrides.team {team!r} must match {TEAM_RE.pattern}")
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


def fnv1a32(text: str) -> int:
    """Runtime spell id: rules5e::spell_id_of (FNV-1a 32 over the UTF-8 content id)."""
    h = 2166136261
    for b in text.encode("utf-8"):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def runtime_id_collisions(entries: list[tuple[str, str, str]], cross_pack_only: bool = False) -> list[str]:
    """entries: (pack key = resolved pack path, file label, content_id). Errors for:
    - different content ids with the same 32-bit runtime id (spell_id_of collision), and
    - the same content id defined twice (same pack, or two packs of one set).
    Matches rules5e_content::add_spell. cross_pack_only: report only pairs from different packs."""
    by_hash: dict[int, tuple[str, str, str]] = {}
    out = []
    for key, where, cid in sorted(entries):
        h = fnv1a32(cid)
        prev = by_hash.get(h)
        if prev is not None and not (cross_pack_only and prev[0] == key):
            if prev[2] != cid:
                out.append(f"{where}: spell id '{cid}' collides with '{prev[2]}' ({prev[1]}) "
                           f"at runtime id {h:#010x} (rename one)")
            else:
                out.append(f"{where}: spell id '{cid}' is already defined in {prev[1]} (duplicate content id)")
        by_hash.setdefault(h, (key, where, cid))
    return out


REACTION_EFFECT_TRIGGER = {"ac_bonus": "being_hit", "counter": "spell_cast"}
UPCAST_EFFECTS = {"dice": ("spell_attack", "save", "heal", "auto_hit"), "darts": ("auto_hit",),
                  "beams": ("spell_attack",), "targets": ("save", "heal")}


def check_spell(doc, errs, ctx):
    """Cross-checks for spell@1 that the schema can't express. rules5e_content (spell_loader.cpp)
    enforces the same rules with the same wording; keep them in sync."""
    lvl, effs, ct = doc["level"], doc["effects"], doc["casting_time"]
    if len(effs) != 1:
        errs.append("effects: rules5e resolves exactly one effect per spell for now")
        return
    eff = effs[0]
    kind = eff["kind"]
    scaling = doc.get("scaling", {})
    cantrip, upcast = scaling.get("cantrip"), scaling.get("upcast", {})
    if cantrip is not None:
        if lvl != 0:
            errs.append("scaling.cantrip is only for cantrips (level 0)")
        if cantrip["levels"] != sorted(cantrip["levels"]):
            errs.append("scaling.cantrip.levels must be ascending")
        if cantrip["adds"] == "beams" and kind != "spell_attack":
            errs.append("scaling.cantrip.adds beams needs a spell_attack effect")
    if upcast and lvl == 0:
        errs.append("scaling.upcast is only for leveled spells")
    for key in sorted(upcast):
        if kind not in UPCAST_EFFECTS[key]:
            errs.append(f"scaling.upcast.{key} not allowed on a {kind} effect")
        elif key == "dice" and "dice" not in eff and "damage" not in eff:
            errs.append("scaling.upcast.dice needs effect dice or damage")
    want = REACTION_EFFECT_TRIGGER.get(kind)
    if (ct["kind"] == "reaction") != (want is not None):
        errs.append(f"effect kind {kind} and casting_time.kind {ct['kind']} disagree (ac_bonus/counter are reactions, and only they)")
    elif want and ct["trigger"]["kind"] != want:
        errs.append(f"effect kind {kind} needs casting_time.trigger.kind {want}")
    if "or_targeted_by" in ct.get("trigger", {}) and ct["trigger"]["kind"] != "being_hit":
        errs.append("casting_time.trigger.or_targeted_by is only for being_hit")
    if (kind == "ac_bonus") != (doc["duration"]["kind"] == "until_start_of_caster_turn"):
        errs.append("ac_bonus effects last until_start_of_caster_turn (and only they use it)")
    if "targets" in doc and kind not in ("save", "heal"):
        errs.append(f"targets not allowed on a {kind} effect (one target, per dart/beam, or self)")
    refs = [("effect blocks_spell", eff.get("blocks_spell"))] + \
           [("casting_time.trigger.or_targeted_by", r) for r in ct.get("trigger", {}).get("or_targeted_by", [])]
    for where, ref in refs:
        if ref and ref not in ctx["spell_ids"] and not ctx["external_ok"](ref):
            errs.append(f"{where} '{ref}' is not a spell@1 in this pack or its depends")


CHECKS = {"monster": check_monster, "character": check_character, "chunk": check_chunk, "spell": check_spell}


def pack_set_key(pack: Path) -> str:
    """Packs under tests/compat/<level>/ form one set per level; every other pack is the live set."""
    parts = pack.resolve().parts
    for i in range(len(parts) - 2):
        if parts[i] == "tests" and parts[i + 1] == "compat":
            return "compat/" + parts[i + 2]
    return "live"


def pack_spell_ids(pack: Path) -> list[tuple[str, str, str]]:
    out = []
    for f in sorted(pack.rglob("*.json")):
        try:
            d = json.loads(f.read_text(encoding="utf-8"))
        except ValueError:
            continue
        if isinstance(d, dict) and str(d.get("$schema", "")).startswith("spell@") and isinstance(d.get("id"), str):
            label = pack.relative_to(ROOT) if pack.is_relative_to(ROOT) else pack
            out.append((str(pack.resolve()), f"{label}/{f.relative_to(pack).as_posix()}", d["id"]))
    return out


# Effect kinds with a registered engine hook (eng::sim::FeatureRegistry::builtin()) and a
# kind-tagged branch in common v1 $defs/effect. Others are allowed but warned about.
KNOWN_EFFECT_KINDS = frozenset({"damage_bonus", "bonus_action_grant", "self_heal"})


COMMON_ID = "https://isoforge.dev/schemas/common/v1.schema.json"


def is_active_effects(tag: str) -> bool:
    """Whether the engine applies this document's feature effects (docs/CONTENT_COMPAT.md §1).
    character@3+ and monster@3+; older files are migrated with effects marked inert."""
    typ, _, ver = tag.partition("@")
    return typ in ("character", "monster") and ver.isdigit() and int(ver) >= 3


def effect_findings(rel: str, doc: dict, tag: str, registry: Registry) -> tuple[list[str], list[str]]:
    """(errors, warnings) for features[]/traits[] effects.

    - 'inert' is reserved for the engine's @2->@3 migrations: error in every version.
    - Unknown kinds: warning (the engine reports and ignores them).
    - Known kinds, active documents (character@3+, monster@3+): the kind-tagged contract
      (common@1 $defs/effect) is an error; cost 'reaction' is reserved -> error.
    - Known kinds, character@1/@2, monster@1/@2: effects are inert descriptions, so findings are warnings.
    """
    errs, warns = [], []
    active = is_active_effects(tag)
    v = Draft202012Validator({"$ref": COMMON_ID + "#/$defs/effect"}, registry=registry)
    for key in ("features", "traits"):
        for i, f in enumerate(doc.get(key) or []):
            eff = f.get("effect") if isinstance(f, dict) else None
            if not isinstance(eff, dict):
                continue
            where = f"{rel}: /{key}/{i}/effect"
            if "inert" in eff:
                errs.append(f"{where}: 'inert' is reserved for the engine's @2->@3 migrations; "
                            "do not author it")
            kind = eff.get("kind")
            if not isinstance(kind, str):
                continue
            if kind not in KNOWN_EFFECT_KINDS:
                warns.append(f"{where}: unknown effect kind '{kind}' "
                             f"(engine ignores it; known: {', '.join(sorted(KNOWN_EFFECT_KINDS))})")
                continue
            found = []
            if eff.get("cost") == "reaction":
                found.append("cost 'reaction' is reserved (not supported by the engine yet); "
                             "use 'action' or 'bonus_action'")
            # @3 schemas (trait_active) already report contract violations as errors.
            if not active:
                for e in sorted(v.iter_errors(eff), key=lambda e: list(map(str, e.absolute_path))):
                    path = "/".join(map(str, e.absolute_path))
                    found.append(f"{kind}{'.' + path if path else ''}: {e.message[:160]}")
            if active:
                errs += [f"{where}: {m}" for m in found]
            else:
                warns += [f"{where}: {m} (inert in {tag}; becomes an error at @3)" for m in found]
    return errs, warns


def validate_pack(pack: Path, registry: Registry, by_tag: dict, warnings: list[str] | None = None,
                  pset: "packset.PackSet | None" = None) -> list[str]:
    """Returns errors; non-fatal findings are appended to `warnings` (if given).
    pset: the pack set references resolve against (default: this pack alone)."""
    out: list[str] = runtime_id_collisions(pack_spell_ids(pack))
    warns: list[str] = warnings if warnings is not None else []
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
    # References resolve within this pack or its transitive depends (packset.py, the reference
    # semantics for eng::assets::PackSet). Dependency errors are reported against the pack that
    # declares the dependency.
    pset = pset or packset.PackSet([pack])
    pid = str(man.get("id", ""))
    order, set_errs = pset.resolve([pid]) if pid in pset.by_id else ([], [])
    out += [str(e) for e in set_errs if e.pack == pid]
    spell_ids = {d.get("id") for r, d in docs.items() if isinstance(d, dict) and str(d.get("$schema", "")).startswith("spell@")}
    for dep in order:
        if dep.id == pid:
            continue
        for cid, rel in dep.content_ids.items():
            ids.add(cid)
            if rel.startswith("spells/") or _is_spell(dep.dir / rel):
                spell_ids.add(cid)
    # Deprecated escape hatch: a pack that doesn't (transitively) depend on srd51.core may still
    # reference undefined srd51.* ids, with a warning. Packs depending on srd51.core must resolve.
    hatch = pid != "srd51.core" and all(d.id != "srd51.core" for d in order)
    hatch_used: set[str] = set()

    def external_ok(a: str) -> bool:
        if hatch and a.startswith("srd51."):
            hatch_used.add(a)
            return True
        return False

    ctx = {"ids": ids, "spell_ids": spell_ids, "external_ok": external_ok}
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
        if typ in ("monster", "character") and isinstance(doc, dict):
            e2, w2 = effect_findings(rel, doc, tag, registry)
            errs += [e.split(": ", 1)[1] for e in e2]   # errs is prefixed with rel below
            warns += w2
        out += [f"{rel}: {e}" for e in errs]
    for a in sorted(hatch_used):
        warns.append(f"manifest.json: '{a}' is not defined in this pack or its depends; the srd51.* "
                     "escape hatch is deprecated: add depends [{\"id\": \"srd51.core\", \"version\": \"^1.0\"}]")
    return out


def _is_spell(path: Path) -> bool:
    try:
        return str(json.loads(path.read_text(encoding="utf-8")).get("$schema", "")).startswith("spell@")
    except (OSError, ValueError, AttributeError):
        return False


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
    # Pack sets: the live packs, or one tests/compat/<level> (compat packs never resolve against
    # live ones). With explicit pack arguments, the set is exactly the packs given.
    psets: dict[str, packset.PackSet] = {}
    for key in sorted({pack_set_key(p) for p in packs}):
        psets[key] = packset.PackSet([p for p in packs if pack_set_key(p) == key])
    for pack in packs:
        warns: list[str] = []
        errs = validate_pack(pack, registry, by_tag, warns, psets[pack_set_key(pack)])
        if script_check:
            errs += run_script_check(script_check, pack)
        name = pack.relative_to(ROOT) if pack.is_relative_to(ROOT) else pack
        for w in warns:
            print(f"warning {name}/{w}")
        if errs:
            failed += 1
            print(f"FAIL {name}")
            for e in errs:
                print(f"  {e}")
        else:
            print(f"ok   {name}")
    # Pack sets: packs loaded together must have unique runtime spell ids and content ids
    # (a duplicate content id is an error, as in rules5e_content::add_spell). Frozen compat copies
    # (tests/compat/<level>/...) are their own set, so freezing the live pack doesn't collide with it.
    sets: dict[str, list[Path]] = {}
    for pack in packs:
        sets.setdefault(pack_set_key(pack), []).append(pack)
    cross = []
    for key in sorted(sets):
        cross += runtime_id_collisions([e for pack in sets[key] for e in pack_spell_ids(pack)], cross_pack_only=True)
    for key in sorted(psets):
        cross += [str(e) for e in psets[key].errors]
    for e in cross:
        print(f"FAIL pack set: {e}")
    summary = f"{len(packs) - failed}/{len(packs)} packs valid ({len(by_tag)} schemas)"
    if cross:
        summary = f"FAILED: {summary}, but {len(cross)} pack-set error(s)"
    print(summary)
    sys.stdout.flush()
    if not argv:   # default run (ctest content_validate): also the effect negative cases
        import subprocess
        r = subprocess.run([sys.executable, str(ROOT / "tests" / "validate" / "run_effect_cases.py")])
        failed += r.returncode != 0
    return 1 if failed or cross else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
