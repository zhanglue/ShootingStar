#include "src/recommendation_engine/profile/caching_profile_store.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace recommendation_engine {

using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::optional;
using ::std::size_t;
using ::std::to_string;
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
  const Logger& logger = LoggerRegistry::Get();

  optional<Profile> cached_profile = cache_.Get(user_id);
  if (cached_profile.has_value()) {
    logger.Info(
        "profile_cache_hit",
        {
            {"user_id", to_string(user_id)},
        });
    return cached_profile;
  }

  logger.Info(
      "profile_cache_miss",
      {
          {"user_id", to_string(user_id)},
      });
  optional<Profile> profile = profile_store_->FindByUserId(user_id);
  if (!profile.has_value()) {
    return ::std::nullopt;
  }

  cache_.Put(user_id, *profile);
  return profile;
}

}  // namespace recommendation_engine
