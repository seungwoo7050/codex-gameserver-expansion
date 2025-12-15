#include <gtest/gtest.h>

#include "server/api_response.hpp"

TEST(JsonEnvelopeTest, SuccessShape) {
  nlohmann::json payload{{"status", "ok"}};
  auto env = server::MakeSuccessEnvelope(payload);
  EXPECT_TRUE(env["success"].get<bool>());
  EXPECT_EQ(env["data"], payload);
  EXPECT_TRUE(env["error"].is_null());
  EXPECT_TRUE(env.contains("meta"));
  EXPECT_TRUE(env["meta"].contains("timestamp"));
}

TEST(JsonEnvelopeTest, ErrorShape) {
  auto env = server::MakeErrorEnvelope("bad_request", "에러");
  EXPECT_FALSE(env["success"].get<bool>());
  EXPECT_TRUE(env["data"].is_null());
  EXPECT_EQ(env["error"]["code"], "bad_request");
  EXPECT_EQ(env["error"]["message"], "에러");
  EXPECT_TRUE(env["error"].contains("detail"));
}
