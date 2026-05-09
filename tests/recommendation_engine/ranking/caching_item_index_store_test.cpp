#include "src/recommendation_engine/ranking/caching_item_index_store.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

namespace shooting_star::recommendation_engine {
namespace {

using ::std::chrono::milliseconds;
using ::std::optional;

class FakeItemIndexStore final : public ItemIndexStore {
 public:
  explicit FakeItemIndexStore(ItemIndexEntry entry) {
    entries_[entry.item_id] = ::std::move(entry);
  }

  optional<ItemIndexEntry> FindByItemId(uint64_t item_id) const override {
    ++lookup_count;
    const auto iter = entries_.find(item_id);
    if (iter == entries_.end()) {
      return ::std::nullopt;
    }
    return iter->second;
  }

  mutable int lookup_count = 0;

 private:
  ::std::unordered_map<uint64_t, ItemIndexEntry> entries_;
};

ItemIndexEntry CreateEntry(uint64_t item_id) {
  ItemIndexEntry entry;
  entry.item_id = item_id;
  entry.title = "title";
  return entry;
}

TEST(CachingItemIndexStoreTest, ReusesCachedEntryBeforeTtlExpires) {
  auto inner_store = ::std::make_unique<FakeItemIndexStore>(CreateEntry(42));
  FakeItemIndexStore* inner_store_ptr = inner_store.get();
  CachingItemIndexStore store(::std::move(inner_store), 30,
                              milliseconds(1000));

  ASSERT_TRUE(store.FindByItemId(42).has_value());
  ASSERT_TRUE(store.FindByItemId(42).has_value());

  EXPECT_EQ(inner_store_ptr->lookup_count, 1);
}

TEST(CachingItemIndexStoreTest, RefetchesEntryAfterTtlExpires) {
  auto inner_store = ::std::make_unique<FakeItemIndexStore>(CreateEntry(42));
  FakeItemIndexStore* inner_store_ptr = inner_store.get();
  CachingItemIndexStore store(::std::move(inner_store), 30, milliseconds(1));

  ASSERT_TRUE(store.FindByItemId(42).has_value());
  ::std::this_thread::sleep_for(milliseconds(2));
  ASSERT_TRUE(store.FindByItemId(42).has_value());

  EXPECT_EQ(inner_store_ptr->lookup_count, 2);
}

TEST(CachingItemIndexStoreTest, DoesNotCacheMissingEntry) {
  auto inner_store = ::std::make_unique<FakeItemIndexStore>(CreateEntry(42));
  FakeItemIndexStore* inner_store_ptr = inner_store.get();
  CachingItemIndexStore store(::std::move(inner_store), 30,
                              milliseconds(1000));

  EXPECT_FALSE(store.FindByItemId(7).has_value());
  EXPECT_FALSE(store.FindByItemId(7).has_value());

  EXPECT_EQ(inner_store_ptr->lookup_count, 2);
}

}  // namespace
}  // namespace shooting_star::recommendation_engine
