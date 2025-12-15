/*
 * 설명: 사용자별 WebSocket 세션을 관리하고 서버 이벤트를 안전하게 전달한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.5.0-match-session.md
 * 테스트: server/tests/e2e/session_flow_test.cpp
 */
#include "server/realtime.hpp"

#include "server/websocket_session.hpp"

namespace server {

void RealtimeCoordinator::Register(int user_id, const std::shared_ptr<WebSocketSession>& session) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_[user_id] = Entry{session, session.get()};
  if (observability_) {
    observability_->SetWebsocketActive(connections_.size());
  }
}

void RealtimeCoordinator::Unregister(int user_id, const WebSocketSession* session) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(user_id);
  if (it == connections_.end()) {
    return;
  }
  if (it->second.raw == session) {
    connections_.erase(it);
    if (observability_) {
      observability_->SetWebsocketActive(connections_.size());
    }
  }
}

void RealtimeCoordinator::SendEventToUser(int user_id, const std::string& event, const nlohmann::json& payload) {
  std::shared_ptr<WebSocketSession> session_ptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(user_id);
    if (it == connections_.end()) {
      return;
    }
    session_ptr = it->second.session.lock();
  }
  if (session_ptr) {
    session_ptr->SendServerEvent(event, payload);
  }
}

void RealtimeCoordinator::SendErrorToUser(int user_id, const std::string& code, const std::string& message) {
  std::shared_ptr<WebSocketSession> session_ptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(user_id);
    if (it == connections_.end()) {
      return;
    }
    session_ptr = it->second.session.lock();
  }
  if (session_ptr) {
    session_ptr->SendServerError(code, message);
  }
}

std::size_t RealtimeCoordinator::ActiveConnections() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size();
}

}  // namespace server
