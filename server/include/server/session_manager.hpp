/*
 * 설명: 매칭된 세션의 생성, 입력 처리, 상태 전파, 종료 및 결과/레이팅 저장을 관리한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <nlohmann/json.hpp>

#include "server/realtime.hpp"
#include "server/result_service.hpp"
#include "server/simulation.hpp"

namespace server {

struct SessionParticipant {
  int user_id;
  std::string username;
};

struct SessionInput {
  std::string session_id;
  int user_id;
  std::uint64_t sequence;
  int target_tick;
  int delta;
};

class SessionManager : public std::enable_shared_from_this<SessionManager> {
 public:
  SessionManager(boost::asio::io_context& ioc, std::shared_ptr<RealtimeCoordinator> coordinator,
                 std::shared_ptr<ResultService> result_service, std::chrono::milliseconds tick_interval,
                 std::size_t max_ticks);

  std::string CreateSession(const std::vector<SessionParticipant>& participants);
  bool IsUserInSession(int user_id) const;
  bool SubmitInput(const SessionInput& input, std::string& error_code, std::string& error_message);
  std::size_t ResultCount() const { return result_service_->Count(); }
  std::size_t ActiveSessionCount() const;

 private:
  struct SessionContext : public std::enable_shared_from_this<SessionContext> {
    std::string id;
    std::vector<SessionParticipant> participants;
    std::unordered_map<int, SessionParticipant> participant_map;
    Simulation simulation;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;
    boost::asio::steady_timer timer;
    std::size_t tick_sent{0};
    bool ended{false};

    SessionContext(boost::asio::io_context& ioc, std::chrono::milliseconds interval)
        : strand(boost::asio::make_strand(ioc)), timer(ioc), tick_interval(interval) {}

    std::chrono::milliseconds tick_interval;
  };

  void StartSession(const std::shared_ptr<SessionContext>& ctx);
  void BroadcastToParticipants(const std::shared_ptr<SessionContext>& ctx, const std::string& event,
                               const nlohmann::json& payload);
  void ScheduleTick(const std::shared_ptr<SessionContext>& ctx);
  void HandleTick(const std::shared_ptr<SessionContext>& ctx);
  void FinishSession(const std::shared_ptr<SessionContext>& ctx);

  boost::asio::io_context& ioc_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::shared_ptr<ResultService> result_service_;
  std::chrono::milliseconds tick_interval_;
  std::size_t max_ticks_;
  std::size_t next_session_id_{1};
  std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions_;
  std::unordered_map<int, std::string> user_to_session_;
  mutable std::mutex mutex_;
};

}  // namespace server
