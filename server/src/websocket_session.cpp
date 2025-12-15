/*
 * 설명: WebSocket 메시지를 읽고 백프레셔/리싱크/세션 입력을 처리하며 서버 이벤트를 전달한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp
 */
#include "server/websocket_session.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>

namespace server {
namespace {
std::string ToIsoString(std::chrono::system_clock::time_point tp) {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%FT%TZ");
  return oss.str();
}
}  // namespace

WebSocketSession::WebSocketSession(boost::beast::websocket::stream<boost::beast::tcp_stream> ws,
                                   const AuthSession& session, std::shared_ptr<ReconnectService> reconnect_service,
                                   std::shared_ptr<RealtimeCoordinator> coordinator,
                                   std::shared_ptr<SessionManager> session_manager, std::size_t max_queue_messages,
                                   std::size_t max_queue_bytes)
    : ws_(std::move(ws)), session_(session), reconnect_service_(std::move(reconnect_service)),
      coordinator_(std::move(coordinator)), session_manager_(std::move(session_manager)),
      max_queue_messages_(max_queue_messages), max_queue_bytes_(max_queue_bytes) {}

WebSocketSession::~WebSocketSession() { coordinator_->Unregister(session_.user.user_id, this); }

void WebSocketSession::Run() {
  snapshot_ = BuildSnapshot();
  resume_token_ = reconnect_service_->IssueToken(session_.user, snapshot_version_, snapshot_, std::nullopt);
  coordinator_->Register(session_.user.user_id, shared_from_this());
  SendAuthState();
  DoRead();
}

void WebSocketSession::DoRead() {
  if (closing_) {
    return;
  }
  auto self = shared_from_this();
  ws_.async_read(buffer_, [self](boost::beast::error_code ec, std::size_t bytes_transferred) {
    self->OnRead(ec, bytes_transferred);
  });
}

void WebSocketSession::OnRead(boost::beast::error_code ec, std::size_t /*bytes_transferred*/) {
  if (ec == boost::beast::websocket::error::closed || closing_) {
    return;
  }
  if (ec) {
    return;
  }

  auto data = boost::beast::buffers_to_string(buffer_.data());
  buffer_.consume(buffer_.size());
  try {
    auto message = nlohmann::json::parse(data);
    auto seq_it = message.find("seq");
    std::uint64_t seq = 0;
    if (seq_it != message.end() && seq_it->is_number_unsigned()) {
      seq = seq_it->get<std::uint64_t>();
    }
    auto type_it = message.find("t");
    auto event_it = message.find("event");
    if (type_it == message.end() || !type_it->is_string()) {
      SendError("bad_request", "잘못된 메시지 형식", seq);
      return DoRead();
    }
    if (*type_it == "event" && event_it != message.end() && event_it->is_string()) {
      if (*event_it == "echo") {
        auto payload_it = message.find("p");
        if (payload_it == message.end() || !payload_it->is_object()) {
          SendError("bad_request", "payload가 누락되었습니다", seq);
          return DoRead();
        }
        if (!payload_it->contains("message") || !(*payload_it)["message"].is_string()) {
          SendError("bad_request", "message 필드가 필요합니다", seq);
          return DoRead();
        }
        SendEcho(*payload_it, seq);
      } else if (*event_it == "resync_request") {
        auto payload_it = message.find("p");
        if (payload_it == message.end() || !payload_it->is_object() || !payload_it->contains("resumeToken") ||
            !(*payload_it)["resumeToken"].is_string()) {
          SendError("invalid_resume_token", "resumeToken이 필요합니다", seq);
          return DoRead();
        }
        HandleResyncRequest(*payload_it, seq);
      } else if (*event_it == "session.input") {
        auto payload_it = message.find("p");
        if (payload_it == message.end() || !payload_it->is_object()) {
          SendError("bad_request", "payload가 누락되었습니다", seq);
          return DoRead();
        }
        HandleSessionInput(*payload_it, seq);
      } else {
        SendError("bad_request", "알 수 없는 이벤트", seq);
      }
    } else {
      SendError("bad_request", "알 수 없는 메시지 유형", seq);
    }
  } catch (const std::exception&) {
    SendError("bad_request", "JSON 파싱 오류", 0);
  }

  if (!closing_) {
    DoRead();
  }
}

void WebSocketSession::SendEcho(const nlohmann::json& message, std::uint64_t seq) {
  nlohmann::json payload = message;
  payload["userId"] = session_.user.user_id;
  WsEnvelope env{.type = "event", .event = "echo", .seq = seq, .payload = payload};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::HandleResyncRequest(const nlohmann::json& message, std::uint64_t seq) {
  auto token = message["resumeToken"].get<std::string>();
  auto snapshot = reconnect_service_->Validate(token, session_.user);
  if (!snapshot) {
    SendError("invalid_resume_token", "이전 resumeToken이 유효하지 않습니다", seq);
    return;
  }
  snapshot_ = BuildSnapshot();
  resume_token_ = reconnect_service_->IssueToken(session_.user, snapshot_version_, snapshot_, token);
  SendResyncState(seq);
}

void WebSocketSession::HandleSessionInput(const nlohmann::json& message, std::uint64_t seq) {
  if (!message.contains("sessionId") || !message.contains("sequence") || !message.contains("targetTick") ||
      !message.contains("delta")) {
    SendError("bad_request", "필수 필드가 없습니다", seq);
    return;
  }
  if (!message["sessionId"].is_string() || !message["sequence"].is_number_unsigned() ||
      !message["targetTick"].is_number_integer() || !message["delta"].is_number_integer()) {
    SendError("bad_request", "필드 형식이 올바르지 않습니다", seq);
    return;
  }
  SessionInput input{message["sessionId"].get<std::string>(),
                     session_.user.user_id,
                     message["sequence"].get<std::uint64_t>(),
                     message["targetTick"].get<int>(),
                     message["delta"].get<int>()};
  std::string error_code;
  std::string error_message;
  if (!session_manager_->SubmitInput(input, error_code, error_message)) {
    SendError(error_code, error_message, seq);
  }
}

void WebSocketSession::SendError(std::string_view code, std::string_view message, std::uint64_t seq) {
  WsEnvelope env{.type = "error", .event = "", .seq = seq, .payload = {{"code", code}, {"message", message}}};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::SendServerEvent(const std::string& event, const nlohmann::json& payload) {
  WsEnvelope env{.type = "event", .event = event, .seq = 0, .payload = payload};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::SendServerError(const std::string& code, const std::string& message) {
  WsEnvelope env{.type = "error", .event = "", .seq = 0, .payload = {{"code", code}, {"message", message}}};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::SendResyncState(std::uint64_t seq) {
  WsEnvelope env{.type = "event",
                 .event = "resync_state",
                 .seq = seq,
                 .payload = {{"resumeToken", resume_token_}, {"snapshot", snapshot_}}};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::SendAuthState() {
  WsEnvelope env{.type = "event",
                 .event = "auth_state",
                 .seq = 0,
                 .payload = {{"userId", session_.user.user_id},
                             {"username", session_.user.username},
                             {"resumeToken", resume_token_},
                             {"snapshotVersion", snapshot_version_}}};
  EnqueueMessage(ToWsJson(env).dump());
}

void WebSocketSession::EnqueueMessage(std::string message) {
  if (closing_) {
    return;
  }
  const auto message_size = message.size();
  if (send_queue_.size() >= max_queue_messages_ || queued_bytes_ + message_size > max_queue_bytes_) {
    TriggerBackpressureClose();
    return;
  }
  send_queue_.push_back(std::move(message));
  queued_bytes_ += message_size;
  if (!writing_) {
    WriteNext();
  }
}

void WebSocketSession::WriteNext() {
  if (send_queue_.empty() || closing_) {
    return;
  }
  writing_ = true;
  auto self = shared_from_this();
  ws_.text(true);
  ws_.async_write(boost::asio::buffer(send_queue_.front()),
                  [self](boost::beast::error_code ec, std::size_t /*bytes_transferred*/) { self->OnWrite(ec); });
}

void WebSocketSession::OnWrite(boost::beast::error_code ec) {
  if (!send_queue_.empty()) {
    queued_bytes_ -= send_queue_.front().size();
    send_queue_.pop_front();
  }
  if (ec) {
    closing_ = true;
    return;
  }
  writing_ = false;
  if (!send_queue_.empty()) {
    WriteNext();
  }
}

void WebSocketSession::TriggerBackpressureClose() {
  if (closing_) {
    return;
  }
  closing_ = true;
  send_queue_.clear();
  queued_bytes_ = 0;
  boost::beast::websocket::close_reason reason{boost::beast::websocket::close_code::policy_error};
  reason.reason = "backpressure_exceeded";
  auto self = shared_from_this();
  ws_.async_close(reason, [self](boost::beast::error_code) {});
}

nlohmann::json WebSocketSession::BuildSnapshot() {
  return {{"version", snapshot_version_},
          {"state", "auth_only"},
          {"issuedAt", NowIsoString()},
          {"user", {{"userId", session_.user.user_id}, {"username", session_.user.username}}}};
}

std::string WebSocketSession::NowIsoString() { return ToIsoString(std::chrono::system_clock::now()); }

}  // namespace server
