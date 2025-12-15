/*
 * 설명: 재연결 토큰 발급과 스냅샷 관리를 담당한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/reconnect_backpressure_test.cpp
 */
#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "server/auth.hpp"

namespace server {

struct ResumeSnapshot {
  AuthUser user;
  std::string token;
  int snapshot_version;
  nlohmann::json snapshot;
  std::chrono::system_clock::time_point issued_at;
};

class ReconnectService {
 public:
  ReconnectService() = default;

  std::string IssueToken(const AuthUser& user, int snapshot_version, const nlohmann::json& snapshot,
                         const std::optional<std::string>& previous_token);
  std::optional<ResumeSnapshot> Validate(const std::string& token, const AuthUser& user);

 private:
  std::string GenerateToken();

  std::mutex mutex_;
  std::unordered_map<std::string, ResumeSnapshot> tokens_;
};

}  // namespace server
