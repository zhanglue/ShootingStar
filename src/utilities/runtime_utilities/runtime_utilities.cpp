#include "src/utilities/runtime_utilities/runtime_utilities.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "absl/strings/str_format.h"

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::string;
using ::std::string_view;
using ::std::chrono::milliseconds;

namespace {

constexpr string_view kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

string Base64Encode(string_view input) {
  string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  for (size_t i = 0; i < input.size(); i += 3) {
    const unsigned char b0 = static_cast<unsigned char>(input[i]);
    const unsigned char b1 =
        i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
    const unsigned char b2 =
        i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;

    output.push_back(kBase64Chars[b0 >> 2]);
    output.push_back(kBase64Chars[((b0 & 0x03) << 4) | (b1 >> 4)]);
    output.push_back(i + 1 < input.size()
                         ? kBase64Chars[((b1 & 0x0f) << 2) | (b2 >> 6)]
                         : '=');
    output.push_back(i + 2 < input.size() ? kBase64Chars[b2 & 0x3f] : '=');
  }

  return output;
}

string GetEnvOrDefault(string_view name, string default_value) {
  if (name.empty()) {
    return default_value;
  }
  const string env_name(name);
  const char* value = ::std::getenv(env_name.c_str());
  if (value == nullptr || string(value).empty()) {
    return default_value;
  }
  return value;
}

bool GetEnvFlagOrDefault(string_view name, bool default_value) {
  const string value = GetEnvOrDefault(name, "");
  if (value.empty()) {
    return default_value;
  }
  return value == "1" || value == "true" || value == "TRUE" ||
         value == "yes" || value == "YES";
}

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

void ValidateTimeoutNotGreater(string_view inner_key, milliseconds inner,
                               string_view outer_key, milliseconds outer) {
  if (inner <= outer) {
    return;
  }
  throw invalid_argument(::absl::StrFormat(
      "%s (%d ms) must be less than or equal to %s (%d ms)",
      inner_key, inner.count(), outer_key, outer.count()));
}

void TrimLeadingSlashes(string& value) {
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
}

void TrimTrailingSlashes(string& value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
}

void TrimWhitespace(string_view& value) {
  while (!value.empty() &&
         ::std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         ::std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
}

}  // namespace utilities
}  // namespace shooting_star
