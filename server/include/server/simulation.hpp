/*
 * 설명: 틱 기반 시뮬레이션 상태와 입력 처리 루프를 제공한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.3.0-tick-loop.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/simulation_determinism_test.cpp
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace server {

struct InputCommand {
  int user_id{0};
  int target_tick{0};
  int delta{0};
  std::uint64_t sequence{0};
};

struct ValidationResult {
  bool accepted{false};
  std::string reason;
};

struct PlayerState {
  int position{0};
  std::uint64_t last_sequence{0};
};

class Simulation {
 public:
  static constexpr int kTickRate = 60;
  static constexpr std::chrono::nanoseconds kTickInterval{
      std::chrono::nanoseconds(1'000'000'000 / kTickRate)};
  static constexpr int kMaxInputsPerTickPerUser = 4;
  static constexpr int kMaxDelta = 3;

  ValidationResult EnqueueInput(const InputCommand& input);
  void AddPlayer(int user_id);
  void TickOnce();
  void RunForDuration(std::chrono::milliseconds duration);

  int CurrentTick() const { return current_tick_; }
  nlohmann::json Snapshot() const;

 private:
  struct UserTracker {
    std::uint64_t last_sequence{0};
    std::unordered_map<int, int> per_tick_count;
  };

  bool ValidateInput(const InputCommand& input, std::string& reason) const;
  void ApplyEvent(const InputCommand& input);

  int current_tick_{0};
  std::map<int, std::vector<InputCommand>> inputs_by_tick_;
  std::unordered_map<int, UserTracker> trackers_;
  std::unordered_map<int, PlayerState> players_;
};

}  // namespace server
