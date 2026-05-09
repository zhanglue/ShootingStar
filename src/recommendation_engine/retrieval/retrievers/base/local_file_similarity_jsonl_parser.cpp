#include "src/recommendation_engine/retrieval/retrievers/base/local_file_similarity_jsonl_parser.h"

#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

namespace shooting_star::recommendation_engine {
namespace {

using ::google::protobuf::Map;
using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::google::protobuf::util::JsonStringToMessage;
using ::std::format;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;
using ::std::vector;

const Value& RequiredField(
    const Map<string, Value>& fields,
    string_view field_name,
    string_view context) {
  const auto iter = fields.find(string(field_name));
  if (iter == fields.end()) {
    throw runtime_error(format("{} is missing field {}", context, field_name));
  }
  return iter->second;
}

uint64_t ParsePositiveUint64(string_view value, string_view context) {
  uint64_t parsed = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, error] = ::std::from_chars(first, last, parsed);
  if (error != ::std::errc() || ptr != last || parsed == 0) {
    throw runtime_error(format("{} contains invalid positive uint64: {}", context, value));
  }
  return parsed;
}

uint64_t ReadPositiveUint64(const Value& value, string_view context) {
  if (value.kind_case() == Value::kStringValue) {
    return ParsePositiveUint64(value.string_value(), context);
  }
  if (value.kind_case() != Value::kNumberValue) {
    throw runtime_error(format("{} must be a number or string", context));
  }

  const double number = value.number_value();
  if (!::std::isfinite(number) || number <= 0.0 ||
      number > static_cast<double>(::std::numeric_limits<uint64_t>::max()) ||
      ::std::floor(number) != number) {
    throw runtime_error(format("{} contains invalid positive uint64", context));
  }
  return static_cast<uint64_t>(number);
}

double ReadDouble(const Value& value, string_view context) {
  if (value.kind_case() == Value::kNumberValue) {
    return value.number_value();
  }
  if (value.kind_case() == Value::kStringValue) {
    try {
      size_t parsed_size = 0;
      const double parsed = ::std::stod(value.string_value(), &parsed_size);
      if (parsed_size == value.string_value().size() && ::std::isfinite(parsed)) {
        return parsed;
      }
    } catch (const ::std::exception&) {
    }
  }
  throw runtime_error(format("{} contains invalid double", context));
}

vector<LocalFileSimilarityNeighbor> ParseNeighbors(
    const Value& value,
    string_view neighbor_id_field,
    string_view context) {
  if (value.kind_case() != Value::kListValue) {
    throw runtime_error(format("{} neighbors must be an array", context));
  }

  vector<LocalFileSimilarityNeighbor> neighbors;
  neighbors.reserve(value.list_value().values_size());
  for (int i = 0; i < value.list_value().values_size(); ++i) {
    const Value& neighbor_value = value.list_value().values(i);
    if (neighbor_value.kind_case() != Value::kStructValue) {
      throw runtime_error(format("{} neighbor {} must be an object", context, i));
    }

    const auto& fields = neighbor_value.struct_value().fields();
    const string neighbor_context = format("{} neighbor {}", context, i);
    neighbors.emplace_back(LocalFileSimilarityNeighbor{
        .id = ReadPositiveUint64(
            RequiredField(fields, neighbor_id_field, neighbor_context),
            neighbor_context + "." + string(neighbor_id_field)),
        .score = ReadDouble(
            RequiredField(fields, "score", neighbor_context),
            neighbor_context + ".score"),
    });
  }
  return neighbors;
}

}  // namespace

LocalFileSimilarityRow ParseLocalFileSimilarityJsonlLine(
    string_view line,
    string_view entity_id_field,
    string_view neighbor_id_field,
    string_view context) {
  Struct row;
  const auto status = JsonStringToMessage(string(line), &row);
  if (!status.ok()) {
    throw runtime_error(format(
        "ParseLocalFileSimilarityJsonlLine failed at {}: {}",
        context, status.ToString()));
  }

  const auto& fields = row.fields();
  return LocalFileSimilarityRow{
      .entity_id = ReadPositiveUint64(
          RequiredField(fields, entity_id_field, context),
          string(context) + "." + string(entity_id_field)),
      .neighbors = ParseNeighbors(
          RequiredField(fields, "neighbors", context),
          neighbor_id_field,
          context),
  };
}

}  // namespace shooting_star::recommendation_engine
