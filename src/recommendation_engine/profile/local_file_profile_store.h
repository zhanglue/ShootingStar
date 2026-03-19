#pragma once

#include <string>
#include <unordered_map>

#include "src/recommendation_engine/profile/profile_store.h"

namespace recommendation_engine {

using UserIdProfileMap = ::std::unordered_map<int, Profile>;

class LocalFileProfileStore final : public ProfileStore {
 public:
  explicit LocalFileProfileStore(const ::std::string& file_path);

  const Profile* FindByUserId(int user_id) const override;

 private:
  bool LoadFromJsonFile(const ::std::string& file_path, ::std::string* error_msg = nullptr);

  UserIdProfileMap profiles_;
};

}  // namespace recommendation_engine
