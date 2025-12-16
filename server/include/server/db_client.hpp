/*
 * 설명: MariaDB 연결과 트랜잭션 재시도 정책을 캡슐화한다.
 * 버전: v1.1.0
 * 관련 문서: POLICIES.md
 */
#pragma once

#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

#include <mariadb/mysql.h>

namespace server {

struct DbConfig {
  std::string host;
  unsigned short port;
  std::string user;
  std::string password;
  std::string database;
};

class DbException : public std::runtime_error {
 public:
  DbException(const std::string& message, unsigned int code, bool retryable)
      : std::runtime_error(message), code(code), retryable(retryable) {}
  unsigned int code;
  bool retryable;
};

class MariaDbClient {
 public:
  explicit MariaDbClient(const DbConfig& config);

  bool ExecuteTransactionWithRetry(const std::function<bool(MYSQL*)>& work) const;
  void WithConnectionRetry(const std::function<void(MYSQL*)>& work) const;

  [[noreturn]] void RaiseError(MYSQL* conn, const std::string& ctx) const;

  void SetTransientInjector(const std::function<bool(std::size_t)>& injector);

  std::string Escape(MYSQL* conn, const std::string& value) const;

 private:
  MYSQL* Connect() const;
  bool IsRetryable(unsigned int code) const;
  void Backoff(std::size_t attempt) const;

  DbConfig config_;
  unsigned int connect_timeout_seconds_ = 2;
  unsigned int query_timeout_seconds_ = 2;
  std::function<bool(std::size_t)> transient_injector_;
};

}  // namespace server
