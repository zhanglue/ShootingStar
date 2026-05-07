#include "src/utilities/local_profile_loader/local_profile_loader.h"

#include <fstream>
#include <limits>
#include <string>
#include <string_view>

#include "src/utilities/pb_data_handler/pb_data_handler.h"

namespace shooting_star {
namespace utilities {

using ::shooting_star::utilities::PBDataHandler;
using ::std::ifstream;
using ::std::numeric_limits;
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
  ::recommendation_engine::Profile profile;
  if (!PBDataHandler::JsonToPB(profile_json, &profile, error_msg)) {
    return false;
  }
  if (profile.user_id() < numeric_limits<int>::min() ||
      profile.user_id() > numeric_limits<int>::max()) {
    if (error_msg != nullptr) {
      *error_msg =
          "LoadProfilesFromJsonlFile failed: user_id is outside int range";
    }
    return false;
  }
  (*loaded_profiles)[static_cast<int>(profile.user_id())] = profile;
  return true;
}

}  // namespace

bool LoadProfilesFromJsonlFile(const string& file_path,
                               UserIdProfileMap* loaded_profiles,
                               string* error_msg) {
  if (loaded_profiles == nullptr) {
    if (error_msg != nullptr) {
      *error_msg = "loaded_profiles pointer must not be null.";
    }
    return false;
  }

  ifstream fin(file_path);
  if (!fin.is_open()) {
    if (error_msg != nullptr) {
      *error_msg = "LoadProfilesFromJsonlFile failed: cannot open file " +
                   file_path;
    }
    return false;
  }

  UserIdProfileMap parsed_profiles;
  string line;
  int line_number = 0;
  while (::std::getline(fin, line)) {
    ++line_number;
    if (IsBlank(line)) {
      continue;
    }
    if (!AddProfileFromJsonLine(line, &parsed_profiles, error_msg)) {
      if (error_msg != nullptr) {
        *error_msg += " at JSONL line " + to_string(line_number);
      }
      return false;
    }
  }

  *loaded_profiles = ::std::move(parsed_profiles);
  return true;
}

bool LoadProfileFromLocalFile(const string& file_path,
                              int64_t user_id,
                              ::recommendation_engine::Profile* profile,
                              string* error_msg) {
  if (profile == nullptr) {
    if (error_msg != nullptr) {
      *error_msg = "profile output pointer must not be null.";
    }
    return false;
  }
  if (user_id < numeric_limits<int>::min() ||
      user_id > numeric_limits<int>::max()) {
    if (error_msg != nullptr) {
      *error_msg =
          "user_id is out of range for local JSONL profile lookup.";
    }
    return false;
  }

  UserIdProfileMap profiles;
  if (!LoadProfilesFromJsonlFile(file_path, &profiles, error_msg)) {
    return false;
  }

  const auto it = profiles.find(static_cast<int>(user_id));
  if (it == profiles.end()) {
    if (error_msg != nullptr) {
      *error_msg = "Cannot find demo profile for user_id " +
                   to_string(user_id) + " in " + file_path;
    }
    return false;
  }

  profile->CopyFrom(it->second);
  return true;
}

}  // namespace utilities
}  // namespace shooting_star
