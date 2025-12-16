/*
 * 설명: MariaDB 기반 레이팅 연산과 리더보드를 제공한다.
 * 버전: v1.1.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/rating_update_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/rating.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include <mariadb/errmsg.h>

namespace server {
namespace {
int ToInt(const char* value) { return value ? std::stoi(value) : 0; }
}  // namespace

RatingService::RatingService(std::shared_ptr<MariaDbClient> db_client) : db_client_(std::move(db_client)) {}

void RatingService::EnsureUser(int user_id, const std::string& username) {
  db_client_->ExecuteTransactionWithRetry([&](MYSQL* conn) {
    EnsureUserInTx(conn, user_id, username);
    return true;
  });
}

void RatingService::EnsureUserInTx(MYSQL* conn, int user_id, const std::string& username) {
  std::ostringstream oss;
  std::string escaped = db_client_->Escape(conn, username);
  std::string username_expr = username.empty() ? "username" : "VALUES(username)";
  oss << "INSERT INTO ratings(user_id, username, rating, wins, losses, updated_at) VALUES (" << user_id << ", '"
      << escaped << "', " << initial_rating_ << ", 0, 0, NOW(6)) ON DUPLICATE KEY UPDATE username = " << username_expr
      << ", updated_at = NOW(6);";
  if (mysql_query(conn, oss.str().c_str()) != 0) {
    db_client_->RaiseError(conn, "사용자 보장 실패");
  }
}

RatingSummary RatingService::ApplyMatchResultInTx(MYSQL* conn, int winner_id, int loser_id) {
  EnsureUserInTx(conn, winner_id, "");
  EnsureUserInTx(conn, loser_id, "");
  return ApplyEloUpdate(conn, winner_id, loser_id);
}

RatingSummary RatingService::ApplyEloUpdate(MYSQL* conn, int winner_id, int loser_id) {
  std::ostringstream select;
  select << "SELECT user_id, username, rating, wins, losses FROM ratings WHERE user_id IN (" << winner_id << ","
         << loser_id << ") FOR UPDATE;";
  if (mysql_query(conn, select.str().c_str()) != 0) {
    db_client_->RaiseError(conn, "레이팅 조회 실패");
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    db_client_->RaiseError(conn, "레이팅 결과 비어 있음");
  }
  std::unordered_map<int, RatingSummary> current;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    int user_id = ToInt(row[0]);
    current[user_id] = BuildSummary(user_id, row);
  }
  mysql_free_result(res);
  auto winner_it = current.find(winner_id);
  auto loser_it = current.find(loser_id);
  if (winner_it == current.end() || loser_it == current.end()) {
    throw DbException("레이팅 조회 중 누락", 0, false);
  }

  double expected_winner = ExpectedScore(winner_it->second.rating, loser_it->second.rating);
  double expected_loser = ExpectedScore(loser_it->second.rating, winner_it->second.rating);

  int winner_next = ApplyElo(winner_it->second.rating, expected_winner, 1.0);
  int loser_next = ApplyElo(loser_it->second.rating, expected_loser, 0.0);

  std::ostringstream update_winner;
  update_winner << "UPDATE ratings SET rating=" << winner_next << ", wins=" << (winner_it->second.wins + 1)
                << ", updated_at=NOW(6) WHERE user_id=" << winner_id << ";";
  if (mysql_query(conn, update_winner.str().c_str()) != 0) {
    db_client_->RaiseError(conn, "승자 갱신 실패");
  }

  std::ostringstream update_loser;
  update_loser << "UPDATE ratings SET rating=" << loser_next << ", losses=" << (loser_it->second.losses + 1)
               << ", updated_at=NOW(6) WHERE user_id=" << loser_id << ";";
  if (mysql_query(conn, update_loser.str().c_str()) != 0) {
    db_client_->RaiseError(conn, "패자 갱신 실패");
  }

  return RatingSummary{winner_id, winner_it->second.username, winner_next, winner_it->second.wins + 1,
                       winner_it->second.losses};
}

std::optional<RatingSummary> RatingService::GetSummary(int user_id) {
  std::optional<RatingSummary> result;
  db_client_->WithConnectionRetry([&](MYSQL* conn) {
    std::ostringstream oss;
    oss << "SELECT user_id, username, rating, wins, losses FROM ratings WHERE user_id=" << user_id << ";";
    if (mysql_query(conn, oss.str().c_str()) != 0) {
      db_client_->RaiseError(conn, "프로필 조회 실패");
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
      db_client_->RaiseError(conn, "프로필 결과 없음");
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
      result = BuildSummary(user_id, row);
    }
    mysql_free_result(res);
  });
  return result;
}

LeaderboardPage RatingService::GetLeaderboard(std::size_t page, std::size_t size) {
  LeaderboardPage page_data{0, {}};
  db_client_->WithConnectionRetry([&](MYSQL* conn) {
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM ratings;";
    if (mysql_query(conn, count_sql.str().c_str()) != 0) {
      db_client_->RaiseError(conn, "리더보드 카운트 실패");
    }
    MYSQL_RES* count_res = mysql_store_result(conn);
    if (!count_res) {
      db_client_->RaiseError(conn, "리더보드 카운트 결과 없음");
    }
    MYSQL_ROW count_row = mysql_fetch_row(count_res);
    page_data.total = count_row ? static_cast<std::size_t>(std::stoull(count_row[0])) : 0;
    mysql_free_result(count_res);

    std::size_t offset = (page - 1) * size;
    std::ostringstream query_sql;
    query_sql << "SELECT user_id, username, rating, wins, losses FROM ratings ORDER BY rating DESC, user_id ASC LIMIT "
              << size << " OFFSET " << offset << ";";
    if (mysql_query(conn, query_sql.str().c_str()) != 0) {
      db_client_->RaiseError(conn, "리더보드 조회 실패");
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
      db_client_->RaiseError(conn, "리더보드 결과 없음");
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
      page_data.entries.push_back(BuildSummary(ToInt(row[0]), row));
    }
    mysql_free_result(res);
  });
  return page_data;
}

RatingSummary RatingService::BuildSummary(int user_id, MYSQL_ROW row) const {
  return RatingSummary{user_id, row[1] ? row[1] : "", ToInt(row[2]), ToInt(row[3]), ToInt(row[4])};
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
