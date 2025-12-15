# TEST_STRATEGY.md
Test gates with fixed runners and fixed topology.

Seed doc for tooling. Generated docs and code comments MUST be Korean.

---

## 1) Canonical runners (LOCKED)
All tests MUST be runnable via these scripts (STATE.md is the only command source):
- `./scripts/test-unit.sh`
- `./scripts/test-it.sh`
- `./scripts/test-e2e.sh`

CI must call the same scripts. No duplicate “CI-only commands”.

---

## 2) Canonical compose files (LOCKED)
- Integration deps:
  - `infra/compose/it.yml`
- E2E topology:
  - `infra/compose/e2e.yml`

E2E tests must target LB:
- base URL: `http://localhost:8080`

---

## 3) CTest labeling (RECOMMENDED, but fixed if used)
If using CTest labels, use exactly:
- `unit`
- `it`
- `e2e`
- `baseline` (regression suite that must always pass)

Scripts may implement labels via `ctest -L ...` internally.

---

## 4) Mandatory scenarios (minimum)
### Baseline (v1.0.0 regression)
- baseline E2E smoke (must always pass)

### v1.1.0 (DB)
- duplicate finalize => exactly-once effect
- concurrent finalize => exactly-once effect
- transient DB failures => bounded retries

### v1.2.0 (Redis)
- atomic join/cancel/timeout race correctness
- Redis down/latency => degrade matches POLICIES.md

### v1.3.0 (multi-instance)
- reconnect via lb lands on different gateway => resume OK
- session termination policy is deterministic (documented + tested)

---

## 5) Flakiness policy (STRICT)
If a test is flaky:
- treat as a failing test
- fix determinism (timeouts, waits, ordering, random seeds)
Do not “increase sleeps” as the primary fix.

---

## 6) E2E harness contract (fixed)
E2E runner must:
- exit non-zero on failure
- print minimal diagnostics (failed step + last error code + traceId if available)
- never require manual input
