/*
 * 설명: 서버 전체 수명주기를 관리한다.
 * 버전: v1.1.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include "server/auth.hpp"
#include "server/config.hpp"
#include "server/db_client.hpp"
#include "server/match_queue.hpp"
#include "server/observability.hpp"
#include "server/realtime.hpp"
#include "server/reconnect.hpp"
#include "server/result_service.hpp"
#include "server/session_manager.hpp"
#include "server/rating.hpp"

namespace server {

class Listener;

class ServerApp {
 public:
  explicit ServerApp(const AppConfig& config);
  ~ServerApp();

  void Run();
  void Stop();

  boost::asio::io_context& GetContext() { return ioc_; }
  const AppConfig& GetConfig() const { return config_; }
  std::shared_ptr<AuthService> GetAuthService() { return auth_service_; }
  std::shared_ptr<ReconnectService> GetReconnectService() { return reconnect_service_; }
  std::shared_ptr<SessionManager> GetSessionManager() { return session_manager_; }
  std::shared_ptr<MatchQueueService> GetMatchQueue() { return match_queue_; }
  std::shared_ptr<RatingService> GetRatingService() { return rating_service_; }
  std::shared_ptr<ResultService> GetResultService() { return result_service_; }
  std::shared_ptr<Observability> GetObservability() { return observability_; }
  std::size_t DebugResultCount() const { return result_service_->Count(); }

 private:
  void RunWorkers();

  AppConfig config_;
  boost::asio::io_context ioc_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
  std::shared_ptr<Listener> listener_;
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<ReconnectService> reconnect_service_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::shared_ptr<Observability> observability_;
  std::shared_ptr<MariaDbClient> db_client_;
  std::shared_ptr<RatingService> rating_service_;
  std::shared_ptr<ResultRepository> result_repository_;
  std::shared_ptr<ResultService> result_service_;
  std::shared_ptr<SessionManager> session_manager_;
  std::shared_ptr<MatchQueueService> match_queue_;
  std::vector<std::thread> workers_;
  std::atomic<bool> running_{false};
};

}  // namespace server
