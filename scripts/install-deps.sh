#!/usr/bin/env bash
set -euo pipefail

# 필수 빌드/테스트 의존성을 설치한다. idempotent하게 동작하도록 apt-get으로 재설치해도 무방하다.
if ! command -v apt-get >/dev/null 2>&1; then
  echo "apt-get을 찾을 수 없습니다. 의존성을 수동으로 설치해야 합니다." >&2
  exit 1
fi

APT_GET_CMD="apt-get"
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    APT_GET_CMD="sudo apt-get"
  else
    echo "루트 권한이 없고 sudo도 사용할 수 없습니다. 패키지를 수동으로 설치하세요." >&2
    exit 1
  fi
fi

${APT_GET_CMD} update
${APT_GET_CMD} install -y --no-install-recommends \
  build-essential \
  cmake \
  git \
  pkg-config \
  libssl-dev \
  libboost-all-dev \
  libmariadb-dev \
  libmariadb-dev-compat \
  libmariadb3
