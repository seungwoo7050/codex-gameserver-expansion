cmake_minimum_required(VERSION 3.16)
project(codex_gameserver VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost 1.70 REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(server_core
  src/api_response.cpp
  src/auth.cpp
  src/app.cpp
  src/match_queue.cpp
  src/http_session.cpp
  src/reconnect.cpp
  src/realtime.cpp
  src/observability.cpp
  src/rating.cpp
  src/result_repository.cpp
  src/result_service.cpp
  src/session_manager.cpp
  src/simulation.cpp
  src/websocket_session.cpp
)

target_include_directories(server_core PUBLIC include)

target_link_libraries(server_core
  PUBLIC Boost::system Boost::thread Threads::Threads nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto
)

target_compile_definitions(server_core PRIVATE BOOST_BEAST_USE_STD_STRING_VIEW)

add_executable(server_app src/main.cpp)

target_link_libraries(server_app PRIVATE server_core)

enable_testing()

add_executable(unit_json_envelope_test tests/unit/json_envelope_test.cpp)
target_link_libraries(unit_json_envelope_test PRIVATE server_core GTest::gtest_main)

add_executable(unit_simulation_determinism_test tests/unit/simulation_determinism_test.cpp)
target_link_libraries(unit_simulation_determinism_test PRIVATE server_core GTest::gtest_main)

add_executable(unit_rating_update_test tests/unit/rating_update_test.cpp)
target_link_libraries(unit_rating_update_test PRIVATE server_core GTest::gtest_main)

add_executable(e2e_auth_flow_test tests/e2e/auth_flow_test.cpp)
target_link_libraries(e2e_auth_flow_test PRIVATE server_core GTest::gtest_main Boost::system Boost::thread)

add_executable(e2e_reconnect_backpressure_test tests/e2e/reconnect_backpressure_test.cpp)
target_link_libraries(e2e_reconnect_backpressure_test PRIVATE server_core GTest::gtest_main Boost::system Boost::thread)

add_executable(e2e_session_flow_test tests/e2e/session_flow_test.cpp)
target_link_libraries(e2e_session_flow_test PRIVATE server_core GTest::gtest_main Boost::system Boost::thread)

add_executable(e2e_rating_leaderboard_test tests/e2e/rating_leaderboard_test.cpp)
target_link_libraries(e2e_rating_leaderboard_test PRIVATE server_core GTest::gtest_main Boost::system Boost::thread)
add_executable(e2e_metrics_ops_test tests/e2e/metrics_ops_test.cpp)
target_link_libraries(e2e_metrics_ops_test PRIVATE server_core GTest::gtest_main Boost::system Boost::thread)

include(GoogleTest)
gtest_discover_tests(unit_json_envelope_test)
gtest_discover_tests(unit_simulation_determinism_test)
gtest_discover_tests(unit_rating_update_test)
gtest_discover_tests(e2e_auth_flow_test)
gtest_discover_tests(e2e_reconnect_backpressure_test)
gtest_discover_tests(e2e_session_flow_test)
gtest_discover_tests(e2e_rating_leaderboard_test)
gtest_discover_tests(e2e_metrics_ops_test)
