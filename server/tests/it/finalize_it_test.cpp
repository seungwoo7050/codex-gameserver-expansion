#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <mariadb/mysql.h>
#include <nlohmann/json.hpp>

#include "server/rating.hpp"
#include "server/result_repository.hpp"
#include "server/result_service.hpp"
#include "server/session_manager.hpp"

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

MYSQL* OpenBlockingConnection(const server::DbConfig& cfg) {
  MYSQL* conn = mysql_init(nullptr);
  if (!conn) {
    return nullptr;
  }
  unsigned int timeout = 2;
  mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
  mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
  mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
  if (!mysql_real_connect(conn, cfg.host.c_str(), cfg.user.c_str(), cfg.password.c_str(), cfg.database.c_str(), cfg.port,
                          nullptr, 0)) {
    return nullptr;
  }
  mysql_query(conn, "SET SESSION innodb_lock_wait_timeout=1;");
  return conn;
}

std::shared_ptr<server::ResultService> BuildService(std::shared_ptr<server::MariaDbClient> db_client,
                                                    std::shared_ptr<server::ResultRepository>& repository,
                                                    std::shared_ptr<server::RatingService>& rating_service) {
  repository = std::make_shared<server::ResultRepository>(db_client);
  rating_service = std::make_shared<server::RatingService>(db_client);
  repository->ClearAll();
  return std::make_shared<server::ResultService>(db_client, repository, rating_service);
}

server::MatchResultRecord SampleRecord() {
  return server::MatchResultRecord{"match-1", 1, 2, 1, 10, std::chrono::system_clock::now(), nlohmann::json::object()};
}

std::vector<server::SessionParticipant> SampleParticipants() {
  return {server::SessionParticipant{1, "alpha"}, server::SessionParticipant{2, "beta"}};
}

TEST(FinalizeItTest, DuplicateFinalizeStopsDoubleApply) {
  auto db_client = std::make_shared<server::MariaDbClient>(TestDbConfig());
  std::shared_ptr<server::ResultRepository> repository;
  std::shared_ptr<server::RatingService> rating_service;
  auto service = BuildService(db_client, repository, rating_service);

  auto record = SampleRecord();
  auto participants = SampleParticipants();

  ASSERT_TRUE(service->FinalizeResult(record, participants));
  ASSERT_TRUE(service->FinalizeResult(record, participants));

  EXPECT_EQ(repository->Count(), 1u);
  auto winner = rating_service->GetSummary(1);
  auto loser = rating_service->GetSummary(2);
  ASSERT_TRUE(winner.has_value());
  ASSERT_TRUE(loser.has_value());
  EXPECT_EQ(winner->rating, 1016);
  EXPECT_EQ(loser->rating, 984);
}

TEST(FinalizeItTest, ConcurrentFinalizeUsesDbGuards) {
  auto db_client = std::make_shared<server::MariaDbClient>(TestDbConfig());
  std::shared_ptr<server::ResultRepository> repository;
  std::shared_ptr<server::RatingService> rating_service;
  auto service = BuildService(db_client, repository, rating_service);

  auto record = SampleRecord();
  auto participants = SampleParticipants();

  std::thread t1([&]() { service->FinalizeResult(record, participants); });
  std::thread t2([&]() { service->FinalizeResult(record, participants); });
  t1.join();
  t2.join();

  EXPECT_EQ(repository->Count(), 1u);
  auto winner = rating_service->GetSummary(1);
  auto loser = rating_service->GetSummary(2);
  ASSERT_TRUE(winner.has_value());
  ASSERT_TRUE(loser.has_value());
  EXPECT_EQ(winner->wins, 1);
  EXPECT_EQ(loser->losses, 1);
}

TEST(FinalizeItTest, TransientFailureRetriesTransaction) {
  auto db_client = std::make_shared<server::MariaDbClient>(TestDbConfig());
  std::shared_ptr<server::ResultRepository> repository;
  std::shared_ptr<server::RatingService> rating_service;
  auto service = BuildService(db_client, repository, rating_service);

  rating_service->EnsureUser(1, "alpha");
  rating_service->EnsureUser(2, "beta");

  MYSQL* blocker = OpenBlockingConnection(TestDbConfig());
  ASSERT_NE(blocker, nullptr);
  mysql_autocommit(blocker, 0);
  ASSERT_EQ(mysql_query(blocker, "START TRANSACTION;"), 0);
  ASSERT_EQ(mysql_query(blocker, "SELECT * FROM ratings WHERE user_id IN (1,2) FOR UPDATE;"), 0);

  auto record = SampleRecord();
  auto participants = SampleParticipants();

  bool success = false;
  std::thread worker([&]() { success = service->FinalizeResult(record, participants); });
  // 락 대기 타임아웃(2초)을 확실히 넘기도록 충분히 대기시켜 재시도 경로를 강제한다.
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  mysql_commit(blocker);
  mysql_close(blocker);
  worker.join();

  ASSERT_TRUE(success);
  EXPECT_EQ(repository->Count(), 1u);
  auto winner = rating_service->GetSummary(1);
  auto loser = rating_service->GetSummary(2);
  ASSERT_TRUE(winner.has_value());
  ASSERT_TRUE(loser.has_value());
  EXPECT_EQ(winner->wins, 1);
  EXPECT_EQ(loser->losses, 1);
}

}  // namespace
