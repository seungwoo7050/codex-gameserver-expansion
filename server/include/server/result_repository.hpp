/*
 * 설명: 세션 종료 결과를 idempotent하게 저장하는 저장소를 정의한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace server {

struct MatchResultRecord {
  std::string session_id;
  int user1_id;
  int user2_id;
  int winner_user_id;
  int tick_count;
  std::chrono::system_clock::time_point ended_at;
  nlohmann::json snapshot;
};

class ResultRepository {
 public:
 bool SaveIfAbsent(const MatchResultRecord& record);
  bool Exists(const std::string& session_id) const;
  std::size_t Count() const;
  std::optional<MatchResultRecord> Find(const std::string& session_id) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, MatchResultRecord> records_;
};

}  // namespace server
