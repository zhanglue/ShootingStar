#include <getopt.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/clients/common.h"
#include "src/utilities/redis_client/redis_client.h"

namespace {

using ::shooting_star::clients::ElapsedMillisSince;
using ::shooting_star::clients::ParseIntArg;
using ::shooting_star::clients::PrintTimestampUtc;
using ::shooting_star::clients::ClientExitCode;
using ::shooting_star::clients::PrintRunStartedAtUtc;
using ::shooting_star::utilities::RedisClient;
using ::shooting_star::utilities::RedisErrorCode;
using ::shooting_star::utilities::RedisScoredMemberListResult;
using ::shooting_star::utilities::RedisStatus;
using ::std::chrono::milliseconds;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::endl;
using ::std::size_t;
using ::std::string;
using ::std::string_view;
using ::std::vector;

constexpr char kDefaultRedisHost[] =
    "redis-write.recommendation-engine-redis.svc.cluster.local";
constexpr int kDefaultRedisPort = 6379;
constexpr int kDefaultRedisDb = 0;
constexpr char kDefaultKeyPrefix[] = "rec:item_cf:v1:neighbors";
constexpr char kDefaultItemIds[] = "8893,113705,5733";
constexpr int kDefaultTopK = 5;
constexpr int kDefaultConnectTimeoutMs = 2000;
constexpr int kDefaultSocketTimeoutMs = 2000;
constexpr int kDefaultPoolSize = 4;
constexpr int kDefaultPoolWaitTimeoutMs = 2000;
constexpr int kDefaultRetryMaxAttempts = 2;
constexpr int kDefaultRetryDelayMs = 10;

struct Config {
  string host;
  int port = kDefaultRedisPort;
  int db = kDefaultRedisDb;
  string username;
  string password;
  string key_prefix;
  vector<string> item_ids;
  int top_k = kDefaultTopK;
  int connect_timeout_ms = kDefaultConnectTimeoutMs;
  int socket_timeout_ms = kDefaultSocketTimeoutMs;
  int pool_size = kDefaultPoolSize;
  int pool_wait_timeout_ms = kDefaultPoolWaitTimeoutMs;
  int retry_max_attempts = kDefaultRetryMaxAttempts;
  int retry_delay_ms = kDefaultRetryDelayMs;
};

RedisClient::Config BuildRedisClientConfig(const Config& config);

string GetEnvOrDefault(const char* name, string default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return default_value;
  }
  return value;
}

string GetEnvIntText(const char* name, int default_value) {
  return GetEnvOrDefault(name, std::to_string(default_value));
}

vector<string> SplitCsv(string_view csv) {
  vector<string> values;
  size_t start = 0;
  while (start <= csv.size()) {
    const size_t comma = csv.find(',', start);
    const size_t end = comma == string_view::npos ? csv.size() : comma;
    string value(csv.substr(start, end - start));
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    while (!value.empty() && value.back() == ' ') {
      value.pop_back();
    }
    if (!value.empty()) {
      values.push_back(std::move(value));
    }
    if (comma == string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return values;
}

bool LoadIntEnv(const char* env_name, const string& config_name, int* value) {
  return ParseIntArg(GetEnvIntText(env_name, *value), config_name, value);
}

Config LoadDefaultConfig() {
  Config config;
  config.host = GetEnvOrDefault("REDIS_HOST", kDefaultRedisHost);
  LoadIntEnv("REDIS_PORT", "REDIS_PORT", &config.port);
  LoadIntEnv("REDIS_DB", "REDIS_DB", &config.db);
  config.username = GetEnvOrDefault("REDIS_USERNAME", "");
  config.password = GetEnvOrDefault("REDIS_PASSWORD", "");
  config.key_prefix = GetEnvOrDefault("REDIS_KEY_PREFIX", kDefaultKeyPrefix);
  config.item_ids =
      SplitCsv(GetEnvOrDefault("REDIS_ITEM_IDS", kDefaultItemIds));
  LoadIntEnv("REDIS_TOP_K", "REDIS_TOP_K", &config.top_k);
  LoadIntEnv("REDIS_CONNECT_TIMEOUT_MS", "REDIS_CONNECT_TIMEOUT_MS",
             &config.connect_timeout_ms);
  LoadIntEnv("REDIS_SOCKET_TIMEOUT_MS", "REDIS_SOCKET_TIMEOUT_MS",
             &config.socket_timeout_ms);
  LoadIntEnv("REDIS_POOL_SIZE", "REDIS_POOL_SIZE", &config.pool_size);
  LoadIntEnv("REDIS_POOL_WAIT_TIMEOUT_MS", "REDIS_POOL_WAIT_TIMEOUT_MS",
             &config.pool_wait_timeout_ms);
  LoadIntEnv("REDIS_RETRY_MAX_ATTEMPTS", "REDIS_RETRY_MAX_ATTEMPTS",
             &config.retry_max_attempts);
  LoadIntEnv("REDIS_RETRY_DELAY_MS", "REDIS_RETRY_DELAY_MS",
             &config.retry_delay_ms);
  return config;
}

void PrintUsage() {
  cout << "Usage: redis_client [options]\n"
       << "Options:\n"
       << "  -h, --help                         Show this help message\n"
       << "  -H, --redis-host <HOST>            Redis host\n"
       << "                                     (default: " << kDefaultRedisHost
       << ")\n"
       << "  -p, --redis-port <PORT>            Redis port (default: "
       << kDefaultRedisPort << ")\n"
       << "  -d, --redis-db <DB>                Redis DB (default: "
       << kDefaultRedisDb << ")\n"
       << "  -U, --username <USERNAME>          Optional Redis ACL username\n"
       << "  -P, --password <PASSWORD>          Redis password (default: REDIS_PASSWORD)\n"
       << "  -k, --key-prefix <PREFIX>          Item-CF key prefix\n"
       << "                                     (default: " << kDefaultKeyPrefix
       << ")\n"
       << "  -i, --item-ids <IDS>               Comma-separated item ids\n"
       << "                                     (default: " << kDefaultItemIds
       << ")\n"
       << "  -n, --top-k <N>                    Number of neighbors per item\n"
       << "                                     (default: " << kDefaultTopK << ")\n"
       << "  -t, --connect-timeout-ms <MILLIS>  Redis connect timeout\n"
       << "  -s, --socket-timeout-ms <MILLIS>   Redis socket timeout\n"
       << "\n"
       << "Environment variables with matching names are also supported:\n"
       << "  REDIS_HOST, REDIS_PORT, REDIS_DB, REDIS_USERNAME, REDIS_PASSWORD,\n"
       << "  REDIS_KEY_PREFIX, REDIS_ITEM_IDS, REDIS_TOP_K,\n"
       << "  REDIS_CONNECT_TIMEOUT_MS, REDIS_SOCKET_TIMEOUT_MS,\n"
       << "  REDIS_POOL_SIZE, REDIS_POOL_WAIT_TIMEOUT_MS,\n"
       << "  REDIS_RETRY_MAX_ATTEMPTS, REDIS_RETRY_DELAY_MS\n";
}

bool ParseArgs(int argc, char** argv, Config* config) {
  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"redis-host", required_argument, nullptr, 'H'},
      {"redis-port", required_argument, nullptr, 'p'},
      {"redis-db", required_argument, nullptr, 'd'},
      {"username", required_argument, nullptr, 'U'},
      {"password", required_argument, nullptr, 'P'},
      {"key-prefix", required_argument, nullptr, 'k'},
      {"item-ids", required_argument, nullptr, 'i'},
      {"top-k", required_argument, nullptr, 'n'},
      {"connect-timeout-ms", required_argument, nullptr, 't'},
      {"socket-timeout-ms", required_argument, nullptr, 's'},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "hH:p:d:U:P:k:i:n:t:s:",
                            long_options, &option_index)) != -1) {
    switch (opt) {
      case 'h':
        PrintUsage();
        return false;
      case 'H':
        config->host = optarg;
        break;
      case 'p':
        if (!ParseIntArg(optarg, "redis_port", &config->port)) {
          return false;
        }
        break;
      case 'd':
        if (!ParseIntArg(optarg, "redis_db", &config->db)) {
          return false;
        }
        break;
      case 'U':
        config->username = optarg;
        break;
      case 'P':
        config->password = optarg;
        break;
      case 'k':
        config->key_prefix = optarg;
        break;
      case 'i':
        config->item_ids = SplitCsv(optarg);
        break;
      case 'n':
        if (!ParseIntArg(optarg, "top_k", &config->top_k)) {
          return false;
        }
        break;
      case 't':
        if (!ParseIntArg(optarg, "connect_timeout_ms",
                         &config->connect_timeout_ms)) {
          return false;
        }
        break;
      case 's':
        if (!ParseIntArg(optarg, "socket_timeout_ms",
                         &config->socket_timeout_ms)) {
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
  if (config.host.empty()) {
    cout << "Error: Redis host is required.\n";
    return false;
  }
  if (config.key_prefix.empty()) {
    cout << "Error: Redis key prefix is required.\n";
    return false;
  }
  if (config.item_ids.empty()) {
    cout << "Error: At least one item id is required.\n";
    return false;
  }
  if (config.top_k <= 0) {
    cout << "Error: top_k must be greater than zero.\n";
    return false;
  }
  return true;
}

RedisClient::Config BuildRedisClientConfig(const Config& config) {
  RedisClient::Config redis_config;
  redis_config.host = config.host;
  redis_config.port = config.port;
  redis_config.db = config.db;
  redis_config.user = config.username;
  redis_config.password = config.password;
  redis_config.connect_timeout = milliseconds(config.connect_timeout_ms);
  redis_config.socket_timeout = milliseconds(config.socket_timeout_ms);
  redis_config.pool_size = static_cast<size_t>(config.pool_size);
  redis_config.pool_wait_timeout = milliseconds(config.pool_wait_timeout_ms);
  redis_config.retry.max_attempts = config.retry_max_attempts;
  redis_config.retry.delay = milliseconds(config.retry_delay_ms);
  return redis_config;
}

void PrintConfig(const Config& config) {
  cout << "Client config:" << endl;
  cout << "  host: " << config.host << endl;
  cout << "  port: " << config.port << endl;
  cout << "  db: " << config.db << endl;
  cout << "  username: " << config.username << endl;
  cout << "  password: " << (config.password.empty() ? "<empty>" : "<set>")
       << endl;
  cout << "  key_prefix: " << config.key_prefix << endl;
  cout << "  item_ids:";
  for (const string& item_id : config.item_ids) {
    cout << " " << item_id;
  }
  cout << endl;
  cout << "  top_k: " << config.top_k << endl;
  cout << "  connect_timeout_ms: " << config.connect_timeout_ms << endl;
  cout << "  socket_timeout_ms: " << config.socket_timeout_ms << endl;
  cout << "  pool_size: " << config.pool_size << endl;
  cout << "  pool_wait_timeout_ms: " << config.pool_wait_timeout_ms
       << endl;
  cout << "  retry_max_attempts: " << config.retry_max_attempts << endl;
  cout << "  retry_delay_ms: " << config.retry_delay_ms << endl;
  cout << endl;
}
class RedisItemCfClient {
 public:
  static RedisItemCfClient Create(const Config& config) {
    RedisClient client = RedisClient::Create(BuildRedisClientConfig(config));
    return RedisItemCfClient(std::move(client), config.key_prefix,
                             config.top_k);
  }

  RedisItemCfClient(RedisClient client, string key_prefix, int top_k)
      : client_(std::move(client)),
        key_prefix_(std::move(key_prefix)),
        top_k_(top_k) {}

  bool launch_request(const vector<string>& item_ids) {
    cout << "Redis PING request: PING" << endl;
    PrintTimestampUtc("Redis PING request started at UTC");
    const auto ping_start = steady_clock::now();
    const RedisStatus ping = client_.Ping();
    PrintTimestampUtc("Redis PING response received at UTC");
    const int64_t ping_elapsed_ms = ElapsedMillisSince(ping_start);
    cout << "Redis PING elapsed: " << ping_elapsed_ms << " ms\n";
    PrintRedisStatus("Redis PING response", ping);
    if (!ping.ok) {
      return false;
    }

    int non_empty_keys = 0;
    for (const string& item_id : item_ids) {
      const string key = BuildItemSimilarityKey(key_prefix_, item_id);
      cout << "Redis ZREVRANGE request:" << endl;
      cout << "  key: " << key << endl;
      cout << "  start: 0" << endl;
      cout << "  stop: " << top_k_ - 1 << endl;

      PrintTimestampUtc("Redis ZREVRANGE request started at UTC");
      const auto query_start = steady_clock::now();
      RedisScoredMemberListResult result =
          client_.ZRevRangeWithScores(key, 0, top_k_ - 1);
      PrintTimestampUtc("Redis ZREVRANGE response received at UTC");
      const int64_t query_elapsed_ms = ElapsedMillisSince(query_start);
      cout << "Redis ZREVRANGE elapsed: " << query_elapsed_ms << " ms\n";
      PrintRedisStatus("Redis ZREVRANGE response status", result.status);
      if (!result.status.ok) {
        return false;
      }

      cout << "Redis ZREVRANGE response values for key " << key << ":"
           << endl;
      if (result.values.empty()) {
        cout << "  <no neighbors>" << endl;
        continue;
      }

      ++non_empty_keys;
      for (const auto& neighbor : result.values) {
        cout << "  item_id=" << neighbor.member
             << " score=" << neighbor.score << "\n";
      }
    }

    if (non_empty_keys == 0) {
      cout << "No configured item ids returned neighbors.\n";
      return false;
    }

    cout << "\nRedis item_cf client completed successfully.\n";
    return true;
  }

 private:
  static string RedisErrorCodeName(RedisErrorCode code) {
    switch (code) {
      case RedisErrorCode::kNone:
        return "none";
      case RedisErrorCode::kInvalidArgument:
        return "invalid_argument";
      case RedisErrorCode::kIoError:
        return "io_error";
      case RedisErrorCode::kTimeout:
        return "timeout";
      case RedisErrorCode::kClosed:
        return "closed";
      case RedisErrorCode::kReplyError:
        return "reply_error";
      case RedisErrorCode::kProtocolError:
        return "protocol_error";
      case RedisErrorCode::kUnknown:
        return "unknown";
    }
    return "unknown";
  }

  static void PrintRedisStatus(const string& title, const RedisStatus& status) {
    cout << title << ":" << endl;
    cout << "  ok: " << (status.ok ? "true" : "false") << endl;
    cout << "  error_code: " << RedisErrorCodeName(status.error_code)
         << endl;
    cout << "  error_message: " << status.error_message << endl;
    cout << "  attempts: " << status.attempts << endl;
  }

  static string BuildItemSimilarityKey(const string& key_prefix,
                                       const string& item_id) {
    if (!key_prefix.empty() && key_prefix.back() == ':') {
      return key_prefix + item_id;
    }
    return key_prefix + ":" + item_id;
  }

  RedisClient client_;
  string key_prefix_;
  int top_k_ = 0;
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
    RedisItemCfClient client = RedisItemCfClient::Create(config);
    if (!client.launch_request(config.item_ids)) {
      return ClientExitCode::kErr;
    }
  } catch (const std::exception& ex) {
    cout << "Redis item_cf client failed with exception: " << ex.what()
                << "\n";
    return ClientExitCode::kErr;
  }
  return ClientExitCode::kOk;
}
