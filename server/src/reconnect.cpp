/*
 * 설명: 재연결 토큰을 생성하고 사용자별 스냅샷을 저장한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/reconnect_backpressure_test.cpp
 */
#include "server/reconnect.hpp"

#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace server {
namespace {
std::string BytesToHex(const std::vector<unsigned char>& data) {
  std::ostringstream oss;
  for (unsigned char byte : data) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
  }
  return oss.str();
}
}

std::string ReconnectService::GenerateToken() {
  std::vector<unsigned char> buffer(16);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& b : buffer) {
    b = static_cast<unsigned char>(dist(gen));
  }
  return BytesToHex(buffer);
}

std::string ReconnectService::IssueToken(const AuthUser& user, int snapshot_version, const nlohmann::json& snapshot,
                                         const std::optional<std::string>& previous_token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (previous_token) {
    tokens_.erase(*previous_token);
  }
  auto token = GenerateToken();
  ResumeSnapshot record{user, token, snapshot_version, snapshot, std::chrono::system_clock::now()};
  tokens_[token] = record;
  return token;
}

std::optional<ResumeSnapshot> ReconnectService::Validate(const std::string& token, const AuthUser& user) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tokens_.find(token);
  if (it == tokens_.end()) {
    return std::nullopt;
  }
  if (it->second.user.user_id != user.user_id) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace server
