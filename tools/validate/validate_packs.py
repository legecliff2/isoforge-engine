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
    from jsonschema.exceptions import best_match
    from referencing import Registry, Resource
    import xxhash
except ImportError:  # pragma: no cover
    sys.exit("error: needs jsonschema + xxhash (pip install -r tools/validate/requirements.txt)")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import packset  # noqa: E402
import checks_v4 as c4  # noqa: E402

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
FLAG_KNOWN_MASK = 0x3F   # bit4 climbable, bit5 open_air (feat/move-modes)
FLAG_OPEN_AIR = 0x20


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
    h.u16(2 if (doc.get("lights") or "ambient" in doc) else 1)  # format_version: 1 unless chunk@2 fields are present
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
    chunk2_tail(doc, h)
    return h.value()


AMBIENT_KIND = {"none": 0, "sun": 1, "moon": 2}
AMBIENT_LEVEL = {"dark": 0, "dim": 1, "bright": 2}


def chunk_light_stable_id(x: int, y: int, level: int, index: int) -> int:
    h = Canon(); h.i32(x); h.i32(y); h.i16(level); h.u32(index); h.u8(ord("L"))
    v = h.value()
    return (v & 0xFFFFFFFF) ^ (v >> 32)


def chunk2_tail(doc: dict, h: Canon) -> None:
    """chunk@2 lights + ambient (R-G13), folded only when present; 'visual' is never hashed."""
    lights, amb = doc.get("lights", []), doc.get("ambient")
    if not lights and amb is None:
        return
    c = doc["coord"]
    h.u32(len(lights))
    for i, l in enumerate(lights):
        flags = (1 if l.get("magical") else 0) | (2 if l.get("sunlight") else 0) | (4 if l.get("starts_unlit") else 0)
        h.i32(l["tile"]["x"]); h.i32(l["tile"]["y"]); h.u16(l["bright_ft"]); h.u16(l.get("dim_ft", 0)); h.u8(flags)
        h.str(l.get("preset", "")); h.u32(chunk_light_stable_id(c["x"], c["y"], c["level"], i))
    h.u8(1 if amb is not None else 0)
    if amb is not None:
        h.u8(AMBIENT_KIND[amb["kind"]]); h.u8(AMBIENT_LEVEL[amb["level"]])


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


def check_stat_block(doc: dict, errs: list[str], pb_expected: int, rolled_hp: bool = True, pb_checked: bool = False) -> None:
    ab = doc["abilities"]
    pb = doc.get("proficiency_bonus", pb_expected)   # optional at monster@5 (default: the CR table, EQUIPMENT 5.2)
    if pb_checked:   # monster@5: checks_v4.check_equipment_v5 reports a mismatch (or accepts proficiency_bonus_reason)
        pass
    elif pb != pb_expected:
        errs.append(f"proficiency_bonus {pb} != expected {pb_expected}")
    for a, v in sorted(doc.get("saving_throws", {}).items()):
        if v != modifier(ab[a]) + pb:
            errs.append(f"saving_throws.{a} = {v}, expected {modifier(ab[a]) + pb} (mod + proficiency)")
    for s, v in sorted(doc.get("skills", {}).items()):
        base = modifier(ab[SKILL_ABILITY[s]])
        if v not in (base + pb, base + 2 * pb):
            errs.append(f"skills.{s} = {v}, expected {base + pb} (or {base + 2 * pb} with expertise)")
    perc = doc.get("skills", {}).get("perception", modifier(ab["wis"]))
    printed_ok = doc.get("$schema") == "monster@5" and doc.get("passive_perception_reason") == "srd_printed"   # Tier-A §25
    if doc["senses"]["passive_perception"] != 10 + perc and not printed_ok:
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
    check_stat_block(doc, errs, cr_proficiency(ch["rating"]), pb_checked=doc.get("$schema") == "monster@5")


def check_character(doc, errs, ctx):
    if sum(c["level"] for c in doc["classes"]) != doc["level"]:
        errs.append("level != sum of classes[].level")
    check_stat_block(doc, errs, level_proficiency(doc["level"]), rolled_hp=False)
    if doc["level"] == 1:  # 1st level: max hit die + CON mod
        hd = int(doc["classes"][0]["hit_die"][1:])
        want = hd + modifier(doc["abilities"]["con"])
        # SRD Draconic Resilience (Sorcerer, Draconic Bloodline): hit point maximum +1 per sorcerer level.
        extra = sum(1 for c in doc["classes"] if c.get("class") == "sorcerer" and c.get("subclass") == "draconic")
        # SRD Dwarven Toughness (hill dwarf): hit point maximum +1 per level.
        tough = doc["level"] if doc.get("race") == "hill_dwarf" else 0
        if doc["hit_points"]["average"] != want + extra + tough:
            errs.append(f"level-1 hit_points {doc['hit_points']['average']} != {want + extra + tough} (max hit die + CON"
                        f"{' + 1 Draconic Resilience' if extra else ''}{' + 1 Dwarven Toughness' if tough else ''})")
    errs.extend(c4.check_race_senses(doc))


# R-G16 chunk `surprise` (docs/CONTENT_CONVENTIONS.md section 8): reserved, no loader reads it yet.
SURPRISE_RESERVED = ("surprise is reserved: no loader reads it yet (R-G16; the future chunk loader passes it to "
                     "sim::Combat::apply_surprise, #123), so it has no effect until that loader lands")


def spawn_team(spawn: dict, typ: str | None) -> str | None:
    """sim spawn_team_name (combat.cpp): overrides.team, else 'party' for a character archetype, 'monsters' for a
    monster one. None when the archetype's schema type is unknown (srd51.* escape hatch) and there is no override."""
    team = spawn.get("overrides", {}).get("team")
    if isinstance(team, str):
        return team
    return {"character": "party", "monster": "monsters"}.get(typ)


def check_chunk_surprise(doc, errs, ctx) -> None:
    """The refs the future loader resolves: spawns[] indices must exist and be combatants (sim::Combat::apply_surprise
    refuses an unknown id, and a spawn whose archetype is not a monster / character is no combatant); each side must be
    the team of a combatant spawn of this chunk."""
    types = ctx.get("types", {})
    spawns = doc["spawns"]
    sp = doc["surprise"]
    kinds = [types.get(s["archetype"]) for s in spawns]
    # combatant spawns: monster / character archetypes, plus unknown types (escape hatch: assume a combatant)
    combat = [i for i, k in enumerate(kinds) if k in ("monster", "character") or k is None]
    for k, idx in enumerate(sp.get("spawns", [])):
        where = f"surprise.spawns[{k}]"
        if idx >= len(spawns):
            errs.append(f"{where}: index {idx} is not a spawns[] index (this chunk has {len(spawns)} spawns)")
        elif idx not in combat:
            errs.append(f"{where}: spawns[{idx}] ({spawns[idx]['archetype']}, a {kinds[idx]}) is not a combatant "
                        "(only monster and character archetypes can be surprised)")
    teams = {i: spawn_team(spawns[i], kinds[i]) for i in combat}
    for k, side in enumerate(sp.get("sides", [])):
        members = {i for i, t in teams.items() if t == side}
        unknown = {i for i, t in teams.items() if t is None} if side in ("party", "monsters") else set()
        if not members and not unknown:
            errs.append(f"surprise.sides[{k}]: no combatant spawn in this chunk is on team {side!r} "
                        f"(teams here: {sorted(t for t in set(teams.values()) if t)})")


# R-G11 creature facts (docs/CONTENT_CONVENTIONS.md 4.6): monster@4 / character@4 accept them, but their reader is the sim
# archetype loader (engine/core/sim/src/content.cpp, Engine Core), which does not read them yet: reserved, warning only.
CREATURE_FACTS = ("no_breath", "shapechanger", "evil")


def creature_fact_warnings(doc) -> list[str]:
    """R-G12 (7.2q): with a shapechanger trait present the boolean is read (the trait requires it), so its reserved
    warning is dropped; the boolean without the trait stays legal and keeps the warning."""
    if not isinstance(doc, dict):
        return []
    read = {"shapechanger"} if c4.has_effect_kind(doc, "shapechanger") else set()
    return [f"/{k}: {k} is reserved: the sim archetype loader does not read it yet (R-G11 creature fact, Combatant::{k}; "
            "Engine Core), so it has no effect until that loader lands" for k in CREATURE_FACTS if k in doc and k not in read]


def chunk_warnings(doc) -> list[str]:
    """Non-fatal chunk findings (prefixed with the file by the caller)."""
    if not isinstance(doc, dict) or "surprise" not in doc:
        return []
    return [SURPRISE_RESERVED]


def check_chunk(doc, errs, ctx):
    pal = len(doc["palette"])
    layers = decode_layers(doc)
    for li, layer in enumerate(layers):
        for i, (tile, elev, flags) in enumerate(layer):
            at = f"layers[{li}] cell {i} (x={i % CHUNK_SIZE},y={i // CHUNK_SIZE})"
            if tile >= pal:
                errs.append(f"{at}: tile {tile} outside palette ({pal})")
            if flags & ~FLAG_KNOWN_MASK:
                errs.append(f"{at}: reserved flag bits 6-7 set")
            # feat/move-modes: open air (bit 5) marks a cell without ground; blocks_sight may ride along (fog over a chasm).
            if tile == 0 and (elev or (flags & ~(FLAG_OPEN_AIR | 0x02))):
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
    if "surprise" in doc:
        check_chunk_surprise(doc, errs, ctx)
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
UPCAST_EFFECTS = {"dice": ("spell_attack", "save", "heal", "auto_hit", "zone", "hp_pool", "delayed_damage", "sustained_damage",
                           "summon_weapon", "weapon_smite"),   # 7.2zg Spiritual Weapon; 7.2zh Branding Smite
                  "darts": ("auto_hit",),
                  "beams": ("spell_attack",), "targets": ("save", "heal", "apply_condition", "apply_modifier", "temp_hp")}   # 7.2ze Bless / Bane


FIRST_EFFECT_ONLY = ("hp_pool", "dispel", "revive", "end_condition", "sustained_damage",   # R-G15, spell_loader.cpp
                     "teleport",   # R-G9 (sim #144)
                     "weapon_imbue", "duplicates",   # 7.2zf
                     c4.ZG_KEYS["haste"], c4.ZG_KEYS["slow"], c4.ZG_KEYS["summon_weapon"], c4.ZG_KEYS["temp_hp"],   # 7.2zg
                     c4.ZH_KEYS["weapon_smite"],   # 7.2zh
                     c4.ZI_KEYS["stabilize"])   # 7.2zi
TIMED_BY_DURATION = ("hp_pool", "sustained_damage")   # their timer is the spell's duration in rounds


def check_spell(doc, errs, ctx):
    """Cross-checks for spell@1 that the schema can't express. rules5e_content (spell_loader.cpp)
    enforces the same rules with the same wording; keep them in sync."""
    lvl, effs, ct = doc["level"], doc["effects"], doc["casting_time"]
    errs += c4.check_spell_modifiers(doc)
    errs += c4.check_spell_gaps(doc)   # 7.2zc
    errs += c4.check_spell_batch2(doc)   # 7.2zd
    errs += c4.check_spell_batch3(doc)   # 7.2ze
    errs += c4.check_spell_imbue_mirror(doc)   # 7.2zf
    errs += c4.check_spell_batch_zg(doc)   # 7.2zg
    errs += c4.check_spell_batch_zh(doc)   # 7.2zh
    errs += c4.check_spell_batch_zi(doc)   # 7.2zi
    eff = effs[0]   # the first effect carries targeting, scaling and the reaction/duration rules
    if len(effs) > 1 and any(e["kind"] in REACTION_EFFECT_TRIGGER for e in effs):
        errs.append("effects: ac_bonus and counter reactions cannot be combined with other effects")
        return
    # R-G15 (#120): of the new kinds only delayed_damage rides another effect (the loader's wording).
    for i, e in enumerate(effs[1:], 1):
        if e["kind"] in FIRST_EFFECT_ONLY:
            errs.append(f"effects[{i}]: {e['kind']} is only allowed as the first effect")
            return
        if c4.ZG_KEYS["on_miss"] in e:   # 7.2zg Acid Arrow (spell_loader.cpp wording)
            errs.append(f"effects[{i}]: on_miss is only allowed on the first effect")
            return
    if eff.get("applies_to", "all") != "all":
        errs.append("effects[0].applies_to needs a previous effect")
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
        elif key == "dice" and "dice" not in eff and "damage" not in eff and not zone_has_damage(eff):
            errs.append("scaling.upcast.dice needs effect dice or damage")
    want = REACTION_EFFECT_TRIGGER.get(kind)
    damaged = ct["kind"] == "reaction" and ct.get("trigger", {}).get("kind") == c4.ZG_KEYS["damaged"]   # 7.2zg Hellish Rebuke
    if damaged:
        want = c4.ZG_KEYS["damaged"]
        if kind != "save":
            errs.append("casting_time.trigger.kind damaged needs a save effect")
        elif not ct["trigger"].get("requires_sight", False):
            errs.append("casting_time.trigger.kind damaged needs requires_sight")
        elif doc["range"]["kind"] != "ranged":
            errs.append("casting_time.trigger.kind damaged needs a ranged range")
    if (ct["kind"] == "reaction") != (want is not None):
        errs.append(f"effect kind {kind} and casting_time.kind {ct['kind']} disagree (ac_bonus/counter are reactions, and only they)")
    elif want and ct["trigger"]["kind"] != want:
        errs.append(f"effect kind {kind} needs casting_time.trigger.kind {want}")
    if "or_targeted_by" in ct.get("trigger", {}) and ct["trigger"]["kind"] != "being_hit":
        errs.append("casting_time.trigger.or_targeted_by is only for being_hit")
    if (kind == "ac_bonus") != (doc["duration"]["kind"] == "until_start_of_caster_turn"):
        errs.append("ac_bonus effects last until_start_of_caster_turn (and only they use it)")
    if "targets" in doc and kind not in ("save", "heal", "apply_condition", "polymorph", "max_hp_bonus",
                                                 "apply_modifier", c4.ZG_KEYS["haste"], c4.ZG_KEYS["slow"]):   # polymorph: its own wording; 7.2zd Aid; 7.2ze Bless / Bane; 7.2zg Slow (6) / Haste (1), as the loader
        errs.append(f"targets not allowed on a {kind} effect (one target, per dart/beam, or self)")
    if kind in TIMED_BY_DURATION and doc["duration"]["kind"] not in ("rounds", "minutes", "hours"):
        errs.append(f"{kind} needs a rounds, minutes or hours duration")
    check_spell_zone(doc, errs)
    errs += c4.check_polymorph_spell(doc, ctx)
    refs = [("effect blocks_spell", eff.get("blocks_spell"))] + \
           [("casting_time.trigger.or_targeted_by", r) for r in ct.get("trigger", {}).get("or_targeted_by", [])]
    for where, ref in refs:
        if ref and ref not in ctx["spell_ids"] and not ctx["external_ok"](ref):
            errs.append(f"{where} '{ref}' is not a spell@1 in this pack or its depends")


ZONE_ENTER_ONLY = ("zone_enter",)
ZONE_OWN_TURN = ("zone_enter", "zone_move_within")


def zone_has_damage(eff) -> bool:
    """A zone's damage lives in its trigger rows (scaling.upcast.dice scales them)."""
    return eff.get("kind") == "zone" and any("damage" in t["effect"] for t in eff["zone"].get("triggers", []))


def check_spell_zone(doc, errs):
    """R-G11 zones (spell@1 kind `zone`, docs/CONTENT_CONVENTIONS.md section 4). The loader contract: rules5e_content must
    refuse the same documents with the same wording. Messages start with `zone` (not `effect`), so the loader's negative-case
    test does not compare them before the loader parses zones."""
    effs = doc["effects"]
    for i, e in enumerate(effs[1:], 1):
        if e["kind"] == "zone":
            errs.append(f"zone must be the primary effect (effects[{i}] is a zone)")
    eff = effs[0]
    if eff["kind"] != "zone":
        return
    z = eff["zone"]
    if "area" in doc:
        errs.append("zone: area not allowed (zone.shape / size_ft give the zone's geometry)")
    if doc["duration"]["kind"] == "instantaneous":
        errs.append("zone: needs a lasting duration (not instantaneous)")
    if z["anchor"] == "creature" and doc["range"]["kind"] != "self":
        errs.append("zone: anchor creature (an aura on the caster) needs range self")
    if "move" in z and z["anchor"] != "point":
        errs.append("zone: move needs anchor point")
    flags = z.get("flags", [])
    if "light_obscured" in flags and "heavy_obscured" in flags:
        errs.append("zone: flags light_obscured and heavy_obscured are exclusive")
    if not flags and not z.get("triggers") and "aura_modifier" not in z and "aura_immunity" not in z:
        errs.append("zone: needs triggers or flags (it would do nothing)")
    # R-G11 rules PR 2 keys (CONTENT_CONVENTIONS 4.6; loader parse_zone uses the same messages). Type / range / shape-arm
    # errors are the schema's; these are the cross-field rules.
    sided = z.get("sided") is True
    if ("side_ft" in z) != sided:
        errs.append("zone: side_ft is required for (and only for) sided zones")
    if "aura_bonus_ability" in z and "aura_modifier" not in z:
        errs.append("zone: aura_bonus_ability needs aura_modifier")
    if "aura_modifier" in z and z["aura_modifier"]["stat"] != "save":
        errs.append("zone.aura_modifier stat must be save")
    if any(k in z for k in ("aura_modifier", "aura_immunity", "aura_allies_only")) and z["anchor"] != "creature":
        errs.append("zone: aura fields need anchor creature")
    for i, t in enumerate(z.get("triggers", [])):
        where = f"zone.triggers[{i}]"
        if t["effect"]["kind"] not in ("save", "damage", "apply_condition"):
            errs.append(f"{where}.effect.kind '{t['effect']['kind']}' is not a zone trigger effect (save, damage, apply_condition)")
        if t.get("own_turn_only") and t["on"] not in ZONE_OWN_TURN:
            errs.append(f"{where}: own_turn_only is only for zone_enter / zone_move_within")
        if t.get("counts_aura_move_as_enter") and t["on"] not in ZONE_ENTER_ONLY:
            errs.append(f"{where}: counts_aura_move_as_enter is only for zone_enter")
        if t.get("while_in_zone") and "condition" not in t["effect"]:
            errs.append(f"{where}: while_in_zone needs an effect condition")
        if t.get("counts_aura_move_as_enter") and z["anchor"] == "point" and "move" not in z:
            errs.append(f"{where}: counts_aura_move_as_enter on a zone that never moves")
        if t.get("region") == "zone_or_side":
            if not sided:
                errs.append(f"{where}: region zone_or_side needs a sided zone")
            if t["on"] not in ("zone_appear", "zone_start_turn", "zone_end_turn"):
                errs.append(f"{where}: region zone_or_side is only for zone_appear / zone_start_turn / zone_end_turn")
        if t["effect"]["kind"] != "save" and (t.get("auto_success_damage") or t.get("auto_success_conditions")
                                              or t.get("auto_success_no_breath") or t.get("shapechanger_disadvantage")):
            errs.append(f"{where}: auto_success / shapechanger_disadvantage need effect kind save")
        if "evil_damage_type" in t and "damage" not in t["effect"]:
            errs.append(f"{where}: evil_damage_type needs effect damage")


def spell_material_warnings(doc) -> list[str]:
    """Priced material components (docs/CONTENT_CONVENTIONS.md section 7). Warnings, not errors: the documents load, but a
    priced material without `item` can never be provided (the sim refuses the cast), and `item` without a cost does nothing.
    `item` is not checked against item@1 documents (the sim needs none; an equipment[] entry with value_gp provides it)."""
    m = doc.get("components", {}).get("m")
    if not isinstance(m, dict):
        return []
    cost = m.get("cost_cp", 0)
    if cost > 0 and "item" not in m:
        return [f"/components/m: cost_cp {cost} without item: no equipment entry can provide this material, so the sim "
                "refuses the cast (priced materials)"]
    if "item" in m and cost <= 0:
        return [f"/components/m: item '{m['item']}' is ignored without cost_cp (a material with no cost needs nothing; "
                "priced materials)"]
    return []


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


# Effect kinds with a registered engine hook, cost values, action kinds and top-level keys are per schema
# tag: checks_v4.TAG_TABLE (the single place that knows what each version accepts; no `if version >= N`).
# Kinds without a hook at a tag are allowed but warned about.
KNOWN_EFFECT_KINDS = c4.LIVE_AT_3_EFFECT_KINDS   # @<=3 row (kept for callers/tests that import it)


COMMON_ID = "https://isoforge.dev/schemas/common/v1.schema.json"


def is_active_effects(tag: str) -> bool:
    """Whether the engine applies this document's feature effects (docs/CONTENT_COMPAT.md §1):
    the tag table's `active_effects` (character/monster @3+); older files are migrated with effects marked inert."""
    row = c4.tag_row(tag)
    return bool(row and row.active_effects)


def effect_findings(rel: str, doc: dict, tag: str, registry: Registry, on_disk: bool = True) -> tuple[list[str], list[str]]:
    """(errors, warnings) for features[]/traits[] effects, driven by the tag's row of checks_v4.TAG_TABLE.

    - 'inert' is reserved for the engine's migrations: error in every version for a file on disk. An
      in-memory migration result (on_disk=False) legitimately carries it, and its parameters are not
      validated beyond `kind` (section 8.3: relaxed rule for inert effects of new-set kinds).
    - Unknown kinds (no hook at this tag): warning (the engine reports and ignores them).
    - Known kinds, active documents (character@3+, monster@3+): the kind-tagged contract
      (common@1 $defs/effect, and the common@2 arms at @4) is an error; cost values outside the tag's
      row are errors ('reaction' is reserved up to @3 and legal from @4).
    - Known kinds, character@1/@2, monster@1/@2: effects are inert descriptions, so findings are warnings.
    """
    errs, warns = [], []
    row = c4.tag_row(tag)
    if row is None:
        return errs, warns
    active = row.active_effects
    v = Draft202012Validator({"$ref": COMMON_ID + "#/$defs/effect"}, registry=registry)
    seen_once: dict[str, str] = {}
    for key in ("features", "traits"):
        for i, f in enumerate(doc.get(key) or []):
            eff = f.get("effect") if isinstance(f, dict) else None
            if not isinstance(eff, dict):
                continue
            where = f"{rel}: /{key}/{i}/effect"
            if "inert" in eff and on_disk:
                errs.append(f"{where}: 'inert' is reserved for the engine's @2->@3 / @3->@4 migrations; "
                            "do not author it")
            kind = eff.get("kind")
            if not isinstance(kind, str):
                continue
            if kind not in row.effect_kinds:
                hint = (f"; '{kind}' is loaded only from @4 documents: the sim lists it inert here (the 3 -> 4 migration's "
                        "kind list is frozen, so migrating does not carry it)") if kind in c4.COMPILED_EFFECT_KINDS_AT_4 else ""
                warns.append(f"{where}: unknown effect kind '{kind}' "
                             f"(engine ignores it; known: {', '.join(sorted(row.effect_kinds))}){hint}")
                continue
            if kind in c4.NO_STACK_EFFECT_KINDS and "inert" not in eff:
                if kind in seen_once:
                    warns.append(f"{where}: duplicate '{kind}' effect (already declared at {seen_once[kind]}); "
                                 "a second declaration adds nothing")
                else:
                    seen_once[kind] = f"/{key}/{i}/effect"
            found = []
            if eff.get("cost") == "reaction" and "reaction" not in row.cost_values:
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
    if active:   # R-G14 metamagic / sculpt_spells: pools and declarations (sim #143 load errors)
        m_errs, m_warns = c4.check_metamagic(doc)
        errs += [f"{rel}: {m}" for m in m_errs]
        warns += [f"{rel}: {m}" for m in m_warns]
        if row.legacy_stamps:   # @4: on_event usage (sim #158)
            u_errs, u_warns = c4.check_on_event_usage(doc)
            errs += [f"{rel}: {m}" for m in u_errs]
            warns += [f"{rel}: {m}" for m in u_warns]
            t_errs, t_warns = c4.check_sim170(doc)   # sim #170
            errs += [f"{rel}: {m}" for m in t_errs]
            warns += [f"{rel}: {m}" for m in t_warns]
            s_errs, s_warns = c4.check_sim172_174(doc)   # sim #172 swarm / #174 giant spider follow-up
            errs += [f"{rel}: {m}" for m in s_errs]
            warns += [f"{rel}: {m}" for m in s_warns]
        gr_errs, gr_warns = c4.check_grapple_rider(doc)   # Druid gap G: grapple rider fields, monster@5 only; Tier-A §27
        errs += [f"{rel}: {m}" for m in gr_errs]
        warns += [f"{rel}: {m}" for m in gr_warns]
        errs += [f"{rel}: {m}" for m in c4.check_slot_recovery(doc)]   # 7.2n slot_recovery (sim #148 load errors)
        if "gaze" in row.effect_kinds:   # 7.2p gaze (@4): loader defaults left implicit
            g_errs, g_warns = c4.check_gaze(doc)
            errs += [f"{rel}: {m}" for m in g_errs]
            warns += [f"{rel}: {m}" for m in g_warns]
        if "transparent" in row.effect_kinds:   # 7.2w transparent (@4): loader refusals, highest-wins warning
            t_errs, t_warns = c4.check_transparent(doc, tag.split("@")[0])
            errs += [f"{rel}: {m}" for m in t_errs]
            warns += [f"{rel}: {m}" for m in t_warns]
        se_errs, se_warns = c4.check_save_effect_riders(doc, tag)   # Tier-A §26 Dreadful Glare (monster@5 only)
        errs += [f"{rel}: {m}" for m in se_errs]
        warns += [f"{rel}: {m}" for m in se_warns]
        sa_errs, sa_warns = c4.check_save_advantage(doc, tag)   # batch 3 save_advantage (monster@5 only)
        errs += [f"{rel}: {m}" for m in sa_errs]
        warns += [f"{rel}: {m}" for m in sa_warns]
        au_errs, au_warns = c4.check_aura(doc, tag)   # batch 3 aura (ghast Stench, monster@5 only)
        errs += [f"{rel}: {m}" for m in au_errs]
        warns += [f"{rel}: {m}" for m in au_warns]
        errs += [f"{rel}: {m}" for m in c4.check_batch3_actions(doc, tag)]   # batch 3 weapon-only action keys
        errs += [f"{rel}: {m}" for m in c4.check_until_cured(doc, tag)]   # batch 3 until_cured (death dog)
        errs += [f"{rel}: {m}" for m in c4.check_multiattack_count(doc, tag)]   # batch 3 count_dice (violet fungus)
        errs += [f"{rel}: {m}" for m in c4.check_teleport_actions(doc, tag)]   # batch 3 teleport action (blink dog)
        if "extra_attack" in row.effect_kinds:   # EC #179 extra_attack (@4): loader refusals, highest-wins warning
            x_errs, x_warns = c4.check_extra_attack(doc, tag.split("@")[0])
            errs += [f"{rel}: {m}" for m in x_errs]
            warns += [f"{rel}: {m}" for m in x_warns]
    return errs, warns


def authored_inert_findings(doc: dict, tag: str) -> list[str]:
    """`inert` on reactions[] / bonus_actions[] entries of a file on disk (@4 arms only; at @<=3 the closed
    v1 action already rejects it). Only the schema and the in-memory migration output accept it."""
    row = c4.tag_row(tag)
    if row is None or not row.legacy_stamps:
        return []
    out = []
    for key in ("reactions", "bonus_actions"):
        for i, e in enumerate(doc.get(key) or []):
            if isinstance(e, dict) and "inert" in e:
                out.append(f"/{key}/{i}: 'inert' is reserved for the engine's @3->@4 migration; do not author it")
    return out


COMMON2_ID = "https://isoforge.dev/schemas/common/v2.schema.json"
NATIVE_EFFECT_ARMS = {"on_event": "on_event_effect", "apply_modifier": "apply_modifier_effect",
                      "grant_action": "grant_action_effect", "heal_pool": "heal_pool_effect",
                      "wild_shape": "wild_shape_effect", "shapechanger": "shapechanger_effect",   # R-G12 7.2q
                      "extra_attack": "extra_attack_effect",   # EC #179
                      "spell_mod": "spell_mod_effect",         # 7.2zb
                      "heal_bonus": "heal_bonus_effect"}       # 7.2zc


def schema_message(e, registry: Registry | None = None, drill: bool = True) -> str:
    """jsonschema's message, kept readable: messages that open with the offending instance's repr
    ('{...} is not valid under any of the given schemas') would otherwise be cut off before the part
    that says what is wrong, so a long repr is abbreviated to '{...}'."""
    msg, rep = e.message, repr(e.instance)
    if len(msg) > 200 and msg.startswith(rep):
        if isinstance(e.instance, (dict, list)):
            msg = "{...}" + msg[len(rep):]
    msg = msg[:200]
    inst = e.instance
    if (e.validator == "anyOf" and registry is not None and isinstance(inst, dict)
            and inst.get("kind") in NATIVE_EFFECT_ARMS and "inert" not in inst):
        # the @4 effect is an anyOf of three arms: report the failure inside the arm its `kind` selects
        arm = Draft202012Validator({"$ref": f"{COMMON2_ID}#/$defs/{NATIVE_EFFECT_ARMS[inst['kind']]}"}, registry=registry)
        deepest = best_match(arm.iter_errors(inst))
        if deepest is not None and deepest.instance is inst and deepest.validator in ("anyOf", "oneOf"):
            # the arm's own top-level alternatives (7.2zb spell_mod: at least one optional key): name the closest one
            inner = best_match(deepest.context) if deepest.context else None
            return f"{inst['kind']} effect is invalid: {(inner or deepest).message[:200]}"[:300]
        if deepest is not None:
            where = "/".join(map(str, deepest.absolute_path))
            return f"{inst['kind']} effect is invalid: {'/' + where + ': ' if where else ''}{schema_message(deepest, registry)}"[:300]
    if (drill and e.validator == "anyOf" and registry is not None and isinstance(inst, dict) and "inert" not in inst
            and inst.get("kind") in c4.LIVE_AT_3_EFFECT_KINDS | c4.OPEN_EFFECT_KINDS_RG7):
        # a live open kind (common@1 contract): report the failure inside the effect_live arm
        arm = Draft202012Validator({"$ref": f"{COMMON2_ID}#/$defs/effect_live"}, registry=registry)
        deepest = best_match(arm.iter_errors(inst))
        if deepest is not None:
            where = "/".join(map(str, deepest.absolute_path))
            return f"{inst['kind']} effect is invalid: {'/' + where + ': ' if where else ''}{schema_message(deepest, registry, False)}"[:300]
    if (drill and e.validator == "oneOf" and registry is not None and isinstance(inst, dict) and "window" in inst
            and "inert" not in inst and list(e.absolute_path)[-2:-1] == ["reactions"]):
        # a reactions[] entry with a window is the common@2 reaction arm (not the migration's legacy stamp)
        arm = Draft202012Validator({"$ref": f"{COMMON2_ID}#/$defs/reaction"}, registry=registry)
        deepest = best_match(arm.iter_errors(inst))
        if deepest is not None:
            where = "/".join(map(str, deepest.absolute_path))
            return f"{msg} [reaction: {'/' + where + ': ' if where else ''}{schema_message(deepest, registry, False)}]"[:400]
    if (e.validator == "anyOf" and isinstance(inst, dict) and inst.get("kind") in ("save_effect", "reaction")
            and "inert" not in inst and list(e.absolute_path)[-1:] == ["effect"]):
        return (f"'{inst['kind']}' is an action / reaction kind, not a trait effect kind: it is valid here only as the "
                "migration's inert stamp (use actions[] / reactions[])")
    if e.validator in ("anyOf", "oneOf") and e.context:
        deepest = best_match(e.context)   # the most relevant failure inside the alternatives
        if deepest is not None:
            where = "/".join(map(str, list(deepest.absolute_path)[len(list(e.absolute_path)):]))
            msg += f" [closest: {'/' + where + ': ' if where else ''}{deepest.message[:120]}]"
    return msg


def validate_doc(rel: str, doc: dict, tag: str, registry: Registry, by_tag: dict, pack_id: str, ctx: dict,
                 on_disk: bool = True) -> tuple[list[str], list[str]]:
    """Validate one document of schema `tag`: (errors without the file prefix, warnings with it).
    on_disk=False validates an in-memory migration result: the migration's `inert` stamps are accepted
    (they are only rejected in authored files)."""
    errs: list[str] = []
    warns: list[str] = []
    typ = tag.split("@")[0]
    did = str(doc.get("id", ""))
    if typ not in ("manifest", "chunk") and not (did.startswith(pack_id + ".") or did.startswith("srd51.")):
        errs.append(f"id '{did}' must be namespaced under pack id '{pack_id}' or srd51.")
    reject_floats(doc, "", errs)
    v = Draft202012Validator(by_tag[tag], registry=registry)
    schema_errs = sorted(v.iter_errors(doc), key=lambda e: list(map(str, e.absolute_path)))
    # monster@4 / character@4 reach the same defs through the stat_block $ref and their own properties,
    # so each finding can be reported twice: de-duplicate, keeping order.
    errs += list(dict.fromkeys(f"/{'/'.join(map(str, e.absolute_path))}: {schema_message(e, registry)}" for e in schema_errs))
    if not schema_errs and typ in CHECKS:
        CHECKS[typ](doc, errs, ctx)
    if typ == "chunk":
        warns += [f"{rel}: {w}" for w in chunk_warnings(doc)]
    if not schema_errs and typ == "spell":
        warns += [f"{rel}: {w}" for w in spell_material_warnings(doc)]
        warns += [f"{rel}: {w}" for w in c4.polymorph_spell_warnings(doc, ctx)]
    if not schema_errs and typ in ("monster", "character"):
        warns += [f"{rel}: {w}" for w in creature_fact_warnings(doc)]
    row = c4.tag_row(tag)
    if not schema_errs and row is not None and row.cross_refs:
        if typ == "item":
            e4, w4 = c4.check_item(doc, ctx)
        else:
            e4, w4 = c4.check_stat_block_v4(typ, doc, ctx)
            if tag in ("monster@5", "character@5"):
                e5, w5 = c4.check_equipment_v5(typ, doc, ctx)
                e4, w4 = e4 + e5, w4 + w5
            if tag == "character@5":   # PR C: SRD proficiencies and the derived-vs-printed check
                for fn in (c4.check_character_proficiencies, c4.check_character_derive):
                    e7, w7 = fn(doc, ctx)
                    e4, w4 = e4 + e7, w4 + w7
            if tag == "monster@5":   # EQUIPMENT 5.6 derived-vs-printed (rules5e::derive_monster mirror)
                e6, w6 = c4.check_monster_derive(doc, ctx)
                e4, w4 = e4 + e6, w4 + w6
        errs += e4
        warns += [f"{rel}: {w}" for w in w4]
    if typ in ("monster", "character") and isinstance(doc, dict):
        e2, w2 = effect_findings(rel, doc, tag, registry, on_disk)
        errs += [e.split(": ", 1)[1] for e in e2]   # errs is prefixed with rel by the caller
        warns += w2
        if on_disk:
            errs += authored_inert_findings(doc, tag)
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
    item_ids = {d.get("id") for r, d in docs.items() if isinstance(d, dict) and str(d.get("$schema", "")).startswith("item@")}
    for dep in order:
        if dep.id == pid:
            continue
        for cid, rel in dep.content_ids.items():
            ids.add(cid)
            if rel.startswith("spells/") or _is_spell(dep.dir / rel):
                spell_ids.add(cid)
            if rel.startswith("items/") or _is_item(dep.dir / rel):
                item_ids.add(cid)
    # Deprecated escape hatch: a pack that doesn't (transitively) depend on srd51.core may still
    # reference undefined srd51.* ids, with a warning. Packs depending on srd51.core must resolve.
    hatch = pid != "srd51.core" and all(d.id != "srd51.core" for d in order)
    hatch_used: set[str] = set()

    def external_ok(a: str) -> bool:
        if hatch and a.startswith("srd51."):
            hatch_used.add(a)
            return True
        return False

    # content id -> schema type (monster, character, ...): chunk surprise derives spawn teams from it
    types = {d.get("id"): str(d.get("$schema", "")).split("@")[0] for r, d in docs.items()
             if r != "manifest.json" and isinstance(d, dict)}
    for dep in order:
        if dep.id != pid:
            for cid, rel in dep.content_ids.items():
                types.setdefault(cid, _schema_type(dep.dir / rel))
    # R-G12 (7.2q) transform forms: monster content id -> (creature type, challenge.rating), dependency packs included
    monsters = {d.get("id"): c4.monster_facts(d) for r, d in docs.items()
                if r != "manifest.json" and isinstance(d, dict) and str(d.get("$schema", "")).startswith("monster@")}
    for dep in order:
        if dep.id != pid:
            for cid, rel in dep.content_ids.items():
                if cid not in monsters and (rel.startswith("monsters/") or _schema_type(dep.dir / rel) == "monster"):
                    try:
                        monsters[cid] = c4.monster_facts(json.loads((dep.dir / rel).read_text(encoding="utf-8")))
                    except (OSError, ValueError):
                        pass
    ctx = {"ids": ids, "spell_ids": spell_ids, "items": item_ids, "external_ok": external_ok, "types": types,
           "monsters": monsters,
           # item@2 documents of this pack (EQUIPMENT 3.1 hand slots: the category / hands of an equipped item)
           "item_docs": {d.get("id"): d for r, d in docs.items()
                         if isinstance(d, dict) and str(d.get("$schema", "")).startswith("item@")}}
    for dep in order:   # dependency packs' items (srd51.item.shield equipped by a test pack's monster)
        if dep.id != pid:
            for cid, rel in dep.content_ids.items():
                if cid not in ctx["item_docs"] and rel.startswith("items/"):
                    try:
                        ctx["item_docs"][cid] = json.loads((dep.dir / rel).read_text(encoding="utf-8"))
                    except (OSError, ValueError):
                        pass
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
    levels_needed: dict[int, list[str]] = {}
    for rel in sorted(docs):
        doc = docs[rel]
        tag = doc.get("$schema") if isinstance(doc, dict) else None
        if tag not in by_tag:
            out.append(f"{rel}: unknown or missing $schema {tag!r}")
            continue
        errs, w = validate_doc(rel, doc, tag, registry, by_tag, str(man.get("id", "")), ctx)
        warns += w
        out += [f"{rel}: {e}" for e in errs]
        row = c4.tag_row(tag)
        if row is not None and row.needs_api_level > 1:
            levels_needed.setdefault(row.needs_api_level, []).append(f"{rel} ({tag})")
    # 8.5: a pack containing any @4 or item@1 document must declare api_level >= 2.
    declared = man.get("api_level")
    for need in sorted(levels_needed):
        if isinstance(declared, int) and declared < need:
            out.append(f"manifest.json: api_level {declared} is below {need}, which {', '.join(levels_needed[need])} "
                       f"require (a pack containing any @4 or item@1 document must declare api_level >= {need})")
    for a in sorted(hatch_used):
        warns.append(f"manifest.json: '{a}' is not defined in this pack or its depends; the srd51.* "
                     "escape hatch is deprecated: add depends [{\"id\": \"srd51.core\", \"version\": \"^1.0\"}]")
    return out


def _schema_type(path: Path) -> str | None:
    try:
        return str(json.loads(path.read_text(encoding="utf-8")).get("$schema", "")).split("@")[0] or None
    except (OSError, ValueError, AttributeError):
        return None


def _is_spell(path: Path) -> bool:
    try:
        return str(json.loads(path.read_text(encoding="utf-8")).get("$schema", "")).startswith("spell@")
    except (OSError, ValueError, AttributeError):
        return False


def _is_item(path: Path) -> bool:
    try:
        return str(json.loads(path.read_text(encoding="utf-8")).get("$schema", "")).startswith("item@")
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
        # Schema-structure lints (fast, no packs): common@2 stray-ref / re-declaration lint (VERSION_BUMP_BATCH
        # 1.2) and the allowed == required + optional per-arm test for the propertyNames unions (3.1).
        for lint in ("test_common2_lint.py", "test_union_arms.py"):
            r = subprocess.run([sys.executable, str(ROOT / "tests" / "validate" / lint)], capture_output=True, text=True)
            print(("ok   " if r.returncode == 0 else "FAIL ") + f"schema lint {lint}")
            if r.returncode != 0:
                print(r.stdout + r.stderr)
            failed += r.returncode != 0
    return 1 if failed or cross else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
