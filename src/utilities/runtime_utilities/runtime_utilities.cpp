#include "src/utilities/runtime_utilities/runtime_utilities.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::string;
using ::std::string_view;
using ::std::chrono::milliseconds;
using ::std::chrono::steady_clock;
using ::std::chrono::system_clock;

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

RpcDeadlineStatus CheckGrpcServerDeadline(
    const ::grpc::ServerContext* context,
    steady_clock::time_point server_deadline) {
  if (context != nullptr && context->IsCancelled()) {
    return RpcDeadlineStatus::kCancelled;
  }
  if (context != nullptr && context->deadline() <= system_clock::now()) {
    return RpcDeadlineStatus::kClientDeadlineExceeded;
  }
  if (steady_clock::now() >= server_deadline) {
    return RpcDeadlineStatus::kServerDeadlineExceeded;
  }
  return RpcDeadlineStatus::kOk;
}

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

void ValidateTimeoutSumNotGreater(string_view first_inner_key,
                                  milliseconds first_inner,
                                  string_view second_inner_key,
                                  milliseconds second_inner,
                                  string_view outer_key,
                                  milliseconds outer) {
  const milliseconds inner_sum = first_inner + second_inner;
  if (inner_sum <= outer) {
    return;
  }
  throw invalid_argument(::absl::StrFormat(
      "%s (%d ms) + %s (%d ms) must be less than or equal to %s (%d ms)",
      first_inner_key, first_inner.count(), second_inner_key,
      second_inner.count(), outer_key, outer.count()));
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

string GenerateGuid() {
  absl::BitGen bitgen;
  ::std::array<uint8_t, 16> guid;
  for (auto& byte : guid) {
    byte = absl::Uniform<uint8_t>(bitgen);
  }

  guid[6] = static_cast<uint8_t>((guid[6] & 0x0F) | 0x40);
  guid[8] = static_cast<uint8_t>((guid[8] & 0x3F) | 0x80);

  char buffer[37];
  std::snprintf(buffer, sizeof(buffer),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                "%02x%02x%02x%02x%02x%02x",
                guid[0], guid[1], guid[2], guid[3], guid[4], guid[5], guid[6],
                guid[7], guid[8], guid[9], guid[10], guid[11], guid[12],
                guid[13], guid[14], guid[15]);
  return string(buffer);
}

}  // namespace utilities
}  // namespace shooting_star
