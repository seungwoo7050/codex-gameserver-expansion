/*
 * 설명: HTTP 연결을 처리하고 인증/큐/리더보드 엔드포인트 및 WS 업그레이드를 제공한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "server/api_response.hpp"
#include "server/auth.hpp"
#include "server/config.hpp"
#include "server/match_queue.hpp"
#include "server/observability.hpp"
#include "server/realtime.hpp"
#include "server/reconnect.hpp"
#include "server/rating.hpp"
#include "server/session_manager.hpp"
#include "server/websocket_session.hpp"

namespace server {

 class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(boost::asio::ip::tcp::socket socket, const AppConfig& config,
              std::shared_ptr<AuthService> auth_service,
              std::shared_ptr<ReconnectService> reconnect_service,
              std::shared_ptr<RealtimeCoordinator> coordinator,
              std::shared_ptr<SessionManager> session_manager,
              std::shared_ptr<MatchQueueService> match_queue,
              std::shared_ptr<RatingService> rating_service,
              std::shared_ptr<Observability> observability);
  void Run();

 private:
  void DoRead();
  void OnRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void HandleRequest();
  void SendResponse(std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> res);
  void HandleWebSocket();
  std::optional<AuthSession> ExtractAuthSession();
  std::string RemoteIp();
  std::string ParseBearer(const std::string& header_value);

  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  AppConfig config_;
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<ReconnectService> reconnect_service_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::shared_ptr<SessionManager> session_manager_;
  std::shared_ptr<MatchQueueService> match_queue_;
  std::shared_ptr<RatingService> rating_service_;
  std::shared_ptr<Observability> observability_;
  std::chrono::steady_clock::time_point request_start_;
  std::string trace_id_;
};

}  // namespace server
