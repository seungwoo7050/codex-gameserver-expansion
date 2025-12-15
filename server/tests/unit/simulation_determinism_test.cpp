#include <gtest/gtest.h>

#include "server/simulation.hpp"

namespace {

std::vector<server::InputCommand> BuildInputSequence() {
  return {
      {1, 1, 1, 1}, {2, 1, -1, 1}, {1, 2, 1, 2}, {2, 2, 1, 2}, {1, 3, -1, 3}, {2, 4, 2, 3}};
}

void ApplySequence(server::Simulation& sim, const std::vector<server::InputCommand>& sequence) {
  for (const auto& input : sequence) {
    auto result = sim.EnqueueInput(input);
    ASSERT_TRUE(result.accepted) << result.reason;
  }

  int max_tick = 0;
  for (const auto& input : sequence) {
    max_tick = std::max(max_tick, input.target_tick);
  }

  for (int i = 0; i < max_tick; ++i) {
    sim.TickOnce();
  }
}

}  // namespace

TEST(SimulationDeterminismTest, AppliesInputSequenceToExpectedSnapshot) {
  server::Simulation sim;
  auto sequence = BuildInputSequence();

  ApplySequence(sim, sequence);

  nlohmann::json expected{{"tick", 4},
                          {"players", nlohmann::json::array({{{"userId", 1}, {"position", 1}, {"lastSequence", 3}},
                                                             {{"userId", 2}, {"position", 2}, {"lastSequence", 3}}})}};
  EXPECT_EQ(sim.Snapshot(), expected);
}

TEST(SimulationDeterminismTest, ReplaysProduceIdenticalSnapshots) {
  auto sequence = BuildInputSequence();

  server::Simulation sim_a;
  server::Simulation sim_b;

  ApplySequence(sim_a, sequence);
  ApplySequence(sim_b, sequence);

  EXPECT_EQ(sim_a.Snapshot(), sim_b.Snapshot());
}

TEST(SimulationTickLoopTest, RunsForDurationWithoutRunaway) {
  server::Simulation sim;
  const int start_tick = sim.CurrentTick();

  sim.RunForDuration(std::chrono::milliseconds(120));

  const int produced = sim.CurrentTick() - start_tick;
  EXPECT_GE(produced, 6);
  EXPECT_LE(produced, 10);
}
