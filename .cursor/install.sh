#!/usr/bin/env bash
# Idempotent Cloud Agent bootstrap.
#
# Clang on Ubuntu 24.04 selects the newest GCC installation (GCC 14) for
# libstdc++ even when the default g++ is GCC 13. The base image ships GCC 14
# crt files and headers without libstdc++-14-dev, so tools/ci_local.sh's Clang
# job fails at link time with "cannot find -lstdc++". The remaining packages
# are the Linux deps from docs/CI.md so a fresh image can configure and test.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build clang python3-venv ccache libstdc++-14-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxss-dev libxfixes-dev libxtst-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols \
  libegl-dev libgl-dev libvulkan-dev libudev-dev libdbus-1-dev
