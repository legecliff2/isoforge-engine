#!/usr/bin/env python3
"""Reference implementation of eng::world::compute_content_hash / spawn_stable_id
for chunk@1 compact JSON (see docs/CHUNK_HASH.md). Requires: pip install xxhash

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

def content_hash(doc) -> int:
    c = doc["coord"]
    b = bytearray()
    b += struct.pack("<Hiih", 1, c["x"], c["y"], c["level"])          # format_version, coord
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
    return xxh3(bytes(b))

if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8") as f:
            print(f"{content_hash(json.load(f)):016x}  {path}")
