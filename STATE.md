# STATE.md
Single source of truth for: canonical commands + current target.

Seed doc for tooling. Keep it short and exact.

---

## 1) Current baseline
- Baseline tag (must exist): v1.0.0
- Archive folder (must exist): `archive/v1.0.0/`

---

## 2) Next target version
- Next target: v1.1.0

---

## 3) Canonical commands (ONLY these are “standard”)

### 3.1 Build
- Debug:
  - `./scripts/build.sh Debug`
- Release:
  - `./scripts/build.sh Release`

### 3.2 Unit tests
- `./scripts/test-unit.sh`

### 3.3 Integration tests (real DB/Redis via compose)
- `./scripts/test-it.sh`

### 3.4 E2E tests (fixed multi-instance topology)
- `./scripts/test-e2e.sh`

### 3.5 Clean (optional, but standardized)
- `./scripts/clean.sh`

If any of the above scripts are missing, they must be created BEFORE further development.
If their behavior changes, update this file in the same change set.

---

## 4) Canonical local topologies (file names are fixed)
- Integration dependencies only:
  - `infra/compose/it.yml`
- E2E full topology:
  - `infra/compose/e2e.yml`

Developers and CI must use these filenames.
Do not create alternative compose entrypoints unless VERSIONING explicitly requires it.

---

## 5) Minimal environment assumptions (do not over-specify)
Required tools (typical):
- CMake + a C++17 compiler
- Docker + Docker Compose v2

Exact versions are not enforced here; compatibility issues should be handled by scripts (clear error output).

---

## 6) Invariants
- v1.0 baseline regression stays green unless VERSIONING explicitly allows breaking.
- v1.3+ internal RPC = gRPC (mandatory).
- v1.5+ tracing = OpenTelemetry (mandatory).
