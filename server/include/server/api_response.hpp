/*
 * 설명: REST/WS 응답 엔벨로프 생성을 담당한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/unit/json_envelope_test.cpp
 */
#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace server {

nlohmann::json MakeSuccessEnvelope(const nlohmann::json& data);
nlohmann::json MakeErrorEnvelope(std::string_view code, std::string_view message);

struct WsEnvelope {
  std::string type;
  std::string event;
  std::uint64_t seq;
  nlohmann::json payload;
};

nlohmann::json ToWsJson(const WsEnvelope& env);

}  // namespace server
