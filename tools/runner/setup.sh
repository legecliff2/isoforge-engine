#!/usr/bin/env bash
# Idempotent install of one GitHub Actions self-hosted runner for isoforge-engine.
#   sudo RUNNER_TOKEN=<registration token> tools/runner/setup.sh
# Env: RUNNER_TOKEN (required unless already configured), REPO_URL, RUNNER_GROUP (optional),
#      RUNNER_DIR (/opt/actions-runner), RUNNER_USER (ghrunner), RUNNER_NAME (isoforge-<host>),
#      RUNNER_VERSION, CPU_QUOTA (400%), MEM_MAX (8G).
# Non-ephemeral on purpose: one persistent runner keeps its ccache warm (that is the point of
# self-hosting), and an ephemeral runner would need a fresh registration token per job (an
# auto-re-register loop holding a PAT). Workspace hygiene is done by checkout clean + the
# cleanup step in cmake-ci. A repository-level runner cannot limit which workflow ref is
# allowed to use it. Before this repo is public, register it on an organization runner
# group pinned to main (docs/CI_SELF_HOSTED.md). Optional: REPO_URL=https://github.com/ORG
# and RUNNER_GROUP=<group> (the group must already exist; config.sh --runnergroup).
set -euo pipefail
REPO_URL=${REPO_URL:-https://github.com/legecliff2/isoforge-engine}
RUNNER_DIR=${RUNNER_DIR:-/opt/actions-runner}
RUNNER_USER=${RUNNER_USER:-ghrunner}
RUNNER_NAME=${RUNNER_NAME:-isoforge-$(hostname -s)}
RUNNER_VERSION=${RUNNER_VERSION:-$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest | sed -n 's/.*"tag_name": *"v\([^"]*\)".*/\1/p')}
CPU_QUOTA=${CPU_QUOTA:-400%}
MEM_MAX=${MEM_MAX:-8G}
LABELS=self-hosted,linux,x64,isoforge
SVC=actions-runner-isoforge
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }

# Build deps (cmake-ci skips apt on self-hosted). Python >= 3.12 is required for content_validate:
# cmake-ci skips actions/setup-python on self-hosted (no toolcache) and uses system python3.
if command -v apt-get >/dev/null; then
  DEBIAN_FRONTEND=noninteractive apt-get install -y -q ninja-build ccache cmake g++ clang git curl python3-venv \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev >/dev/null
fi
# Ensure python3 is >= 3.12. Distro python3 is enough on Debian 13 / Ubuntu 24.04+; older Ubuntu
# gets python3.12 from deadsnakes (python3 may still be older — cmake-ci prefers python3.12).
py_ok() { command -v "$1" >/dev/null && "$1" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 12) else 1)' 2>/dev/null; }
if ! py_ok python3 && ! py_ok python3.12; then
  if ! command -v apt-get >/dev/null; then
    echo "need python3 >= 3.12 (or python3.12) for content_validate; found $(python3 --version 2>&1 || echo none)"
    exit 1
  fi
  echo "installing Python 3.12 (need >= 3.12 for content_validate; found $(python3 --version 2>&1 || echo none))"
  DEBIAN_FRONTEND=noninteractive apt-get install -y -q software-properties-common >/dev/null || true
  if command -v add-apt-repository >/dev/null && add-apt-repository -y ppa:deadsnakes/ppa; then
    DEBIAN_FRONTEND=noninteractive apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q python3.12 python3.12-venv >/dev/null
  else
    # Debian / no PPA: distro python3 or python3.12 from main if packaged.
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q python3 python3-venv >/dev/null || true
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q python3.12 python3.12-venv >/dev/null || true
  fi
fi
if ! py_ok python3 && ! py_ok python3.12; then
  echo "need python3 >= 3.12 (or python3.12) for content_validate; found $(python3 --version 2>&1 || echo none)"
  exit 1
fi
if py_ok python3.12; then echo "python: $(command -v python3.12) ($(python3.12 --version))"
else echo "python: $(command -v python3) ($(python3 --version))"; fi
id "$RUNNER_USER" >/dev/null 2>&1 || useradd --system --create-home --shell /usr/sbin/nologin "$RUNNER_USER"
install -d -o "$RUNNER_USER" -g "$RUNNER_USER" "$RUNNER_DIR"
if [ ! -x "$RUNNER_DIR/config.sh" ]; then
  tgz=actions-runner-linux-x64-$RUNNER_VERSION.tar.gz
  curl -fsSL -o "/tmp/$tgz" "https://github.com/actions/runner/releases/download/v$RUNNER_VERSION/$tgz"
  sudo -u "$RUNNER_USER" tar -xzf "/tmp/$tgz" -C "$RUNNER_DIR"; rm -f "/tmp/$tgz"
  "$RUNNER_DIR/bin/installdependencies.sh" >/dev/null || true
fi
if [ ! -f "$RUNNER_DIR/.runner" ]; then
  : "${RUNNER_TOKEN:?set RUNNER_TOKEN (registration token) in the environment}"
  # $3 is RUNNER_GROUP. Empty means config.sh's default group (Default on an org URL).
  ( cd "$RUNNER_DIR" && sudo -u "$RUNNER_USER" --preserve-env=RUNNER_TOKEN bash -c \
      'args=(--unattended --url "$0" --token "$RUNNER_TOKEN" --name "$1" --labels "$2" --work _work --replace); if [ -n "$3" ]; then args+=(--runnergroup "$3"); fi; ./config.sh "${args[@]}"' \
      "$REPO_URL" "$RUNNER_NAME" "$LABELS" "${RUNNER_GROUP:-}" >/dev/null )
  echo "registered $RUNNER_NAME"
else
  echo "already configured: $(sed -n 's/.*"agentName": *"\([^"]*\)".*/\1/p' "$RUNNER_DIR/.runner" | tr -d '\0')"
fi
if ! [ -d /run/systemd/system ]; then
  # Container without systemd (e.g. tini as PID 1): same limits approximated with nice/ionice,
  # taskset (CPU_QUOTA as a CPU count) and a raised oom_score_adj so the runner is killed first
  # under memory pressure (ulimit -v breaks the .NET runner's GC reservation, and MemoryMax needs
  # cgroups/systemd). Re-run after reboot.
  ncpu=$(( ${CPU_QUOTA%\%} / 100 )); [ "$ncpu" -ge 1 ] || ncpu=1
  if pgrep -u "$RUNNER_USER" -f "$RUNNER_DIR/bin/Runner.Listener" >/dev/null; then echo "runner already running"; exit 0; fi
  sudo -u "$RUNNER_USER" CMAKE_BUILD_PARALLEL_LEVEL=4 setsid bash -c \
    "echo 500 > /proc/self/oom_score_adj; cd '$RUNNER_DIR' && exec taskset -c 0-$((ncpu-1)) nice -n 10 ionice -c3 ./run.sh" \
    >>"$RUNNER_DIR/runner-service.log" 2>&1 < /dev/null &
  echo "started without systemd (nice 10, ionice idle, cpus 0-$((ncpu-1)), oom_score_adj 500; MemoryMax needs systemd)"
  exit 0
fi
cat > /etc/systemd/system/$SVC.service <<UNIT
[Unit]
Description=GitHub Actions runner ($RUNNER_NAME, isoforge-engine)
After=network-online.target
Wants=network-online.target
[Service]
User=$RUNNER_USER
WorkingDirectory=$RUNNER_DIR
ExecStart=$RUNNER_DIR/run.sh
Restart=always
RestartSec=10
KillMode=process
KillSignal=SIGTERM
TimeoutStopSec=5min
Nice=10
IOSchedulingClass=idle
CPUQuota=$CPU_QUOTA
MemoryMax=$MEM_MAX
TasksMax=4096
Environment=CMAKE_BUILD_PARALLEL_LEVEL=4
[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now $SVC
systemctl --no-pager --lines=0 status $SVC || true
