#include "src/utilities/local_profile_loader/local_profile_loader.h"

#include <limits>
#include <optional>
#include <string>

#include "src/recommendation_engine/profile/local_file_profile_store.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

using ::recommendation_engine::LocalFileProfileStore;
using ::recommendation_engine::Profile;
using ::std::optional;
using ::std::string;

bool LoadProfileFromLocalFile(const string& profile_data_path,
                              const string& executable_path,
                              int64_t user_id,
                              Profile* profile,
                              string* error_msg) {
  if (profile == nullptr) {
    if (error_msg != nullptr) {
      *error_msg = "profile output pointer must not be null.";
    }
    return false;
  }

  if (user_id < ::std::numeric_limits<int>::min() || user_id > ::std::numeric_limits<int>::max()) {
    if (error_msg != nullptr) {
      *error_msg = "user_id is out of range for LocalFileProfileStore lookup.";
    }
    return false;
  }

  try {
    const string resolved_profile_data_path =
        ResolveWorkspaceRelativePath(profile_data_path, executable_path);
    const LocalFileProfileStore store(resolved_profile_data_path);
    optional<Profile> loaded_profile = store.FindByUserId(static_cast<int>(user_id));
    if (!loaded_profile.has_value()) {
      if (error_msg != nullptr) {
        *error_msg = "Cannot find demo profile for user_id " + ::std::to_string(user_id) +
                     " in " + resolved_profile_data_path;
      }
      return false;
    }

    profile->CopyFrom(*loaded_profile);
    return true;
  } catch (const ::std::exception& ex) {
    if (error_msg != nullptr) {
      *error_msg = ex.what();
    }
    return false;
  }
}

}  // namespace utilities
}  // namespace shooting_star
