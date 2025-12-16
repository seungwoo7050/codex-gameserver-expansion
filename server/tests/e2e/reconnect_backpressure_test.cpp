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

class BackpressureServerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    host_ = ResolveHost();
    port_ = ResolvePort();
    WaitForReady();
  }

  SimpleHttpResponse PostJson(const std::string& target, const nlohmann::json& body, const std::string& token = "") {
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

  SimpleHttpResponse Get(const std::string& target) {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve(host_, std::to_string(port_));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::get, target, 11};
    req.set(boost::beast::http::field::host, host_);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);

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
    EXPECT_EQ(reg_res.status, boost::beast::http::status::created);
    EXPECT_TRUE(reg_res.body["success"].get<bool>());

    auto login_res = PostJson("/api/auth/login", {{"username", username}, {"password", password}});
    EXPECT_EQ(login_res.status, boost::beast::http::status::ok);
    EXPECT_TRUE(login_res.body["success"].get<bool>());
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

TEST_F(BackpressureServerFixture, SlowConsumerTriggersClose) {
  std::string token = RegisterAndLogin("slow", "password123");

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
  bool closed_by_exception = false;
  boost::system::error_code ec;
  try {
    ws.read(buffer);
    buffer.consume(buffer.size());

    std::string large_message(300, 'x');
    nlohmann::json echo_req{{"t", "event"}, {"seq", 1}, {"event", "echo"}, {"p", {{"message", large_message}}}};
    boost::system::error_code write_ec;
    ws.write(boost::asio::buffer(echo_req.dump()), write_ec);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ws.read(buffer, ec);
  } catch (const std::exception&) {
    closed_by_exception = true;
  }
  EXPECT_TRUE(closed_by_exception || ec == boost::beast::websocket::error::closed);
}

TEST_F(BackpressureServerFixture, ReconnectAndResyncReturnsSnapshot) {
  std::string token = RegisterAndLogin("reuser", "password123");

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
  boost::system::error_code ec;
  ws.read(buffer, ec);
  ASSERT_FALSE(ec) << ec.message();
  auto first_auth = nlohmann::json::parse(boost::beast::buffers_to_string(buffer.cdata()));
  std::string resume_token = first_auth["p"]["resumeToken"].get<std::string>();
  ws.close(boost::beast::websocket::close_code::normal, ec);

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws2{ioc};
  ws2.next_layer().connect(results);
  ws2.set_option(boost::beast::websocket::stream_base::decorator([&token](boost::beast::websocket::request_type& req) {
    req.set(boost::beast::http::field::authorization, "Bearer " + token);
  }));
  ws2.handshake(host_, "/ws");

  buffer.consume(buffer.size());
  ws2.read(buffer, ec);
  ASSERT_FALSE(ec) << ec.message();
  buffer.consume(buffer.size());

  nlohmann::json resync_req{{"t", "event"}, {"seq", 2}, {"event", "resync_request"}, {"p", {{"resumeToken", resume_token}}}};
  ws2.write(boost::asio::buffer(resync_req.dump()), ec);
  ASSERT_FALSE(ec) << ec.message();
  ws2.read(buffer, ec);
  ASSERT_FALSE(ec) << ec.message();
  auto resync_msg = nlohmann::json::parse(boost::beast::buffers_to_string(buffer.cdata()));

  EXPECT_EQ(resync_msg["event"], "resync_state");
  EXPECT_TRUE(resync_msg["p"].contains("resumeToken"));
  EXPECT_EQ(resync_msg["p"]["snapshot"]["version"], 1);
  EXPECT_EQ(resync_msg["p"]["snapshot"]["state"], "auth_only");
  EXPECT_EQ(resync_msg["p"]["snapshot"]["user"]["username"], "reuser");
}
