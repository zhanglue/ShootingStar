#include "src/utilities/runtime_utilities/runtime_utilities.h"

#include <cstdlib>

#include "absl/strings/str_format.h"

namespace shooting_star {
namespace utilities {

::std::string ResolveWorkspaceRelativePath(
    const ::std::string& configured_path,
    const ::std::string& default_relative_path) {
  if (!configured_path.empty()) {
    return configured_path;
  }

  const char* workspace_dir = ::std::getenv("BUILD_WORKSPACE_DIRECTORY");
  if (workspace_dir != nullptr) {
    return absl::StrFormat("%s/%s", workspace_dir, default_relative_path);
  }

  return default_relative_path;
}

}  // namespace utilities
}  // namespace shooting_star
