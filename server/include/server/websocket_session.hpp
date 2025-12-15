/*
 * 설명: WebSocket 연결의 메시지 처리, 백프레셔, 리싱크 및 세션 이벤트 전달을 관리한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp
 */
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include "server/api_response.hpp"
#include "server/auth.hpp"
#include "server/realtime.hpp"
#include "server/reconnect.hpp"
#include "server/session_manager.hpp"

namespace server {

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  WebSocketSession(boost::beast::websocket::stream<boost::beast::tcp_stream> ws, const AuthSession& session,
                   std::shared_ptr<ReconnectService> reconnect_service,
                   std::shared_ptr<RealtimeCoordinator> coordinator,
                   std::shared_ptr<SessionManager> session_manager, std::size_t max_queue_messages,
                   std::size_t max_queue_bytes);
  ~WebSocketSession();
  void Run();

  void SendServerEvent(const std::string& event, const nlohmann::json& payload);
  void SendServerError(const std::string& code, const std::string& message);

 private:
  void DoRead();
  void OnRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void SendEcho(const nlohmann::json& message, std::uint64_t seq);
  void HandleResyncRequest(const nlohmann::json& message, std::uint64_t seq);
  void HandleSessionInput(const nlohmann::json& message, std::uint64_t seq);
  void SendError(std::string_view code, std::string_view message, std::uint64_t seq);
  void SendResyncState(std::uint64_t seq);
  void SendAuthState();
  void EnqueueMessage(std::string message);
  void WriteNext();
  void OnWrite(boost::beast::error_code ec);
  void TriggerBackpressureClose();
  nlohmann::json BuildSnapshot();
  std::string NowIsoString();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer buffer_;
  AuthSession session_;
  std::shared_ptr<ReconnectService> reconnect_service_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::shared_ptr<SessionManager> session_manager_;
  std::deque<std::string> send_queue_;
  std::size_t queued_bytes_{0};
  bool writing_{false};
  bool closing_{false};
  std::size_t max_queue_messages_;
  std::size_t max_queue_bytes_;
  std::string resume_token_;
  nlohmann::json snapshot_;
  int snapshot_version_{1};
};

}  // namespace server
