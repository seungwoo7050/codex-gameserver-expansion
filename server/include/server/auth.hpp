/*
 * 설명: 인증 상태, 사용자 저장, 토큰 및 레이트리밋 관리를 담당한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp
 */
#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <boost/asio/ip/address.hpp>

namespace server {

struct AuthUser {
  int user_id;
  std::string username;
};

struct AuthSession {
  std::string token;
  AuthUser user;
  std::chrono::system_clock::time_point expires_at;
};

struct AuthConfig {
  std::chrono::seconds token_ttl{std::chrono::seconds(3600)};
  std::chrono::seconds login_window{std::chrono::seconds(60)};
  std::size_t login_max_attempts{5};
};

class RateLimiter {
 public:
  RateLimiter(std::size_t max_attempts, std::chrono::seconds window);
  bool Allow(const std::string& key, std::chrono::system_clock::time_point now);

 private:
  struct Bucket {
    std::size_t count{0};
    std::chrono::system_clock::time_point window_start{};
  };
  std::unordered_map<std::string, Bucket> buckets_;
  std::size_t max_attempts_;
  std::chrono::seconds window_;
  std::mutex mutex_;
};

class AuthService {
 public:
  explicit AuthService(const AuthConfig& config);

  std::optional<AuthUser> RegisterUser(const std::string& username, const std::string& password,
                                       std::string& error_code, std::string& error_message);
  std::optional<AuthSession> Login(const std::string& username, const std::string& password,
                                   const std::string& ip,
                                   std::string& error_code, std::string& error_message);
  bool Logout(const std::string& token);
  std::optional<AuthSession> ValidateToken(const std::string& token);

  AuthConfig GetConfig() const { return config_; }

 private:
  struct UserRecord {
    int id;
    std::string username;
    std::string salt_hex;
    std::string hash_hex;
  };

  std::string GenerateSalt();
  std::string HashPassword(const std::string& password, const std::string& salt_hex);
  bool VerifyPassword(const std::string& password, const UserRecord& user);
  std::string GenerateToken();
  void CleanupExpired(std::chrono::system_clock::time_point now);

  AuthConfig config_;
  RateLimiter rate_limiter_;
  int next_user_id_{1};
  std::unordered_map<std::string, UserRecord> users_;
  std::unordered_map<std::string, AuthSession> sessions_;
};

}  // namespace server
