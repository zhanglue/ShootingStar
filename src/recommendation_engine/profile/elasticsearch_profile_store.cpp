#include "src/recommendation_engine/profile/elasticsearch_profile_store.h"

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

#include "absl/strings/str_format.h"
#include "src/utilities/pb_data_handler/pb_data_handler.h"

namespace recommendation_engine {

using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::MessageToJsonString;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::ElasticsearchResult;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::PBDataHandler;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::to_string;

namespace {

constexpr int kHttpNotFoundStatusCode = 404;
constexpr char kEsFoundField[] = "found";
constexpr char kEsSourceField[] = "_source";

bool RequireField(const ::google::protobuf::Map<string, Value>& fields,
                  const string& name, const Value** value, string* error_msg) {
  const auto it = fields.find(name);
  if (it == fields.end()) {
    *error_msg = "Elasticsearch response is missing JSON field: " + name;
    return false;
  }
  *value = &it->second;
  return true;
}

}  // namespace

ElasticsearchProfileStore::ElasticsearchProfileStore(ElasticsearchClient client,
                                                     string index)
    : client_(::std::move(client)), index_(::std::move(index)) {
  if (index_.empty()) {
    throw runtime_error("ElasticsearchProfileStore index must not be empty");
  }
}

optional<Profile> ElasticsearchProfileStore::FindByUserId(int user_id) const {
  const Logger& logger = LoggerRegistry::Get();

  const ElasticsearchResult result =
      client_.Get(index_, to_string(user_id));
  if (!result.ok) {
    if (result.status_code == kHttpNotFoundStatusCode) {
      logger.Info(
        "profile_not_found_in_elasticsearch_store",
        {
          {"user_id", to_string(user_id)},
          {"profile_es_index", index_},
        });
      return ::std::nullopt;
    }
    throw runtime_error(::absl::StrFormat(
        "Failed to fetch profile from Elasticsearch: status=%d error=%s "
        "body=%s",
        result.status_code, result.error_message, result.body));
  }

  Profile profile;
  if (!ParseProfileFromGetResponse(result.body, &profile)) {
    return ::std::nullopt;
  }

  logger.Info(
    "profile_read_from_elasticsearch_store",
    {
      {"user_id", to_string(user_id)},
      {"profile_es_index", index_},
    });
  return profile;
}

bool ElasticsearchProfileStore::ParseProfileFromGetResponse(
    const string& response_body, Profile* profile) const {
  Struct get_response;
  const auto parse_status = JsonStringToMessage(response_body, &get_response);
  if (!parse_status.ok()) {
    throw runtime_error("Failed to parse Elasticsearch JSON response: " +
                        parse_status.ToString());
  }

  string error_msg;
  const auto& response_fields = get_response.fields();
  const Value* found = nullptr;
  if (!RequireField(response_fields, kEsFoundField, &found, &error_msg)) {
    throw runtime_error(error_msg);
  }
  if (!found->bool_value()) {
    return false;
  }

  const Value* source = nullptr;
  if (!RequireField(response_fields, kEsSourceField, &source, &error_msg)) {
    throw runtime_error(error_msg);
  }
  if (!source->has_struct_value()) {
    throw runtime_error("Elasticsearch _source field is not a JSON object.");
  }

  string profile_json;
  const auto serialize_status =
      MessageToJsonString(source->struct_value(), &profile_json);
  if (!serialize_status.ok()) {
    throw runtime_error("Failed to serialize Elasticsearch _source JSON: " +
                        serialize_status.ToString());
  }

  if (!PBDataHandler::JsonToPB(profile_json, profile, &error_msg)) {
    throw runtime_error("Failed to parse Profile protobuf: " + error_msg);
  }
  return true;
}

}  // namespace recommendation_engine
