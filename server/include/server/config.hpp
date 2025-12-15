/*
 * 설명: 서버 환경설정 로딩과 기본값을 정의한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp
 */
#pragma once

#include <cstddef>
#include <string>

namespace server {

struct AppConfig {
  unsigned short port;
  std::string db_host;
  unsigned short db_port;
  std::string db_user;
  std::string db_password;
  std::string db_name;
  std::string redis_host;
  unsigned short redis_port;
  std::string log_level;
  std::size_t auth_token_ttl_seconds;
  std::size_t login_rate_window_seconds;
  std::size_t login_rate_limit_max;
  std::size_t ws_queue_limit_messages;
  std::size_t ws_queue_limit_bytes;
  std::size_t match_queue_timeout_seconds;
  std::size_t session_tick_interval_ms;
  std::string ops_token;
};

AppConfig LoadConfigFromEnv();

}  // namespace server
