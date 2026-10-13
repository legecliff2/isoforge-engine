#pragma once
// Per-process scratch directory for tests that write files. Lives under the build tree
// (TEST_SCRATCH_DIR) and includes the process id, so concurrent ctest runs (two build trees, or
// the same binary twice) never share paths. Removed on destruction.
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <process.h>
#define ENG_TEST_GETPID _getpid
#else
#include <unistd.h>
#define ENG_TEST_GETPID getpid
#endif

struct ScratchDir {
    std::filesystem::path path;
    explicit ScratchDir(const std::string& name)
        : path(std::filesystem::path(TEST_SCRATCH_DIR) / (name + "_" + std::to_string(ENG_TEST_GETPID()))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path.parent_path());
    }
    ~ScratchDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
};
