# STACK.md
Authoritative: repo shape + fixed topology + fixed technology choices.

Seed doc for tooling. Generated docs and code comments MUST be Korean.

---

## 1) Executables (fixed naming)
- v1.0: `gateway` (single binary)
- v1.3+: split binaries (names are fixed):
  - `gateway`
  - `matchmaking`
  - `session`

---

## 2) Internal communication (LOCKED)
- v1.3+ service-to-service RPC: gRPC + protobuf (mandatory)
  - proto location: `server/proto/*.proto`
  - generated stubs live under `server/libs/rpc/` (or equivalent)
- Public interfaces:
  - HTTP + WS via `gateway` only

No internal HTTP between services in v1.3+ unless VERSIONING explicitly allows.

---

## 3) Observability technology (LOCKED by version)
- v1.0~v1.4:
  - Prometheus metrics endpoint (`/metrics`) + structured logs
- v1.5+:
  - OpenTelemetry tracing is mandatory
  - trace propagation across all internal gRPC calls is mandatory

---

## 4) Canonical Docker Compose topology (FIXED)

### 4.1 Integration topology (dependencies only)
File: `infra/compose/it.yml`
Services (names fixed):
- `mariadb`  (exposed to host: 3306)
- `redis`    (exposed to host: 6379)

### 4.2 E2E topology (multi-instance)
File: `infra/compose/e2e.yml`
Services (names + counts fixed):
- `mariadb`  (3306)
- `redis`    (6379)
- `matchmaking1` (internal only)
- `session1`     (internal only)
- `session2`     (internal only)
- `gateway1`     (exposed: 18080 -> container 8080)
- `gateway2`     (exposed: 18081 -> container 8080)
- `lb`           (exposed: 8080 -> routes to gateway1/gateway2)

Client E2E always targets:
- HTTP/WS base: `http://localhost:8080` (lb)

Direct gateway ports (18080/18081) exist only for debugging; tests must use lb.

---

## 5) Fixed ports (inside containers)
- gateway public HTTP/WS: 8080
- gateway ops/admin (if present): 9090 (NOT exposed by default in e2e)
- matchmaking gRPC: 50051
- session gRPC: 50052

---

## 6) Repo layout (target)
- server/
  - apps/
    - gateway/
    - matchmaking/
    - session/
  - libs/
    - common/
    - net/
    - rpc/        (gRPC stubs/helpers)
  - proto/
  - tests/
    - unit/
    - it/
    - e2e/
- infra/
  - compose/
    - it.yml
    - e2e.yml
- scripts/         (canonical commands; see STATE.md)
- design/
  - protocol/contract.md (Korean, canonical public contract)
- reports/
- archive/

---

## 7) Dependency baseline (approved)
- Boost.Asio, Boost.Beast
- nlohmann/json
- spdlog + fmt
- GoogleTest
- MariaDB client library
- Redis client library
- (v1.3+) gRPC + protobuf
- (v1.5+) OpenTelemetry SDK

---

## 8) Config convention (fixed)
- Default config path (all apps):
  - `config/dev.yaml` for local runs
- Runtime overrides allowed via env vars, but env names must be documented in POLICIES.md.
