/*
 * 설명: 매칭 큐 입장/취소/타임아웃을 관리하고 페어링 시 세션을 생성한다.
 * 버전: v0.8.0
 * 관련 문서: design/server/v0.5.0-match-session.md
 * 테스트: server/tests/e2e/session_flow_test.cpp
 */
#include "server/match_queue.hpp"

namespace server {

MatchQueueService::MatchQueueService(boost::asio::io_context& ioc, std::shared_ptr<SessionManager> session_manager,
                                     std::shared_ptr<RealtimeCoordinator> coordinator, std::chrono::seconds default_timeout)
    : timer_(ioc), session_manager_(std::move(session_manager)), coordinator_(std::move(coordinator)),
      default_timeout_(default_timeout) {}

bool MatchQueueService::Join(const AuthUser& user, std::chrono::seconds timeout, std::string& error_code,
                             std::string& error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (user_index_.count(user.user_id) > 0 || session_manager_->IsUserInSession(user.user_id)) {
    error_code = "queue_duplicate";
    error_message = "이미 큐에 있거나 세션에 참여 중입니다";
    return false;
  }
  if (timeout.count() <= 0) {
    timeout = default_timeout_;
  }
  auto now = std::chrono::steady_clock::now();
  queue_.push_back(QueueEntry{user, now + timeout, now});
  auto it = std::prev(queue_.end());
  user_index_[user.user_id] = it;
  EnsureTimer();
  return true;
}

bool MatchQueueService::Cancel(int user_id, std::string& error_code, std::string& error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = user_index_.find(user_id);
  if (it == user_index_.end()) {
    error_code = "queue_not_found";
    error_message = "대기열에 존재하지 않습니다";
    return false;
  }
  queue_.erase(it->second);
  user_index_.erase(it);
  return true;
}

void MatchQueueService::EnsureTimer() {
  if (timer_active_) {
    return;
  }
  timer_active_ = true;
  timer_.expires_after(std::chrono::seconds(1));
  auto self = shared_from_this();
  timer_.async_wait([self](const boost::system::error_code& ec) { self->OnTick(ec); });
}

void MatchQueueService::OnTick(const boost::system::error_code& ec) {
  if (ec) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HandleTimeouts();
    PairIfPossible();
  }
  timer_.expires_after(std::chrono::seconds(1));
  auto self = shared_from_this();
  timer_.async_wait([self](const boost::system::error_code& next_ec) { self->OnTick(next_ec); });
}

void MatchQueueService::PairIfPossible() {
  while (queue_.size() >= 2) {
    auto first_it = queue_.begin();
    auto first = *first_it;
    queue_.erase(first_it);
    auto second_it = queue_.begin();
    auto second = *second_it;
    queue_.erase(second_it);
    user_index_.erase(first.user.user_id);
    user_index_.erase(second.user.user_id);
    std::vector<SessionParticipant> participants{{first.user.user_id, first.user.username},
                                                 {second.user.user_id, second.user.username}};
    session_manager_->CreateSession(participants);
  }
}

void MatchQueueService::HandleTimeouts() {
  auto now = std::chrono::steady_clock::now();
  auto it = queue_.begin();
  while (it != queue_.end()) {
    if (it->expires_at > now) {
      ++it;
      continue;
    }
    int uid = it->user.user_id;
    user_index_.erase(uid);
    it = queue_.erase(it);
    coordinator_->SendErrorToUser(uid, "queue_timeout", "매칭 타임아웃이 발생했습니다");
  }
}

std::size_t MatchQueueService::QueueLength() {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

}  // namespace server
