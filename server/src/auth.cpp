/*
 * 설명: 인증/계정 로직과 토큰/레이트리밋을 관리한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp
 */
#include "server/auth.hpp"

#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace server {

namespace {
std::string BytesToHex(const unsigned char* data, std::size_t len) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < len; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  }
  return oss.str();
}

bool HexToBytes(const std::string& hex, std::vector<unsigned char>& out) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  out.clear();
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    unsigned int byte;
    std::istringstream iss(hex.substr(i, 2));
    iss >> std::hex >> byte;
    if (iss.fail()) {
      return false;
    }
    out.push_back(static_cast<unsigned char>(byte));
  }
  return true;
}

std::string RandomHex(std::size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  RAND_bytes(buffer.data(), static_cast<int>(buffer.size()));
  return BytesToHex(buffer.data(), buffer.size());
}

std::mutex& AuthMutex() {
  static std::mutex mutex;
  return mutex;
}
}  // namespace

RateLimiter::RateLimiter(std::size_t max_attempts, std::chrono::seconds window)
    : max_attempts_(max_attempts), window_(window) {}

bool RateLimiter::Allow(const std::string& key, std::chrono::system_clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bucket = buckets_[key];
  if (bucket.window_start.time_since_epoch().count() == 0) {
    bucket.window_start = now;
  }
  auto elapsed = now - bucket.window_start;
  if (elapsed > window_) {
    bucket.window_start = now;
    bucket.count = 0;
  }
  if (bucket.count >= max_attempts_) {
    return false;
  }
  ++bucket.count;
  return true;
}

AuthService::AuthService(const AuthConfig& config)
    : config_(config), rate_limiter_(config.login_max_attempts, config.login_window) {}

std::optional<AuthUser> AuthService::RegisterUser(const std::string& username, const std::string& password,
                                                 std::string& error_code, std::string& error_message) {
  std::lock_guard<std::mutex> lock(AuthMutex());
  if (username.empty() || password.empty()) {
    error_code = "bad_request";
    error_message = "username과 password가 필요합니다";
    return std::nullopt;
  }
  if (users_.find(username) != users_.end()) {
    error_code = "duplicate_user";
    error_message = "이미 존재하는 사용자명입니다";
    return std::nullopt;
  }
  UserRecord rec;
  rec.id = next_user_id_++;
  rec.username = username;
  rec.salt_hex = GenerateSalt();
  rec.hash_hex = HashPassword(password, rec.salt_hex);
  users_.emplace(username, rec);
  return AuthUser{rec.id, rec.username};
}

std::optional<AuthSession> AuthService::Login(const std::string& username, const std::string& password,
                                             const std::string& ip,
                                             std::string& error_code, std::string& error_message) {
  auto now = std::chrono::system_clock::now();
  if (!rate_limiter_.Allow(ip, now)) {
    error_code = "rate_limited";
    error_message = "로그인 시도 제한을 초과했습니다";
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(AuthMutex());
  auto it = users_.find(username);
  if (it == users_.end()) {
    error_code = "unauthorized";
    error_message = "자격 증명이 올바르지 않습니다";
    return std::nullopt;
  }
  if (!VerifyPassword(password, it->second)) {
    error_code = "unauthorized";
    error_message = "자격 증명이 올바르지 않습니다";
    return std::nullopt;
  }
  CleanupExpired(now);
  AuthSession session;
  session.token = GenerateToken();
  session.user = AuthUser{it->second.id, it->second.username};
  session.expires_at = now + config_.token_ttl;
  sessions_[session.token] = session;
  return session;
}

bool AuthService::Logout(const std::string& token) {
  std::lock_guard<std::mutex> lock(AuthMutex());
  return sessions_.erase(token) > 0;
}

std::optional<AuthSession> AuthService::ValidateToken(const std::string& token) {
  auto now = std::chrono::system_clock::now();
  std::lock_guard<std::mutex> lock(AuthMutex());
  auto it = sessions_.find(token);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  if (now > it->second.expires_at) {
    sessions_.erase(it);
    return std::nullopt;
  }
  return it->second;
}

std::string AuthService::GenerateSalt() {
  return RandomHex(16);
}

std::string AuthService::HashPassword(const std::string& password, const std::string& salt_hex) {
  std::vector<unsigned char> salt;
  if (!HexToBytes(salt_hex, salt)) {
    return {};
  }
  std::vector<unsigned char> output(32);
  const int iterations = 100000;
  PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                    static_cast<int>(salt.size()), iterations, EVP_sha256(),
                    static_cast<int>(output.size()), output.data());
  return BytesToHex(output.data(), output.size());
}

bool AuthService::VerifyPassword(const std::string& password, const UserRecord& user) {
  auto computed = HashPassword(password, user.salt_hex);
  if (computed.size() != user.hash_hex.size()) {
    return false;
  }
  return CRYPTO_memcmp(computed.data(), user.hash_hex.data(), computed.size()) == 0;
}

std::string AuthService::GenerateToken() { return RandomHex(32); }

void AuthService::CleanupExpired(std::chrono::system_clock::time_point now) {
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (now > it->second.expires_at) {
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace server
