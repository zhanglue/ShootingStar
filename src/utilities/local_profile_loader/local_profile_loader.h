#pragma once

#include <cstdint>
#include <string>

#include "protos/recommendation_engine/profile.pb.h"

namespace shooting_star {
namespace utilities {

bool LoadProfileFromLocalFile(const ::std::string& profile_data_path,
                              const ::std::string& executable_path,
                              int64_t user_id,
                              ::recommendation_engine::Profile* profile,
                              ::std::string* error_msg = nullptr);

}  // namespace utilities
}  // namespace shooting_star
