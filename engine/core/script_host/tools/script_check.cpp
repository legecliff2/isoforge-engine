// script_check <pack_dir>... : strict-typecheck every scripts/**/*.luau of each pack exactly as
// the engine does at pack load. Prints "path:line: message"; exit 1 on any error.
#include <cstdio>
#include <string>

#include "script_host/script_host.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: script_check <pack_dir>...\n");
        return 2;
    }
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        auto r = eng::script::check_pack_scripts(argv[i]);
        for (const auto& e : r.errors) std::fprintf(stderr, "%s: %s\n", argv[i], e.c_str());
        std::printf("%s %s (%zu scripts, api_level %u)\n", r.errors.empty() ? "ok  " : "FAIL", argv[i],
                    r.scripts, r.api_level);
        if (!r.errors.empty()) ++failed;
    }
    return failed ? 1 : 0;
}
