#include <cstdlib>

#include <gtest/gtest.h>

#include "server/rating.hpp"
#include "server/result_repository.hpp"

namespace {

server::DbConfig TestDbConfig() {
  server::DbConfig cfg;
  const char* host = std::getenv("DB_HOST");
  const char* port = std::getenv("DB_PORT");
  const char* user = std::getenv("DB_USER");
  const char* pass = std::getenv("DB_PASSWORD");
  const char* name = std::getenv("DB_NAME");
  cfg.host = host ? host : "127.0.0.1";
  cfg.port = port ? static_cast<unsigned short>(std::stoi(port)) : 3306;
  cfg.user = user ? user : "app";
  cfg.password = pass ? pass : "app_pass";
  cfg.database = name ? name : "app_db";
  return cfg;
}

TEST(RatingServiceTest, ApplyMatchUpdatesBothPlayersWithDb) {
  auto db_client = std::make_shared<server::MariaDbClient>(TestDbConfig());
  auto repository = std::make_shared<server::ResultRepository>(db_client);
  repository->ClearAll();
  server::RatingService service(db_client);

  db_client->ExecuteTransactionWithRetry([&](MYSQL* conn) {
    service.EnsureUserInTx(conn, 1, "alpha");
    service.EnsureUserInTx(conn, 2, "beta");
    service.ApplyMatchResultInTx(conn, 1, 2);
    return true;
  });

  auto summary = service.GetSummary(1);
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->rating, 1016);
  EXPECT_EQ(summary->wins, 1);
  EXPECT_EQ(summary->losses, 0);

  auto loser = service.GetSummary(2);
  ASSERT_TRUE(loser.has_value());
  EXPECT_EQ(loser->rating, 984);
  EXPECT_EQ(loser->wins, 0);
  EXPECT_EQ(loser->losses, 1);
  EXPECT_EQ(loser->matches(), 1);
}

}  // namespace
