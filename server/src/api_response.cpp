/*
 * 설명: JSON 응답 엔벨로프를 생성하고 직렬화한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/unit/json_envelope_test.cpp
 */
#include "server/api_response.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace server {
namespace {
std::string CurrentTimestamp() {
  using clock = std::chrono::system_clock;
  auto now = clock::now();
  auto itt = clock::to_time_t(now);
  std::ostringstream ss;
  ss << std::put_time(std::gmtime(&itt), "%FT%TZ");
  return ss.str();
}
}  // namespace

nlohmann::json MakeSuccessEnvelope(const nlohmann::json& data) {
  nlohmann::json envelope;
  envelope["success"] = true;
  envelope["data"] = data;
  envelope["error"] = nullptr;
  envelope["meta"] = {{"timestamp", CurrentTimestamp()}};
  return envelope;
}

nlohmann::json MakeErrorEnvelope(std::string_view code, std::string_view message) {
  nlohmann::json envelope;
  envelope["success"] = false;
  envelope["data"] = nullptr;
  envelope["error"] = {{"code", code}, {"message", message}, {"detail", nullptr}};
  envelope["meta"] = {{"timestamp", CurrentTimestamp()}};
  return envelope;
}

nlohmann::json ToWsJson(const WsEnvelope& env) {
  nlohmann::json j;
  j["t"] = env.type;
  j["seq"] = env.seq;
  if (env.type == "event") {
    j["event"] = env.event;
  } else {
    j["event"] = nullptr;
  }
  j["p"] = env.payload;
  return j;
}

}  // namespace server
