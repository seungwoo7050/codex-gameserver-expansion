/*
 * 설명: 서버 진입점으로 환경설정을 로드해 실행한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp
 */
#include <csignal>
#include <iostream>

#include "server/app.hpp"

int main() {
  using namespace server;
  AppConfig config = LoadConfigFromEnv();
  ServerApp app(config);

  std::signal(SIGINT, [](int) {
    std::cout << "SIGINT 수신, 종료를 준비합니다\n";
  });

  app.Run();
  return 0;
}
