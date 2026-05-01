#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "protos/recommendation_engine/profile.pb.h"

namespace recommendation_engine::user_cf {

class ProfileStore {
 public:
  virtual ~ProfileStore() = default;

  virtual ::std::optional<UserCfProfile> FindByUserId(uint64_t user_id) const = 0;

  virtual ::std::vector<::std::optional<UserCfProfile>> FindByUserIds(
      const ::std::vector<uint64_t>& user_ids) const {
    ::std::vector<::std::optional<UserCfProfile>> profiles;
    profiles.reserve(user_ids.size());
    for (const uint64_t user_id : user_ids) {
      profiles.emplace_back(FindByUserId(user_id));
    }
    return profiles;
  }
};

}  // namespace recommendation_engine::user_cf
