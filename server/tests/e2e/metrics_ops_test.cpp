#include <chrono>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "server/app.hpp"
#include "server/api_response.hpp"

namespace {

server::AppConfig TestConfig(unsigned short port) {
  server::AppConfig cfg{};
  cfg.port = port;
  cfg.db_host = "localhost";
  cfg.db_port = 3306;
  cfg.db_user = "app";
  cfg.db_password = "app_pass";
  cfg.db_name = "app_db";
  cfg.redis_host = "localhost";
  cfg.redis_port = 6379;
  cfg.log_level = "info";
  cfg.auth_token_ttl_seconds = 3600;
  cfg.login_rate_window_seconds = 60;
  cfg.login_rate_limit_max = 5;
  cfg.ws_queue_limit_messages = 8;
  cfg.ws_queue_limit_bytes = 65536;
  cfg.match_queue_timeout_seconds = 10;
  cfg.session_tick_interval_ms = 100;
  cfg.ops_token = "ops-secret";
  return cfg;
}

struct SimpleHttpResponse {
  boost::beast::http::status status;
  nlohmann::json body;
};

void ExpectSuccessEnvelope(const nlohmann::json& body) {
  ASSERT_TRUE(body.is_object());
  EXPECT_TRUE(body.contains("success"));
  EXPECT_TRUE(body["success"].get<bool>());
  ASSERT_TRUE(body.contains("data"));
  EXPECT_TRUE(body["data"].is_object());
  ASSERT_TRUE(body.contains("error"));
  EXPECT_TRUE(body["error"].is_null());
  ASSERT_TRUE(body.contains("meta"));
  EXPECT_TRUE(body["meta"].is_object());
}

void ExpectErrorEnvelope(const nlohmann::json& body, const std::string& code) {
  ASSERT_TRUE(body.is_object());
  EXPECT_TRUE(body.contains("success"));
  EXPECT_FALSE(body["success"].get<bool>());
  ASSERT_TRUE(body.contains("data"));
  EXPECT_TRUE(body["data"].is_null());
  ASSERT_TRUE(body.contains("error"));
  EXPECT_TRUE(body["error"].is_object());
  EXPECT_EQ(body["error"]["code"], code);
}

class MetricsOpsFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = TestConfig(18084);
    app_ = std::make_unique<server::ServerApp>(config_);
    server_thread_ = std::thread([this]() { app_->Run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  void TearDown() override {
    app_->Stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  SimpleHttpResponse Get(const std::string& target, const std::string& extra_header_name = "",
                         const std::string& extra_header_value = "") {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve("127.0.0.1", std::to_string(config_.port));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::get, target, 11};
    req.set(boost::beast::http::field::host, "localhost");
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (!extra_header_name.empty()) {
      req.set(extra_header_name, extra_header_value);
    }

    boost::beast::http::write(stream, req);

    boost::beast::flat_buffer buffer;
    boost::beast::http::response<boost::beast::http::string_body> res;
    boost::beast::http::read(stream, buffer, res);

    SimpleHttpResponse result{res.result(), nlohmann::json::parse(res.body())};
    boost::beast::error_code ec;
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    return result;
  }

  server::AppConfig config_;
  std::unique_ptr<server::ServerApp> app_;
  std::thread server_thread_;
};

}  // namespace

TEST_F(MetricsOpsFixture, MetricsAndOpsEndpoints) {
  auto first = Get("/metrics");
  ASSERT_EQ(first.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(first.body);
  EXPECT_TRUE(first.body["data"].contains("requests"));
  auto initial_total = first.body["data"]["requests"]["total"].get<std::uint64_t>();

  auto unauthorized_ops = Get("/ops/status");
  EXPECT_EQ(unauthorized_ops.status, boost::beast::http::status::unauthorized);
  ExpectErrorEnvelope(unauthorized_ops.body, "unauthorized");

  auto authed_ops = Get("/ops/status", "X-Ops-Token", config_.ops_token);
  ASSERT_EQ(authed_ops.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(authed_ops.body);
  EXPECT_TRUE(authed_ops.body["data"].contains("activeSessions"));

  auto health = Get("/api/health");
  ASSERT_EQ(health.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(health.body);

  auto second = Get("/metrics");
  ASSERT_EQ(second.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(second.body);
  auto second_total = second.body["data"]["requests"]["total"].get<std::uint64_t>();
  EXPECT_GE(second_total, initial_total + 2);
}
