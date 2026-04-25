#include "src/utilities/config_helper/config_helper.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::out_of_range;
using ::std::string;
using ::std::string_view;

namespace {

string JoinKey(string_view prefix, string_view key) {
  if (prefix.empty()) {
    return string(key);
  }
  string joined(prefix);
  joined.push_back('.');
  joined.append(key);
  return joined;
}

void FlattenYamlNode(
    const YAML::Node& node,
    string_view prefix,
    ::std::map<string, string>* values) {
  if (!node || node.IsNull()) {
    return;
  }

  if (node.IsScalar()) {
    if (prefix.empty()) {
      throw invalid_argument(
          "YAML root scalar cannot be converted to keyed config");
    }
    (*values)[string(prefix)] = node.Scalar();
    return;
  }

  if (node.IsMap()) {
    for (const auto& entry : node) {
      if (!entry.first.IsScalar()) {
        throw invalid_argument("YAML config keys must be scalars");
      }
      const string key = JoinKey(prefix, entry.first.Scalar());
      FlattenYamlNode(entry.second, key, values);
    }
    return;
  }

  if (node.IsSequence()) {
    if (prefix.empty()) {
      throw invalid_argument(
          "YAML root sequence cannot be converted to keyed config");
    }
    for (size_t index = 0; index < node.size(); ++index) {
      const string key = JoinKey(prefix, ::std::to_string(index));
      FlattenYamlNode(node[index], key, values);
    }
    return;
  }

  throw invalid_argument("Unsupported YAML node type");
}

string Lowercase(string_view value) {
  string normalized(value);
  ::std::transform(
      normalized.begin(), normalized.end(), normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(::std::tolower(ch)); });
  return normalized;
}

int ParseIntValue(string_view key, string_view value) {
  int parsed = 0;
  const char* begin = value.data();
  const char* end = begin + value.size();
  const ::std::from_chars_result result = ::std::from_chars(begin, end, parsed);
  if (result.ec != ::std::errc() || result.ptr != end) {
    throw invalid_argument("Invalid integer config value for key: " +
                           string(key));
  }
  return parsed;
}

bool ParseBoolValue(string_view key, string_view value) {
  const string normalized = Lowercase(value);
  if (normalized == "true" || normalized == "1" || normalized == "yes" ||
      normalized == "on") {
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no" ||
      normalized == "off") {
    return false;
  }
  throw invalid_argument("Invalid boolean config value for key: " +
                         string(key));
}

}  // namespace

int ConfigHelper::GetPositiveInt(string_view key, int default_value) const {
  const int value = GetInt(key, default_value);
  if (value <= 0) {
    throw invalid_argument(string(key) + " must be greater than 0");
  }
  return value;
}

void YamlConfigHelper::LoadFromYamlFile(const string& file_path) {
  ::std::map<string, string> loaded_values;
  try {
    FlattenYamlNode(YAML::LoadFile(file_path), "", &loaded_values);
  } catch (const YAML::Exception& ex) {
    throw invalid_argument("Failed to load YAML config file '" + file_path +
                           "': " + ex.what());
  }

  for (const auto& [key, value] : loaded_values) {
    values_[key] = value;
  }
}

void YamlConfigHelper::LoadFromYamlFileIfExists(const string& file_path) {
  ::std::error_code error;
  const bool exists = ::std::filesystem::exists(file_path, error);
  if (error) {
    throw invalid_argument("Failed to inspect YAML config file '" + file_path +
                           "': " + error.message());
  }
  if (!exists) {
    return;
  }
  LoadFromYamlFile(file_path);
}

bool YamlConfigHelper::Has(string_view key) const {
  return values_.find(string(key)) != values_.end();
}

string YamlConfigHelper::GetString(string_view key, string default_value) const {
  const auto iter = values_.find(string(key));
  if (iter == values_.end()) {
    return default_value;
  }
  return iter->second;
}

int YamlConfigHelper::GetInt(string_view key, int default_value) const {
  const auto iter = values_.find(string(key));
  if (iter == values_.end()) {
    return default_value;
  }
  return ParseIntValue(key, iter->second);
}

uint16_t YamlConfigHelper::GetUInt16(
    string_view key,
    uint16_t default_value) const {
  const int parsed = GetInt(key, default_value);
  if (parsed < 0 || parsed > ::std::numeric_limits<uint16_t>::max()) {
    throw out_of_range("Config value is outside uint16_t range for key: " +
                       string(key));
  }
  return static_cast<uint16_t>(parsed);
}

bool YamlConfigHelper::GetBool(string_view key, bool default_value) const {
  const auto iter = values_.find(string(key));
  if (iter == values_.end()) {
    return default_value;
  }
  return ParseBoolValue(key, iter->second);
}

void YamlConfigHelper::Set(string key, string value) {
  values_[::std::move(key)] = ::std::move(value);
}

const ::std::map<string, string>& YamlConfigHelper::values() const {
  return values_;
}

}  // namespace utilities
}  // namespace shooting_star
