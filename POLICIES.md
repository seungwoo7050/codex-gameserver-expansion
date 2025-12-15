# POLICIES.md
Runtime policies locked to prevent divergent implementations.

Seed doc for tooling. Generated docs and code comments MUST be Korean.

---

## 1) Timeouts (defaults, MUST be configurable)
- HTTP request hard deadline: 1s
- WS message handling logic budget: 50ms
- gRPC default deadline: 300ms (per-call override allowed)
- MariaDB:
  - connect: 2s
  - query: 2s
- Redis:
  - command: 200ms

No unbounded waits. No infinite retries.

---

## 2) Retry policy (STRICT, bounded)
Retry ONLY transient errors and ONLY if the operation is idempotent.

- DB retryable:
  - deadlock
  - lock wait timeout
  - transient disconnect/timeout
- Redis retryable:
  - transient timeout/unavailable

Max attempts: 3 total
Backoff: exponential + jitter
If not idempotent, add an idempotency guard first (see §3).

---

## 3) Exactly-once effect (DB-enforced pattern, NOT optional)
Finalize/result/rating MUST be “exactly-once effect” via DB constraints + transaction.

Recommended minimal schema constraints (names are recommendations; constraints are mandatory):
- `match_results`:
  - UNIQUE(match_id)
- `rating_applies` (or equivalent):
  - UNIQUE(match_id, user_id)  // prevents double apply per user per match

Canonical finalize transaction:
1) BEGIN
2) INSERT INTO match_results(match_id, ...) ...
   - if duplicate key: treat as idempotent success (already finalized)
3) For each user:
   - INSERT INTO rating_applies(match_id, user_id, ...) ...
     - if duplicate key: skip rating update for that user
   - UPDATE ratings SET ... WHERE user_id = ...
4) COMMIT

No “best effort”. No in-memory dedupe as the source of truth.

---

## 4) Redis usage (v1.2+)
### 4.1 Key namespace (fixed prefixes)
- Matchmaking queue:
  - `mm:q:<mode>`
- In-flight join marker (optional but recommended):
  - `mm:join:<mode>:<userId>`
- Rate limit:
  - `rl:<scope>:<key>`
- Session routing hint:
  - `route:sess:<sessionId> -> <sessionServerId>`
- Resume token:
  - `resume:<token> -> {userId, sessionId, issuedAt, ...}`

### 4.2 TTL defaults (fixed unless VERSIONING overrides)
- resume token TTL: 10 minutes
- routing hint TTL: session_lifetime + 5 minutes
- rate limit buckets: policy-specific (documented per endpoint)

### 4.3 Atomicity (mandatory)
Any correctness-critical operation MUST be atomic via Lua.
Examples:
- join/cancel/timeout race
- double-join prevention
- match creation enqueue/dequeue

MULTI/EXEC is allowed only if it is proven safe under concurrency and tested.

---

## 5) Degrade behavior (DEFAULTS ARE FIXED)
When dependencies are unhealthy, behavior must be deterministic.

### 5.1 MariaDB unhealthy
- Any write that requires DB (finalize/rating/result) => FAIL CLOSED
  - return UNAVAILABLE (and do not fake success)
- Read endpoints that require DB => UNAVAILABLE
No silent fallbacks.

### 5.2 Redis unhealthy (v1.2+)
- Matchmaking join/cancel => FAIL CLOSED (reject with UNAVAILABLE)
- Resume token validation => FAIL CLOSED (UNAVAILABLE)
- Rate limit:
  - auth / ops => FAIL CLOSED
  - health check => FAIL OPEN (allowed)

Any deviation requires explicit VERSIONING note + tests.

---

## 6) Uniform error model (fixed)
All protocols (HTTP/WS/gRPC) map to stable error codes:
- INVALID_ARGUMENT
- UNAUTHENTICATED
- PERMISSION_DENIED
- NOT_FOUND
- CONFLICT
- RESOURCE_EXHAUSTED
- DEADLINE_EXCEEDED
- UNAVAILABLE
- INTERNAL

Responses MUST include traceId when available.
Human messages are Korean, short, and contain no secrets.

---

## 7) Observability minimum (fixed)
### 7.1 Logs (structured)
Required fields when available:
- traceId
- userId, sessionId
- route/event
- latency_ms
- error_code

### 7.2 Metrics (Prometheus, fixed names)
- requests_total{service,route,code}
- request_latency_ms{service,route} (histogram/summary)
- ws_connections{state}
- matchmaking_queue_depth{mode}
- sessions_active
- db_latency_ms
- redis_latency_ms
- tick_delay_ms

---

## 8) Security minimum (fixed)
- No secrets in repo.
- No secrets in logs/metrics/traces.
- ops endpoints must be isolated and protected (RBAC required in v1.6+).
