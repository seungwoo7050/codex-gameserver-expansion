# design/protocol/contract.md

## 버전
- 적용 버전: v1.1.0 (정본)
- 변경 이력:
  - v1.1.0: finalize/레이팅 경로가 MariaDB 제약과 단일 트랜잭션으로 정확히 한 번만 반영되도록 명시하고, DB 재시도/타임아웃 정책을 고정.
  - v1.0.0: 헬스 체크 버전 표기 갱신, 실패/중복 방지 규칙을 문서화하고 E2E 테스트 기준을 명시.

## 공통 규칙
- 문자 인코딩: UTF-8
- 타임존: Asia/Seoul
- 서버 포트: `${SERVER_PORT}` (기본 8080)
- Content-Type: 모든 HTTP 응답은 `application/json; charset=utf-8`
- 민감정보 노출 금지: 응답/로그/메트릭/ops에는 토큰·비밀번호가 포함되지 않는다.
- 데이터 저장: 결과/레이팅만 MariaDB에 영속화되며 `match_results.match_id` 고유키와
   `rating_applies(match_id, user_id)` 고유키로 결과/레이팅 적용이 정확히 한 번만 기록된다. 큐/세션/인증은
   Redis 미도입 상태라 프로세스 메모리에서 처리된다.
- idempotency: 큐 참가/매칭/결과 반영은 중복 호출로 상태가 어긋나지 않도록 설계되어야 한다.

## 환경 변수
- `SERVER_PORT` (기본 8080)
- `DB_HOST` (기본 mariadb)
- `DB_PORT` (기본 3306)
- `DB_USER` (기본 app)
- `DB_PASSWORD` (기본 app_pass)
- `DB_NAME` (기본 app_db)
- `REDIS_HOST` (기본 redis)
- `REDIS_PORT` (기본 6379)
- `LOG_LEVEL` (기본 info)
- `AUTH_TOKEN_TTL_SECONDS` (기본 3600)
- `LOGIN_RATE_LIMIT_WINDOW` (기본 60)
- `LOGIN_RATE_LIMIT_MAX` (기본 5)
- `WS_QUEUE_LIMIT_MESSAGES` (기본 8)
- `WS_QUEUE_LIMIT_BYTES` (기본 65536)
- `MATCH_QUEUE_TIMEOUT_SECONDS` (기본 10)
- `SESSION_TICK_INTERVAL_MS` (기본 100)
- `OPS_TOKEN` (/ops/status 보호용 토큰, 기본 빈 문자열)

## REST 응답 엔벨로프
- 성공: `{ "success": true, "data": <object>, "error": null, "meta": {"timestamp": "ISO8601"} }`
- 실패: `{ "success": false, "data": null, "error": { "code": "<ERROR_CODE>", "message": "한국어", "detail": null }, "meta": {"timestamp": "ISO8601"} }`

### 에러 코드 최소 목록
- `internal_error`: 서버 내부 오류
- `not_found`: 존재하지 않는 경로
- `bad_request`: 요청 파싱/검증 실패
- `unauthorized`: 토큰 누락/만료/불일치
- `duplicate_user`: 이미 존재하는 사용자명
- `rate_limited`: 로그인 레이트리밋 초과
- `queue_duplicate`: 이미 큐에 들어있음
- `queue_timeout`: 매칭 타임아웃 발생(WS 오류 이벤트로만 전송)
- `queue_not_found`: 취소할 엔트리가 없음
- `session_not_found`: 존재하지 않는 세션 ID(WS 입력 시)
- `session_closed`: 종료된 세션(WS 입력 시)
- `not_participant`: 참가자가 아닌 사용자가 입력을 보냄
- `input_invalid`: 입력 필드/시퀀스/틱 검증 실패
- `leaderboard_range`: 페이지/사이즈 범위 오류
- `invalid_resume_token`: 리싱크 토큰이 잘못됨(WS 오류)

## HTTP 엔드포인트
### GET /api/health
- 목적: 서버 기동 및 의존성 접근 가능 여부 확인
- 응답 200 본문:
```json
{
  "success": true,
  "data": {"status": "ok", "version": "v1.0.0"},
  "error": null,
  "meta": {"timestamp": "2024-01-01T00:00:00Z"}
}
```

### POST /api/auth/register
- 목적: 신규 사용자 등록(메모리 저장)
- 요청: `Content-Type: application/json`
```json
{"username": "user1", "password": "plainPassword"}
```
- 성공 201 본문: 성공 엔벨로프 + `data: {"userId": <number>, "username": "user1"}`
- 실패 코드: `bad_request`(필드 누락/형식 오류), `duplicate_user`(이미 존재)

### POST /api/auth/login
- 목적: 토큰 발급
- 요청 본문: `{ "username": "user1", "password": "pw" }`
- 성공 200 본문: 성공 엔벨로프 +
```json
{
  "token": "<hex>",
  "expiresAt": "2024-01-01T00:00:00Z",
  "user": {"userId": 1, "username": "user1"}
}
```
- 실패: `unauthorized`(자격 증명 오류), `rate_limited`(429), `bad_request`(JSON 오류)
- 토큰 만료: `AUTH_TOKEN_TTL_SECONDS` 이후 만료

### POST /api/auth/logout
- 목적: 토큰 폐기
- 인증: `Authorization: Bearer <token>`
- 성공 200 본문: `data: {"loggedOut": true}`
- 실패: `unauthorized`

### GET /api/profile
- 목적: 인증된 사용자 프로필/레이팅 확인(메모리 기반)
- 인증 필수: `Authorization: Bearer <token>`
- 성공 200 본문: `data: {"userId", "username", "rating", "wins", "losses", "matches"}`
- 실패: `unauthorized`

### POST /api/queue/join
- 목적: 매칭 큐 진입
- 인증 필수
- 요청 본문:
```json
{"mode": "normal", "timeoutSeconds": 5} // timeoutSeconds 선택, 미지정 시 환경변수 기본값 사용
```
- 성공 200 본문: `data: {"queued": true, "mode": "normal", "expiresAt": "ISO8601"}`
- 실패: `unauthorized`, `bad_request`(mode/timeout 오류), `queue_duplicate`(이미 큐 또는 세션 보유)

### POST /api/queue/cancel
- 목적: 큐 취소
- 인증 필수
- 성공 200 본문: `data: {"canceled": true}`
- 실패: `unauthorized`, `queue_not_found`(HTTP 404)

### GET /api/leaderboard
- 목적: 레이팅/전적 순위 조회(메모리 기반)
- 쿼리: `page`(기본 1), `size`(기본 10, 1~50)
- 성공 200 본문: `data: {"page", "size", "total", "entries": [{"rank", "userId", "username", "rating", "wins", "losses", "matches"}]}`
- 실패: `leaderboard_range`(HTTP 400)

### GET /metrics
- 목적: 관측성 메트릭(요청/에러 카운터, WS/세션/큐 규모)
- 인증: 불필요
- 성공 200 본문: `data: {"requests": {"total", "errors"}, "connections": {"websocket"}, "sessions": {"active"}, "queue": {"length"}}`

### GET /ops/status
- 목적: 운영 확인용 상태
- 인증: 헤더 `X-Ops-Token: <token>` 값이 `${OPS_TOKEN}`과 일치해야 함. 미설정 또는 불일치 시 401 + `unauthorized`.
- 성공 200 본문: `data: {"activeSessions", "queueLength", "activeWebsocket", "errorCount"}`

## WebSocket 계약
- 경로: `/ws`
- 업그레이드: HTTP 헤더 `Authorization: Bearer <token>` 필수. 누락/검증 실패 시 HTTP 401 + REST 오류 엔벨로프 후 업그레이드 거부.
- 메시지 엔벨로프: `{ "t": "event" | "error", "seq": <number>, "event": "<name>"|null, "p": <object|null> }`
  - 서버는 수신 메시지의 `seq`를 그대로 돌려준다(없으면 0).
  - 오류 메시지: `t`=`error`, `event`는 null, `p`는 `{ "code": "<ERROR_CODE>", "message": "한국어" }`.

### 연결 직후 이벤트
- `auth_state` (서버→클라이언트)
```json
{"t":"event","seq":0,"event":"auth_state","p":{"userId":1,"username":"user1","resumeToken":"<hex>","snapshotVersion":1}}
```

### 지원 이벤트
- `echo`
  - 클라이언트→서버: `{ "t": "event", "seq": <number>, "event": "echo", "p": {"message": "텍스트"} }`
  - 서버→클라이언트: `{ "t": "event", "seq": 동일, "event": "echo", "p": {"message": "원문", "userId": <number>} }`
- `resync_request`
  - 클라이언트→서버: `{ "t": "event", "seq": <number>, "event": "resync_request", "p": {"resumeToken": "<hex>"} }`
  - 성공 시: `event`=`resync_state`, `p`=`{ "resumeToken": "<새 토큰>", "snapshot": {"version":<number>, "state":"auth_only", "issuedAt":"ISO8601", "user":{...}} }`
  - 실패 시: `t`=`error`, `p.code`=`invalid_resume_token`
- 세션 이벤트(매칭 후 서버→클라이언트)
  - `session.created`: `p`=`{ "sessionId": "uuid", "createdAt": "ISO8601", "participants": [{"userId","username"}, ...] }`
  - `session.started`: `p`=`{ "sessionId": "uuid", "tick": 0, "tickIntervalMs": <number>, "state": {"players": [...], "tick": <number>} }`
  - `session.state`: `p`=`{ "sessionId": "uuid", "tick": <number>, "players": [{"userId","position","lastSequence"}], "issuedAt": "ISO8601" }`
  - `session.ended`: `p`=`{ "sessionId": "uuid", "reason": "completed", "result": {"winnerUserId": <number>, "ticks": <number>} }`
- 클라이언트 입력
  - 이벤트명 `session.input`
  - 메시지: `{ "t": "event", "seq": <number>, "event": "session.input", "p": {"sessionId": "uuid", "sequence": <uint>, "targetTick": <int>, "delta": <int>} }`
  - 실패 시 오류 이벤트: 코드 `session_not_found` | `session_closed` | `not_participant` | `input_invalid`

### 백프레셔
- 연결별 대기열이 `WS_QUEUE_LIMIT_MESSAGES` 또는 `WS_QUEUE_LIMIT_BYTES`를 초과하면 close code `1008(policy_violation)` + reason `backpressure_exceeded` 로 종료된다.

## 큐/매칭 정책
- 큐 모드: `normal`만 지원.
- 타임아웃: 기본 `${MATCH_QUEUE_TIMEOUT_SECONDS}` 초, 요청 본문 `timeoutSeconds`로 재정의 가능.
- 중복 방지: 이미 큐/세션 보유 시 `queue_duplicate` 반환.
- 페어링: 먼저 들어온 두 명을 즉시 매칭, 두 사용자에게 `session.created` → `session.started` → 주기적 `session.state` → `session.ended` 순서로 전달.
- 타임아웃: 지정 시간이 지나면 WS 오류 이벤트 `queue_timeout` 전송.

## 핵심 플로우 및 테스트 기준
- register/login: 인증 실패 시 401 + `unauthorized` 코드, 성공 시 토큰과 만료 시각을 제공한다.
- 큐 참가 → 매칭 → 세션 시작/종료 → 결과 저장: 한 세션 종료 시 결과는 1건만 기록되고, 동일 `match_id` finalize 반복 호출은
  DB 고유키(`match_results.match_id`, `rating_applies.match_id,user_id`)로 1회만 효과가 반영된다.
- 레이팅/리더보드: 결과가 반영되면 승자/패자 레이팅이 조정되고 리더보드 조회로 순위를 확인한다.
- 실패 시나리오:
  - 인증 누락: 보호 REST는 401 + `unauthorized`, WS 업그레이드는 401 + REST 오류 엔벨로프 후 거부된다.
  - 큐 중복: 동일 사용자가 다시 참가하면 409 + `queue_duplicate`.
  - 큐 타임아웃: 매칭 실패 시 WS 오류 이벤트 `queue_timeout`.
  - 세션 종료 후 입력: 참가자가 종료된 세션 ID로 `session.input`을 보내면 오류 이벤트 `session_not_found`로 거절되고 결과는 추가로 반영되지 않는다.

## 현재 미구현/제한
 - Redis 연동은 설정값만 존재하며 큐/세션/인증은 모두 메모리에서 처리된다.
 - Redis 도입 전까지 큐 상태/WS 세션 힌트는 프로세스 재시작 시 초기화되고 결과/레이팅만 MariaDB에 남는다.
- /metrics 값은 프로세스 메모리 스냅샷이며 Prometheus 노출 포맷을 제공하지 않는다.
- ops 엔드포인트는 단일 토큰 비교만 제공하며 세분화된 권한/감사 로그는 없다.
