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

To run Windows Release on a PR, add the `ci-windows-release` label and then push a commit (or close
and reopen the PR). Adding a label alone does not trigger CI, and "Re-run jobs" replays the original
event payload, so it does not see a label added afterwards.

`python3 tools/ci_conditions_test.py` evaluates the real `if:` conditions and the ci-required gate for
PR (with/without the label, docs-only, mac paths), push, schedule and dispatch.

Minutes savers: ccache (Linux/macOS) / sccache (Windows, with `/Z7` embedded debug info) cached with
`actions/cache` per OS + preset, keyed on `cmake/Dependencies.cmake` (the `ci` presets set `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` because SDL3's precompiled header defeats ccache with Clang and sccache with MSVC `/Yu`, and `CMAKE_CXX_SCAN_FOR_MODULES=OFF` because GCC's `-fmodules-ts` makes every C++ object uncacheable; the project uses no C++ modules); `ctest --parallel 4`; the
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

Packages (Debian/Ubuntu). `libstdc++-14-dev` is required because Clang selects GCC 14's
libstdc++ even when the default `g++` is GCC 13. Without it the Clang job fails at link time
with `cannot find -lstdc++` (GCC 14 headers and crt files can be present while the library is not):

```sh
sudo apt-get install -y cmake ninja-build clang python3-venv ccache libstdc++-14-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev
```

Windows and macOS jobs are not mirrored; use the `win-msvc` / `mac-clang-debug` presets directly.

## Workflow for PRs (minimising CI minutes)

- **Run `tools/ci_local.sh` before marking a PR ready.** It mirrors both Linux jobs locally.
- **Draft PRs don't run CI.** The `pull_request` trigger only fires on `opened`, `synchronize`,
  `reopened` and `ready_for_review`, and the `changes` job (which every other job needs) is skipped
  when `github.event.pull_request.draft` is true. Opening a PR as a draft and marking it ready starts
  CI once. `ci-required` passes on a draft (`changes` skipped on purpose); on any non-draft run a
  skipped `changes` fails the gate.
- **Stacked chains:** keep the lower PRs in draft or don't push them separately; run CI once on the
  top PR (it contains the whole stack), then merge the chain in order, bottom first.
- **Concurrency:** one group per workflow + PR number (or ref). A new push cancels the PR's
  in-progress run. Runs on `main` (push, schedule, dispatch) get a group per sha + event and are never
  cancelled.

## Linux runners

Linux GCC Debug, Linux Clang Release, and the nightly gate run on `ubuntu-latest` for
every event, including push to `main`, `schedule`, and `workflow_dispatch`. Windows and
macOS stay GitHub-hosted. `changes` and `ci-required` stay on `ubuntu-latest`.

`linux-runner` does not pick a machine. It sets Linux caps: compile 4, and test 8 when
`CI_BURST` is `'true'` or test 4 when it is `'false'` (#299). Windows keeps compile 4 /
test 4.

The old box is not part of CI. Unregister it with `tools/runner/uninstall.sh` right
before the repository is public, and set fork-PR approval to **Require approval for
all external contributors** once it is. Details: [CI_SELF_HOSTED.md](CI_SELF_HOSTED.md).
`tools/runner/` stays in the tree; the workflows do not call it. GitHub-hosted jobs
use `actions/setup-python@v5` with Python 3.12. The dev packages they install are
listed in `.github/actions/cmake-ci/action.yml`.

## Nightly macOS gate

The scheduled run (`17 7 * * *`) starts with `changes`, then `linux-runner` (hosted
caps only), then `nightly-gate` on `ubuntu-latest` (`actions: read`). It queries
`GET /repos/{repo}/actions/workflows/ci.yml/runs?branch=main&event=schedule&status=success&per_page=1`
and skips macOS when that run's `head_sha` equals the current main SHA (main hasn't changed since the
last successful nightly). `workflow_dispatch` doesn't depend on the gate and always runs macOS.
