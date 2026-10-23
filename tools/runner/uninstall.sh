#!/usr/bin/env bash
# Stop and remove the runner service; deregister if RUNNER_TOKEN (a *removal* token) is set.
#   sudo [RUNNER_TOKEN=<removal token>] tools/runner/uninstall.sh
set -euo pipefail
RUNNER_DIR=${RUNNER_DIR:-/opt/actions-runner}; RUNNER_USER=${RUNNER_USER:-ghrunner}; SVC=actions-runner-isoforge
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
if [ -d /run/systemd/system ]; then
  systemctl disable --now $SVC 2>/dev/null || true
  rm -f /etc/systemd/system/$SVC.service; systemctl daemon-reload
else
  pkill -u "$RUNNER_USER" -f "$RUNNER_DIR/run" || true; pkill -u "$RUNNER_USER" -f Runner.Listener || true
fi
if [ -n "${RUNNER_TOKEN:-}" ] && [ -f "$RUNNER_DIR/.runner" ]; then
  ( cd "$RUNNER_DIR" && sudo -u "$RUNNER_USER" --preserve-env=RUNNER_TOKEN bash -c './config.sh remove --token "$RUNNER_TOKEN"' >/dev/null )
else
  echo "not deregistered on GitHub (no RUNNER_TOKEN); remove it under Settings > Actions > Runners"
fi
[ "${KEEP_FILES:-0}" = 1 ] || rm -rf "$RUNNER_DIR"
echo "uninstalled"
