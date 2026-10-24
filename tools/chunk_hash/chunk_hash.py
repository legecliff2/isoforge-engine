#!/usr/bin/env python3
"""Reference implementation of eng::world::compute_content_hash / spawn_stable_id
for chunk@1 / chunk@2 compact JSON (see docs/CHUNK_HASH.md). Requires: pip install xxhash

usage: chunk_hash.py file.json [...]   -> prints "<hex16>  <file>"
"""
import json, struct, sys
import xxhash

N, LAYERS = 32, 4

def canon(obj):  # matches nlohmann::json::dump(): sorted keys, compact, UTF-8
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)

def s(b: bytearray, text: str):
    raw = text.encode("utf-8")
    b += struct.pack("<I", len(raw)) + raw

def xxh3(data: bytes) -> int:
    return xxhash.xxh3_64_intdigest(data)  # seed 0

def spawn_stable_id(x, y, level, index):
    h = xxh3(struct.pack("<iihI", x, y, level, index))
    return (h & 0xFFFFFFFF) ^ (h >> 32)

def elevation_is_ground(doc) -> bool:
    """Mirror of Chunk::elevation_is_ground: format_version wins, else "$schema" chunk@N (only 3 = ground)."""
    if "format_version" in doc:
        return doc["format_version"] >= 3
    return doc.get("$schema") == "chunk@3"


def content_hash(doc) -> int:
    c = doc["coord"]
    b = bytearray()
    lights, amb = doc.get("lights", []), doc.get("ambient")
    v3 = elevation_is_ground(doc)   # chunk@3: version 3 and always the chunk@2 tail (docs/CHUNK_HASH.md)
    v2 = v3 or bool(lights) or amb is not None
    b += struct.pack("<Hiih", 3 if v3 else 2 if v2 else 1, c["x"], c["y"], c["level"])   # format_version (1 without chunk@2 fields), coord
    b += struct.pack("<I", len(doc["palette"]))
    for p in doc["palette"]:
        s(b, p)
    for layer in doc["layers"]:
        tiles = layer.get("tiles", [0] * (N * N))
        elev = layer.get("elevation", [0] * (N * N))
        flags = layer.get("flags", [0] * (N * N))
        for i in range(N * N):
            b += struct.pack("<HBB", tiles[i], elev[i], flags[i])
    spawns = doc["spawns"]
    b += struct.pack("<I", len(spawns))
    for i, sp in enumerate(spawns):
        s(b, sp["archetype"])
        b += struct.pack("<ii", sp["tile"]["x"], sp["tile"]["y"])
        b += struct.pack("<I", spawn_stable_id(c["x"], c["y"], c["level"], i))
        ov = sp.get("overrides", {})
        s(b, canon(ov) if ov else "")
    b += struct.pack("<I", len(doc["triggers"]))
    for t in doc["triggers"]:
        s(b, canon(t))
    if v2:  # chunk@2: lights (never 'visual'), then ambient
        b += struct.pack("<I", len(lights))
        for i, l in enumerate(lights):
            flags = (1 if l.get("magical") else 0) | (2 if l.get("sunlight") else 0) | (4 if l.get("starts_unlit") else 0)
            b += struct.pack("<iiHHB", l["tile"]["x"], l["tile"]["y"], l["bright_ft"], l.get("dim_ft", 0), flags)
            s(b, l.get("preset", ""))
            h = xxh3(struct.pack("<iihIB", c["x"], c["y"], c["level"], i, ord("L")))
            b += struct.pack("<I", (h & 0xFFFFFFFF) ^ (h >> 32))
        b += struct.pack("<B", 1 if amb is not None else 0)
        if amb is not None:
            b += struct.pack("<BB", {"none": 0, "sun": 1, "moon": 2}[amb["kind"]], {"dark": 0, "dim": 1, "bright": 2}[amb["level"]])
    return xxh3(bytes(b))

if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8") as f:
            print(f"{content_hash(json.load(f)):016x}  {path}")
