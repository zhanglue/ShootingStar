#include "src/recommendation_engine/profile/local_file_profile_store.h"

#include <optional>
#include <stdexcept>
#include <string>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"

namespace recommendation_engine {

using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::LoadProfilesFromJsonlFile;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::to_string;

LocalFileProfileStore::LocalFileProfileStore(const string& file_path) {
  string error_msg;
  if (!LoadProfilesFromJsonlFile(file_path, &profiles_, &error_msg)) {
    throw runtime_error(error_msg);
  }
}

optional<Profile> LocalFileProfileStore::FindByUserId(int user_id) const {
  const Logger& logger = LoggerRegistry::Get();

  const auto it = profiles_.find(user_id);
  if (it == profiles_.end()) {
    logger.Error("profile_not_found_in_local_file_store",
                 {
                     {"user_id", to_string(user_id)},
                 });
    return ::std::nullopt;
  }

  logger.Info("profile_read_from_local_file_store",
              {
                  {"user_id", to_string(user_id)},
              });
  return it->second;
}

}  // namespace recommendation_engine
