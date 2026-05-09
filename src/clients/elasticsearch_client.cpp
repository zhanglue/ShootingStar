#include <getopt.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "protos/recommendation_engine/profile.pb.h"
#include "src/clients/common.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/pb_data_handler/pb_data_handler.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace {

using ::google::protobuf::Struct;
using ::google::protobuf::Value;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::MessageToJsonString;
using ::shooting_star::recommendation_engine::Profile;
using ::shooting_star::clients::ElapsedMillisSince;
using ::shooting_star::clients::ParseInt64Arg;
using ::shooting_star::clients::ParseIntArg;
using ::shooting_star::clients::PrintTimestampUtc;
using ::shooting_star::clients::ClientExitCode;
using ::shooting_star::clients::PrintRunStartedAtUtc;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::ElasticsearchResult;
using ::shooting_star::utilities::GetEnvFlagOrDefault;
using ::shooting_star::utilities::GetEnvOrDefault;
using ::shooting_star::utilities::PBDataHandler;
using ::std::chrono::milliseconds;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::endl;
using ::std::string;

constexpr char kDefaultEsUrl[] =
    "https://item-index-es-http.recommendation-engine-es.svc:9200";
constexpr char kDefaultProfileIndex[] = "movielens_32m_user_profile";
constexpr char kDefaultUserId[] = "1";
constexpr char kDefaultCaCertPath[] = "/mnt/elasticsearch-ca/ca.crt";
constexpr int kDefaultEsRequestTimeoutMs = 4000;
constexpr int kDefaultEsHttpClientAcquireTimeoutMs = 2000;
constexpr int kDefaultEsHttpClientRequestTimeoutMs = 2000;
constexpr int kDefaultEsHttpClientConnectTimeoutMs = 2000;

struct Config {
  string es_url;
  string username;
  string password;
  string ca_cert_path;
  string profile_index;
  string user_id;
  bool verify_ssl = true;
  int timeout_ms = kDefaultEsRequestTimeoutMs;
  int acquire_timeout_ms = kDefaultEsHttpClientAcquireTimeoutMs;
  int http_request_timeout_ms = kDefaultEsHttpClientRequestTimeoutMs;
  int connect_timeout_ms = kDefaultEsHttpClientConnectTimeoutMs;
};

ElasticsearchClient::Config BuildElasticsearchClientConfig(
    const Config& config);

Config LoadDefaultConfig() {
  Config config;
  config.es_url = GetEnvOrDefault("ES_BASE_URL", kDefaultEsUrl);
  config.username = GetEnvOrDefault("ES_USERNAME", "elastic");
  config.password = GetEnvOrDefault("ES_PASSWORD", "");
  config.ca_cert_path = GetEnvOrDefault("ES_CA_CERT_PATH", kDefaultCaCertPath);
  config.profile_index =
      GetEnvOrDefault("ES_PROFILE_INDEX", kDefaultProfileIndex);
  config.user_id = GetEnvOrDefault("ES_PROFILE_USER_ID", kDefaultUserId);
  config.verify_ssl = GetEnvFlagOrDefault("ES_VERIFY_SSL", true);
  return config;
}

void PrintUsage() {
  cout << "Usage: elasticsearch_client [options]\n"
       << "Options:\n"
       << "  -h, --help                    Show this help message\n"
       << "  -e, --es-url <URL>            Elasticsearch URL\n"
       << "                                (default: " << kDefaultEsUrl << ")\n"
       << "  -U, --username <USERNAME>     Elasticsearch username (default: elastic)\n"
       << "  -P, --password <PASSWORD>     Elasticsearch password (default: ES_PASSWORD)\n"
       << "  -a, --ca-cert-path <PATH>     CA certificate path\n"
       << "                                (default: " << kDefaultCaCertPath
       << ")\n"
       << "  -k, --no-verify-ssl           Disable TLS certificate verification\n"
       << "  -i, --index <INDEX>           Profile index\n"
       << "                                (default: " << kDefaultProfileIndex
       << ")\n"
       << "  -u, --user-id <USER_ID>       Profile document _id (default: "
       << kDefaultUserId << ")\n"
       << "  -t, --timeout-ms <MILLIS>     ES client operation budget (default: "
       << kDefaultEsRequestTimeoutMs << ")\n"
       << "\n"
       << "Environment variables with matching names are also supported:\n"
       << "  ES_BASE_URL, ES_USERNAME, ES_PASSWORD, ES_CA_CERT_PATH,\n"
       << "  ES_VERIFY_SSL, ES_PROFILE_INDEX, ES_PROFILE_USER_ID\n";
}

bool ParseArgs(int argc, char** argv, Config* config) {
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
        return false;
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
        if (!ParseIntArg(optarg, "timeout_ms", &config->timeout_ms)) {
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

bool ValidateConfig(const Config& config) {
  if (config.password.empty()) {
    cout << "Error: ES password is required. Set ES_PASSWORD or pass "
         << "--password.\n";
    return false;
  }
  return true;
}

ElasticsearchClient::Config BuildElasticsearchClientConfig(
    const Config& config) {
  ElasticsearchClient::Config es_config;
  es_config.base_url = config.es_url;
  es_config.username = config.username;
  es_config.password = config.password;
  es_config.request_timeout = milliseconds(config.timeout_ms);
  es_config.http_config.acquire_timeout = milliseconds(config.acquire_timeout_ms);
  es_config.http_config.request_timeout =
      milliseconds(config.http_request_timeout_ms);
  es_config.http_config.connect_timeout = milliseconds(config.connect_timeout_ms);
  es_config.http_config.verify_ssl = config.verify_ssl;
  es_config.http_config.ca_cert_path = config.ca_cert_path;
  return es_config;
}

void PrintConfig(const Config& config) {
  cout << "Client config:" << endl;
  cout << "  es_url: " << config.es_url << endl;
  cout << "  username: " << config.username << endl;
  cout << "  password: " << (config.password.empty() ? "<empty>" : "<set>")
       << endl;
  cout << "  ca_cert_path: " << config.ca_cert_path << endl;
  cout << "  profile_index: " << config.profile_index << endl;
  cout << "  user_id: " << config.user_id << endl;
  cout << "  verify_ssl: " << (config.verify_ssl ? "true" : "false")
       << endl;
  cout << "  timeout_ms: " << config.timeout_ms << endl;
  cout << "  acquire_timeout_ms: " << config.acquire_timeout_ms << endl;
  cout << "  http_request_timeout_ms: " << config.http_request_timeout_ms
       << endl;
  cout << "  connect_timeout_ms: " << config.connect_timeout_ms << endl;
  cout << endl;
}
class ElasticsearchProfileClient {
 public:
  static ElasticsearchProfileClient Create(const Config& config) {
    ElasticsearchClient client =
        ElasticsearchClient::Create(BuildElasticsearchClientConfig(config));
    return ElasticsearchProfileClient(std::move(client));
  }

  explicit ElasticsearchProfileClient(ElasticsearchClient client)
      : client_(std::move(client)) {}

  bool launch_request(const string& profile_index, const string& user_id) {
    int64_t expected_user_id = 0;
    if (!ParseInt64Arg(user_id, "user_id", &expected_user_id)) {
      return false;
    }

    cout << "Elasticsearch Health request:" << endl;
    cout << "  endpoint: GET /_cluster/health" << endl;
    PrintTimestampUtc("Elasticsearch Health request started at UTC");
    const auto health_start = steady_clock::now();
    const ElasticsearchResult health = client_.Health();
    PrintTimestampUtc("Elasticsearch Health response received at UTC");
    cout << "Elasticsearch Health elapsed: "
         << ElapsedMillisSince(health_start) << " ms\n";
    PrintElasticsearchResult("Elasticsearch Health response", health);
    if (!health.ok) {
      return false;
    }

    cout << "Elasticsearch Get request:" << endl;
    cout << "  index: " << profile_index << endl;
    cout << "  id: " << user_id << endl;
    PrintTimestampUtc("Elasticsearch Get request started at UTC");
    const auto get_start = steady_clock::now();
    const ElasticsearchResult get_result = client_.Get(profile_index, user_id);
    PrintTimestampUtc("Elasticsearch Get response received at UTC");
    cout << "Elasticsearch Get elapsed: " << ElapsedMillisSince(get_start)
         << " ms\n";
    PrintElasticsearchResult("Elasticsearch Get response", get_result);
    if (!get_result.ok) {
      return false;
    }

    Profile profile;
    if (!ParseProfileFromGetResponse(get_result.body, &profile)) {
      return false;
    }

    cout << "Fetched profile protobuf:" << endl;
    cout << profile.DebugString() << endl;
    cout << "Fetched profile summary:" << endl;
    cout << "  user_id: " << profile.user_id() << "\n"
         << "  username: " << profile.demographics().username() << "\n"
         << "  rated_items: " << profile.behaviors().rated_items_size() << "\n"
         << "  rating_count: " << profile.stats().rating_count() << "\n"
         << "  profile_confidence: " << profile.stats().profile_confidence()
         << "\n";

    if (profile.user_id() != expected_user_id) {
      cout << "Fetched profile user_id does not match requested id.\n";
      return false;
    }
    if (profile.behaviors().rated_items_size() <= 0 ||
        profile.stats().rating_count() <= 0 ||
        profile.stats().profile_confidence() <= 0.0) {
      cout << "Fetched profile does not look like generated MovieLens data.\n";
      return false;
    }

    cout << "\nElasticsearch profile client completed successfully.\n";
    return true;
  }

 private:
  static void PrintElasticsearchResult(const string& title,
                                       const ElasticsearchResult& result) {
    cout << title << ":" << endl;
    cout << "  ok: " << (result.ok ? "true" : "false") << endl;
    cout << "  status_code: " << result.status_code << endl;
    cout << "  error_message: " << result.error_message << endl;
    cout << "  body: " << result.body << endl;
  }

  static bool RequireField(const google::protobuf::Map<string, Value>& fields,
                           const string& name,
                           const Value** value) {
    const auto it = fields.find(name);
    if (it == fields.end()) {
      cout << "Elasticsearch response is missing JSON field: " << name << "\n";
      return false;
    }
    *value = &it->second;
    return true;
  }

  static bool ParseProfileFromGetResponse(const string& response_body,
                                          Profile* profile) {
    Struct get_response;
    const auto parse_status = JsonStringToMessage(response_body, &get_response);
    if (!parse_status.ok()) {
      cout << "Failed to parse Elasticsearch JSON response: "
           << parse_status.ToString() << "\n";
      return false;
    }

    const auto& response_fields = get_response.fields();
    const Value* found = nullptr;
    if (!RequireField(response_fields, "found", &found)) {
      return false;
    }
    if (!found->bool_value()) {
      cout << "Elasticsearch document was not found.\n";
      return false;
    }

    const Value* source = nullptr;
    if (!RequireField(response_fields, "_source", &source)) {
      return false;
    }
    if (!source->has_struct_value()) {
      cout << "Elasticsearch _source field is not a JSON object.\n";
      return false;
    }

    string profile_json;
    const auto serialize_status =
        MessageToJsonString(source->struct_value(), &profile_json);
    if (!serialize_status.ok()) {
      cout << "Failed to serialize Elasticsearch _source JSON: "
           << serialize_status.ToString() << "\n";
      return false;
    }

    string error;
    if (!PBDataHandler::JsonToPB(profile_json, profile, &error)) {
      cout << "Failed to parse Profile protobuf: " << error << "\n";
      return false;
    }
    return true;
  }

  ElasticsearchClient client_;
};


}  // namespace

int main(int argc, char** argv) {
  Config config = LoadDefaultConfig();
  if (!ParseArgs(argc, argv, &config)) {
    return ClientExitCode::kArgs;
  }
  PrintRunStartedAtUtc();
  PrintConfig(config);
  if (!ValidateConfig(config)) {
    return ClientExitCode::kErr;
  }

  try {
    ElasticsearchProfileClient client = ElasticsearchProfileClient::Create(config);
    if (!client.launch_request(config.profile_index, config.user_id)) {
      return ClientExitCode::kErr;
    }
  } catch (const std::exception& ex) {
    cout << "Elasticsearch profile client failed with exception: "
                << ex.what() << "\n";
    return ClientExitCode::kErr;
  }
  return ClientExitCode::kOk;
}
