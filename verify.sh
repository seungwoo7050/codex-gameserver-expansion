#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

if command -v apt-get >/dev/null 2>&1; then
  # CI 환경에서 MariaDB 개발 패키지를 찾지 못하는 경우가 있어 표준 설치 스크립트를 호출한다.
  "${ROOT_DIR}/scripts/install-deps.sh"
fi

BUILD_DIR="${ROOT_DIR}/build"

cmake -S server -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
