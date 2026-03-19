#include "src/utilities/runtime_utilities/runtime_utilities.h"

#include <filesystem>
#include <vector>

namespace shooting_star {
namespace utilities {

using ::std::string;

namespace {

string NormalizePath(const ::std::filesystem::path& path) {
  return path.lexically_normal().string();
}

void AddAncestorDirectories(
    const ::std::filesystem::path& base_dir,
    ::std::vector<::std::filesystem::path>* search_dirs) {
  if (base_dir.empty()) {
    return;
  }

  for (::std::filesystem::path dir = base_dir;; dir = dir.parent_path()) {
    search_dirs->push_back(dir);
    if (dir == dir.root_path()) {
      break;
    }
  }
}

}  // namespace

string ResolveWorkspaceRelativePath(const string& path, const string& executable_path) {
  if (path.empty()) {
    return path;
  }

  const ::std::filesystem::path relative_path(path);
  if (relative_path.is_absolute()) {
    return path;
  }

  ::std::vector<::std::filesystem::path> search_dirs;
  if (!executable_path.empty()) {
    AddAncestorDirectories(
        ::std::filesystem::absolute(executable_path).parent_path(),
        &search_dirs);
  }

  AddAncestorDirectories(::std::filesystem::current_path(), &search_dirs);

  for (const ::std::filesystem::path& dir : search_dirs) {
    const ::std::filesystem::path candidate = dir / relative_path;
    if (::std::filesystem::exists(candidate)) {
      return NormalizePath(candidate);
    }
  }

  if (!executable_path.empty()) {
    return NormalizePath(
        ::std::filesystem::absolute(executable_path).parent_path() / relative_path);
  }

  return NormalizePath(::std::filesystem::current_path() / relative_path);
}

}  // namespace utilities
}  // namespace shooting_star
