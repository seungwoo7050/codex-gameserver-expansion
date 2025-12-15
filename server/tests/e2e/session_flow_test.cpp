#include <chrono>
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
#include "server/app.hpp"

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
  cfg.match_queue_timeout_seconds = 2;
  cfg.session_tick_interval_ms = 50;
  cfg.ops_token = "ops-token";
  return cfg;
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
}

void ExpectWsEventEnvelope(const nlohmann::json& msg, const std::string& event_name) {
  ASSERT_TRUE(msg.is_object());
  EXPECT_EQ(msg["t"], "event");
  ASSERT_TRUE(msg.contains("seq"));
  EXPECT_TRUE(msg["seq"].is_number_unsigned());
  EXPECT_EQ(msg["event"], event_name);
  ASSERT_TRUE(msg.contains("p"));
  EXPECT_TRUE(msg["p"].is_object());
}

void ExpectWsError(const nlohmann::json& msg, const std::string& code) {
  ASSERT_TRUE(msg.is_object());
  EXPECT_EQ(msg["t"], "error");
  ASSERT_TRUE(msg.contains("p"));
  EXPECT_TRUE(msg["p"].is_object());
  EXPECT_EQ(msg["p"]["code"], code);
}

class SessionFlowFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = TestConfig(18082);
    app_ = std::make_unique<server::ServerApp>(config_);
    server_thread_ = std::thread([this]() { app_->Run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }

  void TearDown() override {
    app_->Stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  SimpleHttpResponse PostJson(const std::string& target, const nlohmann::json& body, const std::string& token = "") {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::beast::tcp_stream stream{ioc};
    auto const results = resolver.resolve("127.0.0.1", std::to_string(config_.port));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::post, target, 11};
    req.set(boost::beast::http::field::host, "localhost");
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
    auto const results = resolver.resolve("127.0.0.1", std::to_string(config_.port));
    stream.connect(results);

    boost::beast::http::request<boost::beast::http::string_body> req{boost::beast::http::verb::get, target, 11};
    req.set(boost::beast::http::field::host, "localhost");
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

  using WebSocket = boost::beast::websocket::stream<boost::beast::tcp_stream>;

  std::unique_ptr<WebSocket> ConnectWs(const std::string& token) {
    auto ws = std::make_unique<WebSocket>(ioc_);
    boost::asio::ip::tcp::resolver resolver{ioc_};
    auto const results = resolver.resolve("127.0.0.1", std::to_string(config_.port));
    ws->next_layer().connect(results);
    ws->set_option(boost::beast::websocket::stream_base::decorator([&token](boost::beast::websocket::request_type& req) {
      req.set(boost::beast::http::field::authorization, "Bearer " + token);
    }));
    ws->handshake("127.0.0.1", "/ws");
    return ws;
  }

  nlohmann::json ReadWs(WebSocket& ws, boost::beast::flat_buffer& buffer) {
    buffer.consume(buffer.size());
    ws.read(buffer);
    auto raw = boost::beast::buffers_to_string(buffer.cdata());
    return nlohmann::json::parse(raw);
  }

  server::AppConfig config_;
  boost::asio::io_context ioc_;
  std::unique_ptr<server::ServerApp> app_;
  std::thread server_thread_;
};

TEST_F(SessionFlowFixture, MatchAndPersistResult) {
  std::string token_a = RegisterAndLogin("alice", "pw1");
  std::string token_b = RegisterAndLogin("bob", "pw2");

  auto ws_a = ConnectWs(token_a);
  auto ws_b = ConnectWs(token_b);
  boost::beast::flat_buffer buf_a;
  boost::beast::flat_buffer buf_b;
  auto auth_a = ReadWs(*ws_a, buf_a);
  auto auth_b = ReadWs(*ws_b, buf_b);
  ExpectWsEventEnvelope(auth_a, "auth_state");
  ExpectWsEventEnvelope(auth_b, "auth_state");

  auto join_a = PostJson("/api/queue/join", {{"mode", "normal"}, {"timeoutSeconds", 5}}, token_a);
  auto join_b = PostJson("/api/queue/join", {{"mode", "normal"}, {"timeoutSeconds", 5}}, token_b);
  EXPECT_EQ(join_a.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join_a.body);
  EXPECT_TRUE(join_a.body["data"].contains("expiresAt"));
  EXPECT_EQ(join_b.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join_b.body);

  std::string session_id;
  bool started = false;
  for (int i = 0; i < 6 && !started; ++i) {
    auto msg = ReadWs(*ws_a, buf_a);
    if (msg.contains("event")) {
      if (msg["event"] == "session.created") {
        ExpectWsEventEnvelope(msg, "session.created");
        session_id = msg["p"]["sessionId"].get<std::string>();
        EXPECT_TRUE(msg["p"].contains("participants"));
      }
      if (msg["event"] == "session.started") {
        ExpectWsEventEnvelope(msg, "session.started");
        started = true;
        if (session_id.empty() && msg["p"].contains("sessionId")) {
          session_id = msg["p"]["sessionId"].get<std::string>();
        }
        EXPECT_TRUE(msg["p"].contains("state"));
      }
    }
  }

  ASSERT_FALSE(session_id.empty());
  auto created_b = ReadWs(*ws_b, buf_b);
  ExpectWsEventEnvelope(created_b, "session.created");

  nlohmann::json input_a{{"t", "event"},
                         {"seq", 1},
                         {"event", "session.input"},
                         {"p", {{"sessionId", session_id}, {"sequence", 1}, {"targetTick", 1}, {"delta", 1}}}};
  ws_a->write(boost::asio::buffer(input_a.dump()));
  nlohmann::json input_b{{"t", "event"},
                         {"seq", 1},
                         {"event", "session.input"},
                         {"p", {{"sessionId", session_id}, {"sequence", 1}, {"targetTick", 1}, {"delta", 1}}}};
  ws_b->write(boost::asio::buffer(input_b.dump()));

  bool ended = false;
  for (int i = 0; i < 12 && !ended; ++i) {
    auto msg = ReadWs(*ws_a, buf_a);
    if (msg.contains("event") && msg["event"] == "session.ended") {
      ExpectWsEventEnvelope(msg, "session.ended");
      ended = true;
      EXPECT_EQ(msg["p"]["reason"], "completed");
      EXPECT_TRUE(msg["p"].contains("result"));
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(app_->DebugResultCount(), 1);
}

TEST_F(SessionFlowFixture, DuplicateQueueJoinRejected) {
  std::string token = RegisterAndLogin("solo", "pw1");
  auto join1 = PostJson("/api/queue/join", {{"mode", "normal"}}, token);
  auto join2 = PostJson("/api/queue/join", {{"mode", "normal"}}, token);
  EXPECT_EQ(join1.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join1.body);
  EXPECT_EQ(join2.status, boost::beast::http::status::conflict);
  ExpectErrorEnvelope(join2.body, "queue_duplicate");
}

TEST_F(SessionFlowFixture, QueueTimeoutProducesError) {
  std::string token = RegisterAndLogin("timeout", "pw1");
  auto ws = ConnectWs(token);
  boost::beast::flat_buffer buffer;
  auto auth_msg = ReadWs(*ws, buffer);
  ExpectWsEventEnvelope(auth_msg, "auth_state");

  auto join = PostJson("/api/queue/join", {{"mode", "normal"}, {"timeoutSeconds", 2}}, token);
  EXPECT_EQ(join.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join.body);

  bool got_timeout = false;
  for (int i = 0; i < 6 && !got_timeout; ++i) {
    auto msg = ReadWs(*ws, buffer);
    if (msg.contains("t") && msg["t"] == "error") {
      ExpectWsError(msg, "queue_timeout");
      got_timeout = true;
    }
  }
  EXPECT_TRUE(got_timeout);
}

TEST_F(SessionFlowFixture, LateInputAfterEndIsRejected) {
  std::string token_a = RegisterAndLogin("lateA", "pw1");
  std::string token_b = RegisterAndLogin("lateB", "pw2");

  auto ws_a = ConnectWs(token_a);
  auto ws_b = ConnectWs(token_b);
  boost::beast::flat_buffer buf_a;
  boost::beast::flat_buffer buf_b;
  auto auth_a = ReadWs(*ws_a, buf_a);
  auto auth_b = ReadWs(*ws_b, buf_b);
  ExpectWsEventEnvelope(auth_a, "auth_state");
  ExpectWsEventEnvelope(auth_b, "auth_state");

  auto join_a = PostJson("/api/queue/join", {{"mode", "normal"}}, token_a);
  auto join_b = PostJson("/api/queue/join", {{"mode", "normal"}}, token_b);
  ASSERT_EQ(join_a.status, boost::beast::http::status::ok);
  ASSERT_EQ(join_b.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join_a.body);
  ExpectSuccessEnvelope(join_b.body);

  std::string session_id;
  bool ended = false;
  for (int i = 0; i < 24 && !ended; ++i) {
    auto msg = ReadWs(*ws_a, buf_a);
    if (!msg.contains("event")) {
      continue;
    }
    if (msg["event"] == "session.created") {
      ExpectWsEventEnvelope(msg, "session.created");
      session_id = msg["p"]["sessionId"].get<std::string>();
    }
    if (msg["event"] == "session.started") {
      ExpectWsEventEnvelope(msg, "session.started");
      if (session_id.empty() && msg["p"].contains("sessionId")) {
        session_id = msg["p"]["sessionId"].get<std::string>();
      }
    }
    if (msg["event"] == "session.ended") {
      ExpectWsEventEnvelope(msg, "session.ended");
      ended = true;
    }
  }

  ASSERT_FALSE(session_id.empty());
  ASSERT_TRUE(ended);

  for (int i = 0; i < 10 && app_->DebugResultCount() < 1; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT_EQ(app_->DebugResultCount(), 1);

  nlohmann::json late_input{{"t", "event"},
                             {"seq", 99},
                             {"event", "session.input"},
                             {"p", {{"sessionId", session_id}, {"sequence", 99}, {"targetTick", 5}, {"delta", 3}}}};
  ws_a->write(boost::asio::buffer(late_input.dump()));

  auto error_msg = ReadWs(*ws_a, buf_a);
  ExpectWsError(error_msg, "session_not_found");
  EXPECT_EQ(app_->DebugResultCount(), 1);
}

}  // namespace
