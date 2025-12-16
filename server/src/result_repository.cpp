/*
 * 설명: 세션 결과와 레이팅 가드 정보를 MariaDB에 저장한다.
 * 버전: v1.1.0
 * 관련 문서: design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/result_repository.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

#include <mariadb/errmsg.h>

namespace server {
namespace {
int ToInt(const char* value) { return value ? std::stoi(value) : 0; }
std::chrono::system_clock::time_point ParseTimestamp(const std::string& text) {
  std::tm tm{};
  std::istringstream iss(text);
  iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  return tp;
}
constexpr unsigned int kDuplicateEntry = 1062;
}  // namespace

ResultRepository::ResultRepository(std::shared_ptr<MariaDbClient> db_client) : db_client_(std::move(db_client)) {}

bool ResultRepository::InsertMatchResult(MYSQL* conn, const MatchResultRecord& record) {
  std::ostringstream oss;
  std::string escaped_id = db_client_->Escape(conn, record.session_id);
  std::string escaped_snapshot = db_client_->Escape(conn, record.snapshot.dump());
  oss << "INSERT INTO match_results(match_id, user1_id, user2_id, winner_user_id, tick_count, ended_at, snapshot) VALUES('"
      << escaped_id << "', " << record.user1_id << ", " << record.user2_id << ", " << record.winner_user_id << ", "
      << record.tick_count << ", '" << ToTimestamp(record.ended_at) << "', '" << escaped_snapshot << "');";
  if (mysql_query(conn, oss.str().c_str()) != 0) {
    if (mysql_errno(conn) == kDuplicateEntry) {
      return false;
    }
    db_client_->RaiseError(conn, "결과 저장 실패");
  }
  return true;
}

bool ResultRepository::InsertRatingGuard(MYSQL* conn, const std::string& match_id, int user_id) {
  std::ostringstream oss;
  oss << "INSERT INTO rating_applies(match_id, user_id, applied_at) VALUES('" << db_client_->Escape(conn, match_id)
      << "', " << user_id << ", NOW(6));";
  if (mysql_query(conn, oss.str().c_str()) != 0) {
    if (mysql_errno(conn) == kDuplicateEntry) {
      return false;
    }
    db_client_->RaiseError(conn, "레이팅 가드 저장 실패");
  }
  return true;
}

std::size_t ResultRepository::Count() const {
  std::size_t count = 0;
  db_client_->WithConnectionRetry([&](MYSQL* conn) {
    const char* sql = "SELECT COUNT(*) FROM match_results;";
    if (mysql_query(conn, sql) != 0) {
      db_client_->RaiseError(conn, "결과 카운트 실패");
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
      db_client_->RaiseError(conn, "카운트 결과 없음");
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && row[0]) {
      count = static_cast<std::size_t>(std::stoull(row[0]));
    }
    mysql_free_result(res);
  });
  return count;
}

std::optional<MatchResultRecord> ResultRepository::Find(const std::string& session_id) const {
  std::optional<MatchResultRecord> result;
  db_client_->WithConnectionRetry([&](MYSQL* conn) {
    std::ostringstream oss;
    oss << "SELECT match_id, user1_id, user2_id, winner_user_id, tick_count, ended_at, snapshot FROM match_results WHERE match_id='"
        << db_client_->Escape(conn, session_id) << "';";
    if (mysql_query(conn, oss.str().c_str()) != 0) {
      db_client_->RaiseError(conn, "결과 조회 실패");
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
      db_client_->RaiseError(conn, "결과 결과 없음");
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
      result = BuildRecord(row);
    }
    mysql_free_result(res);
  });
  return result;
}

void ResultRepository::ClearAll() const {
  db_client_->WithConnectionRetry([&](MYSQL* conn) {
    mysql_query(conn, "DELETE FROM ratings;");
    mysql_query(conn, "DELETE FROM rating_applies;");
    mysql_query(conn, "DELETE FROM match_results;");
  });
}

MatchResultRecord ResultRepository::BuildRecord(MYSQL_ROW row) const {
  std::string match_id = row[0] ? row[0] : "";
  MatchResultRecord record{match_id,
                           ToInt(row[1]),
                           ToInt(row[2]),
                           ToInt(row[3]),
                           ToInt(row[4]),
                           ParseTimestamp(row[5] ? row[5] : "1970-01-01 00:00:00"),
                           nlohmann::json::parse(row[6] ? row[6] : "{}")};
  return record;
}

std::string ResultRepository::ToTimestamp(const std::chrono::system_clock::time_point& tp) const {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

}  // namespace server
