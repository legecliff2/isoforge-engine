// procgen_dump <seed-string> <out_pack_dir> <x,y,level>...
// Writes <out>/manifest.json and <out>/chunks/<x>_<y>_<level>.json (chunk@1, origin procedural).
#include <charconv>
#include <string_view>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "procgen/dungeon.hpp"

// "x,y,level" (no sscanf: MSVC C4996 is fatal under /WX).
static bool parse_coord(std::string_view s, int& x, int& y, int& level) {
    int* out[3] = {&x, &y, &level};
    const char* p = s.data();
    const char* end = s.data() + s.size();
    for (int i = 0; i < 3; ++i) {
        auto [q, ec] = std::from_chars(p, end, *out[i]);
        if (ec != std::errc{} || (i < 2 && (q == end || *q != ','))) return false;
        p = i < 2 ? q + 1 : q;
    }
    return p == end;
}

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
        if (!parse_coord(argv[a], x, y, level)) { std::fprintf(stderr, "bad coord %s\n", argv[a]); return 2; }
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
