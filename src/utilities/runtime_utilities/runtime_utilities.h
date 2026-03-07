#pragma once

#include <string>

namespace shooting_star {
namespace utilities {

::std::string ResolveWorkspaceRelativePath(
    const ::std::string& configured_path,
    const ::std::string& default_relative_path);

}  // namespace utilities
}  // namespace shooting_star
