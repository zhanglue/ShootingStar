#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "protos/recommendation_engine/profile.pb.h"

namespace shooting_star {
namespace utilities {

using UserIdProfileMap = ::std::unordered_map<int, ::recommendation_engine::Profile>;

bool LoadProfilesFromJsonlFile(const ::std::string& file_path,
                               UserIdProfileMap* loaded_profiles,
                               ::std::string* error_msg = nullptr);

bool LoadProfileFromLocalFile(const ::std::string& file_path,
                              int64_t user_id,
                              ::recommendation_engine::Profile* profile,
                              ::std::string* error_msg = nullptr);

}  // namespace utilities
}  // namespace shooting_star
