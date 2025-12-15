# CODING_GUIDE.md
Coding rules for this repository (C++17).

This file is written in English for AI/tooling.
All generated docs and code comments MUST be Korean.

---

## 1) Global rules

### 1.1 Language
- Identifiers: English.
- Comments: Korean only.

### 1.2 No binaries in repo
- Do not commit non-text files.
- Build outputs must remain local/CI artifacts only.

### 1.3 Required file header comment (Korean)
For significant .hpp/.cpp files add:
- 설명(역할)
- 버전: vX.Y.Z
- 관련 문서 경로(있으면)
- 테스트 경로(있으면)

Example:
/*
 * 설명: 세션 상태와 틱 업데이트를 관리한다.
 * 버전: v0.5.0
 * 관련 문서: design/server/v0.5.0-session.md
 * 테스트: server/tests/e2e/session_flow_test.cpp
 */

---

## 2) C++17 rules
- Standard: C++17 only.
- Prefer RAII, avoid owning raw pointers.
- Avoid exceptions crossing async boundaries.
- Never block I/O threads with DB/Redis calls.

---

## 3) Contract and external interfaces

### 3.1 Canonical contract doc
- `design/protocol/contract.md` (Korean) is the source of truth.

### 3.2 Contract compliance tests (required)
When you add/modify an endpoint or WS event, add a test that checks:
- REST response envelope keys exist and types match
- WS message envelope keys exist and types match
- event name is exactly the documented token
- essential payload fields exist and are type-correct

You do not need full JSON Schema tooling; field-level assertions are acceptable.

---

## 4) Testing structure (recommended)

- Unit tests:
  - server/tests/unit/**
  - Focus: simulation transitions, rating calculation, queue pairing logic

- Integration/E2E tests:
  - server/tests/e2e/**
  - Start a server instance on an ephemeral port (or reuse a test runner that boots the server in-process).
  - Use HTTP client + WS client to run real flows.

Minimum E2E expectations by version:
- v0.1.0: /health + ws echo
- v0.2.0: register/login + ws auth association
- v0.5.0+: queue→match→session start/end + DB result verification

---

## 5) Concurrency rules
- Per-session state must be updated in a serialized execution context (strand/mailbox).
- Per-connection outbound queue must have a hard limit.
- Define explicit policies for:
  - slow consumers
  - reconnect/resync
  - timeouts

---

## 6) Persistence rules
- MariaDB:
  - prepared statements
  - explicit transactions where needed
- Redis:
  - clear prefixes and TTL rules
- Idempotency:
  - finalization of match result must not double-apply rating or double-insert results.

---

## 7) Observability rules
- Structured logs must include traceId and (when available) userId/sessionId.
- Metrics endpoint must not leak secrets or sensitive data.
