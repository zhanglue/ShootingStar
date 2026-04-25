#include <getopt.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "protos/recommendation_engine/profile.pb.h"
#include "src/clients/client_runtime.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/pb_data_handler/pb_data_handler.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::MessageToJsonString;
using ::recommendation_engine::Profile;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::ElasticsearchResult;
using ::shooting_star::utilities::GetEnvFlagOrDefault;
using ::shooting_star::utilities::GetEnvOrDefault;
using ::shooting_star::utilities::PBDataHandler;
using ::std::chrono::milliseconds;
using ::std::cout;
using ::std::string;

namespace recommendation_engine {
namespace {

constexpr char kDefaultEsUrl[] =
    "https://item-index-es-http.recommendation-engine-es.svc:9200";
constexpr char kDefaultProfileIndex[] = "movielens_32m_user_profile";
constexpr char kDefaultUserId[] = "1";
constexpr char kDefaultCaCertPath[] = "/mnt/elasticsearch-ca/ca.crt";

struct Config {
  string es_url;
  string username;
  string password;
  string ca_cert_path;
  string profile_index;
  string user_id;
  bool verify_ssl = true;
  int timeout_ms = 5000;
};

void PrintUsage() {
  cout << "Usage: recommendation_engine_es_client [options]\n"
       << "Options:\n"
       << "  -h, --help                    Show this help message\n"
       << "  -e, --es-url <URL>            Elasticsearch URL\n"
       << "                                (default: " << kDefaultEsUrl << ")\n"
       << "  -U, --username <USERNAME>     Elasticsearch username (default: elastic)\n"
       << "  -P, --password <PASSWORD>     Elasticsearch password (default: ES_PASSWORD)\n"
       << "  -a, --ca-cert-path <PATH>     CA certificate path\n"
       << "                                (default: " << kDefaultCaCertPath << ")\n"
       << "  -k, --no-verify-ssl           Disable TLS certificate verification\n"
       << "  -i, --index <INDEX>           Profile index\n"
       << "                                (default: " << kDefaultProfileIndex << ")\n"
       << "  -u, --user-id <USER_ID>       Profile document _id (default: "
       << kDefaultUserId << ")\n"
       << "  -t, --timeout-ms <MILLIS>     Request timeout (default: 5000)\n"
       << "\n"
       << "Environment variables with matching names are also supported:\n"
       << "  ES_BASE_URL, ES_USERNAME, ES_PASSWORD, ES_CA_CERT_PATH,\n"
       << "  ES_VERIFY_SSL, ES_PROFILE_INDEX, ES_PROFILE_USER_ID\n";
}

bool ParseInt(const string& text, const string& name, int* value) {
  try {
    *value = ::std::stoi(text);
  } catch (const ::std::invalid_argument&) {
    ::std::cerr << "Error: " << name << " is not a valid integer: " << text << "\n";
    return false;
  } catch (const ::std::out_of_range&) {
    ::std::cerr << "Error: " << name << " is out of range: " << text << "\n";
    return false;
  }
  return true;
}

Config LoadDefaultConfig() {
  Config config;
  config.es_url = GetEnvOrDefault("ES_BASE_URL", kDefaultEsUrl);
  config.username = GetEnvOrDefault("ES_USERNAME", "elastic");
  config.password = GetEnvOrDefault("ES_PASSWORD", "");
  config.ca_cert_path = GetEnvOrDefault("ES_CA_CERT_PATH", kDefaultCaCertPath);
  config.profile_index = GetEnvOrDefault("ES_PROFILE_INDEX", kDefaultProfileIndex);
  config.user_id = GetEnvOrDefault("ES_PROFILE_USER_ID", kDefaultUserId);
  config.verify_ssl = GetEnvFlagOrDefault("ES_VERIFY_SSL", true);
  return config;
}

bool ParseArgs(int argc, char** argv, Config* config, bool* should_run) {
  *should_run = true;
  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"es-url", required_argument, nullptr, 'e'},
      {"username", required_argument, nullptr, 'U'},
      {"password", required_argument, nullptr, 'P'},
      {"ca-cert-path", required_argument, nullptr, 'a'},
      {"no-verify-ssl", no_argument, nullptr, 'k'},
      {"index", required_argument, nullptr, 'i'},
      {"user-id", required_argument, nullptr, 'u'},
      {"timeout-ms", required_argument, nullptr, 't'},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "he:U:P:a:ki:u:t:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
      case 'h':
        PrintUsage();
        *should_run = false;
        return true;
      case 'e':
        config->es_url = optarg;
        break;
      case 'U':
        config->username = optarg;
        break;
      case 'P':
        config->password = optarg;
        break;
      case 'a':
        config->ca_cert_path = optarg;
        break;
      case 'k':
        config->verify_ssl = false;
        break;
      case 'i':
        config->profile_index = optarg;
        break;
      case 'u':
        config->user_id = optarg;
        break;
      case 't':
        if (!ParseInt(optarg, "timeout_ms", &config->timeout_ms)) {
          return false;
        }
        break;
      default:
        PrintUsage();
        return false;
    }
  }
  return true;
}

bool RequireField(const ::google::protobuf::Map<string, Value>& fields,
                  const string& name,
                  const Value** value) {
  const auto it = fields.find(name);
  if (it == fields.end()) {
    ::std::cerr << "Elasticsearch response is missing JSON field: " << name << "\n";
    return false;
  }
  *value = &it->second;
  return true;
}

bool ParseProfileFromGetResponse(const string& response_body, Profile* profile) {
  Struct get_response;
  const auto parse_status = JsonStringToMessage(response_body, &get_response);
  if (!parse_status.ok()) {
    ::std::cerr << "Failed to parse Elasticsearch JSON response: "
                << parse_status.ToString() << "\n";
    return false;
  }

  const auto& response_fields = get_response.fields();
  const Value* found = nullptr;
  if (!RequireField(response_fields, "found", &found)) {
    return false;
  }
  if (!found->bool_value()) {
    ::std::cerr << "Elasticsearch document was not found.\n";
    return false;
  }

  const Value* source = nullptr;
  if (!RequireField(response_fields, "_source", &source)) {
    return false;
  }
  if (!source->has_struct_value()) {
    ::std::cerr << "Elasticsearch _source field is not a JSON object.\n";
    return false;
  }

  string profile_json;
  const auto serialize_status =
      MessageToJsonString(source->struct_value(), &profile_json);
  if (!serialize_status.ok()) {
    ::std::cerr << "Failed to serialize Elasticsearch _source JSON: "
                << serialize_status.ToString() << "\n";
    return false;
  }

  string error;
  if (!PBDataHandler::JsonToPB(profile_json, profile, &error)) {
    ::std::cerr << "Failed to parse Profile protobuf: " << error << "\n";
    return false;
  }
  return true;
}

bool RunSmokeCheck(const Config& config) {
  if (config.password.empty()) {
    ::std::cerr << "Error: ES password is required. Set ES_PASSWORD or pass "
                << "--password.\n";
    return false;
  }

  ::shooting_star::clients::PrintRunStartedAtUtc();

  ElasticsearchClient::Config es_config;
  es_config.base_url = config.es_url;
  es_config.username = config.username;
  es_config.password = config.password;
  es_config.request_timeout = milliseconds(config.timeout_ms);
  es_config.http_config.verify_ssl = config.verify_ssl;
  es_config.http_config.ca_cert_path = config.ca_cert_path;

  cout << "Connecting to Elasticsearch at: " << config.es_url << "\n"
       << "Profile index: " << config.profile_index << "\n"
       << "Profile user id: " << config.user_id << "\n"
       << "TLS verification: " << (config.verify_ssl ? "enabled" : "disabled") << "\n";
  if (!config.ca_cert_path.empty()) {
    cout << "CA certificate path: " << config.ca_cert_path << "\n";
  }
  cout << ::std::endl;

  ElasticsearchClient client = ElasticsearchClient::Create(::std::move(es_config));

  const ElasticsearchResult health = client.Health();
  if (!health.ok) {
    ::std::cerr << "Elasticsearch health check failed: " << health.error_message
                << "\n"
                << health.body << "\n";
    return false;
  }
  cout << "Cluster health response: " << health.body << "\n";

  const ElasticsearchResult get_result =
      client.Get(config.profile_index, config.user_id);
  if (!get_result.ok) {
    ::std::cerr << "Profile document fetch failed: " << get_result.error_message
                << "\n"
                << get_result.body << "\n";
    return false;
  }

  Profile profile;
  if (!ParseProfileFromGetResponse(get_result.body, &profile)) {
    return false;
  }

  cout << "Fetched profile summary:\n"
       << "  user_id: " << profile.user_id() << "\n"
       << "  username: " << profile.demographics().username() << "\n"
       << "  rated_items: " << profile.behaviors().rated_items_size() << "\n"
       << "  rating_count: " << profile.stats().rating_count() << "\n"
       << "  profile_confidence: " << profile.stats().profile_confidence() << "\n";

  if (profile.user_id() != ::std::stoll(config.user_id)) {
    ::std::cerr << "Fetched profile user_id does not match requested id.\n";
    return false;
  }
  if (profile.behaviors().rated_items_size() <= 0 ||
      profile.stats().rating_count() <= 0 ||
      profile.stats().profile_confidence() <= 0.0) {
    ::std::cerr << "Fetched profile does not look like generated MovieLens data.\n";
    return false;
  }

  cout << "\nElasticsearch profile smoke check passed.\n";
  return true;
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  recommendation_engine::Config config =
      recommendation_engine::LoadDefaultConfig();
  bool should_run = true;
  if (!recommendation_engine::ParseArgs(argc, argv, &config, &should_run)) {
    return 1;
  }
  if (!should_run) {
    return 0;
  }

  try {
    return recommendation_engine::RunSmokeCheck(config) ? 0 : 1;
  } catch (const ::std::exception& ex) {
    ::std::cerr << "Elasticsearch profile smoke check failed with exception: "
                << ex.what() << "\n";
    return 1;
  }
}
