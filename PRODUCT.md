# PRODUCT_SPEC.md

What to build (portfolio-oriented C++17 game server).

This file is written in English for AI/tooling.
All generated docs and code comments MUST be Korean.

---

## 1) Product goal (portfolio)
Demonstrate C++17 server capability in:
- async networking (HTTP + WS)
- safe concurrency model
- real-time tick loop
- backpressure and reconnect handling
- persistence and consistency (DB + Redis)
- observability (logs/metrics)
- reproducible tests + measurable performance

Not a feature-bloat project.

---

## 2) Functional scope (intentionally limited)

### Included (core)
- Accounts & auth (register/login/logout)
- Matchmaking queue (normal + rated)
- 2-participant session (authoritative server simulation)
- Result persistence
- Rating update + leaderboard query
- Operator endpoints (minimal) for basic inspection (later versions)

### Optional (only if scheduled in VERSIONING)
- Observer role (read-only WS consumer) to demonstrate fan-out/backpressure

### Explicitly excluded (unless VERSIONING adds later)
- friends graph
- chat
- tournaments
- replay/export pipelines
- OAuth providers
- distributed/multi-process architecture

---

## 3) Session model (neutral description)
A session has exactly 2 participants.
- Clients send input events.
- Server runs fixed-tick simulation and broadcasts state updates.
- Session ends by a deterministic condition (e.g. score threshold).
- Server stores the result.

Game visuals/theme are irrelevant; focus is server correctness.

---

## 4) Non-functional requirements (must be demonstrable)
- Determinism: same input stream => same outcome (within defined precision).
- Backpressure: slow clients must not exhaust memory or stall the server.
- Reconnect: define and implement a stable reconnect + resync policy.
- Abuse prevention: basic rate limiting for auth and control endpoints.
- Observability: logs/metrics sufficient to debug typical failures.