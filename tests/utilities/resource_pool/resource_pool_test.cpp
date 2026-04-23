#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

#include "src/utilities/resource_pool/resource_pool.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::std::chrono::milliseconds;

struct TestResource {
  explicit TestResource(int value_in) : value(value_in) {}

  int value;
};

TEST(ResourcePoolTest, AcquireBorrowsAndReturnsResource) {
  ResourcePool<TestResource>::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(10);
  ResourcePool<TestResource> pool(
      config, [] { return ::std::make_unique<TestResource>(7); });

  EXPECT_EQ(pool.size(), 1);
  EXPECT_EQ(pool.available(), 1);

  {
    ::std::optional<ResourcePool<TestResource>::Lease> lease = pool.Acquire();
    ASSERT_TRUE(lease.has_value());
    ASSERT_NE(lease->get(), nullptr);
    EXPECT_EQ(lease->get()->value, 7);
    EXPECT_EQ(pool.available(), 0);
  }

  EXPECT_EQ(pool.available(), 1);
}

TEST(ResourcePoolTest, AcquireTimesOutWhenAllResourcesAreBorrowed) {
  ResourcePool<TestResource>::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(5);
  ResourcePool<TestResource> pool(
      config, [] { return ::std::make_unique<TestResource>(3); });

  ::std::optional<ResourcePool<TestResource>::Lease> first_lease = pool.Acquire();
  ASSERT_TRUE(first_lease.has_value());

  ::std::optional<ResourcePool<TestResource>::Lease> second_lease = pool.Acquire();
  EXPECT_FALSE(second_lease.has_value());
}

TEST(ResourcePoolTest, MoveLeaseKeepsSingleOwnership) {
  ResourcePool<TestResource>::Config config;
  config.pool_size = 1;
  config.acquire_timeout = milliseconds(10);
  ResourcePool<TestResource> pool(
      config, [] { return ::std::make_unique<TestResource>(11); });

  {
    ::std::optional<ResourcePool<TestResource>::Lease> first_lease = pool.Acquire();
    ASSERT_TRUE(first_lease.has_value());
    ResourcePool<TestResource>::Lease moved_lease = ::std::move(*first_lease);

    EXPECT_FALSE(static_cast<bool>(*first_lease));
    EXPECT_TRUE(static_cast<bool>(moved_lease));
    EXPECT_EQ(moved_lease->value, 11);
    EXPECT_EQ(pool.available(), 0);
  }

  EXPECT_EQ(pool.available(), 1);
}

TEST(ResourcePoolTest, MoveAssignLeaseReturnsPreviousResource) {
  ResourcePool<TestResource>::Config config;
  config.pool_size = 2;
  config.acquire_timeout = milliseconds(10);
  int next_value = 1;
  ResourcePool<TestResource> pool(
      config, [&next_value] {
        return ::std::make_unique<TestResource>(next_value++);
      });

  {
    ::std::optional<ResourcePool<TestResource>::Lease> first_lease = pool.Acquire();
    ASSERT_TRUE(first_lease.has_value());
    ::std::optional<ResourcePool<TestResource>::Lease> second_lease = pool.Acquire();
    ASSERT_TRUE(second_lease.has_value());
    EXPECT_EQ(pool.available(), 0);

    ResourcePool<TestResource>::Lease reassigned = ::std::move(*first_lease);
    EXPECT_FALSE(static_cast<bool>(*first_lease));
    EXPECT_EQ(pool.available(), 0);

    reassigned = ::std::move(*second_lease);
    EXPECT_FALSE(static_cast<bool>(*second_lease));
    EXPECT_TRUE(static_cast<bool>(reassigned));
    EXPECT_EQ(pool.available(), 1);
  }

  EXPECT_EQ(pool.available(), 2);
}

TEST(ResourcePoolTest, RejectsZeroPoolSize) {
  ResourcePool<TestResource>::Config config;
  config.pool_size = 0;
  config.acquire_timeout = milliseconds(10);

  EXPECT_THROW(
      ResourcePool<TestResource>(config, [] {
        return ::std::make_unique<TestResource>(1);
      }),
      ::std::invalid_argument);
}

TEST(ResourcePoolTest, RejectsEmptyFactory) {
  ResourcePool<TestResource>::Factory factory;
  EXPECT_THROW(ResourcePool<TestResource>(std::move(factory)),
               ::std::invalid_argument);
}

TEST(ResourcePoolTest, RejectsNullResourceFromFactory) {
  EXPECT_THROW(
      ResourcePool<TestResource>([] { return ::std::unique_ptr<TestResource>(); }),
      ::std::runtime_error);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
