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

Packages (Debian/Ubuntu):

```sh
sudo apt-get install -y cmake ninja-build clang python3-venv ccache \
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

## Self-hosted Linux runner

All Linux jobs (`Linux GCC Debug`, `Linux Clang Release`) and the nightly gate run on
`runs-on: [self-hosted, linux, isoforge]`. Windows and macOS stay GitHub-hosted. Linux and
Windows Debug still run on every ready PR.

- Setup: `sudo RUNNER_TOKEN=<registration token> tools/runner/setup.sh` (idempotent; installs to
  `/opt/actions-runner` as the system user `ghrunner` with labels `self-hosted,linux,x64,isoforge`).
  Removal: `sudo tools/runner/uninstall.sh` (pass a removal token as `RUNNER_TOKEN` to deregister).
  Pass the token only through the environment, never as an argument or in a file.
- One runner, one job at a time, **not ephemeral**. Persistence keeps the local ccache warm
  (`~ghrunner/.cache/isoforge-ccache/<preset>`, 3 GB per preset, outside the workspace). An ephemeral
  runner would need a new registration token for every job.
  The workspace is cleaned between runs (`actions/checkout` clean plus a final `rm -rf build .ccache`).
  On self-hosted runs `actions/cache` is skipped, because the local ccache is faster.
- Limits: systemd unit with `Nice=10`, `IOSchedulingClass=idle`, `CPUQuota=400%`, `MemoryMax=8G`.
  Jobs use `CMAKE_BUILD_PARALLEL_LEVEL=4` and `CTEST_PARALLEL_LEVEL=2`. On hosts without systemd
  the script falls back to `nice -n10 ionice -c3 taskset` (4 CPUs) and `ulimit -v`.
- Build deps (ninja, ccache, X11/Wayland dev headers) are installed by `setup.sh`. cmake-ci skips
  `apt-get` on self-hosted runners and fails fast if ninja is missing.
- **Security:** the repo is private, so only collaborators can open PRs that run here. Fork PRs
  must never run on the self-hosted runner: `changes` is skipped when the PR head repo is not this
  repo, so no job reaches the runner and `ci-required` fails closed. Keep "Require approval for
  fork pull request workflows" enabled, and never add `pull_request_target` to this workflow.

## Nightly macOS gate

The scheduled run (`17 7 * * *`) starts with `nightly-gate` (on the self-hosted runner,
`actions: read`). It queries
`GET /repos/{repo}/actions/workflows/ci.yml/runs?branch=main&event=schedule&status=success&per_page=1`
and skips macOS when that run's `head_sha` equals the current main SHA (main hasn't changed since the
last successful nightly). `workflow_dispatch` doesn't depend on the gate and always runs macOS.
