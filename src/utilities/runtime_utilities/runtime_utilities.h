#pragma once

#include <string>

namespace shooting_star {
namespace utilities {

::std::string ResolveWorkspaceRelativePath(
    const ::std::string& path,
    const ::std::string& executable_path = "");

}  // namespace utilities
}  // namespace shooting_star
