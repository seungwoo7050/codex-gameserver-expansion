/*
 * 설명: Elo 기반 레이팅 연산과 리더보드 페이지네이션을 수행한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/rating_update_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/rating.hpp"

#include <algorithm>
#include <cmath>

namespace server {

RatingService::RatingService() = default;

void RatingService::EnsureUser(int user_id, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(user_id);
  if (it == entries_.end()) {
    entries_.emplace(user_id, Entry{username, initial_rating_, 0, 0});
    return;
  }
  if (!username.empty()) {
    it->second.username = username;
  }
}

RatingSummary RatingService::ApplyMatchResult(int winner_id, int loser_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto ensure = [this](int user_id) -> Entry& {
    auto it = entries_.find(user_id);
    if (it == entries_.end()) {
      auto inserted = entries_.emplace(user_id, Entry{"", initial_rating_, 0, 0});
      return inserted.first->second;
    }
    return it->second;
  };

  Entry& winner = ensure(winner_id);
  Entry& loser = ensure(loser_id);

  double expected_winner = ExpectedScore(winner.rating, loser.rating);
  double expected_loser = ExpectedScore(loser.rating, winner.rating);

  winner.rating = ApplyElo(winner.rating, expected_winner, 1.0);
  loser.rating = ApplyElo(loser.rating, expected_loser, 0.0);
  winner.wins += 1;
  loser.losses += 1;

  return RatingSummary{winner_id, winner.username, winner.rating, winner.wins, winner.losses};
}

std::optional<RatingSummary> RatingService::GetSummary(int user_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(user_id);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  return RatingSummary{user_id, it->second.username, it->second.rating, it->second.wins, it->second.losses};
}

LeaderboardPage RatingService::GetLeaderboard(std::size_t page, std::size_t size) {
  std::vector<std::pair<int, Entry>> items;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : entries_) {
      items.emplace_back(kv.first, kv.second);
    }
  }
  std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
    if (a.second.rating == b.second.rating) {
      return a.first < b.first;
    }
    return a.second.rating > b.second.rating;
  });

  std::size_t total = items.size();
  std::size_t start = (page - 1) * size;
  std::size_t end = std::min(start + size, total);
  std::vector<RatingSummary> summaries;
  if (start < total) {
    summaries.reserve(end - start);
    for (std::size_t i = start; i < end; ++i) {
      summaries.push_back(RatingSummary{items[i].first, items[i].second.username, items[i].second.rating,
                                        items[i].second.wins, items[i].second.losses});
    }
  }
  return LeaderboardPage{total, summaries};
}

double RatingService::ExpectedScore(int rating_a, int rating_b) const {
  double exponent = static_cast<double>(rating_b - rating_a) / 400.0;
  return 1.0 / (1.0 + std::pow(10.0, exponent));
}

int RatingService::ApplyElo(int rating, double expected, double score) const {
  double delta = static_cast<double>(k_factor_) * (score - expected);
  int next = static_cast<int>(std::round(static_cast<double>(rating) + delta));
  return next;
}

}  // namespace server
