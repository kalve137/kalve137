#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

sudo=""
if [ "$(id -u)" -ne 0 ]; then
    sudo="sudo"
fi

export DEBIAN_FRONTEND=noninteractive
$sudo apt-get update
$sudo apt-get install -y --no-install-recommends \
    autoconf autoconf-archive automake bison build-essential ca-certificates cmake curl desktop-file-utils flex git libtool \
    ninja-build pkg-config python3 python3-venv tar unzip zip \
    libdbus-1-3 libegl1-mesa-dev libfontconfig1-dev libfreetype-dev libgl1-mesa-dev libglib2.0-0t64 \
    libglu1-mesa-dev libx11-dev libxext-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-0 libxmu-dev \
    libxrandr-dev libxt-dev libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1
