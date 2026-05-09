#include "src/recommendation_engine/ranking/caching_item_index_store.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace shooting_star::recommendation_engine {

using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::optional;
using ::std::size_t;
using ::std::to_string;
using ::std::unique_ptr;

CachingItemIndexStore::CachingItemIndexStore(
    unique_ptr<ItemIndexStore> item_index_store,
    size_t capacity,
    ::std::chrono::milliseconds ttl)
    : item_index_store_(::std::move(item_index_store)),
      cache_(capacity, ttl) {
  if (item_index_store_ == nullptr) {
    throw ::std::invalid_argument(
        "CachingItemIndexStore item_index_store must not be null");
  }
}

optional<ItemIndexEntry> CachingItemIndexStore::FindByItemId(
    uint64_t item_id) const {
  const Logger& logger = LoggerRegistry::Get();

  optional<ItemIndexEntry> cached_entry = cache_.Get(item_id);
  if (cached_entry.has_value()) {
    logger.Info(
        "item_index_cache_hit",
        {
            {"item_id", to_string(item_id)},
        });
    return cached_entry;
  }

  logger.Info(
      "item_index_cache_miss",
      {
          {"item_id", to_string(item_id)},
      });
  optional<ItemIndexEntry> entry = item_index_store_->FindByItemId(item_id);
  if (!entry.has_value()) {
    return ::std::nullopt;
  }

  cache_.Put(item_id, *entry);
  return entry;
}

}  // namespace shooting_star::recommendation_engine
