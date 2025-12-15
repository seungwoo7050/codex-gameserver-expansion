/*
 * 설명: 구조화 로그와 간단한 메트릭 카운터를 관리한다.
 * 버전: v1.0.0
 * 관련 문서: design/ops/v1.0.0-runbook.md, design/protocol/contract.md
 */
#pragma once

#include <atomic>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace server {

struct LogContext {
  std::string trace_id;
  std::optional<int> user_id;
  std::optional<std::string> session_id;
  std::string name;
  long latency_ms{0};
};

struct MetricsSnapshot {
  std::uint64_t request_total{0};
  std::uint64_t request_errors{0};
  std::uint64_t websocket_active{0};
  std::uint64_t active_sessions{0};
  std::uint64_t queue_length{0};
};

class Observability {
 public:
  std::string NextTraceId();
  void IncrementRequest();
  void IncrementError();
  void SetWebsocketActive(std::uint64_t count);
  MetricsSnapshot Snapshot(std::uint64_t active_sessions, std::uint64_t queue_length) const;
  void Log(const LogContext& ctx) const;

 private:
  std::atomic<std::uint64_t> request_total_{0};
  std::atomic<std::uint64_t> request_errors_{0};
  std::atomic<std::uint64_t> websocket_active_{0};
  std::atomic<std::uint64_t> trace_counter_{0};
};

}  // namespace server
