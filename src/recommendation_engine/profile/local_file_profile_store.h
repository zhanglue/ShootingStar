#pragma once

#include <optional>
#include <string>

#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"

namespace shooting_star::recommendation_engine {

class LocalFileProfileStore final : public ProfileStore {
 public:
  explicit LocalFileProfileStore(const ::std::string& file_path);

 ::std::optional<Profile> FindByUserId(int user_id) const override;

 private:
  ::shooting_star::utilities::UserIdProfileMap profiles_;
};

}  // namespace shooting_star::recommendation_engine
