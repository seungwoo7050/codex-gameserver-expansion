/*
 * 설명: Redis 큐를 모사한 매칭 대기열을 관리하며 페어링/타임아웃/취소를 처리한다.
 * 버전: v0.8.0
 * 관련 문서: design/server/v0.5.0-match-session.md
 * 테스트: server/tests/e2e/session_flow_test.cpp
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "server/auth.hpp"
#include "server/realtime.hpp"
#include "server/session_manager.hpp"

namespace server {

class MatchQueueService : public std::enable_shared_from_this<MatchQueueService> {
 public:
  MatchQueueService(boost::asio::io_context& ioc, std::shared_ptr<SessionManager> session_manager,
                    std::shared_ptr<RealtimeCoordinator> coordinator, std::chrono::seconds default_timeout);

  bool Join(const AuthUser& user, std::chrono::seconds timeout, std::string& error_code, std::string& error_message);
  bool Cancel(int user_id, std::string& error_code, std::string& error_message);
  std::size_t QueueLength();

 private:
  struct QueueEntry {
    AuthUser user;
    std::chrono::steady_clock::time_point expires_at;
    std::chrono::steady_clock::time_point joined_at;
  };

  void EnsureTimer();
  void OnTick(const boost::system::error_code& ec);
  void PairIfPossible();
  void HandleTimeouts();

  boost::asio::steady_timer timer_;
  std::shared_ptr<SessionManager> session_manager_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::chrono::seconds default_timeout_;
  std::list<QueueEntry> queue_;
  std::unordered_map<int, std::list<QueueEntry>::iterator> user_index_;
  std::mutex mutex_;
  bool timer_active_{false};
};

}  // namespace server
