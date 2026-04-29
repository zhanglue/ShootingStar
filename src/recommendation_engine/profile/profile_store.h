#pragma once

#include <optional>
#include <string_view>

#include "protos/recommendation_engine/profile.pb.h"

namespace recommendation_engine {

class ProfileStore {
 public:
  static constexpr ::std::string_view kLocalStoreType = "local";
  static constexpr ::std::string_view kElasticsearchStoreType = "elasticsearch";

  virtual ~ProfileStore() = default;

  virtual ::std::optional<Profile> FindByUserId(int user_id) const = 0;

};

}  // namespace recommendation_engine
