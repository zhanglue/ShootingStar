#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

#include "src/utilities/http_client/curl_handle_pool.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::std::chrono::milliseconds;

TEST(CurlHandlePoolTest, AcquireBorrowsAndReturnsHandle) {
  CurlHandlePool::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(10);
  CurlHandlePool pool(config);

  EXPECT_EQ(pool.size(), 1);
  EXPECT_EQ(pool.available(), 1);

  {
    ::std::optional<CurlHandlePool::Lease> lease = pool.Acquire();
    ASSERT_TRUE(lease.has_value());
    EXPECT_NE(lease->get(), nullptr);
    EXPECT_EQ(pool.available(), 0);
  }

  EXPECT_EQ(pool.available(), 1);
}

TEST(CurlHandlePoolTest, AcquireTimesOutWhenAllHandlesAreBorrowed) {
  CurlHandlePool::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(5);
  CurlHandlePool pool(config);

  ::std::optional<CurlHandlePool::Lease> first_lease = pool.Acquire();
  ASSERT_TRUE(first_lease.has_value());

  ::std::optional<CurlHandlePool::Lease> second_lease = pool.Acquire();
  EXPECT_FALSE(second_lease.has_value());
}

TEST(CurlHandlePoolTest, MoveLeaseKeepsSingleOwnership) {
  CurlHandlePool::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(10);
  CurlHandlePool pool(config);

  {
    ::std::optional<CurlHandlePool::Lease> first_lease = pool.Acquire();
    ASSERT_TRUE(first_lease.has_value());
    CurlHandlePool::Lease moved_lease = ::std::move(*first_lease);

    EXPECT_FALSE(static_cast<bool>(*first_lease));
    EXPECT_TRUE(static_cast<bool>(moved_lease));
    EXPECT_EQ(pool.available(), 0);
  }

  EXPECT_EQ(pool.available(), 1);
}

TEST(CurlHandlePoolTest, RejectsZeroPoolSize) {
  CurlHandlePool::Config config;
  config.pool_size = 0;
  config.acquire_timeout = milliseconds(10);
  EXPECT_THROW(CurlHandlePool pool(config), ::std::invalid_argument);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
