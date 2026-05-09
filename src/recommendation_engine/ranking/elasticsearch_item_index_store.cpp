#include "src/recommendation_engine/ranking/elasticsearch_item_index_store.h"

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <google/protobuf/struct.pb.h>

#include "src/utilities/json_parser/json_parser.h"

namespace shooting_star::recommendation_engine {
namespace {

using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::ElasticsearchResult;
using ::shooting_star::utilities::JsonParser;
using ::std::format;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;

constexpr int kHttpNotFoundStatusCode = 404;
constexpr char kEsSourceField[] = "_source";

}  // namespace

ElasticsearchItemIndexStore::ElasticsearchItemIndexStore(
    ElasticsearchClient client,
    string index)
    : client_(::std::move(client)), index_(::std::move(index)) {
  if (index_.empty()) {
    throw runtime_error("ElasticsearchItemIndexStore index must not be empty");
  }
}

optional<ItemIndexEntry> ElasticsearchItemIndexStore::FindByItemId(
    uint64_t item_id) const {
  const ElasticsearchResult result =
      client_.Get(index_, ::std::to_string(item_id));
  if (!result.ok) {
    if (result.status_code == kHttpNotFoundStatusCode) {
      return ::std::nullopt;
    }
    throw runtime_error(format(
        "Failed to fetch item index from Elasticsearch: status={} error={} body={}",
        result.status_code,
        result.error_message,
        result.body));
  }

  return ParseItemFromGetResponse(result.body);
}

optional<ItemIndexEntry> ElasticsearchItemIndexStore::ParseItemFromGetResponse(
    const string& response_body) const {
  const Struct get_response =
      JsonParser::ParseObject(response_body, "Elasticsearch response");

  const auto& response_fields = get_response.fields();
  const Value& found = JsonParser::RequiredField(response_fields,
                                                 "found",
                                                 "Elasticsearch response");
  if (found.kind_case() != Value::kBoolValue) {
    throw runtime_error("Elasticsearch response.found must be a bool.");
  }
  if (!found.bool_value()) {
    return ::std::nullopt;
  }

  const Value* source = JsonParser::FindField(response_fields, kEsSourceField);
  if (source == nullptr) {
    throw runtime_error("Elasticsearch response is missing JSON field: _source");
  }
  if (!source->has_struct_value()) {
    throw runtime_error("Elasticsearch _source field is not a JSON object.");
  }

  return ItemIndexStore::ParseEntryFromJsonObject(source->struct_value(),
                                                  "Elasticsearch _source");
}

}  // namespace shooting_star::recommendation_engine
