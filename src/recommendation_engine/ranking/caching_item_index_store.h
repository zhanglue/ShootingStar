#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

#include "src/recommendation_engine/ranking/item_index_store.h"
#include "src/utilities/lru_cache/lru_cache.h"

namespace shooting_star::recommendation_engine {

class CachingItemIndexStore final : public ItemIndexStore {
 public:
  CachingItemIndexStore(::std::unique_ptr<ItemIndexStore> item_index_store,
                        ::std::size_t capacity,
                        ::std::chrono::milliseconds ttl);

  ::std::optional<ItemIndexEntry> FindByItemId(
      uint64_t item_id) const override;

 private:
  ::std::unique_ptr<ItemIndexStore> item_index_store_;

  // Store parsed ItemIndexEntry values, not raw ES JSON strings. Ranking uses
  // the parsed shape repeatedly, and cache hits should avoid both the network
  // fetch and the JSON parsing path.
  mutable ::shooting_star::utilities::LruCache<uint64_t, ItemIndexEntry> cache_;
};

}  // namespace shooting_star::recommendation_engine
