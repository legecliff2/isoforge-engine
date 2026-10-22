# CI

Workflow: `.github/workflows/ci.yml`; every OS job runs the composite action
`.github/actions/cmake-ci` (configure, lint, build, test).

| Job | Preset | When |
|---|---|---|
| Detect changes | – | always (docs-only changes skip the build jobs) |
| Linux GCC Debug | `ci-linux-gcc` | every PR, every push to `main` |
| Linux Clang Release | `ci-linux-clang` (`ctest -LE python_only`) | every PR, every push to `main` |
| Windows MSVC Debug | `ci-win-msvc` / `ci-win-msvc-debug` | every PR, every push to `main` |
| Windows MSVC Release | `ci-win-msvc` / `ci-win-msvc-release` (`ctest -LE python_only`) | every push to `main`, manual dispatch, PRs labelled `ci-windows-release` |
| macOS AppleClang Debug | `ci-mac-clang` | nightly 07:17 UTC, manual dispatch, PRs touching platform/render/determinism paths |
| ci-required | – | always; the single required check. `success` or `skipped` needs pass, anything else fails |

To run Windows Release on a PR, add the `ci-windows-release` label and then push or re-run the
workflow (adding a label alone does not trigger CI).

Minutes savers: ccache (Linux/macOS) / sccache (Windows, with `/Z7` embedded debug info) cached with
`actions/cache` per OS + preset, keyed on `cmake/Dependencies.cmake` (`CCACHE_SLOPPINESS` lets ccache cache SDL3's precompiled-header objects, and the `ci` presets set `CMAKE_CXX_SCAN_FOR_MODULES=OFF` because GCC's `-fmodules-ts` makes every C++ object uncacheable; the project uses no C++ modules); `ctest --parallel 4`; the
`python_only` label marks the pure-Python validator tests (no compiled code), which only the Debug
jobs run. `content_validate` itself runs the compiled `script_check` and runs in every job.

## Running the Linux jobs locally: `tools/ci_local.sh`

```sh
tools/ci_local.sh               # Linux GCC Debug, then Linux Clang Release
tools/ci_local.sh --job gcc     # just Linux GCC Debug
tools/ci_local.sh --job clang   # just Linux Clang Release (--full-tests to include python_only)
tools/ci_local.sh --job lint    # configure + depcheck/include_lint for both presets (fast)
tools/ci_local.sh --fresh       # wipe build/<preset> first, like a clean CI runner
```

Same presets and steps as CI: configure, `ctest -R "^(depcheck|include_lint)"`, build, check that
`content_validate` is registered, full `ctest`. Prints a PASS/FAIL line per step and exits non-zero
if anything failed. Uses ccache when installed (`--no-ccache` to disable).

Packages (Debian/Ubuntu):

```sh
sudo apt-get install -y cmake ninja-build clang python3-venv ccache \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev
```

Windows and macOS jobs are not mirrored; use the `win-msvc` / `mac-clang-debug` presets directly.
