# AGENTS.md
AI coding agent rules for this repository (C++17 multiplayer server platform).

Seed docs may be English for tooling. However:
- All generated docs and all source code comments MUST be Korean.

---

## 0) Canonical seed truth (STRICT)
Agents MUST treat ONLY the following as seed truth:
- AGENTS.md
- STATE.md
- STACK.md
- PRODUCT.md
- POLICIES.md
- VERSIONING.md
- TEST_STRATEGY.md

Everything else is either:
- generated output, or
- archive reference, or
- implementation details.

---

## 1) Mandatory read order (STRICT)
1) AGENTS.md
2) STATE.md
3) VERSIONING.md (target version section only)
4) STACK.md + POLICIES.md + TEST_STRATEGY.md (relevant sections only)
5) PRODUCT.md (relevant sections only)
6) Only then read code under the touched modules.

Do NOT read the whole repo by default.

---

## 2) Non-negotiable constraints

### 2.1 One version per change set
A change set MUST target exactly one version in VERSIONING.md.

### 2.2 Canonical commands (NO invention)
All build/run/test MUST be executed via canonical scripts defined in STATE.md:
- `./scripts/build.sh`
- `./scripts/test-unit.sh`
- `./scripts/test-it.sh`
- `./scripts/test-e2e.sh`

Agents MUST NOT invent new ad-hoc commands as “the standard”.
If scripts must change, update STATE.md in the same change set.

### 2.3 Canonical topology (NO drift)
Docker Compose topology is canonical:
- `infra/compose/it.yml`
- `infra/compose/e2e.yml`

Service names, ports, and instance counts are defined in STACK.md.
Agents MUST NOT create alternative topologies unless VERSIONING explicitly requires it.

### 2.4 Technology choices (locked)
- v1.3+ internal service-to-service RPC is gRPC + protobuf (mandatory).
- v1.5+ tracing is OpenTelemetry (mandatory).

Changing these requires STACK.md + VERSIONING update in the same change set.

### 2.5 Contract-first (public behavior)
Canonical public interface is:
- `design/protocol/contract.md` (Korean)

If public behavior changes:
1) Update contract.md
2) Update/Write contract validation tests
3) Implement

### 2.6 “No tests, no done”
If behavior changes, tests MUST change.
Flaky tests are treated as failures. Version is NOT complete.

### 2.7 No binaries in repo
Never commit non-text artifacts (binaries, archives, DB dumps, images, etc.).

---

## 3) v1.0.0 archive policy (MANDATORY)
To prevent regressions and agent “guessing”:
- git tag `v1.0.0` MUST exist and never be rewritten.
- `archive/v1.0.0/**` MUST exist (text-only).
- Baseline regression suite MUST keep passing unless VERSIONING explicitly permits breaking changes.

When touching a v1.0 behavior:
- add/adjust a regression test that would catch accidental change.

---

## 4) Standard delivery checklist (MANDATORY)
- code + tests
- `reports/vX.Y.Z.md` (Korean: repro steps + results + metrics/risks)
- STATE.md updated (if commands/topology changed, which is rare)
- contract.md updated if public behavior changed
