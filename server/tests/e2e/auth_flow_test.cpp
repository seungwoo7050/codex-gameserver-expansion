#include <chrono>
#include <cstdlib>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
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

struct SimpleHttpResponse {
  boost::beast::http::status status;
  nlohmann::json body;
};

void ExpectSuccessEnvelope(const nlohmann::json& body) {
  ASSERT_TRUE(body.is_object());
  ASSERT_TRUE(body.contains("success"));
  EXPECT_TRUE(body["success"].is_boolean());
  EXPECT_TRUE(body["success"].get<bool>());
  ASSERT_TRUE(body.contains("data"));
  EXPECT_TRUE(body["data"].is_object());
  ASSERT_TRUE(body.contains("error"));
  EXPECT_TRUE(body["error"].is_null());
  ASSERT_TRUE(body.contains("meta"));
  EXPECT_TRUE(body["meta"].is_object());
  EXPECT_TRUE(body["meta"].contains("timestamp"));
  EXPECT_TRUE(body["meta"]["timestamp"].is_string());
}

void ExpectErrorEnvelope(const nlohmann::json& body, const std::string& code) {
  ASSERT_TRUE(body.is_object());
  ASSERT_TRUE(body.contains("success"));
  EXPECT_TRUE(body["success"].is_boolean());
  EXPECT_FALSE(body["success"].get<bool>());
  ASSERT_TRUE(body.contains("data"));
  EXPECT_TRUE(body["data"].is_null());
  ASSERT_TRUE(body.contains("error"));
  EXPECT_TRUE(body["error"].is_object());
  EXPECT_EQ(body["error"]["code"], code);
  ASSERT_TRUE(body.contains("meta"));
  EXPECT_TRUE(body["meta"].is_object());
}

void ExpectWsEventEnvelope(const nlohmann::json& body, const std::string& event_name) {
  ASSERT_TRUE(body.is_object());
  EXPECT_EQ(body["t"], "event");
  ASSERT_TRUE(body.contains("seq"));
  EXPECT_TRUE(body["seq"].is_number_unsigned());
  EXPECT_EQ(body["event"], event_name);
  ASSERT_TRUE(body.contains("p"));
  EXPECT_TRUE(body["p"].is_object());
}

class ServerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    host_ = ResolveHost();
    port_ = ResolvePort();
    WaitForReady();
  }

  SimpleHttpResponse PostJson(const std::string& target, const nlohmann::json& body,
                              const std::string& token = "") {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve(host_, std::to_string(port_));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::post, target, 11};
    req.set(boost::beast::http::field::host, host_);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(boost::beast::http::field::content_type, "application/json");
    req.body() = body.dump();
    req.prepare_payload();
    if (!token.empty()) {
      req.set(boost::beast::http::field::authorization, "Bearer " + token);
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

  SimpleHttpResponse Get(const std::string& target, const std::string& token = "") {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve(host_, std::to_string(port_));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::get, target, 11};
    req.set(boost::beast::http::field::host, host_);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (!token.empty()) {
      req.set(boost::beast::http::field::authorization, "Bearer " + token);
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

  std::string RegisterAndLogin(const std::string& username, const std::string& password) {
    auto reg_res = PostJson("/api/auth/register", {{"username", username}, {"password", password}});
    ExpectSuccessEnvelope(reg_res.body);
    auto login_res = PostJson("/api/auth/login", {{"username", username}, {"password", password}});
    ExpectSuccessEnvelope(login_res.body);
    return login_res.body["data"]["token"].get<std::string>();
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
};

}  // namespace

TEST_F(ServerFixture, HealthEndpoint) {
  auto res = Get("/api/health");
  EXPECT_EQ(res.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(res.body);
  EXPECT_TRUE(res.body["data"].contains("status"));
  EXPECT_EQ(res.body["data"]["status"], "ok");
  EXPECT_EQ(res.body["data"]["version"], "v1.0.0");
}

TEST_F(ServerFixture, AuthFlowProtectedEndpoint) {
  std::string token = RegisterAndLogin("alice", "password123");

  auto profile_res = Get("/api/profile", token);
  EXPECT_EQ(profile_res.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(profile_res.body);
  EXPECT_EQ(profile_res.body["data"]["username"], "alice");

  auto logout_res = PostJson("/api/auth/logout", nlohmann::json::object(), token);
  EXPECT_EQ(logout_res.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(logout_res.body);
  EXPECT_TRUE(logout_res.body["data"].contains("loggedOut"));
  EXPECT_TRUE(logout_res.body["data"]["loggedOut"].get<bool>());
}

TEST_F(ServerFixture, LoginRejectsWrongPassword) {
  auto reg_res = PostJson("/api/auth/register", {{"username", "eve"}, {"password", "correct"}});
  EXPECT_EQ(reg_res.status, boost::beast::http::status::created);
  ExpectSuccessEnvelope(reg_res.body);

  auto login_res = PostJson("/api/auth/login", {{"username", "eve"}, {"password", "wrong"}});
  EXPECT_EQ(login_res.status, boost::beast::http::status::unauthorized);
  ExpectErrorEnvelope(login_res.body, "unauthorized");
}

TEST_F(ServerFixture, ProtectedEndpointRejectsWithoutAuth) {
  auto res = Get("/api/profile");
  EXPECT_EQ(res.status, boost::beast::http::status::unauthorized);
  ExpectErrorEnvelope(res.body, "unauthorized");
}

TEST_F(ServerFixture, WebSocketRejectsWithoutAuth) {
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::resolver resolver{ioc};
  boost::beast::tcp_stream stream{ioc};
  auto const results = resolver.resolve(host_, std::to_string(port_));
  stream.connect(results);

  boost::beast::http::request<boost::beast::http::empty_body> req{boost::beast::http::verb::get, "/ws", 11};
  req.set(boost::beast::http::field::host, host_);
  req.set(boost::beast::http::field::upgrade, "websocket");
  req.set(boost::beast::http::field::connection, "Upgrade");
  req.set(boost::beast::http::field::sec_websocket_version, "13");
  req.set(boost::beast::http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");

  boost::beast::http::write(stream, req);
  boost::beast::flat_buffer buffer;
  boost::beast::http::response<boost::beast::http::string_body> res;
  boost::beast::error_code ec;
  boost::beast::http::read(stream, buffer, res, ec);
  if (ec == boost::beast::http::error::end_of_stream) {
    ec = {};
  }
  ASSERT_FALSE(ec) << ec.message();

  EXPECT_EQ(res.result(), boost::beast::http::status::unauthorized);
  auto body_json = nlohmann::json::parse(res.body());
  ExpectErrorEnvelope(body_json, "unauthorized");
}

TEST_F(ServerFixture, WebSocketAuthSuccessAndEcho) {
  std::string token = RegisterAndLogin("bob", "pass456");

  boost::asio::io_context ioc;
  boost::asio::ip::tcp::resolver resolver{ioc};
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{ioc};
  auto const results = resolver.resolve(host_, std::to_string(port_));
  ws.next_layer().connect(results);
  ws.set_option(boost::beast::websocket::stream_base::decorator([&token](boost::beast::websocket::request_type& req) {
    req.set(boost::beast::http::field::authorization, "Bearer " + token);
  }));
  ws.handshake(host_, "/ws");

  boost::beast::flat_buffer buffer;
  ws.read(buffer);
  auto initial_msg = boost::beast::buffers_to_string(buffer.cdata());
  auto initial_json = nlohmann::json::parse(initial_msg);
  ExpectWsEventEnvelope(initial_json, "auth_state");
  EXPECT_EQ(initial_json["p"]["username"], "bob");
  EXPECT_TRUE(initial_json["p"].contains("resumeToken"));
  EXPECT_EQ(initial_json["p"]["snapshotVersion"], 1);

  buffer.consume(buffer.size());
  nlohmann::json echo_req{{"t", "event"}, {"seq", 1}, {"event", "echo"}, {"p", {{"message", "hi"}}}};
  ws.write(boost::asio::buffer(echo_req.dump()));
  ws.read(buffer);
  auto reply_str = boost::beast::buffers_to_string(buffer.cdata());
  auto reply_json = nlohmann::json::parse(reply_str);
  ExpectWsEventEnvelope(reply_json, "echo");
  EXPECT_EQ(reply_json["p"]["message"], "hi");
  EXPECT_EQ(reply_json["p"]["userId"], initial_json["p"]["userId"]);

  ws.close(boost::beast::websocket::close_code::normal);
}
