#pragma once

#include <optional>

#include "protos/recommendation_engine/profile.pb.h"

namespace recommendation_engine {

class ProfileStore {
 public:
  virtual ~ProfileStore() = default;

  virtual ::std::optional<Profile> FindByUserId(int user_id) const = 0;
};

}  // namespace recommendation_engine
