/*
 * 설명: MariaDB에 저장된 사용자 레이팅을 조회/갱신한다.
 * 버전: v1.1.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/rating_update_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mariadb/mysql.h>

#include "server/db_client.hpp"

namespace server {

struct RatingSummary {
  int user_id;
  std::string username;
  int rating;
  int wins;
  int losses;
  int matches() const { return wins + losses; }
};

struct LeaderboardPage {
  std::size_t total;
  std::vector<RatingSummary> entries;
};

class RatingService {
 public:
  explicit RatingService(std::shared_ptr<MariaDbClient> db_client);

  void EnsureUser(int user_id, const std::string& username);
  void EnsureUserInTx(MYSQL* conn, int user_id, const std::string& username);
  RatingSummary ApplyMatchResultInTx(MYSQL* conn, int winner_id, int loser_id);
  std::optional<RatingSummary> GetSummary(int user_id);
  LeaderboardPage GetLeaderboard(std::size_t page, std::size_t size);

 private:
  RatingSummary ApplyEloUpdate(MYSQL* conn, int winner_id, int loser_id);
  RatingSummary BuildSummary(int user_id, MYSQL_ROW row) const;
  double ExpectedScore(int rating_a, int rating_b) const;
  int ApplyElo(int rating, double expected, double score) const;

  std::shared_ptr<MariaDbClient> db_client_;
  const int k_factor_ = 32;
  const int initial_rating_ = 1000;
};

}  // namespace server
