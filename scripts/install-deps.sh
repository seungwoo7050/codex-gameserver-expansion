#!/usr/bin/env bash
set -euo pipefail

# 필수 빌드/테스트 의존성을 설치한다. idempotent하게 동작하도록 apt-get으로 재설치해도 무방하다.
if ! command -v apt-get >/dev/null 2>&1; then
  echo "apt-get을 찾을 수 없습니다. 의존성을 수동으로 설치해야 합니다." >&2
  exit 1
fi

apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  git \
  pkg-config \
  libssl-dev \
  libboost-all-dev \
  libmariadb-dev \
  libmariadb-dev-compat \
  libmariadb3
