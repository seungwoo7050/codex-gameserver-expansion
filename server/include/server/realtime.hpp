/*
 * 설명: WebSocket 연결을 사용자별로 관리하고 서버 측 이벤트 전달을 중계한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.5.0-match-session.md
 * 테스트: server/tests/e2e/session_flow_test.cpp
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "server/observability.hpp"

namespace server {

class WebSocketSession;

class RealtimeCoordinator : public std::enable_shared_from_this<RealtimeCoordinator> {
 public:
  void SetObservability(const std::shared_ptr<Observability>& observability) { observability_ = observability; }
  void Register(int user_id, const std::shared_ptr<WebSocketSession>& session);
  void Unregister(int user_id, const WebSocketSession* session);
  void SendEventToUser(int user_id, const std::string& event, const nlohmann::json& payload);
  void SendErrorToUser(int user_id, const std::string& code, const std::string& message);
  std::size_t ActiveConnections() const;

 private:
  struct Entry {
    std::weak_ptr<WebSocketSession> session;
    const WebSocketSession* raw{nullptr};
  };

  std::unordered_map<int, Entry> connections_;
  mutable std::mutex mutex_;
  std::shared_ptr<Observability> observability_;
};

}  // namespace server
