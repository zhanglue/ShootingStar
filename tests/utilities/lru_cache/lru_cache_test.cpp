#include "src/utilities/lru_cache/lru_cache.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace shooting_star {
namespace utilities {
namespace {

using ::std::string;
using ::std::chrono::milliseconds;

TEST(LruCacheTest, EvictsLeastRecentlyUsedEntry) {
  LruCache<int, string> cache(2, milliseconds(1000));

  cache.Put(1, "one");
  cache.Put(2, "two");
  ASSERT_TRUE(cache.Get(1).has_value());
  cache.Put(3, "three");

  EXPECT_TRUE(cache.Get(1).has_value());
  EXPECT_FALSE(cache.Get(2).has_value());
  const auto value = cache.Get(3);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "three");
}

TEST(LruCacheTest, ExpiredEntryReturnsNullptr) {
  LruCache<int, string> cache(2, milliseconds(1));
  cache.Put(1, "one");

  ::std::this_thread::sleep_for(milliseconds(2));

  EXPECT_FALSE(cache.Get(1).has_value());
  EXPECT_EQ(cache.size(), 0);
}

TEST(LruCacheTest, SupportsConcurrentGetAndPut) {
  LruCache<int, string> cache(32, milliseconds(1000));
  ::std::atomic<int> ready = 0;
  ::std::atomic<bool> saw_bad_value = false;
  ::std::vector<::std::thread> threads;

  for (int worker = 0; worker < 8; ++worker) {
    threads.emplace_back([worker, &cache, &ready, &saw_bad_value]() {
      ++ready;
      while (ready.load() < 8) {
        ::std::this_thread::yield();
      }

      for (int i = 0; i < 1000; ++i) {
        const int key = (worker + i) % 16;
        cache.Put(key, "value");
        const auto value = cache.Get(key);
        if (value.has_value() && *value != "value") {
          saw_bad_value = true;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_FALSE(saw_bad_value.load());
  EXPECT_LE(cache.size(), cache.capacity());
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
