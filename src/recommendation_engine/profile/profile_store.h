#pragma once

#include "protos/profile.pb.h"

namespace recommendation_engine {

class ProfileStore {
 public:
  virtual ~ProfileStore() = default;

  virtual const Profile* FindByUserId(int user_id) const = 0;
};

}  // namespace recommendation_engine
