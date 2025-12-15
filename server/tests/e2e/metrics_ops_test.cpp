#include <chrono>
#include <cstdlib>
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

#include "server/api_response.hpp"

namespace {

unsigned short ResolvePort() {
  const char* env_port = std::getenv("E2E_LB_PORT");
  return env_port ? static_cast<unsigned short>(std::stoi(env_port)) : 8080;
}

std::string ResolveHost() {
  const char* env_host = std::getenv("E2E_LB_HOST");
  return env_host ? std::string{env_host} : std::string{"127.0.0.1"};
}

std::string ResolveOpsToken() {
  const char* env_token = std::getenv("OPS_TOKEN");
  return env_token ? std::string{env_token} : std::string{"ops-secret"};
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
    host_ = ResolveHost();
    port_ = ResolvePort();
    ops_token_ = ResolveOpsToken();
    WaitForReady();
  }

  SimpleHttpResponse Get(const std::string& target, const std::string& extra_header_name = "",
                         const std::string& extra_header_value = "") {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve(host_, std::to_string(port_));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::get, target, 11};
    req.set(boost::beast::http::field::host, host_);
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

  void WaitForReady() {
    for (int i = 0; i < 10; ++i) {
      auto res = Get("/api/health");
      if (res.status == boost::beast::http::status::ok) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  std::string host_{};
  unsigned short port_{8080};
  std::string ops_token_{};
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

  auto authed_ops = Get("/ops/status", "X-Ops-Token", ops_token_);
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
