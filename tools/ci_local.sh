#!/usr/bin/env bash
# Run the Linux half of .github/workflows/ci.yml locally (same presets, same steps, same order
# as .github/actions/cmake-ci). See docs/CI.md.
#
#   tools/ci_local.sh                 # both Linux jobs (gcc then clang)
#   tools/ci_local.sh --job gcc       # Linux GCC Debug    (preset ci-linux-gcc)
#   tools/ci_local.sh --job clang     # Linux Clang Release (preset ci-linux-clang)
#   tools/ci_local.sh --job lint      # configure + depcheck/include_lint only (both presets, fast)
#
# Options:
#   --job gcc|clang|lint|all   which CI job(s) to mirror (default: all)
#   --full-tests               Clang Release also runs the python_only tests (CI skips them there)
#   --fresh                    delete build/<preset> first (CI always starts clean)
#   --no-ccache                don't use ccache even if installed (CI uses it)
#   -j N, --parallel N         build/test parallelism (default: nproc)
#   -h, --help
#
# Exit status: 0 if every step of every selected job passed, 1 otherwise, 2 on bad usage.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 2

job=all; fresh=0; full_tests=0; use_ccache=1; par="$(nproc 2>/dev/null || echo 4)"
while [ $# -gt 0 ]; do
  case "$1" in
    --job) job="${2:-}"; shift 2 ;;
    --job=*) job="${1#*=}"; shift ;;
    --fresh) fresh=1; shift ;;
    --full-tests) full_tests=1; shift ;;
    --no-ccache) use_ccache=0; shift ;;
    -j|--parallel) par="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
  esac
done
case "$job" in gcc|clang|lint|all) ;; *) echo "--job must be gcc, clang, lint or all" >&2; exit 2 ;; esac

if [ -t 1 ]; then G=$'\e[32m'; R=$'\e[31m'; B=$'\e[1m'; N=$'\e[0m'; else G=; R=; B=; N=; fi
summary=(); failed=0

# step <job> <name> <cmd...>: run one step, record PASS/FAIL with its duration.
step() {
  local j="$1" name="$2"; shift 2
  echo; echo "${B}==> [$j] $name${N}"; echo "    \$ $*"
  local t0=$SECONDS
  if "$@"; then
    summary+=("${G}PASS${N}  [$j] $name ($((SECONDS - t0))s)"); return 0
  else
    summary+=("${R}FAIL${N}  [$j] $name ($((SECONDS - t0))s)"); failed=1; return 1
  fi
}

launcher=()
if [ "$use_ccache" = 1 ] && command -v ccache >/dev/null; then
  launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  export CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-pch_defines,time_macros,include_file_mtime,include_file_ctime}"  # same as CI
fi

# Prerequisites CI gets from the runner image + the apt step.
missing=()
for t in cmake ninja ctest python3 gcc g++ clang clang++; do command -v "$t" >/dev/null || missing+=("$t"); done
if [ ${#missing[@]} -gt 0 ]; then
  echo "${R}missing tools: ${missing[*]}${N}"
  echo "Debian/Ubuntu: sudo apt-get install -y cmake ninja-build clang python3-venv ccache \\"
  echo "  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \\"
  echo "  libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev"
  exit 1
fi

run_job() {  # run_job <label> <preset> <lint_only> <extra ctest args...>
  local j="$1" preset="$2" lint_only="$3"; shift 3
  local py; py="$(command -v python3)"
  [ "$fresh" = 1 ] && rm -rf "build/$preset"
  step "$j" "Configure" cmake --preset "$preset" -DPython3_EXECUTABLE="$py" "${launcher[@]}" || return 1
  step "$j" "Lint (depcheck + include_lint)" \
       ctest --preset "$preset" -R "^(depcheck|include_lint)" --no-tests=error || return 1
  [ "$lint_only" = 1 ] && return 0
  step "$j" "Build" cmake --build --preset "$preset" --parallel "$par" || return 1
  step "$j" "Require content_validate to be registered" \
       bash -c "ctest --preset '$preset' -N | grep -q content_validate" || return 1
  step "$j" "Test" ctest --preset "$preset" --parallel "$par" "$@" || return 1
}

t_all=$SECONDS
case "$job" in
  gcc|all)   run_job "Linux GCC Debug" ci-linux-gcc 0 ;;&
  clang|all)
    if [ "$full_tests" = 1 ]; then run_job "Linux Clang Release" ci-linux-clang 0
    else run_job "Linux Clang Release" ci-linux-clang 0 -LE python_only; fi ;;
  lint)
    run_job "lint gcc" ci-linux-gcc 1
    run_job "lint clang" ci-linux-clang 1 ;;
esac

echo; echo "${B}==== ci_local.sh summary ($((SECONDS - t_all))s) ====${N}"
printf '%s\n' "${summary[@]}"
if [ "$failed" = 0 ]; then echo "${G}${B}ALL PASSED${N}"; exit 0; else echo "${R}${B}FAILED${N}"; exit 1; fi
