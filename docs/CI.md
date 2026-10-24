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

Pull-request Linux jobs (GCC Debug, Clang Release, and any other Linux row) run on
`ubuntu-latest`. The box (`[self-hosted, linux, isoforge]`) is eligible only when
`github.ref == 'refs/heads/main'` and the event is `push`, `schedule`, or
`workflow_dispatch`. The `runs-on` expression forces that even if the picker outputs the
box labels. Windows and macOS stay GitHub-hosted. Linux and Windows Debug still run on
every ready same-repo PR. `changes` and `ci-required` stay on `ubuntu-latest`.

`CI_BURST` (top-level `env` in `ci.yml`) still chooses what a trusted event does:

- `'true'` (until the revert in #299): pull requests are hosted and do not dispatch a
  canary. A push to `main` may place Linux GCC Debug on the box when the canary accepts
  it; Clang Release and the nightly gate stay on `ubuntu-latest`. Hosted caps are compile
  4 / test 8. Box caps stay 4 / 2.
- `'false'`: a push to `main`, the nightly schedule, and a manual dispatch on `main`
  prefer the box and fall back to `ubuntu-latest` for that same run when the canary is
  not accepted (#281). Pull requests stay on `ubuntu-latest` (compile 4 / test 4) and do
  not dispatch a canary. One pick per trusted run: those jobs are never started on both
  runners.

The box pauses when no agent is active, so a job queued only on the self-hosted runner sits
until GitHub abandons it. `linux-runner` (a short `ubuntu-latest` job) chooses the runner
before any Linux build starts. On a trusted event, listing runners needs repository
`administration: read`, which `GITHUB_TOKEN` cannot be granted, so a 403 is expected. The
job then dispatches `.github/workflows/linux-runner-canary.yml` on the **default branch**
(no checkout, no PR code). If the box accepts the canary, Linux stays on the box; if it is
still queued when the probe window ends, the canary is cancelled and Linux uses
`ubuntu-latest`. A `pull_request` run returns hosted immediately and does not dispatch the
canary.

That does not bind a fork that edits `ci.yml` or adds a workflow. GitHub runs the workflow
file from the pull request, so the fork's `runs-on` is what gets submitted. The control
that stops the box accepting that job is an organization runner group pinned to
`refs/heads/main`. Steps: [CI_SELF_HOSTED.md](CI_SELF_HOSTED.md).

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
  Jobs on the box use `CMAKE_BUILD_PARALLEL_LEVEL=4` and `CTEST_PARALLEL_LEVEL=2`. The hosted
  fallback keeps the build cap at 4 and uses `CTEST_PARALLEL_LEVEL=4` (`8` while `CI_BURST` is
  `'true'`). On hosts without systemd
  the script falls back to `nice -n10 ionice -c3 taskset` (4 CPUs) with `oom_score_adj=500`. That fallback has no hard memory cap: `ulimit -v` breaks the .NET runner, and MemoryMax needs cgroups. This is how the current box (no systemd) runs it; re-run `setup.sh` after a reboot.
- Build deps (ninja, ccache, X11/Wayland dev headers, **Python >= 3.12** via distro `python3` /
  `python3-venv`, or `python3.12` from deadsnakes on older Ubuntu) are installed by `setup.sh`.
  cmake-ci skips `apt-get` and `actions/setup-python` on self-hosted runners (no Python toolcache
  there — setup-python fails with "The version 3.12 with architecture x64 was not found") and uses
  system `python3` / `python3.12` for the `content_validate` venv. It fails fast if ninja or a
  usable Python is missing. GitHub-hosted jobs still use `actions/setup-python@v5` with 3.12.
- **Security:** [CI_SELF_HOSTED.md](CI_SELF_HOSTED.md). Do not add `pull_request_target`.
  The canary workflow is `workflow_dispatch` only, runs only when `github.ref` is
  `refs/heads/main`, and does not check out code. Skipping `changes` on a fork PR is not
  what keeps a fork that edits `ci.yml` off the box.

## Nightly macOS gate

The scheduled run (`17 7 * * *`) starts with `changes`, then `linux-runner`, then
`nightly-gate` (`actions: read`). With `CI_BURST` false and `github.ref` `refs/heads/main`,
the gate runs on the box when the canary is accepted, and on `ubuntu-latest` when it is not.
With `CI_BURST` true the gate stays on `ubuntu-latest`. It queries
`GET /repos/{repo}/actions/workflows/ci.yml/runs?branch=main&event=schedule&status=success&per_page=1`
and skips macOS when that run's `head_sha` equals the current main SHA (main hasn't changed since the
last successful nightly). `workflow_dispatch` doesn't depend on the gate and always runs macOS.
