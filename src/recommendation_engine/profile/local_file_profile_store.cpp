#include "src/recommendation_engine/profile/local_file_profile_store.h"

#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/pb_data_handler/pb_data_handler.h"

namespace recommendation_engine {

using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::PBDataHandler;
using ::std::ifstream;
using ::std::numeric_limits;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;
using ::std::to_string;

namespace {

bool IsBlank(string_view value) {
  return value.find_first_not_of(" \t\r\n") == string_view::npos;
}

bool AddProfileFromJsonLine(const string& profile_json,
                            UserIdProfileMap* loaded_profiles,
                            string* error_msg) {
  Profile profile;
  if (!PBDataHandler::JsonToPB(profile_json, &profile, error_msg)) {
    return false;
  }
  if (profile.user_id() < numeric_limits<int>::min() ||
      profile.user_id() > numeric_limits<int>::max()) {
    if (error_msg != nullptr) {
      *error_msg =
          "LocalFileProfileStore::LoadFromJsonlFile failed: user_id "
          "is outside int range";
    }
    return false;
  }
  (*loaded_profiles)[static_cast<int>(profile.user_id())] = profile;
  return true;
}

}  // namespace

LocalFileProfileStore::LocalFileProfileStore(const string& file_path) {
  string error_msg;
  if (!LoadFromJsonlFile(file_path, &error_msg)) {
    throw runtime_error(error_msg);
  }
}

bool LocalFileProfileStore::LoadFromJsonlFile(const string& file_path,
                                              string* error_msg) {
  ifstream fin(file_path);
  if (!fin.is_open()) {
    if (error_msg != nullptr) {
      *error_msg =
          "LocalFileProfileStore::LoadFromJsonlFile failed: cannot open file " +
          file_path;
    }
    return false;
  }

  UserIdProfileMap loaded_profiles;
  string line;
  int line_number = 0;
  while (::std::getline(fin, line)) {
    ++line_number;
    if (IsBlank(line)) {
      continue;
    }
    if (!AddProfileFromJsonLine(line, &loaded_profiles, error_msg)) {
      if (error_msg != nullptr) {
        *error_msg += " at JSONL line " + to_string(line_number);
      }
      return false;
    }
  }

  profiles_ = ::std::move(loaded_profiles);
  return true;
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
