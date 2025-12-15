/*
 * 설명: 세션 결과를 메모리에 저장하며 session_id로 idempotent 보관을 보장한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/result_repository.hpp"

namespace server {

bool ResultRepository::SaveIfAbsent(const MatchResultRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (records_.count(record.session_id) > 0) {
    return false;
  }
  records_.emplace(record.session_id, record);
  return true;
}

bool ResultRepository::Exists(const std::string& session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.count(session_id) > 0;
}

std::size_t ResultRepository::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

std::optional<MatchResultRecord> ResultRepository::Find(const std::string& session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(session_id);
  if (it == records_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace server
