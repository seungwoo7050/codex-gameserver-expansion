/*
 * 설명: 사용자 레이팅 계산과 리더보드 조회를 담당한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/rating_update_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
  RatingService();

  void EnsureUser(int user_id, const std::string& username);
  RatingSummary ApplyMatchResult(int winner_id, int loser_id);
  std::optional<RatingSummary> GetSummary(int user_id);
  LeaderboardPage GetLeaderboard(std::size_t page, std::size_t size);

 private:
  struct Entry {
    std::string username;
    int rating;
    int wins;
    int losses;
  };

  double ExpectedScore(int rating_a, int rating_b) const;
  int ApplyElo(int rating, double expected, double score) const;

  std::unordered_map<int, Entry> entries_;
  mutable std::mutex mutex_;
  const int k_factor_ = 32;
  const int initial_rating_ = 1000;
};

}  // namespace server
