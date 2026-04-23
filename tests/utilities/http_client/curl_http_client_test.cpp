#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>

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

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
