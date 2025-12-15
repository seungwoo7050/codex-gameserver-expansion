#!/usr/bin/env bash
set -euo pipefail
BUILD_TYPE="${1:-Debug}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
cmake -S "$ROOT_DIR/server" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR"
