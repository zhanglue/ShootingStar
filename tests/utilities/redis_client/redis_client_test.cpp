#include "src/utilities/redis_client/redis_client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

namespace shooting_star {
namespace utilities {
namespace {

using ::std::chrono::milliseconds;

TEST(RedisClientTest, RejectsInvalidHost) {
  RedisClient::Config config;
  config.host = "";

  EXPECT_THROW(RedisClient client(config), ::std::invalid_argument);
}

TEST(RedisClientTest, RejectsInvalidPort) {
  RedisClient::Config config;
  config.port = 0;

  EXPECT_THROW(RedisClient client(config), ::std::invalid_argument);
}

TEST(RedisClientTest, RejectsInvalidPoolSize) {
  RedisClient::Config config;
  config.pool_size = 0;

  EXPECT_THROW(RedisClient client(config), ::std::invalid_argument);
}

TEST(RedisClientTest, RejectsInvalidRetryAttempts) {
  RedisClient::Config config;
  config.retry.max_attempts = 0;

  EXPECT_THROW(RedisClient client(config), ::std::invalid_argument);
}

TEST(RedisClientTest, RejectsNegativeTimeout) {
  RedisClient::Config config;
  config.socket_timeout = milliseconds(-1);

  EXPECT_THROW(RedisClient client(config), ::std::invalid_argument);
}

TEST(RedisClientTest, EmptyMGetReturnsOkWithoutRedisRoundTrip) {
  RedisClient client(RedisClient::Config{});

  RedisStringListResult result = client.MGet({});

  EXPECT_TRUE(result.status.ok);
  EXPECT_EQ(result.status.error_code, RedisErrorCode::kNone);
  EXPECT_EQ(result.status.attempts, 0);
  EXPECT_TRUE(result.values.empty());
}

TEST(RedisClientTest, BuildsRedisKeyFromPrefixAndItemId) {
  EXPECT_EQ(RedisClient::BuildRedisKey("rec:item_cf:v1:neighbors", 42),
            "rec:item_cf:v1:neighbors:42");
  EXPECT_EQ(RedisClient::BuildRedisKey("rec:item_cf:v1:neighbors:", 42),
            "rec:item_cf:v1:neighbors:42");
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
