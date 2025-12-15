/*
 * 설명: HTTP 요청을 처리하고 인증/큐/리더보드/WS 업그레이드를 분기한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/http_session.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "server/api_response.hpp"
#include "server/observability.hpp"

namespace server {

namespace {
std::string ToIsoString(std::chrono::system_clock::time_point tp) {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%FT%TZ");
  return oss.str();
}

std::unordered_map<std::string, std::string> ParseQueryParams(const std::string& query) {
  std::unordered_map<std::string, std::string> params;
  std::size_t pos = 0;
  while (pos < query.size()) {
    auto amp = query.find('&', pos);
    std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    auto eq = pair.find('=');
    if (eq != std::string::npos && eq + 1 <= pair.size()) {
      params.emplace(pair.substr(0, eq), pair.substr(eq + 1));
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return params;
}

std::optional<std::size_t> ParsePositiveInt(const std::string& value) {
  try {
    std::size_t idx = 0;
    auto parsed = std::stoul(value, &idx);
    if (idx != value.size()) {
      return std::nullopt;
    }
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}
}  // namespace

HttpSession::HttpSession(boost::asio::ip::tcp::socket socket, const AppConfig& config,
                         std::shared_ptr<AuthService> auth_service,
                         std::shared_ptr<ReconnectService> reconnect_service,
                         std::shared_ptr<RealtimeCoordinator> coordinator,
                         std::shared_ptr<SessionManager> session_manager,
                         std::shared_ptr<MatchQueueService> match_queue,
                         std::shared_ptr<RatingService> rating_service,
                         std::shared_ptr<Observability> observability)
    : stream_(std::move(socket)), config_(config), auth_service_(std::move(auth_service)),
      reconnect_service_(std::move(reconnect_service)), coordinator_(std::move(coordinator)),
      session_manager_(std::move(session_manager)), match_queue_(std::move(match_queue)),
      rating_service_(std::move(rating_service)), observability_(std::move(observability)) {}

void HttpSession::Run() { DoRead(); }

void HttpSession::DoRead() {
  auto self = shared_from_this();
  req_ = {};
  stream_.expires_after(std::chrono::seconds(30));
  boost::beast::http::async_read(
      stream_, buffer_, req_,
      [self](boost::beast::error_code ec, std::size_t bytes_transferred) {
        self->OnRead(ec, bytes_transferred);
      });
}

void HttpSession::OnRead(boost::beast::error_code ec, std::size_t /*bytes_transferred*/) {
  if (ec == boost::beast::http::error::end_of_stream) {
    boost::beast::error_code ignored;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ignored);
    return;
  }
  if (ec) {
    return;
  }

  if (boost::beast::websocket::is_upgrade(req_)) {
    return HandleWebSocket();
  }

  HandleRequest();
}

void HttpSession::HandleRequest() {
  using namespace boost::beast;
  request_start_ = std::chrono::steady_clock::now();
  trace_id_ = observability_ ? observability_->NextTraceId() : std::string{};
  if (observability_) {
    observability_->IncrementRequest();
  }
  auto res = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();
  res->version(req_.version());
  res->set(http::field::server, "codex-gameserver");
  res->set(http::field::content_type, "application/json; charset=utf-8");

  std::string target_str = std::string(req_.target());
  std::string path = target_str;
  std::string query;
  auto qpos = target_str.find('?');
  if (qpos != std::string::npos) {
    path = target_str.substr(0, qpos);
    query = target_str.substr(qpos + 1);
  }

  if (req_.method() == http::verb::get && path == "/api/health") {
    nlohmann::json payload{{"status", "ok"}, {"version", "v1.0.0"}};
    auto body = MakeSuccessEnvelope(payload).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::get && path == "/metrics") {
    auto snapshot = observability_->Snapshot(session_manager_->ActiveSessionCount(), match_queue_->QueueLength());
    nlohmann::json data{{"requests", { {"total", snapshot.request_total}, {"errors", snapshot.request_errors} }},
                        {"connections", {{"websocket", snapshot.websocket_active}}},
                        {"sessions", {{"active", snapshot.active_sessions}}},
                        {"queue", {{"length", snapshot.queue_length}}}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::get && path == "/ops/status") {
    auto header_it = req_.base().find("X-Ops-Token");
    std::string header_token = header_it == req_.base().end() ? std::string() : std::string(header_it->value());
    if (config_.ops_token.empty() || header_token != config_.ops_token) {
      res->result(http::status::unauthorized);
      auto body = MakeErrorEnvelope("unauthorized", "운영 토큰이 올바르지 않습니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    auto snapshot = observability_->Snapshot(session_manager_->ActiveSessionCount(), match_queue_->QueueLength());
    nlohmann::json data{{"activeSessions", snapshot.active_sessions},
                        {"queueLength", snapshot.queue_length},
                        {"activeWebsocket", snapshot.websocket_active},
                        {"errorCount", snapshot.request_errors}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::post && path == "/api/auth/register") {
    try {
      auto body_json = nlohmann::json::parse(req_.body());
      if (!body_json.contains("username") || !body_json.contains("password") ||
          !body_json["username"].is_string() || !body_json["password"].is_string()) {
        throw std::runtime_error("invalid body");
      }
      std::string error_code;
      std::string error_message;
      auto user = auth_service_->RegisterUser(body_json["username"].get<std::string>(),
                                              body_json["password"].get<std::string>(),
                                              error_code, error_message);
      if (!user) {
        res->result(error_code == "bad_request" ? http::status::bad_request : http::status::conflict);
        auto body = MakeErrorEnvelope(error_code, error_message).dump();
        res->body() = body;
        res->content_length(body.size());
        return SendResponse(res);
      }
      rating_service_->EnsureUser(user->user_id, user->username);
      nlohmann::json data{{"userId", user->user_id}, {"username", user->username}};
      auto body = MakeSuccessEnvelope(data).dump();
      res->result(http::status::created);
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    } catch (const std::exception&) {
      res->result(http::status::bad_request);
      auto body = MakeErrorEnvelope("bad_request", "JSON 본문이 올바르지 않습니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
  }

  if (req_.method() == http::verb::post && path == "/api/auth/login") {
    try {
      auto body_json = nlohmann::json::parse(req_.body());
      if (!body_json.contains("username") || !body_json.contains("password") ||
          !body_json["username"].is_string() || !body_json["password"].is_string()) {
        throw std::runtime_error("invalid body");
      }
      std::string error_code;
      std::string error_message;
      auto ip = RemoteIp();
      auto session = auth_service_->Login(body_json["username"].get<std::string>(),
                                          body_json["password"].get<std::string>(), ip,
                                          error_code, error_message);
      if (!session) {
        if (error_code == "rate_limited") {
          res->result(http::status::too_many_requests);
        } else {
          res->result(http::status::unauthorized);
        }
        auto body = MakeErrorEnvelope(error_code, error_message).dump();
        res->body() = body;
        res->content_length(body.size());
        return SendResponse(res);
      }
      rating_service_->EnsureUser(session->user.user_id, session->user.username);
      nlohmann::json user_info{{"userId", session->user.user_id}, {"username", session->user.username}};
      nlohmann::json data{{"token", session->token},
                          {"expiresAt", ToIsoString(session->expires_at)},
                          {"user", user_info}};
      auto body = MakeSuccessEnvelope(data).dump();
      res->result(http::status::ok);
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    } catch (const std::exception&) {
      res->result(http::status::bad_request);
      auto body = MakeErrorEnvelope("bad_request", "JSON 본문이 올바르지 않습니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
  }

  if (req_.method() == http::verb::post && path == "/api/auth/logout") {
    auto session = ExtractAuthSession();
    if (!session) {
      res->result(http::status::unauthorized);
      auto body = MakeErrorEnvelope("unauthorized", "인증이 필요합니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    auth_service_->Logout(session->token);
    nlohmann::json data{{"loggedOut", true}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::post && path == "/api/queue/join") {
    auto session = ExtractAuthSession();
    if (!session) {
      res->result(http::status::unauthorized);
      auto body = MakeErrorEnvelope("unauthorized", "인증이 필요합니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    try {
      auto body_json = nlohmann::json::parse(req_.body());
      if (!body_json.contains("mode") || !body_json["mode"].is_string()) {
        throw std::runtime_error("mode required");
      }
      std::string mode = body_json["mode"].get<std::string>();
      if (mode != "normal") {
        throw std::runtime_error("mode invalid");
      }
      std::chrono::seconds timeout{config_.match_queue_timeout_seconds};
      if (body_json.contains("timeoutSeconds")) {
        if (!body_json["timeoutSeconds"].is_number_unsigned()) {
          throw std::runtime_error("timeout invalid");
        }
        timeout = std::chrono::seconds(body_json["timeoutSeconds"].get<std::uint64_t>());
      }
      std::string error_code;
      std::string error_message;
      if (!match_queue_->Join(session->user, timeout, error_code, error_message)) {
        res->result(error_code == "queue_duplicate" ? http::status::conflict : http::status::bad_request);
        auto body = MakeErrorEnvelope(error_code, error_message).dump();
        res->body() = body;
        res->content_length(body.size());
        return SendResponse(res);
      }
      nlohmann::json data{{"queued", true},
                          {"mode", mode},
                          {"expiresAt", ToIsoString(std::chrono::system_clock::now() + timeout)}};
      auto body = MakeSuccessEnvelope(data).dump();
      res->result(http::status::ok);
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    } catch (const std::exception&) {
      res->result(http::status::bad_request);
      auto body = MakeErrorEnvelope("bad_request", "mode 또는 timeoutSeconds가 올바르지 않습니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
  }

  if (req_.method() == http::verb::post && path == "/api/queue/cancel") {
    auto session = ExtractAuthSession();
    if (!session) {
      res->result(http::status::unauthorized);
      auto body = MakeErrorEnvelope("unauthorized", "인증이 필요합니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    std::string error_code;
    std::string error_message;
    if (!match_queue_->Cancel(session->user.user_id, error_code, error_message)) {
      res->result(http::status::not_found);
      auto body = MakeErrorEnvelope(error_code, error_message).dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    nlohmann::json data{{"canceled", true}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::get && path == "/api/leaderboard") {
    std::size_t page = 1;
    std::size_t size = 10;
    auto params = ParseQueryParams(query);
    auto parse_or_flag = [&](const std::string& key, std::size_t& dest, std::size_t min, std::size_t max,
                             bool& invalid) {
      auto it = params.find(key);
      if (it == params.end()) {
        return;
      }
      auto parsed = ParsePositiveInt(it->second);
      if (!parsed || *parsed < min || *parsed > max) {
        invalid = true;
        return;
      }
      dest = *parsed;
    };

    bool invalid = false;
    parse_or_flag("page", page, 1, std::numeric_limits<std::size_t>::max(), invalid);
    parse_or_flag("size", size, 1, 50, invalid);
    if (invalid) {
      res->result(http::status::bad_request);
      auto body = MakeErrorEnvelope("leaderboard_range", "page 또는 size 값이 허용 범위를 벗어났습니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }

    auto page_data = rating_service_->GetLeaderboard(page, size);
    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t i = 0; i < page_data.entries.size(); ++i) {
      const auto& e = page_data.entries[i];
      entries.push_back({{"rank", static_cast<int>((page - 1) * size + i + 1)},
                         {"userId", e.user_id},
                         {"username", e.username},
                         {"rating", e.rating},
                         {"wins", e.wins},
                         {"losses", e.losses},
                         {"matches", e.matches()}});
    }
    nlohmann::json data{{"page", page}, {"size", size}, {"total", page_data.total}, {"entries", entries}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  if (req_.method() == http::verb::get && path == "/api/profile") {
    auto session = ExtractAuthSession();
    if (!session) {
      res->result(http::status::unauthorized);
      auto body = MakeErrorEnvelope("unauthorized", "인증이 필요합니다").dump();
      res->body() = body;
      res->content_length(body.size());
      return SendResponse(res);
    }
    auto summary = rating_service_->GetSummary(session->user.user_id);
    int rating = summary ? summary->rating : 1000;
    int wins = summary ? summary->wins : 0;
    int losses = summary ? summary->losses : 0;
    int matches = wins + losses;
    nlohmann::json data{{"userId", session->user.user_id},
                       {"username", session->user.username},
                       {"rating", rating},
                       {"wins", wins},
                       {"losses", losses},
                       {"matches", matches}};
    auto body = MakeSuccessEnvelope(data).dump();
    res->result(http::status::ok);
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }

  res->result(http::status::not_found);
  auto body = MakeErrorEnvelope("not_found", "지원되지 않는 경로입니다").dump();
  res->body() = body;
  res->content_length(body.size());
  SendResponse(res);
}

void HttpSession::SendResponse(std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> res) {
  auto self = shared_from_this();
  if (observability_) {
    if (static_cast<unsigned>(res->result_int()) >= 400) {
      observability_->IncrementError();
    }
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - request_start_)
                       .count();
    observability_->Log(LogContext{trace_id_, std::nullopt, std::nullopt, std::string(req_.target()), latency});
  }
  boost::beast::http::async_write(
      stream_, *res,
      [self, res](boost::beast::error_code ec, std::size_t /*bytes_transferred*/) {
        if (ec) {
          return;
        }
        self->stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
      });
}

void HttpSession::HandleWebSocket() {
  auto self = shared_from_this();
  auto session = ExtractAuthSession();
  if (!session) {
    auto res = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();
    res->version(req_.version());
    res->result(boost::beast::http::status::unauthorized);
    res->set(boost::beast::http::field::content_type, "application/json; charset=utf-8");
    auto body = MakeErrorEnvelope("unauthorized", "WS 업그레이드에는 인증이 필요합니다").dump();
    res->body() = body;
    res->content_length(body.size());
    return SendResponse(res);
  }
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{std::move(stream_)};
  ws.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
  ws.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::response_type& res) {
    res.set(boost::beast::http::field::server, "codex-gameserver");
  }));
  try {
    ws.accept(req_);
    std::make_shared<WebSocketSession>(std::move(ws), *session, reconnect_service_, coordinator_, session_manager_,
                                       config_.ws_queue_limit_messages, config_.ws_queue_limit_bytes)
        ->Run();
  } catch (const std::exception&) {
    boost::beast::error_code ec;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
  }
}

std::optional<AuthSession> HttpSession::ExtractAuthSession() {
  auto auth_it = req_.find(boost::beast::http::field::authorization);
  if (auth_it == req_.end()) {
    return std::nullopt;
  }
  auto token = ParseBearer(std::string(auth_it->value()));
  if (token.empty()) {
    return std::nullopt;
  }
  return auth_service_->ValidateToken(token);
}

std::string HttpSession::RemoteIp() {
  boost::beast::error_code ec;
  auto endpoint = stream_.socket().remote_endpoint(ec);
  if (ec) {
    return "unknown";
  }
  return endpoint.address().to_string();
}

std::string HttpSession::ParseBearer(const std::string& header_value) {
  const std::string prefix = "Bearer ";
  if (header_value.size() <= prefix.size()) {
    return "";
  }
  if (header_value.compare(0, prefix.size(), prefix) != 0) {
    return "";
  }
  return header_value.substr(prefix.size());
}

}  // namespace server
