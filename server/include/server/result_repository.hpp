/*
 * 설명: 세션 종료 결과를 MariaDB에 저장하고 중복을 DB 제약으로 차단한다.
 * 버전: v1.1.0
 * 관련 문서: design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <mariadb/mysql.h>

#include "server/db_client.hpp"

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
  explicit ResultRepository(std::shared_ptr<MariaDbClient> db_client);

  bool InsertMatchResult(MYSQL* conn, const MatchResultRecord& record);
  bool InsertRatingGuard(MYSQL* conn, const std::string& match_id, int user_id);

  std::size_t Count() const;
  std::optional<MatchResultRecord> Find(const std::string& session_id) const;
  void ClearAll() const;

 private:
  MatchResultRecord BuildRecord(MYSQL_ROW row) const;
  std::string ToTimestamp(const std::chrono::system_clock::time_point& tp) const;

  std::shared_ptr<MariaDbClient> db_client_;
};

}  // namespace server
