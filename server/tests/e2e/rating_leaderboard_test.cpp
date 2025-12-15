#include <chrono>
#include <optional>
#include <thread>
#include <vector>

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
#include "server/result_service.hpp"
#include "server/session_manager.hpp"

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
  EXPECT_TRUE(body["success"].get<bool>());
  ASSERT_TRUE(body.contains("data"));
  EXPECT_TRUE(body["data"].is_object());
  ASSERT_TRUE(body.contains("error"));
  EXPECT_TRUE(body["error"].is_null());
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

class RatingLeaderboardFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = TestConfig(18083);
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

  server::AppConfig config_{};
  boost::asio::io_context ioc_;
  std::unique_ptr<server::ServerApp> app_;
  std::thread server_thread_;
};

TEST_F(RatingLeaderboardFixture, DuplicateFinalizeDoesNotDoubleApplyRating) {
  std::string token_a = RegisterAndLogin("raterA", "pw1");
  std::string token_b = RegisterAndLogin("raterB", "pw2");

  auto ws_a = ConnectWs(token_a);
  auto ws_b = ConnectWs(token_b);
  boost::beast::flat_buffer buf_a;
  boost::beast::flat_buffer buf_b;
  auto safe_read = [&](WebSocket& ws, boost::beast::flat_buffer& buffer) -> std::optional<nlohmann::json> {
    try {
      return ReadWs(ws, buffer);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };

  auto auth_a = safe_read(*ws_a, buf_a);
  auto auth_b = safe_read(*ws_b, buf_b);
  ASSERT_TRUE(auth_a.has_value());
  ASSERT_TRUE(auth_b.has_value());
  ExpectWsEventEnvelope(*auth_a, "auth_state");
  ExpectWsEventEnvelope(*auth_b, "auth_state");
  int user_a_id = (*auth_a)["p"]["userId"].get<int>();
  int user_b_id = (*auth_b)["p"]["userId"].get<int>();

  auto join_a = PostJson("/api/queue/join", {{"mode", "normal"}, {"timeoutSeconds", 5}}, token_a);
  auto join_b = PostJson("/api/queue/join", {{"mode", "normal"}, {"timeoutSeconds", 5}}, token_b);
  ASSERT_EQ(join_a.status, boost::beast::http::status::ok);
  ASSERT_EQ(join_b.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(join_a.body);
  ExpectSuccessEnvelope(join_b.body);

  std::string session_id;
  bool started = false;
  for (int i = 0; i < 40 && !started; ++i) {
    auto msg = safe_read(*ws_a, buf_a);
    if (!msg.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    if (!msg->contains("event")) {
      continue;
    }
    if ((*msg)["event"] == "session.created") {
      ExpectWsEventEnvelope(*msg, "session.created");
      session_id = (*msg)["p"]["sessionId"].get<std::string>();
    }
    if ((*msg)["event"] == "session.started") {
      ExpectWsEventEnvelope(*msg, "session.started");
      started = true;
      if (session_id.empty() && (*msg)["p"].contains("sessionId")) {
        session_id = (*msg)["p"]["sessionId"].get<std::string>();
      }
    }
  }

  ASSERT_FALSE(session_id.empty());
  ASSERT_TRUE(started);

  nlohmann::json input_payload{{"t", "event"},
                               {"seq", 1},
                               {"event", "session.input"},
                               {"p", {{"sessionId", session_id}, {"sequence", 1}, {"targetTick", 1}, {"delta", 2}}}};
  ws_a->write(boost::asio::buffer(input_payload.dump()));
  input_payload["seq"] = 2;
  input_payload["p"]["delta"] = 1;
  ws_b->write(boost::asio::buffer(input_payload.dump()));

  int winner_id = 0;
  for (int i = 0; i < 40 && winner_id == 0; ++i) {
    auto msg = safe_read(*ws_a, buf_a);
    if (msg.has_value() && msg->contains("event") && (*msg)["event"] == "session.ended") {
      ExpectWsEventEnvelope(*msg, "session.ended");
      winner_id = (*msg)["p"]["result"]["winnerUserId"].get<int>();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  ASSERT_GT(winner_id, 0);
  ASSERT_EQ(winner_id, user_a_id);

  std::optional<server::MatchResultRecord> record;
  for (int i = 0; i < 20 && !record.has_value(); ++i) {
    record = app_->GetResultService()->Find(session_id);
    if (record.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  ASSERT_TRUE(record.has_value());

  auto profile_a = Get("/api/profile", token_a);
  auto profile_b = Get("/api/profile", token_b);
  ASSERT_EQ(profile_a.status, boost::beast::http::status::ok);
  ASSERT_EQ(profile_b.status, boost::beast::http::status::ok);

  auto rating_a = profile_a.body["data"]["rating"].get<int>();
  auto rating_b = profile_b.body["data"]["rating"].get<int>();
  int winner_rating = winner_id == user_a_id ? rating_a : rating_b;
  int loser_rating = winner_id == user_a_id ? rating_b : rating_a;
  EXPECT_EQ(winner_rating, 1016);
  EXPECT_EQ(loser_rating, 984);

  std::vector<server::SessionParticipant> participants{{user_a_id, "raterA"}, {user_b_id, "raterB"}};
  bool applied_again = app_->GetResultService()->FinalizeResult(*record, participants);
  EXPECT_FALSE(applied_again);

  auto profile_a_second = Get("/api/profile", token_a);
  auto profile_b_second = Get("/api/profile", token_b);
  EXPECT_EQ(profile_a_second.body["data"]["rating"].get<int>(), rating_a);
  EXPECT_EQ(profile_b_second.body["data"]["rating"].get<int>(), rating_b);

  auto leaderboard = Get("/api/leaderboard?page=1&size=10");
  ASSERT_EQ(leaderboard.status, boost::beast::http::status::ok);
  ExpectSuccessEnvelope(leaderboard.body);
  auto entries = leaderboard.body["data"]["entries"];
  ASSERT_GE(entries.size(), 2);
  int loser_id = winner_id == user_a_id ? user_b_id : user_a_id;
  EXPECT_EQ(entries[0]["userId"].get<int>(), winner_id);
  EXPECT_EQ(entries[0]["rating"].get<int>(), 1016);
  EXPECT_EQ(entries[1]["userId"].get<int>(), loser_id);
  EXPECT_EQ(entries[1]["rating"].get<int>(), 984);
  EXPECT_GE(entries[0]["rating"].get<int>(), entries[1]["rating"].get<int>());
}

}  // namespace
