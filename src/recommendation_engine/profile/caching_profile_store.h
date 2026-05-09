#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/lru_cache/lru_cache.h"

namespace shooting_star::recommendation_engine {

class CachingProfileStore final : public ProfileStore {
 public:
  CachingProfileStore(::std::unique_ptr<ProfileStore> profile_store,
                      ::std::size_t capacity, ::std::chrono::milliseconds ttl);

  ::std::optional<Profile> FindByUserId(int user_id) const override;

 private:
  ::std::unique_ptr<ProfileStore> profile_store_;

  // The cache intentionally stores Profile values instead of shared pointers.
  // This keeps ownership simple: cache hits return an independent Profile copy,
  // and callers cannot observe internal cache storage after an entry is updated
  // or evicted. The extra protobuf copy is expected to be cheaper than an
  // Elasticsearch fetch for the current profile shape. If real profiles grow
  // substantially (for example, large embedding vectors or consistently large
  // serialized payloads around 50 KB+ on hot cache-hit paths), consider changing
  // this to LruCache<int, std::shared_ptr<const Profile>> so cache hits copy only
  // a shared pointer while preserving immutable Profile semantics.
  mutable ::shooting_star::utilities::LruCache<int, Profile> cache_;
};

}  // namespace shooting_star::recommendation_engine
