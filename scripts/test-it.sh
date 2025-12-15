#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/infra/compose/it.yml"
export DOCKER_BUILDKIT=1
trap 'docker compose -f "$COMPOSE_FILE" down -v' EXIT

docker compose -f "$COMPOSE_FILE" up -d
READY=0
for _ in {1..30}; do
  if docker compose -f "$COMPOSE_FILE" exec -T mariadb mariadb-admin ping -h localhost --silent; then
    READY=1
    break
  fi
  sleep 2
done
if [[ "$READY" -ne 1 ]]; then
  echo "MariaDB 준비 실패" >&2
  exit 1
fi

for f in "$ROOT_DIR"/infra/db/migrations/*.sql; do
  docker compose -f "$COMPOSE_FILE" exec -T mariadb mariadb --user=app --password=app_pass app_db < "$f"
done

"$ROOT_DIR/scripts/build.sh" Debug
DB_HOST=127.0.0.1 DB_PORT=3306 DB_USER=app DB_PASSWORD=app_pass DB_NAME=app_db \
  ctest --test-dir "$ROOT_DIR/build" --output-on-failure -L it
