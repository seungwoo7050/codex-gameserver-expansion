#!/usr/bin/env python3
"""
부하 하네스: N개의 WebSocket 연결을 동시에 생성하고 큐 진입→세션 시작→입력→종료까지 측정한다.
모든 출력은 표준출력 텍스트 형태로 제공하며, 바이너리를 생성하지 않는다.
"""
import argparse
import asyncio
import json
import math
import time
from dataclasses import dataclass, field
from typing import List

import httpx
import websockets


@dataclass
class ClientTiming:
    register_login: float = 0.0
    ws_connect: float = 0.0
    join_to_start: float = 0.0
    start_to_end: float = 0.0


@dataclass
class ClientResult:
    success: bool
    error: str = ""
    timings: ClientTiming = field(default_factory=ClientTiming)


@dataclass
class HarnessConfig:
    http_base: str
    ws_url: str
    clients: int
    queue_timeout: int
    session_timeout: float
    http_timeout: float
    ws_connect_timeout: float
    password: str
    user_prefix: str
    ramp_delay: float


class Metrics:
    def __init__(self) -> None:
        self.results: List[ClientResult] = []
        self._lock = asyncio.Lock()

    async def add(self, result: ClientResult) -> None:
        async with self._lock:
            self.results.append(result)

    def _percentile(self, values: List[float], p: float) -> float:
        if not values:
            return 0.0
        ordered = sorted(values)
        if len(ordered) == 1:
            return ordered[0]
        idx = (len(ordered) - 1) * p / 100.0
        lower = math.floor(idx)
        upper = math.ceil(idx)
        if lower == upper:
            return ordered[int(idx)]
        weight = idx - lower
        return ordered[lower] * (1 - weight) + ordered[upper] * weight

    def summary(self) -> str:
        success = [r for r in self.results if r.success]
        failures = [r for r in self.results if not r.success]
        join_to_start = [r.timings.join_to_start for r in success if r.timings.join_to_start > 0]
        start_to_end = [r.timings.start_to_end for r in success if r.timings.start_to_end > 0]
        lines = [
            f"총 요청: {len(self.results)}",
            f"성공: {len(success)}",  # noqa: E501 pylint: disable=line-too-long
            f"실패: {len(failures)}",
            f"가입/로그인 평균: {self._avg([r.timings.register_login for r in success]):.3f}s",
            f"WS 핸드셰이크 평균: {self._avg([r.timings.ws_connect for r in success]):.3f}s",
            f"큐→시작 p50/p95: {self._p50_p95(join_to_start)}",
            f"시작→종료 p50/p95: {self._p50_p95(start_to_end)}",
        ]
        if failures:
            errors = {}
            for item in failures:
                errors[item.error] = errors.get(item.error, 0) + 1
            lines.append(f"실패 코드: {errors}")
        return "\n".join(lines)

    def _avg(self, values: List[float]) -> float:
        if not values:
            return 0.0
        return sum(values) / len(values)

    def _p50_p95(self, values: List[float]) -> str:
        if not values:
            return "n/a"
        return f"{self._percentile(values, 50):.3f}s / {self._percentile(values, 95):.3f}s"


async def run_client(idx: int, config: HarnessConfig, metrics: Metrics) -> None:
    timing = ClientTiming()
    username = f"{config.user_prefix}-{idx}-{int(time.time() * 1000)}"
    try:
        if config.ramp_delay > 0:
            await asyncio.sleep(config.ramp_delay * idx)
        async with httpx.AsyncClient(base_url=config.http_base, timeout=config.http_timeout) as client:
            start = time.perf_counter()
            await client.post("/api/auth/register", json={"username": username, "password": config.password})
            login_res = await client.post(
                "/api/auth/login", json={"username": username, "password": config.password}
            )
            if login_res.status_code != 200:
                raise RuntimeError(f"login_failed_{login_res.status_code}")
            timing.register_login = time.perf_counter() - start
            token = login_res.json()["data"]["token"]

            ws_start = time.perf_counter()
            async with websockets.connect(
                config.ws_url,
                additional_headers={"Authorization": f"Bearer {token}"},
                open_timeout=config.ws_connect_timeout,
            ) as ws:
                await asyncio.wait_for(ws.recv(), timeout=config.ws_connect_timeout)
                timing.ws_connect = time.perf_counter() - ws_start

                join_started = time.perf_counter()
                join_res = await client.post(
                    "/api/queue/join",
                    headers={"Authorization": f"Bearer {token}"},
                    json={"mode": "normal", "timeoutSeconds": config.queue_timeout},
                )
                if join_res.status_code != 200:
                    raise RuntimeError(f"join_failed_{join_res.status_code}")

                session_id = None
                started = False
                while True:
                    raw = await asyncio.wait_for(ws.recv(), timeout=config.session_timeout)
                    msg = json.loads(raw)
                    if msg.get("event") == "session.created":
                        session_id = msg.get("p", {}).get("sessionId", session_id)
                    if msg.get("event") == "session.started":
                        started = True
                        session_id = msg.get("p", {}).get("sessionId", session_id)
                        timing.join_to_start = time.perf_counter() - join_started
                        target_tick = max(1, msg.get("p", {}).get("tick", 0) + 1)
                        await ws.send(
                            json.dumps(
                                {
                                    "t": "event",
                                    "seq": 1,
                                    "event": "session.input",
                                    "p": {
                                        "sessionId": session_id or "",
                                        "sequence": 1,
                                        "targetTick": target_tick,
                                        "delta": 1,
                                    },
                                }
                            )
                        )
                    if msg.get("event") == "session.ended" and started:
                        timing.start_to_end = time.perf_counter() - (join_started + timing.join_to_start)
                        await metrics.add(ClientResult(success=True, timings=timing))
                        return
                    if msg.get("t") == "error":
                        raise RuntimeError(msg.get("p", {}).get("code", "error"))
    except Exception as exc:  # noqa: BLE001
        await metrics.add(ClientResult(success=False, error=str(exc), timings=timing))


async def run_harness(config: HarnessConfig) -> None:
    metrics = Metrics()
    tasks = [run_client(i, config, metrics) for i in range(config.clients)]
    await asyncio.gather(*tasks)
    print(metrics.summary())


def parse_args() -> HarnessConfig:
    parser = argparse.ArgumentParser(description="v0.8.0 WebSocket 부하 하네스")
    parser.add_argument("--http-base", default="http://127.0.0.1:8080", help="HTTP 베이스 URL")
    parser.add_argument("--ws-url", default="ws://127.0.0.1:8080/ws", help="WebSocket URL")
    parser.add_argument("--clients", type=int, default=10, help="동시 연결 수")
    parser.add_argument("--queue-timeout", type=int, default=5, help="큐 타임아웃(초)")
    parser.add_argument("--session-timeout", type=float, default=15.0, help="세션 종료 대기(초)")
    parser.add_argument("--http-timeout", type=float, default=5.0, help="HTTP 요청 타임아웃(초)")
    parser.add_argument("--ws-connect-timeout", type=float, default=5.0, help="WS 핸드셰이크 타임아웃(초)")
    parser.add_argument("--password", default="perfpass", help="테스트 계정 패스워드")
    parser.add_argument("--user-prefix", default="perf", help="테스트 계정 이름 접두사")
    parser.add_argument("--ramp-delay", type=float, default=0.05, help="클라이언트 순차 지연(초 단위 계단)")
    args = parser.parse_args()
    return HarnessConfig(
        http_base=args.http_base,
        ws_url=args.ws_url,
        clients=args.clients,
        queue_timeout=args.queue_timeout,
        session_timeout=args.session_timeout,
        http_timeout=args.http_timeout,
        ws_connect_timeout=args.ws_connect_timeout,
        password=args.password,
        user_prefix=args.user_prefix,
        ramp_delay=args.ramp_delay,
    )


if __name__ == "__main__":
    asyncio.run(run_harness(parse_args()))
