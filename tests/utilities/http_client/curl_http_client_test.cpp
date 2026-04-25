#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "src/utilities/http_client/curl_handle_pool.h"
#include "src/utilities/http_client/curl_http_client.h"
#include "src/utilities/http_client/http_client.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::std::chrono::milliseconds;

TEST(CurlHttpClientTest, RejectsNullPool) {
  EXPECT_THROW(CurlHttpClient(::std::shared_ptr<CurlHandlePool>()),
               ::std::invalid_argument);
}

TEST(CurlHttpClientTest, ReturnsInvalidArgumentForEmptyUrl) {
  CurlHttpClient client;

  const HttpResult result = client.Get("");

  EXPECT_EQ(result.error_code, HttpErrorCode::kInvalidArgument);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(CurlHttpClientTest, ReturnsPoolAcquireTimeoutWhenNoHandleAvailable) {
  CurlHandlePool::Config pool_config;
  pool_config.pool_size = 1;
  pool_config.acquire_timeout = milliseconds(5);
  auto pool = ::std::make_shared<CurlHandlePool>(pool_config);

  ::std::optional<CurlHandlePool::Lease> lease = pool->Acquire();
  ASSERT_TRUE(lease.has_value());

  CurlHttpClient client(pool);
  const HttpResult result = client.Get("http://127.0.0.1/");

  EXPECT_EQ(result.error_code, HttpErrorCode::kPoolAcquireTimeout);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(CurlHttpClientTest, RetriesCurlHandleAcquireTimeout) {
  CurlHandlePool::Config pool_config;
  pool_config.pool_size = 1;
  pool_config.acquire_timeout = milliseconds(10);
  auto pool = ::std::make_shared<CurlHandlePool>(pool_config);

  ::std::optional<CurlHandlePool::Lease> held_lease = pool->Acquire();
  ASSERT_TRUE(held_lease.has_value());

  ::std::thread releaser([&held_lease] {
    ::std::this_thread::sleep_for(milliseconds(15));
    held_lease.reset();
  });

  CurlHttpClient::Config config;
  config.acquire_retry.max_attempts = 2;
  config.connect_retry.max_attempts = 1;
  config.request_retry.max_attempts = 1;
  config.connect_timeout = milliseconds(5);
  config.request_timeout = milliseconds(5);
  CurlHttpClient client(pool, config);

  const HttpResult result = client.Get("http://127.0.0.1:1/");
  releaser.join();

  EXPECT_NE(result.error_code, HttpErrorCode::kPoolAcquireTimeout);
  EXPECT_EQ(pool->available(), 1);
}

TEST(CurlHttpClientTest, RejectsInvalidRetryConfig) {
  CurlHttpClient::Config config;
  config.acquire_retry.max_attempts = 0;

  EXPECT_THROW(CurlHttpClient(::std::move(config)), ::std::invalid_argument);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
