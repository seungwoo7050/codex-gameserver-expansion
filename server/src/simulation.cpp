/*
 * 설명: 틱 기반 시뮬레이션 상태와 입력 큐를 처리하여 결정성을 보장한다.
 * 버전: v1.0.0
 * 관련 문서: design/server/v0.3.0-tick-loop.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/unit/simulation_determinism_test.cpp
 */
#include "server/simulation.hpp"

#include <algorithm>
#include <thread>

namespace server {

ValidationResult Simulation::EnqueueInput(const InputCommand& input) {
  std::string reason;
  if (!ValidateInput(input, reason)) {
    return ValidationResult{false, reason};
  }

  auto& tracker = trackers_[input.user_id];
  tracker.last_sequence = input.sequence;
  ++tracker.per_tick_count[input.target_tick];
  inputs_by_tick_[input.target_tick].push_back(input);
  return ValidationResult{true, {}};
}

void Simulation::AddPlayer(int user_id) {
  if (players_.count(user_id) == 0) {
    players_[user_id] = PlayerState{};
  }
}

bool Simulation::ValidateInput(const InputCommand& input, std::string& reason) const {
  if (input.target_tick <= current_tick_) {
    reason = "stale_tick";
    return false;
  }

  if (input.delta > kMaxDelta || input.delta < -kMaxDelta) {
    reason = "delta_out_of_range";
    return false;
  }

  if (input.sequence == 0) {
    reason = "sequence_required";
    return false;
  }

  const auto tracker_it = trackers_.find(input.user_id);
  if (tracker_it != trackers_.end()) {
    if (input.sequence <= tracker_it->second.last_sequence) {
      reason = "sequence_not_monotonic";
      return false;
    }
    const auto count_it = tracker_it->second.per_tick_count.find(input.target_tick);
    if (count_it != tracker_it->second.per_tick_count.end() &&
        count_it->second >= kMaxInputsPerTickPerUser) {
      reason = "tick_input_limit";
      return false;
    }
  }

  return true;
}

void Simulation::ApplyEvent(const InputCommand& input) {
  auto& state = players_[input.user_id];
  state.position += input.delta;
  state.last_sequence = input.sequence;
}

void Simulation::TickOnce() {
  ++current_tick_;
  auto it = inputs_by_tick_.find(current_tick_);
  if (it == inputs_by_tick_.end()) {
    return;
  }

  auto& events = it->second;
  std::stable_sort(events.begin(), events.end(), [](const InputCommand& lhs, const InputCommand& rhs) {
    if (lhs.sequence == rhs.sequence) {
      return lhs.user_id < rhs.user_id;
    }
    return lhs.sequence < rhs.sequence;
  });

  for (const auto& evt : events) {
    ApplyEvent(evt);
  }

  inputs_by_tick_.erase(it);
}

void Simulation::RunForDuration(std::chrono::milliseconds duration) {
  const auto start = std::chrono::steady_clock::now();
  auto next_tick = start + kTickInterval;
  while (std::chrono::steady_clock::now() - start < duration) {
    TickOnce();
    std::this_thread::sleep_until(next_tick);
    next_tick += kTickInterval;
  }
}

nlohmann::json Simulation::Snapshot() const {
  std::vector<int> user_ids;
  user_ids.reserve(players_.size());
  for (const auto& entry : players_) {
    user_ids.push_back(entry.first);
  }
  std::sort(user_ids.begin(), user_ids.end());

  nlohmann::json players_json = nlohmann::json::array();
  for (int user_id : user_ids) {
    const auto& state = players_.at(user_id);
    players_json.push_back({{"userId", user_id}, {"position", state.position}, {"lastSequence", state.last_sequence}});
  }

  return nlohmann::json{{"tick", current_tick_}, {"players", players_json}};
}

}  // namespace server
