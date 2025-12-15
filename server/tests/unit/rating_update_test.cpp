#include <gtest/gtest.h>

#include "server/rating.hpp"

namespace {

TEST(RatingServiceTest, ApplyMatchUpdatesBothPlayers) {
  server::RatingService service;
  service.EnsureUser(1, "alpha");
  service.EnsureUser(2, "beta");

  auto summary = service.ApplyMatchResult(1, 2);
  EXPECT_EQ(summary.rating, 1016);
  EXPECT_EQ(summary.wins, 1);
  EXPECT_EQ(summary.losses, 0);

  auto loser = service.GetSummary(2);
  ASSERT_TRUE(loser.has_value());
  EXPECT_EQ(loser->rating, 984);
  EXPECT_EQ(loser->wins, 0);
  EXPECT_EQ(loser->losses, 1);
  EXPECT_EQ(loser->matches(), 1);
}

}  // namespace
