# VERSION_PROMPTS.md
에이전트 작업 지시용 프롬프트 정본(버전별).
※ 이 파일은 한국어 정본이다.
※ v1.0.0은 “기준선”이며, v1.1.0부터 확장 개발을 진행한다.

---

## 공통 강제 규칙(모든 버전에 반드시 포함)
1) 목표 버전은 vX.Y.Z **하나만**.
2) 표준 커맨드는 STATE.md에 정의된 스크립트만 사용:
   - `./scripts/build.sh`
   - `./scripts/test-unit.sh`
   - `./scripts/test-it.sh`
   - `./scripts/test-e2e.sh`
   (v2.0.0에서만 추가 스크립트 허용 — 해당 섹션 참고)
3) 표준 토폴로지는 STACK.md에 고정된 Compose 파일만 사용:
   - `infra/compose/it.yml`
   - `infra/compose/e2e.yml`
   (v2.0.0에서만 추가 토폴로지 허용 — 해당 섹션 참고)
4) E2E는 반드시 LB로만 검증:
   - base URL: `http://localhost:8080`
5) v1.3+ 내부 통신은 gRPC+protobuf **필수**(예외 금지).
6) v1.5+ 트레이싱은 OpenTelemetry **필수**.
7) 외부 동작 변경이면 순서 고정:
   - `design/protocol/contract.md` → 계약 검증 테스트 → 구현
8) 테스트 없으면 완료 아님(플래키도 실패).
9) 바이너리/덤프/이미지 등 비텍스트 커밋 금지.
10) 코드 주석/생성 문서는 한국어.

필수 산출물(모든 버전 공통):
- 코드 + 테스트
- `reports/vX.Y.Z.md` (한국어: 재현 절차 + 결과 + 지표/리스크)
- 필요 시 `STATE.md/STACK.md/POLICIES.md/TEST_STRATEGY.md/VERSIONING.md` 갱신
  (원칙/정본이 바뀌는 경우만. 무분별한 수정 금지)

---

## 공통 프롬프트 템플릿(복붙)
너는 이 저장소의 AI 코딩 에이전트다. 목표 버전은 vX.Y.Z 하나다.

필독:
- AGENTS.md
- STATE.md
- VERSIONING.md(해당 버전)
- STACK.md
- POLICIES.md
- TEST_STRATEGY.md
- VERSION_PROMPTS.md(해당 버전 섹션)
- 외부 동작 변경 시: design/protocol/contract.md 필독 및 선수정

규칙:
- 스코프 확장 금지. VERSIONING에 없는 기능 추가 금지.
- 표준 커맨드는 STATE.md 스크립트만 사용.
- E2E는 STACK.md의 고정 토폴로지로만 수행하고, 반드시 LB로만 검증.
- v1.3+ 내부 통신은 gRPC로 고정.
- v1.5+ 트레이싱은 OpenTelemetry로 고정.
- 외부 동작 변경이면 contract.md → 테스트 → 구현.
- 테스트 플래키는 실패 처리.

먼저 “짧은 작업 계획(변경 파일/테스트/리스크)”을 쓰고 시작해라.
완료 시 반드시:
- tests green
- reports/vX.Y.Z.md 작성
- 필요 시 문서/상태 갱신

---

# v1.1.0 — MariaDB 실연동 + Exactly-once(정합성)
## 목표
- 결과/레이팅 반영이 “정확히 1회 효과(exactly-once effect)”를 갖도록
  DB 제약 + 단일 트랜잭션으로 강제한다.
- DB 장애/재시도/타임아웃 정책을 코드+테스트로 고정한다.

## 구현 요구사항(빈틈 없이)
1) 마이그레이션/인덱스
- `infra/db/migrations/`에 마이그레이션 추가(텍스트 SQL).
- 최소 요구:
  - match 결과 테이블에 `UNIQUE(match_id)`
  - 레이팅 반영 가드 테이블(또는 동등 구조)에 `UNIQUE(match_id, user_id)`(권장)
  - 조회 성능을 위한 인덱스(leaderboard/rating 조회 경로)

2) finalize/rating 트랜잭션 경계(강제)
- “finalize + rating apply”는 **단일 트랜잭션**이어야 한다.
- 중복 finalize(같은 match_id)는:
  - “성공(멱등)”으로 처리하되, 효과는 1회만 발생해야 한다.
- 정확히-1회 효과는 “메모리 중복 제거”가 아니라 “DB 제약”이 최종 방어선이어야 한다.

3) DB 재시도/타임아웃(정책 고정)
- POLICIES.md의 재시도 규칙 준수:
  - retryable: deadlock / lock wait timeout / transient timeout/disconnect
  - 최대 3회, 지수 백오프 + 지터
- retry 불가 케이스는 즉시 실패로 종료.

4) 외부 동작/계약
- v1.0.0에 존재하던 finalize 경로(HTTP/WS)는 유지한다.
- 만약 계약이 모호하면 contract.md에 “finalize” 토큰/응답(에러 포함)을 명확히 고정하고, 계약 검증 테스트를 추가한다.

## 필수 테스트(반드시 it로 실DB)
- 실행은 `./scripts/test-it.sh`에 포함되어야 한다.
- 최소 시나리오:
  1) duplicate finalize:
     - 동일 match_id로 finalize를 2회 호출해도 레이팅/결과가 1회만 반영
  2) concurrent finalize:
     - finalize를 동시 2호출(2 스레드/2 클라이언트)해도 1회만 반영
  3) transient failure:
     - deadlock/timeout 등 transient 에러를 주입(또는 강제 유발)했을 때
       최대 3회 내에서 재시도하고, 최종 결과가 문서대로(성공/실패) 나옴

## 완료 기준(추가)
- `./scripts/test-unit.sh` + `./scripts/test-it.sh` 모두 green
- `reports/v1.1.0.md`에 포함:
  - 스키마/유니크키/트랜잭션 경계 요약
  - 테스트 커맨드/결과
  - transient 실패 시나리오에서 “어떻게 재시도/차단했는지” 요약

---

# v1.2.0 — Redis 실연동 + Lua 원자성 + degrade 고정
## 목표
- 매칭 큐/레이트리밋/리줌 토큰/라우팅 힌트를 Redis로 이관하고,
  경쟁 조건(중복 join/cancel/timeout)을 Lua로 원자화한다.
- Redis 장애 시 degrade 동작을 “선택”이 아니라 “고정 기본값”으로 구현+테스트한다.

## 구현 요구사항
1) 키/TTL 고정 준수
- POLICIES.md에 정의된 prefix/TTL을 그대로 따른다.
- resumeToken / routing hint는 TTL 필수(기본값 변경 금지).

2) 원자성(필수)
- correctness-critical 경로는 Lua로 원자화:
  - join / cancel / timeout 경쟁
  - double-join 방지
  - match 생성 enqueue/dequeue의 불변조건 보장
- MULTI/EXEC로 대체 금지(정당화+테스트 없으면 불가).

3) degrade 동작(고정)
- Redis unhealthy 시 기본값(FAIL CLOSED)을 그대로 구현:
  - matchmaking join/cancel: UNAVAILABLE로 거절
  - resume 검증: UNAVAILABLE
  - rate limit: auth/ops는 FAIL CLOSED
- “대기/폴백” 같은 변형은 VERSIONING에 명시가 없으면 금지.

4) 계약/테스트
- join/cancel/resume의 외부 동작이 변하면 contract.md와 계약 검증 테스트를 반드시 수정한다.

## 필수 테스트(it)
- `./scripts/test-it.sh`에 포함
- 최소 시나리오:
  1) join/cancel/timeout 레이스:
     - 최종 상태가 일관되고, 중복 매치/유령 큐가 남지 않음
  2) Redis down/latency:
     - 의도적으로 Redis를 kill/지연시키고
       정책대로(FAIL CLOSED) 에러가 나오며 시스템이 무한 대기/폭주하지 않음

## 완료 기준(추가)
- `reports/v1.2.0.md`에 포함:
  - 키/TTL 표(핵심 키만)
  - Lua 스크립트 목록 + “보장하는 불변조건” 1줄 요약씩
  - degrade 테스트 결과

---

# v1.3.0 — 멀티 인스턴스(고정 토폴로지/고정 기술)
## 목표
- Gateway/Matchmaking/Session 분리.
- 멀티 인스턴스에서 “매칭 → 세션 라우팅 → 재접속(resume)”이 성립.
- 내부 통신은 gRPC로 고정.
- E2E에서 “서로 다른 gateway로 재접속해도 resume 성공”을 증명.

## 구현 요구사항
1) 바이너리 분리(고정 이름)
- `gateway`, `matchmaking`, `session`으로 분리한다.

2) 내부 gRPC 계약 고정
- proto는 `server/proto/*.proto`
- 최소 요구 RPC:
  - matchmaking ↔ session: 세션 할당/생성/종료 통지에 필요한 최소 RPC
  - gateway ↔ session: 클라이언트 세션 attach/resume, snapshot/resync 트리거(필요 최소)
- traceId propagation이 가능한 형태로 interceptor/metadata 전파.

3) 라우팅/리줌
- Redis routing hint + resume token 사용(키/TTL은 POLICIES 준수).
- 재접속 플로우:
  - 클라이언트가 resumeToken 제시
  - gateway가 token 검증 + route 조회
  - 올바른 session 서버로 attach
  - 강제 resync(스냅샷) 수행

4) 고정 E2E 토폴로지 준수
- `infra/compose/e2e.yml`의 고정 구성(2 gateway + LB)을 사용한다.
- 테스트는 반드시 `http://localhost:8080`(LB)만 사용한다.

## 필수 테스트(e2e)
- `./scripts/test-e2e.sh`에 포함
- 최소 시나리오:
  1) 세션 시작 → 강제 연결 끊김 → 재접속:
     - 처음은 gateway1을 타고,
     - 재접속은 gateway2로 들어가도 resume 성공(세션 유지/복구 정책대로)
  2) traceId 전파:
     - 동일 traceId가 gateway 로그와 내부 gRPC 호출 로그에 일관되게 관측됨(최소 1 플로우)

## 완료 기준(추가)
- `reports/v1.3.0.md`에 포함:
  - e2e 실행 커맨드/결과
  - resume 시나리오 증빙(최소: traceId + 성공 여부)
  - “라우팅 소스 오브 트루스가 Redis인 이유/제약” 3줄 요약

---

# v1.4.0 — 세션 플릿 운영(Allocator + Drain + Rolling 안전)
## 목표
- 세션 서버를 여러 개 띄우는 “플릿”을 운영 가능한 형태로 고정.
- 매치 생성 시 Allocator가 세션 서버를 선택.
- 롤링 업데이트 중 세션 보호(드레인/그레이스풀 종료)를 구현+E2E로 증명.

## 구현 요구사항
1) Allocator(필수)
- matchmaking이 세션 서버를 선택하는 로직을 갖는다.
- 최소 정책(고정):
  - 라운드로빈 또는 최소 부하(세션 수) 기반 중 1개를 선택해 “명시”하고 테스트로 고정
- draining 상태인 세션 서버는 할당 대상에서 제외.

2) Drain / Graceful shutdown(필수)
- session 서버는 SIGTERM(또는 ops) 시 draining 모드 진입:
  - 신규 세션 수락 중단
  - 기존 세션은 정책대로 처리:
    - “완료까지 유지” 또는 “최대 grace 이후 종료” 중 1개를 선택해 문서+테스트로 고정
- gateway는 draining 대상에 신규 attach/resume을 보내지 않거나,
  보내더라도 “결정적인 에러 코드/리다이렉트 정책”을 고정한다.

3) ops 최소 요구(선택이 아니라 필요)
- 드레인 트리거를 위한 ops 인터페이스(HTTP든 내부 gRPC든) 최소 1개는 있어야 한다.
- 단, 보안 강화는 v1.6에서 본격 처리(여기서는 최소 기능만).

## 필수 테스트(e2e)
- `./scripts/test-e2e.sh`에 포함
- 최소 시나리오:
  1) 세션 진행 중 session1을 드레인/종료:
     - 정책대로 세션이 유지되거나 결정적으로 종료됨
     - 데이터 정합성(결과/정리)이 깨지지 않음
  2) 롤링 시뮬레이션:
     - session2로 신규 할당이 넘어가고, 기존 세션은 정책대로 처리됨

## 완료 기준(추가)
- `reports/v1.4.0.md`:
  - allocator 정책 + 왜 그 정책인지 3줄
  - drain/종료 정책(숫자: grace ms 포함)
  - e2e 결과 및 실패 시 기대 동작 표

---

# v1.5.0 — 관측성 표준(OpenTelemetry 필수 + SLO 관점)
## 목표
- “운영 가능한 수준”의 관측성 세트를 고정:
  - Prometheus metrics 안정화
  - OpenTelemetry tracing 도입
  - 로그/메트릭/트레이스 상관관계
- 최소 SLO(매치 성공률/지연/틱 지연 등)를 정의하고 측정치를 리포트에 남긴다.

## 구현 요구사항
1) Metrics 안정화(필수)
- POLICIES에 정의된 핵심 지표 이름을 고정한다.
- 라벨 폭발(유저/세션 id 라벨 금지) 방지.

2) OTel tracing(필수)
- gateway → (gRPC) matchmaking/session 경로에서 trace context 전파.
- 최소 1개 핵심 플로우에 span 구성:
  - matchmaking enqueue/dequeue
  - session attach/resume
  - finalize/rating

3) Dashboards/Alerts(텍스트 커밋)
- 파일 경로는 고정(추천 고정, 이 버전에서 생성):
  - `infra/observability/grafana/dashboards/*.json`
  - `infra/observability/prometheus/alerts.yml`
- “이 파일들이 무엇을 보는지”를 보고서에 짧게 요약.

## 필수 테스트
- unit 또는 e2e에 포함(최소 1개 자동 검증)
  1) `/metrics` 존재 + 핵심 metric name이 실제로 노출됨
  2) traceId 전파가 자동 검증됨
     - 예: 요청 1회 수행 후 로그에서 동일 traceId가 gateway와 내부 호출에 함께 존재함

## 완료 기준(추가)
- `reports/v1.5.0.md`:
  - SLO 정의(수치 포함) + 현재 측정치(샘플이라도 숫자로)
  - 대시보드/알람 룰 요약
  - trace 전파 증빙

---

# v1.6.0 — Security/Access/Compliance(RBAC/감사/키 롤링/PII 마스킹)
## 목표
- ops 엔드포인트를 RBAC로 통제하고,
  모든 ops 액션에 감사로그를 남기며,
  시크릿/키 롤링과 PII 마스킹을 “테스트로” 고정한다.
- 내부 gRPC에 mTLS를 적용한다(개발/테스트 환경 기준).

## 구현 요구사항
1) RBAC(필수)
- 최소 역할 3개(고정):
  - viewer / operator / admin
- ops 엔드포인트(예: `/ops/**`)는 역할에 따라 allow/deny가 결정적이어야 한다.
- 인증 토큰은 “키 롤링”을 지원해야 한다:
  - current key + previous key 동시 허용
  - 새 토큰은 current로만 발급(발급 기능이 없다면 검증만이라도 rotation을 지원)

2) 감사로그(필수, append-only)
- MariaDB에 audit 로그 테이블(또는 동등 구조) 추가:
  - who(role/subject), when, what(action), result, traceId
- ops 액션 성공/실패 모두 기록.

3) 내부 mTLS(필수)
- gRPC 채널에서 mTLS 사용(서버/클라 인증).
- “키/인증서”는 repo에 커밋하지 않는다.
  - dev/test용 인증서는 스크립트로 런타임 생성하고 `.gitignore` 경로에 저장.
  - 예: `./scripts/gen-dev-certs.sh`로 `var/certs/` 생성(커밋 금지)

4) PII 마스킹(필수)
- 로그/메트릭/트레이스에 아래가 원문으로 남지 않도록 마스킹:
  - token/secret/email/ip(정책에서 정한 항목)
- 마스킹은 “규칙 + 테스트”로 고정.

## 필수 테스트
- `./scripts/test-it.sh` 또는 `./scripts/test-e2e.sh`에 포함(최소 1개 자동)
  1) RBAC:
     - viewer/operator/admin 각각 허용/거부 케이스 검증
  2) 감사로그:
     - ops 액션 수행 시 DB에 audit row가 적재됨
  3) PII 마스킹:
     - 로그 출력(또는 로거 sink)을 캡처해서 토큰/이메일/ip가 원문으로 안 남음을 검증
  4) mTLS:
     - mTLS 미설정 클라이언트는 내부 gRPC 호출이 실패해야 함
     - 올바른 cert로는 성공해야 함

## 완료 기준(추가)
- `reports/v1.6.0.md`:
  - RBAC 매트릭스(역할 x 엔드포인트) 표
  - 감사로그 스키마/예시(민감정보 제거)
  - 키 롤링 절차 요약(환경변수/설정 포함)
  - mTLS 구성/재현 커맨드

---

# v1.7.0 — Reliability Engineering(장애를 “통과 시험”으로)
## 목표
- 실패를 전제로 하는 정책을 코드로 고정:
  - circuit breaker
  - retry budget(재시도 남발 제한)
  - graceful degrade(정책대로 망가지기)
- chaos 시나리오를 “재현 스크립트 + 자동 실행 + 리포트”로 남긴다.

## 구현 요구사항
1) Circuit Breaker(필수)
- 대상(최소):
  - DB 호출
  - Redis 호출
  - 내부 gRPC 호출
- 상태(최소): closed/open/half-open
- 파라미터는 설정으로 고정(기본값 명시).

2) Retry budget(필수)
- 같은 요청/같은 의존성에 대해 재시도를 무한히 하지 않도록 제한.
- “에러율이 높을 때는 재시도를 줄인다” 같은 정책을 최소 형태로라도 구현하고 테스트로 고정.

3) Graceful degrade(필수)
- POLICIES의 “고정 기본값(FAIL CLOSED/OPEN)”을 지키면서,
  시스템이 폭주/무한 대기하지 않도록 한다.

4) Chaos 스크립트(필수)
- 경로 고정(추천):
  - `scripts/chaos/`
- 최소 시나리오 3개:
  1) Redis down
  2) DB latency spike
  3) session 인스턴스 kill
- 스크립트는 사람 개입 없이 실행 가능해야 한다.

## 필수 테스트(e2e)
- `./scripts/test-e2e.sh`에 포함(또는 그 내부에서 chaos를 실행)
- 최소 검증:
  - 각 chaos 시나리오에서:
    - 에러 코드가 정책대로(UNAVAILABLE 등) 결정적으로 나온다
    - 시스템이 회복되면 정상 경로가 다시 동작한다
    - “폭주”(무한 재시도, 큐 폭발, 메모리 누수성 증가)가 관측되지 않는다(메트릭/로그로 근거)

## 완료 기준(추가)
- `reports/v1.7.0.md`:
  - 시나리오별 기대/실제 결과 표
  - 회복 시간(대략이라도 숫자)
  - breaker/budget 파라미터와 그 근거 3줄

---

# v1.8.0 — (옵션) N인 세션 확장 + 플랫폼화
## 목표
- 2인 세션을 N인(예: 20~100)으로 확장 가능한 구조를 만든다.
- 대규모 핵심(interest management, delta/snapshot, backpressure 상한)을 “코드+테스트+부하 리포트”로 증명한다.

## 구현 요구사항(옵션이지만 하면 빡세게 고정)
1) Interest management(필수)
- 정책을 하나 선택해 명시하고 구현/테스트로 고정:
  - 거리 기반 구독 / 그리드 기반 / 룸 기반 등
- 구독 변경이 폭주하지 않도록 상한/쿨다운 규칙 포함.

2) 전송 최적화(필수)
- delta 전송 + 주기적 snapshot
- 메시지 크기 상한, 전송률 상한, 큐 상한(백프레셔) 고정
- 느린 클라이언트가 서버를 망치지 않도록 드랍/압축/샘플링 정책 중 1개를 고정.

3) 부하 하네스(필수)
- 경로 추천:
  - `tools/load/` 또는 `scripts/load/`
- 재현 가능한 커맨드 제공(시드/파라미터 고정).

## 테스트/리포트
- 기능 테스트(e2e 또는 별도 러너) + 최소 부하 리포트
- `reports/v1.8.0.md`:
  - 목표 CCU/동시 세션/N 인원 수치
  - p95 지연/틱 지연/드랍률 같은 핵심 수치
  - 재현 스크립트 커맨드

---

# v2.0.0 — Multi-region + DR(추가 토폴로지/커맨드가 필요한 유일한 버전)
## 목표
- 리전 라우팅 + 리전 장애 시 페일오버.
- 데이터 복제/정합성 레벨을 문서로 고정(무엇은 강하게, 무엇은 eventual).
- DR 런북 + 복구훈련 결과(RTO/RPO)를 숫자로 남긴다.

## 이 버전에서만 허용되는 “정본 확장”
- 멀티리전/DR은 기존 `infra/compose/e2e.yml`만으로 부족할 수 있다.
- 이 버전에서는 아래를 “정본에 추가”하는 변경을 허용한다(반드시 함께 문서 갱신):
  1) Compose 추가:
     - `infra/compose/dr.yml` (새 정본 토폴로지)
  2) 스크립트 추가:
     - `./scripts/test-dr.sh` (DR 시나리오 자동 검증)
  3) 문서 갱신:
     - STATE.md: test-dr.sh 추가
     - STACK.md/TEST_STRATEGY.md: dr.yml과 DR 테스트 게이트 명시
※ 위 3가지를 동시에 하지 않으면 실패 처리.

## 구현 요구사항(최소 고정안)
1) 리전 라우팅
- 클라이언트는 “희망 리전”을 제시할 수 있어야 한다(헤더/쿼리/로그인 정보 등).
- 라우터(또는 gateway)가 다음 규칙을 결정적으로 적용:
  - 1순위: 희망 리전
  - 2순위: 해당 리전 불가 시 대체 리전
- 세션 중에는 리전 이동 금지(세션 어피니티 고정).

2) DR(DB 관점) — 최소로라도 “복구 절차 + 측정”이 있어야 함
- 선택지는 2개 중 하나를 “명시”하고 고정:
  A) MariaDB primary/replica + promote(수동) 절차(권장)
  B) 단순화 모델(공유 DB 등)을 쓰되, 한계/정합성 레벨을 문서로 명확히 선언
- 어떤 선택이든:
  - RTO(복구 시간) 측정
  - RPO(데이터 유실 지점) 측정 또는 “0/비영(非0)” 근거 제시

3) DR 런북(필수)
- `reports/v2.0.0.md`에 런북 포함(또는 별도 문서 링크):
  - 감지(어떤 알람/증상)
  - 판단(언제 failover)
  - 조치(명령/스크립트)
  - 검증(무엇을 확인)
  - 복구 후 정리

## 필수 테스트
- `./scripts/test-dr.sh`가 자동으로:
  1) 기본 라우팅 검증(리전 A로 붙는지)
  2) 리전 A 장애 유발(컨테이너 kill 등)
  3) failover 수행(수동 절차를 스크립트로 자동화하거나, 최소한 자동 검증)
  4) 리전 B로 서비스 지속됨을 검증
  5) RTO/RPO를 산출(간단한 타임스탬프/카운터 기반도 허용)

## 완료 기준(추가)
- v2.0.0은 아래가 모두 있어야 완료:
  - `./scripts/test-dr.sh` green
  - `infra/compose/dr.yml` 존재
  - STATE/STACK/TEST_STRATEGY 갱신 완료
  - `reports/v2.0.0.md`에 RTO/RPO 숫자 + 런북 포함
