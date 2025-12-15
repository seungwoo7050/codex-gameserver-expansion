#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

if command -v apt-get >/dev/null 2>&1 && command -v dpkg >/dev/null 2>&1; then
  if ! dpkg -s libboost-system-dev >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake libboost-all-dev libssl-dev
  fi
fi

BUILD_DIR="${ROOT_DIR}/build"

cmake -S server -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
