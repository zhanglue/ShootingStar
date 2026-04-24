#include "src/recommendation_engine/profile/caching_profile_store.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace recommendation_engine {

using ::std::optional;
using ::std::size_t;
using ::std::unique_ptr;

CachingProfileStore::CachingProfileStore(unique_ptr<ProfileStore> profile_store,
                                         size_t capacity,
                                         ::std::chrono::milliseconds ttl)
    : profile_store_(::std::move(profile_store)), cache_(capacity, ttl) {
  if (profile_store_ == nullptr) {
    throw ::std::invalid_argument(
        "CachingProfileStore profile_store must not be null");
  }
}

optional<Profile> CachingProfileStore::FindByUserId(int user_id) const {
  optional<Profile> cached_profile = cache_.Get(user_id);
  if (cached_profile.has_value()) {
    return cached_profile;
  }

  optional<Profile> profile = profile_store_->FindByUserId(user_id);
  if (!profile.has_value()) {
    return ::std::nullopt;
  }

  cache_.Put(user_id, *profile);
  return profile;
}

}  // namespace recommendation_engine
