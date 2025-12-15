# CLONE_GUIDE.md

## 개요
v1.0.0 안정화 버전을 로컬에서 바로 빌드·테스트·기동할 수 있도록 단계별 절차를 정리한다. 아래 순서를 그대로 따라 하면 계약 문서와 테스트가 일치하는 상태를 재현할 수 있다.

## 요구 사항
- Docker, Docker Compose
- C++ 빌드 도구: g++, cmake, libboost-all-dev, libssl-dev
- (선택) Python 3 + `pip install httpx websockets` (부하 하네스용)

## 빠른 검증(권장)
1. 의존성 설치(우분투 예시, 관리자 권한 필요):
   ```bash
   sudo apt-get update && sudo apt-get install -y build-essential cmake libboost-all-dev libssl-dev
   ```
   - Boost가 없으면 `./verify.sh` 실행 시 동일 패키지를 자동으로 설치한다.
2. 전체 빌드·테스트 실행:
   ```bash
   ./verify.sh
   ```
   - CMake 설정/빌드 후 `ctest --output-on-failure`로 단위/통합/E2E 테스트를 모두 수행한다.

## 수동 빌드 및 서버 실행
```bash
cmake -S server -B build
cmake --build build
./build/server_app
```
- 기본 포트는 8080이며 필요하면 `SERVER_PORT=18080 ./build/server_app`처럼 환경 변수를 앞에 지정한다.

## Docker Compose 기동
1. 스택 빌드/기동:
   ```bash
   cd infra
   docker compose up --build
   ```
2. 종료/정리:
   ```bash
   docker compose down -v
   ```

## 환경 변수 요약
- SERVER_PORT (기본 8080)
- DB_HOST, DB_PORT, DB_USER, DB_PASSWORD, DB_NAME (현재는 메모리 저장이므로 미연결 상태로도 동작)
- REDIS_HOST, REDIS_PORT
- LOG_LEVEL (info/debug 등)
- AUTH_TOKEN_TTL_SECONDS (기본 3600)
- LOGIN_RATE_LIMIT_WINDOW (기본 60), LOGIN_RATE_LIMIT_MAX (기본 5)
- WS_QUEUE_LIMIT_MESSAGES (기본 8), WS_QUEUE_LIMIT_BYTES (기본 65536)
- MATCH_QUEUE_TIMEOUT_SECONDS (기본 10)
- SESSION_TICK_INTERVAL_MS (기본 100)
- OPS_TOKEN (/ops/status 보호용 토큰)

## 핵심 엔드포인트/WS 빠른 확인
- 헬스 체크: `curl http://localhost:8080/api/health`
- 인증: `POST /api/auth/register`, `POST /api/auth/login`, `POST /api/auth/logout` (Authorization: Bearer 필요)
- 보호 API: `GET /api/profile`
- 큐: `POST /api/queue/join`, `POST /api/queue/cancel`
- 리더보드: `GET /api/leaderboard?page=1&size=10`
- 메트릭: `GET http://localhost:8080/metrics`
- 운영 상태: `GET http://localhost:8080/ops/status` (헤더 `X-Ops-Token` 필요)
- WebSocket: `ws://localhost:8080/ws` (업그레이드 헤더에 Authorization 필요, `echo`, `session.input`, `resync_request` 이벤트 지원)

## 부하 하네스(옵션)
1. 로그인 레이트리밋을 완화한 상태로 서버 실행(예: 100회/분):
   ```bash
   LOGIN_RATE_LIMIT_MAX=100 ./build/server_app
   ```
2. 별도 터미널에서 하네스 실행:
   ```bash
   python tools/perf/harness.py --clients 40 --queue-timeout 5 --session-timeout 12 --http-timeout 5 --ws-connect-timeout 5
   ```
   - WS/HTTP 주소가 다를 경우 `--http-base`, `--ws-url`을 지정한다.

## 참고
- 계약/에러 코드/WS 이벤트 스키마는 `design/protocol/contract.md`를 따른다.
- 운영 시나리오 및 관측 방법은 `design/ops/v1.0.0-runbook.md`를 참고한다.
