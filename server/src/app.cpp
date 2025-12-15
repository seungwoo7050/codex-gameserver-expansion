/*
 * 설명: 서버 수명주기와 리스닝 스레드를 관리한다.
 * 버전: v1.1.0
 * 관련 문서: design/protocol/contract.md
 * 테스트: server/tests/e2e/auth_flow_test.cpp, server/tests/e2e/reconnect_backpressure_test.cpp,
 *         server/tests/e2e/session_flow_test.cpp
 */
#include "server/app.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>

#include "server/http_session.hpp"
#include "server/observability.hpp"

namespace server {

class Listener : public std::enable_shared_from_this<Listener> {
 public:
 Listener(boost::asio::io_context& ioc, const boost::asio::ip::tcp::endpoint& endpoint,
           const AppConfig& config, std::shared_ptr<AuthService> auth_service,
           std::shared_ptr<ReconnectService> reconnect_service, std::shared_ptr<RealtimeCoordinator> coordinator,
           std::shared_ptr<SessionManager> session_manager, std::shared_ptr<MatchQueueService> match_queue,
           std::shared_ptr<RatingService> rating_service, std::shared_ptr<Observability> observability)
      : ioc_(ioc), acceptor_(boost::asio::make_strand(ioc)), config_(config), auth_service_(std::move(auth_service)),
        reconnect_service_(std::move(reconnect_service)), coordinator_(std::move(coordinator)),
        session_manager_(std::move(session_manager)), match_queue_(std::move(match_queue)),
        rating_service_(std::move(rating_service)), observability_(std::move(observability)) {
    boost::beast::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      throw boost::beast::system_error{ec};
    }

    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
      throw boost::beast::system_error{ec};
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
      throw boost::beast::system_error{ec};
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
      throw boost::beast::system_error{ec};
    }
  }

  void Run() { DoAccept(); }

  void Stop() {
    boost::beast::error_code ec;
    acceptor_.close(ec);
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        boost::asio::make_strand(ioc_),
        [self = shared_from_this()](boost::beast::error_code ec, boost::asio::ip::tcp::socket socket) {
          if (!ec) {
            std::make_shared<HttpSession>(std::move(socket),
                                          self->config_,
                                          self->auth_service_,
                                          self->reconnect_service_,
                                          self->coordinator_,
                                          self->session_manager_,
                                          self->match_queue_,
                                          self->rating_service_,
                                          self->observability_)
                ->Run();
          }
          if (self->acceptor_.is_open()) {
            self->DoAccept();
          }
        });
  }

  boost::asio::io_context& ioc_;
  boost::asio::ip::tcp::acceptor acceptor_;
  AppConfig config_;
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<ReconnectService> reconnect_service_;
  std::shared_ptr<RealtimeCoordinator> coordinator_;
  std::shared_ptr<SessionManager> session_manager_;
  std::shared_ptr<MatchQueueService> match_queue_;
  std::shared_ptr<RatingService> rating_service_;
  std::shared_ptr<Observability> observability_;
};

ServerApp::ServerApp(const AppConfig& config)
    : config_(config), ioc_(1), work_guard_(boost::asio::make_work_guard(ioc_)) {
  AuthConfig auth_config;
  auth_config.token_ttl = std::chrono::seconds(config.auth_token_ttl_seconds);
  auth_config.login_window = std::chrono::seconds(config.login_rate_window_seconds);
  auth_config.login_max_attempts = config.login_rate_limit_max;
  auth_service_ = std::make_shared<AuthService>(auth_config);
  reconnect_service_ = std::make_shared<ReconnectService>();
  observability_ = std::make_shared<Observability>();
  coordinator_ = std::make_shared<RealtimeCoordinator>();
  coordinator_->SetObservability(observability_);
  DbConfig db_config{config.db_host, config.db_port, config.db_user, config.db_password, config.db_name};
  db_client_ = std::make_shared<MariaDbClient>(db_config);
  rating_service_ = std::make_shared<RatingService>(db_client_);
  result_repository_ = std::make_shared<ResultRepository>(db_client_);
  result_service_ = std::make_shared<ResultService>(db_client_, result_repository_, rating_service_);
  session_manager_ = std::make_shared<SessionManager>(ioc_, coordinator_, result_service_,
                                                     std::chrono::milliseconds(config.session_tick_interval_ms), 5);
  match_queue_ = std::make_shared<MatchQueueService>(ioc_, session_manager_, coordinator_,
                                                     std::chrono::seconds(config.match_queue_timeout_seconds));
}

ServerApp::~ServerApp() { Stop(); }

void ServerApp::Run() {
  try {
    running_ = true;
    boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::tcp::v4(), config_.port};
    listener_ = std::make_shared<Listener>(ioc_, endpoint, config_, auth_service_, reconnect_service_, coordinator_,
                                           session_manager_, match_queue_, rating_service_, observability_);
    listener_->Run();
    std::cout << "서버 시작: 포트 " << config_.port << "\n";
    RunWorkers();
    ioc_.run();
  } catch (const std::exception& ex) {
    std::cerr << "서버 실행 중 예외: " << ex.what() << "\n";
  }
}

void ServerApp::RunWorkers() {
  const unsigned int thread_count = std::max(1u, std::thread::hardware_concurrency());
  // 현재 스레드도 run()을 호출하므로 워커는 thread_count - 1개만 생성한다.
  for (unsigned int i = 0; i + 1 < thread_count; ++i) {
    workers_.emplace_back([this]() { ioc_.run(); });
  }
}

void ServerApp::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  work_guard_.reset();
  if (listener_) {
    listener_->Stop();
  }
  ioc_.stop();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

AppConfig LoadConfigFromEnv() {
  auto get_env = [](const char* key, const char* def) -> std::string {
    const char* val = std::getenv(key);
    return val ? std::string{val} : std::string{def};
  };

  AppConfig cfg;
  cfg.port = static_cast<unsigned short>(std::stoi(get_env("SERVER_PORT", "8080")));
  cfg.db_host = get_env("DB_HOST", "mariadb");
  cfg.db_port = static_cast<unsigned short>(std::stoi(get_env("DB_PORT", "3306")));
  cfg.db_user = get_env("DB_USER", "app");
  cfg.db_password = get_env("DB_PASSWORD", "app_pass");
  cfg.db_name = get_env("DB_NAME", "app_db");
  cfg.redis_host = get_env("REDIS_HOST", "redis");
  cfg.redis_port = static_cast<unsigned short>(std::stoi(get_env("REDIS_PORT", "6379")));
  cfg.log_level = get_env("LOG_LEVEL", "info");
  cfg.auth_token_ttl_seconds = static_cast<std::size_t>(std::stoul(get_env("AUTH_TOKEN_TTL_SECONDS", "3600")));
  cfg.login_rate_window_seconds = static_cast<std::size_t>(std::stoul(get_env("LOGIN_RATE_LIMIT_WINDOW", "60")));
  cfg.login_rate_limit_max = static_cast<std::size_t>(std::stoul(get_env("LOGIN_RATE_LIMIT_MAX", "5")));
  cfg.ws_queue_limit_messages = static_cast<std::size_t>(std::stoul(get_env("WS_QUEUE_LIMIT_MESSAGES", "8")));
  cfg.ws_queue_limit_bytes = static_cast<std::size_t>(std::stoul(get_env("WS_QUEUE_LIMIT_BYTES", "65536")));
  cfg.match_queue_timeout_seconds = static_cast<std::size_t>(std::stoul(get_env("MATCH_QUEUE_TIMEOUT_SECONDS", "10")));
  cfg.session_tick_interval_ms = static_cast<std::size_t>(std::stoul(get_env("SESSION_TICK_INTERVAL_MS", "100")));
  cfg.ops_token = get_env("OPS_TOKEN", "");
  return cfg;
}

}  // namespace server
