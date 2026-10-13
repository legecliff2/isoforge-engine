// procgen_dump <seed-string> <out_pack_dir> <x,y,level>...
// Writes <out>/manifest.json and <out>/chunks/<x>_<y>_<level>.json (chunk@1, origin procedural).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "procgen/dungeon.hpp"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: procgen_dump <seed-string> <out_pack_dir> <x,y,level>...\n");
        return 2;
    }
    auto seed = procgen::parse_seed(argv[1]);
    if (!seed) { std::fprintf(stderr, "bad seed string: %s\n", argv[1]); return 2; }
    const std::filesystem::path out = argv[2];
    std::filesystem::remove_all(out / "chunks");   // no stale chunks from earlier runs
    std::filesystem::create_directories(out / "chunks");
    std::vector<std::string> files;
    for (int a = 3; a < argc; ++a) {
        int x = 0, y = 0, level = 0;
        if (std::sscanf(argv[a], "%d,%d,%d", &x, &y, &level) != 3) { std::fprintf(stderr, "bad coord %s\n", argv[a]); return 2; }
        auto g = procgen::generate(*seed, x, y, int16_t(level));
        if (!g) { std::fprintf(stderr, "unknown gen_version\n"); return 1; }
        const std::string rel = "chunks/" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(level) + ".json";
        std::ofstream(out / rel, std::ios::binary) << procgen::to_json(*g);
        files.push_back(rel);
        std::printf("%s %s %s\n", rel.c_str(), procgen::hash_hex(procgen::content_hash(*g)).c_str(),
                    procgen::walkable_connected(*g) ? "connected" : "DISCONNECTED");
    }
    std::sort(files.begin(), files.end());
    std::string m = "{\n  \"$schema\": \"manifest@1\",\n  \"id\": \"procgen.preview\",\n  \"version\": \"0.1.0\",\n"
                    "  \"engine_min\": \"0.1.0\",\n  \"feature_level\": 1,\n  \"api_level\": 1,\n  \"rules_profile\": \"srd51\",\n"
                    "  \"requires\": [\"iso.tiles.v1\", \"world.procgen.v1\"],\n"
                    "  \"attribution\": \"This work includes material taken from the System Reference Document 5.1 (\\\"SRD 5.1\\\") "
                    "by Wizards of the Coast LLC and available at https://dnd.wizards.com/resources/systems-reference-document. "
                    "The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License available at "
                    "https://creativecommons.org/licenses/by/4.0/legalcode.\",\n  \"contents\": [";
    for (std::size_t i = 0; i < files.size(); ++i) m += (i ? ", \"" : "\"") + files[i] + "\"";
    m += "]\n}\n";
    std::ofstream(out / "manifest.json", std::ios::binary) << m;
    return 0;
}
