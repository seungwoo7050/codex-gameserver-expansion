#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

if command -v apt-get >/dev/null 2>&1; then
  # CI 환경에서 MariaDB 개발 패키지를 찾지 못하는 경우가 있어 표준 설치 스크립트를 호출한다.
  "${ROOT_DIR}/scripts/install-deps.sh"
fi

# 표준 스크립트만 사용하여 단위 테스트를 우선 실행한다. (Docker 미가용 CI에서도 통과 가능)
"${ROOT_DIR}/scripts/test-unit.sh"

# Docker 데몬을 사용할 수 있는 경우에만 it/e2e를 추가로 실행한다.
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  "${ROOT_DIR}/scripts/test-it.sh"
  "${ROOT_DIR}/scripts/test-e2e.sh"
else
  echo "Docker 데몬을 사용할 수 없어 it/e2e 테스트를 건너뜁니다." >&2
fi
