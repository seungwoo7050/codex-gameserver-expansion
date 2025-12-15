# VERSIONING.md
Roadmap + DoD with fixed commands/topology/tech choices.

Seed doc for tooling. Generated docs and code comments MUST be Korean.

---

## 0) Global rules
- One change set targets exactly one version.
- PATCH: bugfix/refactor/tests/docs only (no new features).
- Canonical commands MUST remain stable (STATE.md §3).

---

## 1) Definition of Done (applies to every version)
A version is complete ONLY if:
- Canonical scripts exist and are used:
  - `./scripts/build.sh`
  - `./scripts/test-unit.sh`
  - `./scripts/test-it.sh`
  - `./scripts/test-e2e.sh`
- Canonical compose files exist and are used:
  - `infra/compose/it.yml`
  - `infra/compose/e2e.yml`
- Unit + required it/e2e tests pass (see TEST_STRATEGY.md)
- If public behavior changed:
  - `design/protocol/contract.md` updated
  - contract validation tests updated
- `reports/vX.Y.Z.md` exists (Korean: repro + results + metrics/risks)
- No binaries committed
- Baseline regression remains green unless explicitly allowed

---

## 2) v1.0.0 (baseline)
Status: done
Requirements (must exist):
- git tag `v1.0.0`
- `archive/v1.0.0/**` (text-only)
- baseline regression suite remains green

---

## 3) Roadmap (stable ordering)

### v1.1.0 — MariaDB real persistence + consistency (Exactly-once)
Status: planned
- DB migrations + indexes
- Exactly-once finalize/rating via constraints + single transaction
- DB retry/timeout policy implemented
- it tests:
  - duplicate finalize => no double apply
  - concurrent finalize => no double apply
  - transient DB errors => bounded retries

### v1.2.0 — Redis real integration + atomic queue/rate limit/resume
Status: planned
- Redis keys + TTL per POLICIES.md
- Atomic operations via Lua
- Redis degrade defaults implemented (fail closed where mandated)
- it tests:
  - join/cancel/timeout races
  - redis down/latency => degrade matches POLICIES.md

### v1.3.0 — Multi-instance architecture (gRPC mandatory)
Status: planned
Locked decisions:
- internal RPC = gRPC + protobuf
- e2e topology = STACK.md §4.2

Must:
- split binaries (gateway/matchmaking/session)
- routing + resume across gateway instances
- traceId propagation at least one end-to-end flow
- e2e tests:
  - reconnect via lb lands on different gateway => resume OK

### v1.4.0 — Session fleet ops (allocator + drain safety)
Status: planned
- allocator chooses session server
- drain/graceful shutdown
- e2e verifies rolling behavior is deterministic

### v1.5.0 — Observability standard (OpenTelemetry mandatory)
Status: planned
- OTel tracing + correlation
- dashboards/alerts committed as text
- tests verify /metrics + trace propagation

### v1.6.0 — Security/Access/Compliance
Status: planned
- RBAC for ops
- audit logs
- PII masking tests

### v1.7.0 — Reliability engineering
Status: planned
- circuit breaker + retry budget + graceful degrade
- chaos scripts + reproducible report

### v1.8.0 — Optional N-player scaling
Status: optional
- interest management + delta/snapshot + hard limits
- load harness + perf report

### v2.0.0 — Multi-region + DR
Status: planned
- region routing + failover
- DR runbook + recovery drill report (RTO/RPO)
