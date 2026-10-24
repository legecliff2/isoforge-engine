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
#   -j N, --parallel N         fixed build/test parallelism (wins over CI_LOCAL_JOBS and memory)
#   -h, --help
#
# Parallelism, unless -j or CI_LOCAL_JOBS is set: floor(MemAvailable / CI_LOCAL_MB_PER_JOB)
# (default 1536 MB per job), clamped to [1, nproc]. Read again before each build and each ctest.
# CI_LOCAL_JOBS=N wins over that estimate. -j / --parallel wins over CI_LOCAL_JOBS.
#
# Heavy build/test steps take an exclusive flock on /tmp/ci_local.lock (CI_LOCAL_LOCK to move it,
# CI_LOCAL_NO_LOCK=1 to skip). The lock is held until this process exits. A nested invocation that
# inherits CI_LOCAL_LOCK_HELD does not lock again. Without /proc/meminfo the script uses sysctl
# (hw.memsize / vm.page_free_count, hw.ncpu) or nproc. Without flock it skips the lock and says so.
#
# Exit status: 0 if every step of every selected job passed, 1 otherwise, 2 on bad usage.
:

# Positive integer, base 10 (rejects 0, empty, and non-digits). Prints the normalized value.
ci_local_normalize_posint() {
  local v=$1
  case "$v" in
    ''|*[!0-9]*) return 1 ;;
  esac
  v=$((10#$v))
  if [ "$v" -lt 1 ]; then
    return 1
  fi
  printf '%s\n' "$v"
}

ci_local_is_uint() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
  esac
  return 0
}

# CPU count: nproc, else sysctl hw.ncpu, else 1.
ci_local_nproc() {
  local n=""
  if command -v nproc >/dev/null 2>&1; then
    n=$(nproc 2>/dev/null || true)
  fi
  n=${n//[[:space:]]/}
  if ! ci_local_is_uint "$n" || [ "$n" -lt 1 ]; then
    n=""
    if command -v sysctl >/dev/null 2>&1; then
      n=$(sysctl -n hw.ncpu 2>/dev/null || true)
      n=${n//[[:space:]]/}
    fi
  fi
  if ! ci_local_is_uint "$n" || [ "$n" -lt 1 ]; then
    n=1
  fi
  printf '%s\n' "$((10#$n))"
}

# floor(avail_kb / (mb_per_job * 1024)), clamped to [1, ncpu].
ci_local_jobs_from_mem() {
  local avail_kb=$1 mb=$2 ncpu=$3 jobs
  avail_kb=$((10#$avail_kb))
  mb=$((10#$mb))
  ncpu=$((10#$ncpu))
  jobs=$((avail_kb / (mb * 1024)))
  if [ "$jobs" -lt 1 ]; then
    jobs=1
  fi
  if [ "$jobs" -gt "$ncpu" ]; then
    jobs=$ncpu
  fi
  printf '%s\n' "$jobs"
}

# Print "<kb>\t<source>" or return 1 when no memory figure is available.
# CI_LOCAL_MEMINFO overrides the meminfo path (default /proc/meminfo).
ci_local_mem_kb() {
  local path kb pagesize free_pages bytes
  path=${CI_LOCAL_MEMINFO:-/proc/meminfo}
  if [ -r "$path" ]; then
    kb=$(awk '/^MemAvailable:/ { print $2; exit }' "$path" 2>/dev/null || true)
    kb=${kb//[[:space:]]/}
    if ci_local_is_uint "$kb"; then
      printf '%s\t%s\n' "$kb" "MemAvailable"
      return 0
    fi
    kb=$(awk '
      /^MemFree:/ { f = $2 }
      /^Buffers:/ { b = $2 }
      /^Cached:/ { c = $2 }
      END {
        sum = f + b + c
        if (sum > 0) print sum
      }
    ' "$path" 2>/dev/null || true)
    kb=${kb//[[:space:]]/}
    if ci_local_is_uint "$kb"; then
      printf '%s\t%s\n' "$kb" "MemFree+Buffers+Cached"
      return 0
    fi
  fi
  if command -v sysctl >/dev/null 2>&1; then
    pagesize=$(sysctl -n hw.pagesize 2>/dev/null || true)
    free_pages=$(sysctl -n vm.page_free_count 2>/dev/null || true)
    pagesize=${pagesize//[[:space:]]/}
    free_pages=${free_pages//[[:space:]]/}
    if ci_local_is_uint "$pagesize" && ci_local_is_uint "$free_pages" && [ "$pagesize" -gt 0 ]; then
      kb=$((10#$free_pages * 10#$pagesize / 1024))
      printf '%s\t%s\n' "$kb" "sysctl vm.page_free_count"
      return 0
    fi
    bytes=$(sysctl -n hw.memsize 2>/dev/null || true)
    bytes=${bytes//[[:space:]]/}
    if ci_local_is_uint "$bytes" && [ "$bytes" -ge 1024 ]; then
      kb=$((10#$bytes / 1024))
      printf '%s\t%s\n' "$kb" "sysctl hw.memsize"
      return 0
    fi
  fi
  return 1
}

# Print "<jobs>\t<reason>". Optional arg 1 is a command-line -j, which wins.
# CI_LOCAL_JOBS wins over the memory estimate. Returns 2 on a bad override.
ci_local_resolve_jobs() {
  local fixed=${1:-} ncpu mb line kb source jobs reason forced
  if [ -n "$fixed" ]; then
    if ! fixed=$(ci_local_normalize_posint "$fixed"); then
      echo "ci_local: -j must be a positive integer" >&2
      return 2
    fi
    printf '%s\t%s\n' "$fixed" "command-line -j ${fixed}"
    return 0
  fi
  if [ -n "${CI_LOCAL_JOBS:-}" ]; then
    if ! forced=$(ci_local_normalize_posint "$CI_LOCAL_JOBS"); then
      echo "ci_local: CI_LOCAL_JOBS must be a positive integer (got '${CI_LOCAL_JOBS}')" >&2
      return 2
    fi
    printf '%s\t%s\n' "$forced" "CI_LOCAL_JOBS=${forced}"
    return 0
  fi
  mb=${CI_LOCAL_MB_PER_JOB:-1536}
  if ! mb=$(ci_local_normalize_posint "$mb"); then
    echo "ci_local: CI_LOCAL_MB_PER_JOB must be a positive integer (got '${CI_LOCAL_MB_PER_JOB:-}')" >&2
    return 2
  fi
  ncpu=$(ci_local_nproc)
  if line=$(ci_local_mem_kb); then
    kb=${line%%$'\t'*}
    source=${line#*$'\t'}
    jobs=$(ci_local_jobs_from_mem "$kb" "$mb" "$ncpu")
    reason="${source}=${kb}kB / ${mb}MB, clamp [1, nproc=${ncpu}]"
  else
    jobs=$ncpu
    reason="no memory info; nproc=${ncpu}"
  fi
  printf '%s\t%s\n' "$jobs" "$reason"
}

# Set global `par` and log it. With no argument, this is the startup line.
# `build` and `test` are the per-step re-checks (memory is read again each call).
ci_local_use_jobs() {
  local where=${1:-} line="" jobs="" reason=""
  if [ -n "${jobs_fixed:-}" ]; then
    line=$(ci_local_resolve_jobs "$jobs_fixed") || return $?
  else
    line=$(ci_local_resolve_jobs) || return $?
  fi
  jobs=${line%%$'\t'*}
  reason=${line#*$'\t'}
  if ! ci_local_is_uint "$jobs" || [ "$jobs" -lt 1 ]; then
    echo "ci_local: internal error resolving -j ('${line}')" >&2
    return 2
  fi
  par=$jobs
  if [ -n "$where" ]; then
    printf 'ci_local: -j %s for %s (%s)\n' "$jobs" "$where" "$reason"
  else
    printf 'ci_local: chosen -j %s (%s)\n' "$jobs" "$reason"
  fi
}

ci_local_lock_holder() {
  local lock=$1 pid="" inode=""
  pid=$(awk 'NR == 1 && $1 ~ /^[0-9]+$/ { print $1; exit }' "$lock" 2>/dev/null || true)
  if [ -n "$pid" ]; then
    printf '%s\n' "$pid"
    return 0
  fi
  if [ -r /proc/locks ]; then
    inode=$(stat -c '%i' "$lock" 2>/dev/null || true)
    if [ -n "$inode" ]; then
      pid=$(awk -v ino="$inode" '
        $2 == "FLOCK" {
          n = split($6, parts, ":")
          if (n >= 1 && parts[n] == ino) { print $5; exit }
        }
      ' /proc/locks 2>/dev/null || true)
    fi
  fi
  if [ -n "$pid" ]; then
    printf '%s\n' "$pid"
  fi
}

# Drop the lock if this process took it. Safe to call twice, and a nested process
# (which did not take the lock itself) must not unlock the parent.
ci_local_release_lock() {
  local lock
  if [ "${_ci_local_we_locked:-0}" != 1 ]; then
    return 0
  fi
  lock=${CI_LOCAL_LOCK:-/tmp/ci_local.lock}
  if [ "${_ci_local_lock_owned:-0}" = 1 ]; then
    : >"$lock" 2>/dev/null || true
    flock -u 9 2>/dev/null || true
    _ci_local_lock_owned=0
  fi
  exec 9>&- 2>/dev/null || true
  _ci_local_we_locked=0
  unset CI_LOCAL_LOCK_HELD
  unset CI_LOCAL_LOCK_OWNER
}

ci_local_install_traps() {
  trap 'ci_local_release_lock' EXIT
  trap 'ci_local_release_lock; trap - INT; kill -s INT $$' INT
  trap 'ci_local_release_lock; trap - TERM; kill -s TERM $$' TERM
  trap 'ci_local_release_lock; trap - HUP; kill -s HUP $$' HUP
}

# Exclusive flock around later build/test work. Re-entrant in this process, and nested
# children that inherit CI_LOCAL_LOCK_HELD return without locking (they would deadlock
# on the parent's fd otherwise).
ci_local_acquire_lock() {
  local lock dir holder
  if [ "${CI_LOCAL_NO_LOCK:-}" = 1 ]; then
    if [ "${_ci_local_nolock_noted:-0}" != 1 ]; then
      echo "ci_local: shared lock disabled (CI_LOCAL_NO_LOCK=1)"
      _ci_local_nolock_noted=1
    fi
    return 0
  fi
  if [ "${CI_LOCAL_LOCK_HELD:-}" = 1 ]; then
    if [ "${CI_LOCAL_LOCK_OWNER:-}" != "$$" ]; then
      echo "ci_local: nested call; lock already held by PID ${CI_LOCAL_LOCK_OWNER:-unknown}"
    fi
    return 0
  fi
  if ! command -v flock >/dev/null 2>&1; then
    if [ "${_ci_local_flock_missing:-0}" != 1 ]; then
      echo "ci_local: flock not found; skipping shared build lock"
      _ci_local_flock_missing=1
    fi
    return 0
  fi
  lock=${CI_LOCAL_LOCK:-/tmp/ci_local.lock}
  dir=$(dirname -- "$lock")
  if [ ! -d "$dir" ]; then
    mkdir -p -- "$dir" 2>/dev/null || true
  fi
  # <> does not truncate, so a waiter can still read the holder's PID.
  if ! exec 9<>"$lock"; then
    echo "ci_local: cannot open ${lock}; skipping shared build lock"
    return 0
  fi
  _ci_local_we_locked=1
  if ! flock -n 9; then
    holder=$(ci_local_lock_holder "$lock" || true)
    if [ -n "$holder" ]; then
      echo "ci_local: waiting for lock (held by PID ${holder})"
    else
      echo "ci_local: waiting for lock"
    fi
    if ! flock 9; then
      echo "ci_local: flock failed; skipping shared build lock"
      _ci_local_we_locked=0
      exec 9>&- || true
      return 0
    fi
  fi
  _ci_local_lock_owned=1
  printf '%s\n' "$$" >"$lock" || true
  export CI_LOCAL_LOCK_HELD=1
  export CI_LOCAL_LOCK_OWNER=$$
  ci_local_install_traps
}

ci_local_print_help() {
  awk 'NR > 1 && /^[^#]/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
}

ci_local_parse_args() {
  job=all
  fresh=0
  full_tests=0
  use_ccache=1
  jobs_fixed=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --job)
        if [ $# -lt 2 ]; then
          echo "--job needs a value" >&2
          exit 2
        fi
        job=$2
        shift 2
        ;;
      --job=*) job=${1#*=}; shift ;;
      --fresh) fresh=1; shift ;;
      --full-tests) full_tests=1; shift ;;
      --no-ccache) use_ccache=0; shift ;;
      -j|--parallel)
        if [ $# -lt 2 ]; then
          echo "-j needs a positive integer" >&2
          exit 2
        fi
        if ! jobs_fixed=$(ci_local_normalize_posint "$2"); then
          echo "-j needs a positive integer" >&2
          exit 2
        fi
        shift 2
        ;;
      -h|--help) ci_local_print_help; exit 0 ;;
      *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
  done
  case "$job" in
    gcc|clang|lint|all) ;;
    *) echo "--job must be gcc, clang, lint or all" >&2; exit 2 ;;
  esac
}

# step <job> <name> <cmd...>: run one step, record PASS/FAIL with its duration.
ci_local_step() {
  local j=$1 name=$2
  shift 2
  echo
  echo "${B}==> [$j] $name${N}"
  echo "    \$ $*"
  local t0=$SECONDS
  if "$@"; then
    summary+=("${G}PASS${N}  [$j] $name ($((SECONDS - t0))s)")
    return 0
  else
    summary+=("${R}FAIL${N}  [$j] $name ($((SECONDS - t0))s)")
    failed=1
    return 1
  fi
}

ci_local_run_job() { # ci_local_run_job <label> <preset> <lint_only> <extra ctest args...>
  local j=$1 preset=$2 lint_only=$3
  shift 3
  local py
  py=$(command -v python3)
  if [ "$fresh" = 1 ]; then
    rm -rf "build/$preset"
  fi
  ci_local_step "$j" "Configure" cmake --preset "$preset" -DPython3_EXECUTABLE="$py" "${launcher[@]}" || return 1
  ci_local_step "$j" "Lint (depcheck + include_lint)" \
    ctest --preset "$preset" -R "^(depcheck|include_lint)" --no-tests=error || return 1
  if [ "$lint_only" = 1 ]; then
    return 0
  fi
  # One shared queue for every heavy phase in this process. Taken after configure/lint so a
  # lint-only run does not block builders. Held until exit (including a later compiler job).
  ci_local_acquire_lock
  ci_local_use_jobs build || exit 2
  ci_local_step "$j" "Build" cmake --build --preset "$preset" --parallel "$par" || return 1
  ci_local_step "$j" "Require content_validate to be registered" \
    bash -c "ctest --preset '$preset' -N | grep -q content_validate" || return 1
  ci_local_use_jobs test || exit 2
  ci_local_step "$j" "Test" ctest --preset "$preset" --parallel "$par" "$@" || return 1
}

ci_local_main() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2
  ci_local_parse_args "$@"
  ci_local_install_traps

  if [ -t 1 ]; then
    G=$'\e[32m'; R=$'\e[31m'; B=$'\e[1m'; N=$'\e[0m'
  else
    G=; R=; B=; N=
  fi
  summary=()
  failed=0
  par=""
  launcher=()

  ci_local_use_jobs || exit 2

  if [ "$use_ccache" = 1 ] && command -v ccache >/dev/null; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
    export CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-pch_defines,time_macros,include_file_mtime,include_file_ctime}"
  fi

  # Prerequisites CI gets from the runner image + the apt step.
  local missing=() t
  for t in cmake ninja ctest python3 gcc g++ clang clang++; do
    command -v "$t" >/dev/null || missing+=("$t")
  done
  if [ ${#missing[@]} -gt 0 ]; then
    echo "${R}missing tools: ${missing[*]}${N}"
    echo "Debian/Ubuntu: sudo apt-get install -y cmake ninja-build clang python3-venv ccache libstdc++-14-dev \\"
    echo "  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \\"
    echo "  libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev"
    exit 1
  fi

  # Clang selects the newest installed GCC for libstdc++. On Ubuntu cloud images
  # that is GCC 14, while the default g++ is GCC 13. Without libstdc++-14-dev the
  # Clang job configures, then fails to link with "cannot find -lstdc++".
  local link_err probe
  link_err=$(mktemp)
  probe=$(mktemp)
  if ! printf 'int main(){return 0;}\n' | clang++ -x c++ - -o "$probe" >"$link_err" 2>&1; then
    echo "${R}clang++ cannot link libstdc++${N}"
    if grep -q 'cannot find -lstdc++' "$link_err"; then
      echo "Clang selected a GCC whose libstdc++ dev package is not installed."
    else
      sed 's/^/    /' "$link_err"
    fi
    echo "Debian/Ubuntu: sudo apt-get install -y libstdc++-14-dev"
    rm -f "$probe" "$link_err"
    exit 1
  fi
  rm -f "$probe" "$link_err"

  local t_all=$SECONDS
  case "$job" in
    gcc|all) ci_local_run_job "Linux GCC Debug" ci-linux-gcc 0 ;;&
    clang|all)
      if [ "$full_tests" = 1 ]; then
        ci_local_run_job "Linux Clang Release" ci-linux-clang 0
      else
        ci_local_run_job "Linux Clang Release" ci-linux-clang 0 -LE python_only
      fi
      ;;
    lint)
      ci_local_run_job "lint gcc" ci-linux-gcc 1
      ci_local_run_job "lint clang" ci-linux-clang 1
      ;;
  esac

  echo
  echo "${B}==== ci_local.sh summary ($((SECONDS - t_all))s) ====${N}"
  printf '%s\n' "${summary[@]}"
  if [ "$failed" = 0 ]; then
    echo "${G}${B}ALL PASSED${N}"
    exit 0
  fi
  echo "${R}${B}FAILED${N}"
  exit 1
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -uo pipefail
  ci_local_main "$@"
fi
