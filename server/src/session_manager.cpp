/*
 * 설명: 세션 생성과 틱 루프, 입력 처리, 결과/레이팅 저장을 조율한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/session_flow_test.cpp, server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/session_manager.hpp"

#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <limits>
#include <sstream>

#include <boost/asio/bind_executor.hpp>

namespace server {
namespace {
nlohmann::json BuildStatePayload(const Simulation& simulation) {
  nlohmann::json players_json = nlohmann::json::array();
  auto snapshot = simulation.Snapshot();
  for (const auto& player : snapshot["players"]) {
    players_json.push_back(player);
  }
  return {{"tick", snapshot["tick"]}, {"players", players_json}};
}

std::string ToIsoString(std::chrono::system_clock::time_point tp) {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%FT%TZ");
  return oss.str();
}
}  // namespace

SessionManager::SessionManager(boost::asio::io_context& ioc, std::shared_ptr<RealtimeCoordinator> coordinator,
                               std::shared_ptr<ResultService> result_service, std::chrono::milliseconds tick_interval,
                               std::size_t max_ticks)
    : ioc_(ioc), coordinator_(std::move(coordinator)), result_service_(std::move(result_service)),
      tick_interval_(tick_interval), max_ticks_(max_ticks) {}

std::string SessionManager::CreateSession(const std::vector<SessionParticipant>& participants) {
  auto ctx = std::make_shared<SessionContext>(ioc_, tick_interval_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "session-" << next_session_id_++;
    ctx->id = oss.str();
    ctx->participants = participants;
    for (const auto& p : participants) {
      ctx->participant_map[p.user_id] = p;
      user_to_session_[p.user_id] = ctx->id;
      ctx->simulation.AddPlayer(p.user_id);
    }
    sessions_[ctx->id] = ctx;
  }

  boost::asio::dispatch(ctx->strand, [self = shared_from_this(), ctx]() { self->StartSession(ctx); });
  return ctx->id;
}

bool SessionManager::IsUserInSession(int user_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return user_to_session_.count(user_id) > 0;
}

bool SessionManager::SubmitInput(const SessionInput& input, std::string& error_code, std::string& error_message) {
  std::shared_ptr<SessionContext> ctx;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_to_session_.find(input.user_id);
    if (it == user_to_session_.end()) {
      error_code = "session_not_found";
      error_message = "세션을 찾을 수 없습니다";
      return false;
    }
    auto ctx_it = sessions_.find(it->second);
    if (ctx_it == sessions_.end()) {
      error_code = "session_not_found";
      error_message = "세션을 찾을 수 없습니다";
      return false;
    }
    ctx = ctx_it->second;
  }

  std::promise<bool> done;
  boost::asio::dispatch(ctx->strand, [ctx, input, &error_code, &error_message, &done]() {
    if (ctx->ended) {
      error_code = "session_closed";
      error_message = "세션이 이미 종료되었습니다";
      done.set_value(false);
      return;
    }
    if (ctx->participant_map.count(input.user_id) == 0) {
      error_code = "not_participant";
      error_message = "세션 참가자가 아닙니다";
      done.set_value(false);
      return;
    }
    InputCommand command;
    command.user_id = input.user_id;
    command.sequence = input.sequence;
    command.target_tick = input.target_tick;
    command.delta = input.delta;
    auto validation = ctx->simulation.EnqueueInput(command);
    if (!validation.accepted) {
      error_code = "input_invalid";
      error_message = validation.reason;
      done.set_value(false);
      return;
    }
    done.set_value(true);
  });

  return done.get_future().get();
}

void SessionManager::StartSession(const std::shared_ptr<SessionContext>& ctx) {
  nlohmann::json created_payload{{"sessionId", ctx->id}, {"createdAt", ToIsoString(std::chrono::system_clock::now())}};
  nlohmann::json participants_json = nlohmann::json::array();
  for (const auto& p : ctx->participants) {
    participants_json.push_back({{"userId", p.user_id}, {"username", p.username}});
  }
  created_payload["participants"] = participants_json;
  BroadcastToParticipants(ctx, "session.created", created_payload);

  nlohmann::json started_payload{{"sessionId", ctx->id}, {"tick", 0}, {"tickIntervalMs", tick_interval_.count()},
                                 {"state", BuildStatePayload(ctx->simulation)}};
  BroadcastToParticipants(ctx, "session.started", started_payload);
  ScheduleTick(ctx);
}

void SessionManager::BroadcastToParticipants(const std::shared_ptr<SessionContext>& ctx, const std::string& event,
                                             const nlohmann::json& payload) {
  for (const auto& p : ctx->participants) {
    coordinator_->SendEventToUser(p.user_id, event, payload);
  }
}

void SessionManager::ScheduleTick(const std::shared_ptr<SessionContext>& ctx) {
  ctx->timer.expires_after(ctx->tick_interval);
  auto self = shared_from_this();
  ctx->timer.async_wait(boost::asio::bind_executor(
      ctx->strand, [self, ctx](const boost::system::error_code& ec) {
        if (!ec) {
          self->HandleTick(ctx);
        }
      }));
}

void SessionManager::HandleTick(const std::shared_ptr<SessionContext>& ctx) {
  if (ctx->ended) {
    return;
  }
  ctx->simulation.TickOnce();
  ctx->tick_sent++;
  auto snapshot = ctx->simulation.Snapshot();
  nlohmann::json state_payload{{"sessionId", ctx->id},
                               {"tick", snapshot["tick"]},
                               {"players", snapshot["players"]},
                               {"issuedAt", ToIsoString(std::chrono::system_clock::now())}};
  BroadcastToParticipants(ctx, "session.state", state_payload);

  if (ctx->tick_sent >= max_ticks_) {
    FinishSession(ctx);
    return;
  }
  ScheduleTick(ctx);
}

void SessionManager::FinishSession(const std::shared_ptr<SessionContext>& ctx) {
  if (ctx->ended) {
    return;
  }
  ctx->ended = true;
  auto snapshot = ctx->simulation.Snapshot();
  int winner_user_id = 0;
  int best_position = std::numeric_limits<int>::min();
  for (const auto& player : snapshot["players"]) {
    int pos = player["position"].get<int>();
    int uid = player["userId"].get<int>();
    if (pos > best_position) {
      best_position = pos;
      winner_user_id = uid;
    }
  }
  nlohmann::json result_payload{{"sessionId", ctx->id},
                                {"reason", "completed"},
                                {"result", {{"winnerUserId", winner_user_id}, {"ticks", snapshot["tick"].get<int>()}}}};
  BroadcastToParticipants(ctx, "session.ended", result_payload);

  MatchResultRecord record{ctx->id,
                           ctx->participants.at(0).user_id,
                           ctx->participants.at(1).user_id,
                           winner_user_id,
                           snapshot["tick"].get<int>(),
                           std::chrono::system_clock::now(),
                           snapshot};
  result_service_->FinalizeResult(record, ctx->participants);

  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& p : ctx->participants) {
    user_to_session_.erase(p.user_id);
  }
  sessions_.erase(ctx->id);
}

std::size_t SessionManager::ActiveSessionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

}  // namespace server
