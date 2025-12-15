#include <chrono>
#include <cstdlib>
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

  using WebSocket = boost::beast::websocket::stream<boost::beast::tcp_stream>;

  std::unique_ptr<WebSocket> ConnectWs(const std::string& token) {
    auto ws = std::make_unique<WebSocket>(ioc_);
    boost::asio::ip::tcp::resolver resolver{ioc_};
    auto const results = resolver.resolve(host_, std::to_string(port_));
    ws->next_layer().connect(results);
    ws->set_option(boost::beast::websocket::stream_base::decorator([&token](boost::beast::websocket::request_type& req) {
      req.set(boost::beast::http::field::authorization, "Bearer " + token);
    }));
    ws->handshake(host_, "/ws");
    return ws;
  }

  nlohmann::json ReadWs(WebSocket& ws, boost::beast::flat_buffer& buffer) {
    buffer.consume(buffer.size());
    ws.read(buffer);
    auto raw = boost::beast::buffers_to_string(buffer.cdata());
    return nlohmann::json::parse(raw);
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
  boost::asio::io_context ioc_;
};

}  // namespace

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
  ExpectSuccessEnvelope(join_a.body);
  ExpectSuccessEnvelope(join_b.body);

  std::string session_id;
  bool started = false;
  for (int i = 0; i < 6 && !started; ++i) {
    auto msg = safe_read(*ws_a, buf_a);
    if (!msg.has_value()) {
      continue;
    }
    if ((*msg).contains("event")) {
      if ((*msg)["event"] == "session.created") {
        session_id = (*msg)["p"]["sessionId"].get<std::string>();
      }
      if ((*msg)["event"] == "session.started") {
        started = true;
        if (session_id.empty() && (*msg)["p"].contains("sessionId")) {
          session_id = (*msg)["p"]["sessionId"].get<std::string>();
        }
      }
    }
  }
  ASSERT_FALSE(session_id.empty());

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
    auto msg = safe_read(*ws_a, buf_a);
    if (!msg.has_value()) {
      continue;
    }
    if ((*msg).contains("event") && (*msg)["event"] == "session.ended") {
      ended = true;
    }
  }
  EXPECT_TRUE(ended);

  ws_a->close(boost::beast::websocket::close_code::normal);
  ws_b->close(boost::beast::websocket::close_code::normal);

  auto leaderboard = Get("/api/leaderboard?page=1&size=10");
  ExpectSuccessEnvelope(leaderboard.body);
  ASSERT_TRUE(leaderboard.body["data"].contains("entries"));
  auto entries = leaderboard.body["data"]["entries"];
  ASSERT_TRUE(entries.is_array());
  auto find_rating = [&](int user_id) -> std::optional<int> {
    for (const auto& e : entries) {
      if (e["userId"].get<int>() == user_id) {
        return e["rating"].get<int>();
      }
    }
    return std::nullopt;
  };

  auto rating_a = find_rating(user_a_id);
  auto rating_b = find_rating(user_b_id);
  ASSERT_TRUE(rating_a.has_value());
  ASSERT_TRUE(rating_b.has_value());
  EXPECT_GE(*rating_a, 1016);
  EXPECT_LE(*rating_b, 984);
}

TEST_F(RatingLeaderboardFixture, LeaderboardPaginationValidatesRanges) {
  auto res = Get("/api/leaderboard?page=0&size=0");
  EXPECT_EQ(res.status, boost::beast::http::status::bad_request);
}

}  // namespace
