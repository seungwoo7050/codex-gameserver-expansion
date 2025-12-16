/*
 * 설명: MariaDB 연결과 재시도 로직을 구현한다.
 * 버전: v1.1.0
 * 관련 문서: POLICIES.md
 */
#include "server/db_client.hpp"

#include <chrono>
#include <thread>

#include <mariadb/errmsg.h>

namespace server {
namespace {
constexpr std::size_t kMaxAttempts = 3;
constexpr unsigned int kDeadlock = 1213;
constexpr unsigned int kLockWaitTimeout = 1205;
}  // namespace

MariaDbClient::MariaDbClient(const DbConfig& config) : config_(config) {}

MYSQL* MariaDbClient::Connect() const {
  MYSQL* conn = mysql_init(nullptr);
  if (!conn) {
    throw DbException("MariaDB 초기화 실패", 0, true);
  }
  mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout_seconds_);
  mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &query_timeout_seconds_);
  mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &query_timeout_seconds_);
  if (!mysql_real_connect(conn, config_.host.c_str(), config_.user.c_str(), config_.password.c_str(),
                          config_.database.c_str(), config_.port, nullptr, 0)) {
    RaiseError(conn, "연결 실패");
  }
  if (mysql_query(conn, "SET SESSION innodb_lock_wait_timeout=2;") != 0) {
    RaiseError(conn, "락 대기 타임아웃 설정 실패");
  }
  return conn;
}

bool MariaDbClient::ExecuteTransactionWithRetry(const std::function<bool(MYSQL*)>& work) const {
  for (std::size_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    MYSQL* conn = nullptr;
    try {
      conn = Connect();
      mysql_autocommit(conn, 0);
      if (transient_injector_ && transient_injector_(attempt)) {
        throw DbException("주입된 일시 오류", kDeadlock, true);
      }
      bool commit = work(conn);
      if (commit) {
        if (mysql_commit(conn) != 0) {
          RaiseError(conn, "커밋 실패");
        }
      } else {
        mysql_rollback(conn);
      }
      mysql_close(conn);
      return commit;
    } catch (const DbException& ex) {
      if (conn) {
        mysql_rollback(conn);
        mysql_close(conn);
      }
      if (ex.retryable && attempt < kMaxAttempts) {
        Backoff(attempt);
        continue;
      }
      throw;
    } catch (...) {
      if (conn) {
        mysql_rollback(conn);
        mysql_close(conn);
      }
      throw;
    }
  }
  return false;
}

void MariaDbClient::WithConnectionRetry(const std::function<void(MYSQL*)>& work) const {
  for (std::size_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    MYSQL* conn = nullptr;
    try {
      conn = Connect();
      if (transient_injector_ && transient_injector_(attempt)) {
        throw DbException("주입된 일시 오류", kDeadlock, true);
      }
      work(conn);
      mysql_close(conn);
      return;
    } catch (const DbException& ex) {
      if (conn) {
        mysql_close(conn);
      }
      if (ex.retryable && attempt < kMaxAttempts) {
        Backoff(attempt);
        continue;
      }
      throw;
    } catch (...) {
      if (conn) {
        mysql_close(conn);
      }
      throw;
    }
  }
}

std::string MariaDbClient::Escape(MYSQL* conn, const std::string& value) const {
  std::string escaped;
  escaped.resize(value.size() * 2 + 1);
  auto len = mysql_real_escape_string(conn, escaped.data(), value.c_str(), value.size());
  escaped.resize(len);
  return escaped;
}

void MariaDbClient::RaiseError(MYSQL* conn, const std::string& ctx) const {
  unsigned int code = mysql_errno(conn);
  bool retryable = IsRetryable(code);
  std::string message = ctx + ": " + mysql_error(conn);
  throw DbException(message, code, retryable);
}

bool MariaDbClient::IsRetryable(unsigned int code) const {
  return code == kDeadlock || code == kLockWaitTimeout || code == CR_SERVER_LOST || code == CR_SERVER_GONE_ERROR ||
         code == CR_CONN_HOST_ERROR || code == CR_SERVER_LOST_EXTENDED;
}

void MariaDbClient::Backoff(std::size_t attempt) const {
  std::size_t base_ms = 50 * (1u << (attempt - 1));
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 25);
  std::size_t delay_ms = base_ms + static_cast<std::size_t>(dist(gen));
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
}

void MariaDbClient::SetTransientInjector(const std::function<bool(std::size_t)>& injector) {
  transient_injector_ = injector;
}

}  // namespace server
