/*
 * 설명: 구조화 로그와 간단한 메트릭 카운터를 관리한다.
 * 버전: v1.0.0
 * 관련 문서: design/ops/v1.0.0-runbook.md, design/protocol/contract.md
 */
#include "server/observability.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace server {

std::string Observability::NextTraceId() {
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::ostringstream oss;
  oss << std::hex << now << "-" << trace_counter_.fetch_add(1);
  return oss.str();
}

void Observability::IncrementRequest() { request_total_.fetch_add(1); }

void Observability::IncrementError() { request_errors_.fetch_add(1); }

void Observability::SetWebsocketActive(std::uint64_t count) { websocket_active_.store(count); }

MetricsSnapshot Observability::Snapshot(std::uint64_t active_sessions, std::uint64_t queue_length) const {
  MetricsSnapshot snapshot;
  snapshot.request_total = request_total_.load();
  snapshot.request_errors = request_errors_.load();
  snapshot.websocket_active = websocket_active_.load();
  snapshot.active_sessions = active_sessions;
  snapshot.queue_length = queue_length;
  return snapshot;
}

void Observability::Log(const LogContext& ctx) const {
  nlohmann::json log_json;
  log_json["traceId"] = ctx.trace_id;
  log_json["eventName"] = ctx.name;
  log_json["latencyMs"] = ctx.latency_ms;
  if (ctx.user_id) {
    log_json["userId"] = *ctx.user_id;
  }
  if (ctx.session_id) {
    log_json["sessionId"] = *ctx.session_id;
  }
  std::cout << log_json.dump() << std::endl;
}

}  // namespace server
