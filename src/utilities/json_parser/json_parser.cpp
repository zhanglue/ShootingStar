#include "src/utilities/json_parser/json_parser.h"

#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>

#include <google/protobuf/util/json_util.h>

namespace shooting_star {
namespace utilities {
namespace {

using ::google::protobuf::ListValue;
using ::google::protobuf::Map;
using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::google::protobuf::util::JsonStringToMessage;
using ::std::format;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;

uint64_t ParseUint64String(string_view value, string_view context) {
  uint64_t parsed = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, error] = ::std::from_chars(first, last, parsed);
  if (error != ::std::errc() || ptr != last) {
    throw runtime_error(format("{} contains invalid uint64: {}", context, value));
  }
  return parsed;
}

}  // namespace

Struct JsonParser::ParseObject(string_view json, string_view context) {
  Struct object;
  const auto status = JsonStringToMessage(string(json), &object);
  if (!status.ok()) {
    throw runtime_error(format("{} is not valid JSON object: {}",
                               context,
                               status.ToString()));
  }
  return object;
}

Value JsonParser::ParseValue(string_view json, string_view context) {
  Value value;
  const auto status = JsonStringToMessage(string(json), &value);
  if (!status.ok()) {
    throw runtime_error(format("{} is not valid JSON value: {}",
                               context,
                               status.ToString()));
  }
  return value;
}

const Value* JsonParser::FindField(const Map<string, Value>& fields,
                                   string_view field_name) {
  const auto iter = fields.find(string(field_name));
  if (iter == fields.end()) {
    return nullptr;
  }
  return &iter->second;
}

const Value& JsonParser::RequiredField(const Map<string, Value>& fields,
                                       string_view field_name,
                                       string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr) {
    throw runtime_error(format("{} is missing field {}", context, field_name));
  }
  return *value;
}

bool JsonParser::IsNull(const Value& value) {
  return value.kind_case() == Value::kNullValue;
}

uint64_t JsonParser::ReadUint64Value(const Value& value, string_view context) {
  if (value.kind_case() == Value::kStringValue) {
    return ParseUint64String(value.string_value(), context);
  }
  if (value.kind_case() != Value::kNumberValue) {
    throw runtime_error(format("{} must be a number or string", context));
  }

  const double number = value.number_value();
  if (!::std::isfinite(number) || number < 0.0 ||
      number > static_cast<double>(::std::numeric_limits<uint64_t>::max()) ||
      ::std::floor(number) != number) {
    throw runtime_error(format("{} contains invalid uint64", context));
  }
  return static_cast<uint64_t>(number);
}

int64_t JsonParser::ReadInt64Value(const Value& value, string_view context) {
  if (value.kind_case() == Value::kStringValue) {
    int64_t parsed = 0;
    const string& raw = value.string_value();
    const char* first = raw.data();
    const char* last = raw.data() + raw.size();
    const auto [ptr, error] = ::std::from_chars(first, last, parsed);
    if (error == ::std::errc() && ptr == last) {
      return parsed;
    }
    throw runtime_error(format("{} contains invalid int64: {}", context, raw));
  }
  if (value.kind_case() != Value::kNumberValue) {
    throw runtime_error(format("{} must be a number or string", context));
  }

  const double number = value.number_value();
  if (!::std::isfinite(number) ||
      number < static_cast<double>(::std::numeric_limits<int64_t>::min()) ||
      number > static_cast<double>(::std::numeric_limits<int64_t>::max()) ||
      ::std::floor(number) != number) {
    throw runtime_error(format("{} contains invalid int64", context));
  }
  return static_cast<int64_t>(number);
}

double JsonParser::ReadDoubleValue(const Value& value, string_view context) {
  if (value.kind_case() == Value::kNumberValue) {
    const double number = value.number_value();
    if (::std::isfinite(number)) {
      return number;
    }
  }
  if (value.kind_case() == Value::kStringValue) {
    try {
      size_t parsed_size = 0;
      const double parsed = ::std::stod(value.string_value(), &parsed_size);
      if (parsed_size == value.string_value().size() &&
          ::std::isfinite(parsed)) {
        return parsed;
      }
    } catch (const ::std::exception&) {
    }
  }
  throw runtime_error(format("{} contains invalid double", context));
}

string JsonParser::ReadStringValue(const Value& value, string_view context) {
  if (value.kind_case() != Value::kStringValue) {
    throw runtime_error(format("{} must be a string", context));
  }
  return value.string_value();
}

uint64_t JsonParser::ReadRequiredUint64(const Map<string, Value>& fields,
                                        string_view field_name,
                                        string_view context) {
  return ReadUint64Value(
      RequiredField(fields, field_name, context),
      string(context) + "." + string(field_name));
}

string JsonParser::ReadOptionalString(const Map<string, Value>& fields,
                                      string_view field_name,
                                      string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return "";
  }
  return ReadStringValue(*value, string(context) + "." + string(field_name));
}

int JsonParser::ReadOptionalInt(const Map<string, Value>& fields,
                                string_view field_name,
                                string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return 0;
  }

  const int64_t parsed =
      ReadInt64Value(*value, string(context) + "." + string(field_name));
  if (parsed < ::std::numeric_limits<int>::min() ||
      parsed > ::std::numeric_limits<int>::max()) {
    throw runtime_error(format("{}.{} is outside int range",
                               context,
                               field_name));
  }
  return static_cast<int>(parsed);
}

int64_t JsonParser::ReadOptionalInt64(const Map<string, Value>& fields,
                                      string_view field_name,
                                      string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return 0;
  }
  return ReadInt64Value(*value, string(context) + "." + string(field_name));
}

double JsonParser::ReadOptionalDouble(const Map<string, Value>& fields,
                                      string_view field_name,
                                      string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return 0.0;
  }
  return ReadDoubleValue(*value, string(context) + "." + string(field_name));
}

const Struct* JsonParser::ReadOptionalStruct(const Map<string, Value>& fields,
                                             string_view field_name,
                                             string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return nullptr;
  }
  if (value->kind_case() != Value::kStructValue) {
    throw runtime_error(format("{}.{} must be an object",
                               context,
                               field_name));
  }
  return &value->struct_value();
}

::std::vector<string> JsonParser::ReadOptionalStringList(
    const Map<string, Value>& fields,
    string_view field_name,
    string_view context) {
  const Value* value = FindField(fields, field_name);
  if (value == nullptr || IsNull(*value)) {
    return {};
  }
  if (value->kind_case() != Value::kListValue) {
    throw runtime_error(format("{}.{} must be an array", context, field_name));
  }

  const ListValue& list = value->list_value();
  ::std::vector<string> strings;
  strings.reserve(list.values_size());
  for (int i = 0; i < list.values_size(); ++i) {
    strings.emplace_back(ReadStringValue(
        list.values(i),
        format("{}.{}[{}]", context, field_name, i)));
  }
  return strings;
}

}  // namespace utilities
}  // namespace shooting_star
