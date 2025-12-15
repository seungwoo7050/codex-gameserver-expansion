#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT_DIR/scripts/build.sh" Debug
ctest --test-dir "$ROOT_DIR/build" --output-on-failure -L unit
