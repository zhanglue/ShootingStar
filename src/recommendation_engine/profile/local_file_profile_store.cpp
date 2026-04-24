#include "src/recommendation_engine/profile/local_file_profile_store.h"

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

#include "src/utilities/pb_data_handler/pb_data_handler.h"

namespace recommendation_engine {

using ::google::protobuf::ListValue;
using ::google::protobuf::Value;
using ::google::protobuf::util::MessageToJsonString;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::PBDataHandler;
using ::std::ifstream;
using ::std::istreambuf_iterator;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::to_string;

LocalFileProfileStore::LocalFileProfileStore(const string& file_path) {
  string error_msg;
  if (!LoadFromJsonFile(file_path, &error_msg)) {
    throw runtime_error(error_msg);
  }
}

bool LocalFileProfileStore::LoadFromJsonFile(const string& file_path, string* error_msg) {
  ifstream fin(file_path);
  if (!fin.is_open()) {
    if (error_msg != nullptr) {
      *error_msg = "LocalFileProfileStore::LoadFromJsonFile failed: cannot open file " + file_path;
    }
    return false;
  }

  const string json_content(
      (istreambuf_iterator<char>(fin)),
      istreambuf_iterator<char>());

  ListValue profiles_json;
  const auto status = ::google::protobuf::util::JsonStringToMessage(json_content, &profiles_json);
  if (!status.ok()) {
    if (error_msg != nullptr) {
      *error_msg = "LocalFileProfileStore::LoadFromJsonFile failed: " + status.ToString();
    }
    return false;
  }

  UserIdProfileMap loaded_profiles;
  for (const Value& profile_value : profiles_json.values()) {
    string profile_json;
    const auto to_json_status = MessageToJsonString(profile_value, &profile_json);
    if (!to_json_status.ok()) {
      if (error_msg != nullptr) {
        *error_msg = "LocalFileProfileStore::LoadFromJsonFile failed: " + to_json_status.ToString();
      }
      return false;
    }

    Profile profile;
    if (!PBDataHandler::JsonToPB(profile_json, &profile, error_msg)) {
      return false;
    }

    loaded_profiles[profile.user_id()] = profile;
  }

  profiles_ = ::std::move(loaded_profiles);
  return true;
}

optional<Profile> LocalFileProfileStore::FindByUserId(int user_id) const {
  const Logger& logger = LoggerRegistry::Get();

  const auto it = profiles_.find(user_id);
  if (it == profiles_.end()) {
    logger.Error(
        "profile_not_found_in_local_file_store",
        {
            {"user_id", to_string(user_id)},
        });
    return ::std::nullopt;
  }

  logger.Info(
      "profile_read_from_local_file_store",
      {
          {"user_id", to_string(user_id)},
      });
  return it->second;
}

}  // namespace recommendation_engine
