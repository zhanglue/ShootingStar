#include "src/utilities/runtime_utilities/runtime_utilities.h"

#include <cstdlib>
#include <filesystem>

#include "absl/strings/str_format.h"

namespace shooting_star {
namespace utilities {

using ::std::string;

string ResolveWorkspaceRelativePath(const string& path) {
  if (path.empty()) {
    return path;
  }

  if (::std::filesystem::path(path).is_absolute()) {
    return path;
  }

  const char* workspace_dir = ::std::getenv("BUILD_WORKSPACE_DIRECTORY");
  if (workspace_dir != nullptr) {
    return absl::StrFormat("%s/%s", workspace_dir, path);
  }

  return path;
}

}  // namespace utilities
}  // namespace shooting_star
